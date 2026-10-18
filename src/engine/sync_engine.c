/* Feature macros must come before the first system header (sync_engine.h pulls in <stdint.h>), otherwise glibc hides
   usleep, clock_gettime, lstat, ... under strict C11. */
#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE   /* utimes, lstat with CMAKE_C_EXTENSIONS OFF */
  #define _DARWIN_C_SOURCE
#endif
#include "sync_engine.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
  typedef HANDLE            ShsThread;
  typedef CRITICAL_SECTION  ShsMutex;
  #define SHS_SEP '\\'
#else
  #include <dirent.h>
  #include <errno.h>
  #include <pthread.h>
  #include <sys/stat.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef pthread_t       ShsThread;
  typedef pthread_mutex_t ShsMutex;
  #define SHS_SEP '/'
#endif

#define COPY_CHUNK     (1u << 20)   /* 1 MiB: pause/cancel/progress granularity */
#define MTIME_SLACK_S  2            /* FAT stores mtime with 2 s resolution */

/* ---- tiny platform layer ---------------------------------------------- */

static void MutexInit(ShsMutex *m)   {
#ifdef _WIN32
    InitializeCriticalSection(m);
#else
    pthread_mutex_init(m, NULL);
#endif
}
static void MutexLock(ShsMutex *m)   {
#ifdef _WIN32
    EnterCriticalSection(m);
#else
    pthread_mutex_lock(m);
#endif
}
static void MutexUnlock(ShsMutex *m) {
#ifdef _WIN32
    LeaveCriticalSection(m);
#else
    pthread_mutex_unlock(m);
#endif
}
static void MutexDestroy(ShsMutex *m){
#ifdef _WIN32
    DeleteCriticalSection(m);
#else
    pthread_mutex_destroy(m);
#endif
}
/* Condition variable paired with ShsMutex: threads sleep until another thread changes shared state (no polling). */
#ifdef _WIN32
typedef CONDITION_VARIABLE ShsCond;
static void CondInit(ShsCond *c)                              { InitializeConditionVariable(c); }
static void CondWait(ShsCond *c, ShsMutex *m, unsigned ms)    { SleepConditionVariableCS(c, m, ms); }
static void CondBroadcast(ShsCond *c)                         { WakeAllConditionVariable(c); }
static void CondDestroy(ShsCond *c)                           { (void)c; }
#else
typedef pthread_cond_t ShsCond;
static void CondInit(ShsCond *c)                              { pthread_cond_init(c, NULL); }
static void CondWait(ShsCond *c, ShsMutex *m, unsigned ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_cond_timedwait(c, m, &ts);
}
static void CondBroadcast(ShsCond *c)                         { pthread_cond_broadcast(c); }
static void CondDestroy(ShsCond *c)                           { pthread_cond_destroy(c); }
#endif

static void SleepMs(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}
static double NowSeconds(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

/* ---- debug log: lines go to the callback installed with ShsSyncSetLogger (serialised here) ---- */

static ShsSyncLogFn g_logFn = NULL;
static ShsMutex     g_logLock;
static _Thread_local int t_scanErr;            /* OS error code of the last failed directory open on this thread */

void ShsSyncSetLogger(ShsSyncLogFn fn)
{
    if (fn && !g_logFn) MutexInit(&g_logLock);
    g_logFn = fn;
}

static int LastOsError(void)
{
#ifdef _WIN32
    return (int)GetLastError();
#else
    return errno;
#endif
}

static void DbgLog(const char *fmt, ...)
{
    if (!g_logFn) return;
    char msg[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    MutexLock(&g_logLock);
    if (g_logFn) g_logFn(msg);
    MutexUnlock(&g_logLock);
}

#ifdef _WIN32
static wchar_t *Widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)(n > 0 ? n : 1) * sizeof(wchar_t));
    if (!w) return NULL;
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
    return w;
}
static void Narrow(const wchar_t *w, char *out, size_t cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL)) out[0] = 0;
}
#endif

/* ---- file entries ------------------------------------------------------ */

typedef struct {
    char    *rel;      /* '/'-separated path relative to the root */
    uint64_t size;
    int64_t  mtime;    /* unix seconds */
    bool     isDir;
} Entry;

typedef struct { Entry *v; size_t n, cap; } EntryList;

static bool ListPush(EntryList *l, const char *rel, uint64_t size, int64_t mtime, bool isDir)
{
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 256;
        Entry *nv = (Entry *)realloc(l->v, nc * sizeof(Entry));
        if (!nv) return false;
        l->v = nv; l->cap = nc;
    }
    char *copy = (char *)malloc(strlen(rel) + 1);
    if (!copy) return false;
    strcpy(copy, rel);
    l->v[l->n++] = (Entry){ copy, size, mtime, isDir };
    return true;
}

static void ListFree(EntryList *l)
{
    for (size_t i = 0; i < l->n; i++) free(l->v[i].rel);
    free(l->v);
    memset(l, 0, sizeof *l);
}

static int EntryCmp(const void *a, const void *b) { return strcmp(((const Entry *)a)->rel, ((const Entry *)b)->rel); }

static const Entry *ListFind(const EntryList *l, const char *rel)
{
    Entry key = { (char *)rel, 0, 0, false };
    return (const Entry *)bsearch(&key, l->v, l->n, sizeof(Entry), EntryCmp);
}

/* Join root + '/' + rel into out. */
static void JoinPath(char *out, size_t cap, const char *root, const char *rel)
{
    size_t rl = strlen(root);
    bool endsSep = rl && (root[rl - 1] == '/' || root[rl - 1] == '\\');   /* drive roots like "E:\" */
    snprintf(out, cap, "%s%s%s", root, endsSep ? "" : (SHS_SEP == '\\' ? "\\" : "/"), rel);
#ifdef _WIN32
    for (char *p = out; *p; p++) if (*p == '/') *p = '\\';
#endif
}

/* ---- directory scanner (Win32 / POSIX) --------------------------------- */

/* Scans `root`/`rel` (recursively when `deep`, else just its direct children). A subfolder that cannot be opened (e.g. "System Volume Information" on a USB stick) is
   skipped and logged; when `errJob` is set (the source side) it also counts as an error so the user knows files were
   left out. Only a failure to open the top folder fails the scan. */
static bool ScanDir(const char *root, const char *rel, EntryList *out, volatile int *cancel, ShsSyncJob *errJob, bool deep);
static void RecordError(ShsSyncJob *j, const char *what, const char *rel);

static bool ScanDir(const char *root, const char *rel, EntryList *out, volatile int *cancel, ShsSyncJob *errJob, bool deep)
{
    char dirPath[SHS_SYNC_PATH_MAX];
    if (rel[0]) JoinPath(dirPath, sizeof dirPath, root, rel);
    else        snprintf(dirPath, sizeof dirPath, "%s", root);

#ifdef _WIN32
    char pattern[SHS_SYNC_PATH_MAX + 4];
    size_t dl = strlen(dirPath);
    bool dEnds = dl && (dirPath[dl - 1] == '\\' || dirPath[dl - 1] == '/');
    snprintf(pattern, sizeof pattern, "%s%s*", dirPath, dEnds ? "" : "\\");
    wchar_t *wp = Widen(pattern);
    if (!wp) return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(wp, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        t_scanErr = (int)e;
        if (e == ERROR_FILE_NOT_FOUND) return true;                          /* an empty folder */
        DbgLog("cannot open folder %s (error %lu)%s", dirPath, (unsigned long)e, rel[0] ? " - skipped" : "");
        if (!rel[0]) return false;
        if (errJob) RecordError(errJob, "Cannot read folder", rel);
        return true;
    }
    do {
        if (*cancel) break;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   /* skip symlinks/junctions */
        char name[SHS_SYNC_PATH_MAX], childRel[SHS_SYNC_PATH_MAX];
        Narrow(fd.cFileName, name, sizeof name);
        if (!rel[0] && (!_stricmp(name, "System Volume Information") || !_stricmp(name, "$RECYCLE.BIN"))) continue;   /* Windows' own folders */
        snprintf(childRel, sizeof childRel, "%s%s%s", rel, rel[0] ? "/" : "", name);
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        unsigned long long ft = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        int64_t mt = (int64_t)(ft / 10000000ULL) - 11644473600LL;
        uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!ListPush(out, childRel, isDir ? 0 : sz, mt, isDir)) { FindClose(h); return false; }
        if (isDir && deep && !ScanDir(root, childRel, out, cancel, errJob, true)) { FindClose(h); return false; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
#else
    DIR *d = opendir(dirPath);
    if (!d) {
        t_scanErr = errno;
        DbgLog("cannot open folder %s (error %d)%s", dirPath, errno, rel[0] ? " - skipped" : "");
        if (!rel[0]) return false;
        if (errJob) RecordError(errJob, "Cannot read folder", rel);
        return true;
    }
    struct dirent *de;
    bool ok = true;
    while (ok && !*cancel && (de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char childRel[SHS_SYNC_PATH_MAX], full[SHS_SYNC_PATH_MAX * 2];
        snprintf(childRel, sizeof childRel, "%s%s%s", rel, rel[0] ? "/" : "", de->d_name);
        snprintf(full, sizeof full, "%s/%s", dirPath, de->d_name);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue;
        bool isDir = S_ISDIR(st.st_mode);
        if (!isDir && !S_ISREG(st.st_mode)) continue;
        ok = ListPush(out, childRel, isDir ? 0 : (uint64_t)st.st_size, (int64_t)st.st_mtime, isDir);
        if (ok && isDir && deep) ok = ScanDir(root, childRel, out, cancel, errJob, true);
    }
    closedir(d);
    return ok;
#endif
}

/* ---- filesystem ops ---------------------------------------------------- */

static bool MakeDirs(const char *path)
{
    char buf[SHS_SYNC_PATH_MAX];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; ; p++) {
        if (*p == '/' || *p == '\\' || *p == 0) {
            char c = *p; *p = 0;
#ifdef _WIN32
            if (!(buf[1] == ':' && p == buf + 2)) {       /* skip bare drive "C:" */
                wchar_t *w = Widen(buf);
                if (w) { CreateDirectoryW(w, NULL); free(w); }
            }
#else
            mkdir(buf, 0755);
#endif
            *p = c;
            if (!c) break;
        }
    }
#ifdef _WIN32
    wchar_t *w = Widen(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool SetMtime(const char *path, int64_t secs)
{
#ifdef _WIN32
    wchar_t *w = Widen(path);
    if (!w) return false;
    HANDLE h = CreateFileW(w, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned long long t = ((unsigned long long)(secs + 11644473600LL)) * 10000000ULL;
    FILETIME ft = { (DWORD)(t & 0xFFFFFFFFu), (DWORD)(t >> 32) };
    BOOL ok = SetFileTime(h, NULL, NULL, &ft);
    CloseHandle(h);
    return ok != 0;
#else
    struct timeval tv[2] = { { (time_t)secs, 0 }, { (time_t)secs, 0 } };
    return utimes(path, tv) == 0;
#endif
}

static bool RemoveFile(const char *path)
{
#ifdef _WIN32
    wchar_t *w = Widen(path);
    if (!w) return false;
    SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);   /* clear read-only so delete works */
    BOOL ok = DeleteFileW(w);
    free(w);
    return ok != 0;
#else
    return unlink(path) == 0;
#endif
}

static bool RemoveDirEmpty(const char *path)
{
#ifdef _WIN32
    wchar_t *w = Widen(path);
    if (!w) return false;
    BOOL ok = RemoveDirectoryW(w);
    free(w);
    return ok != 0;
#else
    return rmdir(path) == 0;
#endif
}

static FILE *FsOpen(const char *path, bool write)
{
#ifdef _WIN32
    wchar_t *w = Widen(path);
    if (!w) return NULL;
    FILE *f = _wfopen(w, write ? L"wb" : L"rb");
    free(w);
    return f;
#else
    return fopen(path, write ? "wb" : "rb");
#endif
}

static bool FsReplace(const char *tmp, const char *dst)
{
#ifdef _WIN32
    wchar_t *a = Widen(tmp), *b = Widen(dst);
    BOOL ok = (a && b) ? MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) : FALSE;
    free(a); free(b);
    return ok != 0;
#else
    return rename(tmp, dst) == 0;
#endif
}

/* ---- job --------------------------------------------------------------- */

typedef struct { char *name; uint64_t size; int state; } FileRec;

struct ShsSyncJob {
    /* the batch being processed (worker thread only): files under `source` (just `onlyName` when set) -> `target` */
    char source[SHS_SYNC_PATH_MAX], target[SHS_SYNC_PATH_MAX];
    char onlyName[SHS_SYNC_PATH_MAX];      /* non-empty: the batch is this single file inside `source` */
    char prefix[SHS_SYNC_PATH_MAX];        /* shown in front of file names in the full list ("folder/") */
    bool batchMirror;
    char firstSource[SHS_SYNC_PATH_MAX];   /* what ShsSyncStart was given */
    char baseTarget[SHS_SYNC_PATH_MAX];    /* the target ShsSyncStart was given; added items land inside it */
    bool mirror;
    bool firstAdded;                       /* the first source is an added item (a folder lands in <target>/<name>) */
    bool direct;                           /* removable target: files are written under their final name (no temp + rename) */
    bool quick;                            /* Quick Copy: copy only, existing target files go through the conflict policy */
    volatile int policy;                   /* ShsConflictAction applied to existing target files */
    volatile int resolveAction, resolveSet;/* the UI's answer to a pending conflict */

    char (*pend)[SHS_SYNC_PATH_MAX];       /* queued ShsSyncAddPath items (guarded by lock) */
    uint32_t *pendRec;                     /* each one's placeholder row in the file list */
    int  pendN, pendCap;
    int  closing;                          /* set under lock once the worker will take no more items */
    FileRec *recs;                         /* full file list (guarded by lock) */
    size_t   recN, recCap;

    ShsThread thread;
    bool      threadStarted;
    ShsMutex  lock;
    ShsSyncStatus st;            /* guarded by lock */
    volatile int  cancel;
    volatile int  paused;
    volatile int  verify;
    volatile int  skipId;              /* id of the file the user asked to skip (0 = none) */

    /* rate window (guarded by lock) */
    double   winStart;
    uint64_t winBytes;
};

static void SetState(ShsSyncJob *j, ShsSyncState s)
{
    MutexLock(&j->lock); j->st.state = s; MutexUnlock(&j->lock);
}

static void RecordError(ShsSyncJob *j, const char *what, const char *rel)
{
    DbgLog("error: %s: %s", what, rel);
    MutexLock(&j->lock);
    if (j->st.errorCount++ == 0) snprintf(j->st.errorMsg, sizeof j->st.errorMsg, "%s: %s", what, rel);
    MutexUnlock(&j->lock);
}

/* Adds a planned file to the full list; returns its id (index + 1). */
/* Appends to the full list (caller holds the lock; `name` is copied). Returns the id, 0 on failure. */
static uint32_t RecPushLocked(ShsSyncJob *j, const char *name, uint64_t size, int state)
{
    char *copy = (char *)malloc(strlen(name) + 1);
    if (!copy) return 0;
    strcpy(copy, name);
    if (j->recN == j->recCap) {
        size_t nc = j->recCap ? j->recCap * 2 : 256;
        FileRec *nv = (FileRec *)realloc(j->recs, nc * sizeof(FileRec));
        if (!nv) { free(copy); return 0; }
        j->recs = nv; j->recCap = nc;
    }
    j->recs[j->recN++] = (FileRec){ copy, size, state };
    return (uint32_t)j->recN;
}

static uint32_t RecAdd(ShsSyncJob *j, const char *rel, uint64_t size)
{
    char name[SHS_SYNC_PATH_MAX * 2];
    snprintf(name, sizeof name, "%s%s", j->prefix, rel);     /* worker thread: owns j->prefix */
    MutexLock(&j->lock);
    uint32_t id = RecPushLocked(j, name, size, SHS_FILE_PENDING);
    MutexUnlock(&j->lock);
    return id;
}

static void RecSetLocked(ShsSyncJob *j, uint32_t id, int state)
{
    if (id && id <= j->recN) j->recs[id - 1].state = state;
}

/* Records a file operation at the front of the recent list. `id` ties later progress updates to this entry. */
static void RecentPush(ShsSyncJob *j, const char *rel, uint64_t size, int state, uint32_t id)
{
    MutexLock(&j->lock);
    RecSetLocked(j, id, state);
    memmove(&j->st.recent[1], &j->st.recent[0], sizeof(ShsSyncFileInfo) * (SHS_SYNC_RECENT - 1));
    ShsSyncFileInfo *e = &j->st.recent[0];
    size_t len = strlen(rel), skip = len >= SHS_SYNC_NAME_MAX ? len - (SHS_SYNC_NAME_MAX - 1) : 0;
    snprintf(e->name, sizeof e->name, "%s", rel + skip);
    e->size = size; e->done = state == SHS_FILE_COPYING ? 0 : size; e->state = state; e->id = id;
    if (j->st.recentCount < SHS_SYNC_RECENT) j->st.recentCount++;
    MutexUnlock(&j->lock);
}

/* Finds a still-listed entry by id (caller holds the lock). */
static ShsSyncFileInfo *RecentFind(ShsSyncJob *j, uint32_t id)
{
    for (int i = 0; i < j->st.recentCount; i++)
        if (j->st.recent[i].id == id && (j->st.recent[i].state == SHS_FILE_COPYING || j->st.recent[i].state == SHS_FILE_VERIFYING)) return &j->st.recent[i];
    return NULL;
}

static void RecentSetState(ShsSyncJob *j, uint32_t id, int state)
{
    MutexLock(&j->lock);
    RecSetLocked(j, id, state);
    ShsSyncFileInfo *e = RecentFind(j, id);
    if (e) e->state = state;
    MutexUnlock(&j->lock);
}

static void RecentFinish(ShsSyncJob *j, uint32_t id, bool ok)
{
    MutexLock(&j->lock);
    RecSetLocked(j, id, ok ? SHS_FILE_DONE : SHS_FILE_FAILED);
    ShsSyncFileInfo *e = RecentFind(j, id);
    if (e) { e->state = ok ? SHS_FILE_DONE : SHS_FILE_FAILED; if (ok) e->done = e->size; }
    MutexUnlock(&j->lock);
}

/* Called by every copy thread after each chunk. */
static void AddBytes(ShsSyncJob *j, uint64_t n, uint32_t id)
{
    double now = NowSeconds();
    MutexLock(&j->lock);
    j->winBytes += n;
    j->st.bytesDone += n;
    ShsSyncFileInfo *e = RecentFind(j, id);
    if (e) e->done += n;
    if (now - j->winStart >= 0.5) {
        j->st.rateMBps = (float)((double)j->winBytes / (1024.0 * 1024.0) / (now - j->winStart));
        j->winStart = now; j->winBytes = 0;
    }
    MutexUnlock(&j->lock);
}

/* Blocks while paused; returns false if cancelled. Safe to call from several threads. */
static bool Checkpoint(ShsSyncJob *j)
{
    if (j->paused && !j->cancel) {
        SetState(j, SHS_SYNC_PAUSED);
        while (j->paused && !j->cancel) SleepMs(50);
        if (!j->cancel) {
            MutexLock(&j->lock);
            j->st.state = SHS_SYNC_RUNNING; j->winStart = NowSeconds(); j->winBytes = 0;
            MutexUnlock(&j->lock);
        }
    }
    return !j->cancel;
}
/* ---- copy pipeline: read-ahead thread + parallel writers ----------------------------------------------------------
   The reader walks the files to copy in directory order and reads small ones completely into memory (bounded by the
   buffer budget), so a hard drive sees one sequential read stream instead of many threads seeking. Writer threads take
   files in the same order: buffered files are written from memory, large files are streamed with a chunk size that
   grows with the file. */

#define MAX_COPY_THREADS 16

static volatile int g_cfgThreads = 4;
static volatile int g_cfgBufMiB  = 64;

void ShsSyncConfigure(int threads, int bufferMiB)
{
    g_cfgThreads = threads < 1 ? 1 : (threads > MAX_COPY_THREADS ? MAX_COPY_THREADS : threads);
    g_cfgBufMiB  = bufferMiB < 0 ? 0 : (bufferMiB > 1024 ? 1024 : bufferMiB);
}

#ifdef _WIN32
typedef DWORD (WINAPI *ThreadFn)(LPVOID);
  #define THREAD_FN(name)  static DWORD WINAPI name(LPVOID arg)
  #define THREAD_RESULT    0
static bool ThreadStart(ShsThread *t, ThreadFn fn, void *arg) { *t = CreateThread(NULL, 0, fn, arg, 0, NULL); return *t != NULL; }
static void ThreadJoin(ShsThread t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
typedef void *(*ThreadFn)(void *);
  #define THREAD_FN(name)  static void *name(void *arg)
  #define THREAD_RESULT    NULL
static bool ThreadStart(ShsThread *t, ThreadFn fn, void *arg) { return pthread_create(t, NULL, fn, arg) == 0; }
static void ThreadJoin(ShsThread t) { pthread_join(t, NULL); }
#endif

/* ---- parallel folder scan ---------------------------------------------------------------------------------------------
   Listing a big tree is one metadata call per folder, so it waits on the drive far more than it works. The first levels
   are listed on the calling thread until there are enough folders to share out, then a few threads list the rest at the
   same time. The lists are merged and sorted afterwards, so the order they are found in does not matter. */

#define SCAN_FANOUT   8     /* share the work out once this many folders are waiting */
#define SCAN_MAX_THR  4

typedef struct {
    const char    *root;
    ShsSyncJob    *errJob;
    volatile int  *cancel;
    char         **dirs;     /* folders whose contents are not listed yet */
    size_t         nDirs, next;
    ShsMutex       lock;
} ScanShared;

typedef struct { ScanShared *sh; EntryList out; bool ok; } ScanWorker;

THREAD_FN(ScanWorkerMain)
{
    ScanWorker *w = (ScanWorker *)arg;
    ScanShared *s = w->sh;
    w->ok = true;
    for (;;) {
        MutexLock(&s->lock);
        size_t i = s->next < s->nDirs ? s->next++ : (size_t)-1;
        MutexUnlock(&s->lock);
        if (i == (size_t)-1 || *s->cancel) break;
        if (!ScanDir(s->root, s->dirs[i], &w->out, s->cancel, s->errJob, true)) { w->ok = false; break; }
    }
    return THREAD_RESULT;
}

static void ScanFreeDirs(char **d, size_t n)
{
    for (size_t i = 0; i < n; i++) free(d[i]);
    free(d);
}

/* Appends `from`'s entries to `to` (taking over their strings) and empties `from`. */
static bool ListMove(EntryList *to, EntryList *from)
{
    if (from->n) {
        if (to->n + from->n > to->cap) {
            size_t nc = to->n + from->n;
            Entry *nv = (Entry *)realloc(to->v, nc * sizeof(Entry));
            if (!nv) return false;
            to->v = nv; to->cap = nc;
        }
        memcpy(to->v + to->n, from->v, from->n * sizeof(Entry));
        to->n += from->n;
    }
    free(from->v);
    memset(from, 0, sizeof *from);
    return true;
}

/* Like ScanDir(root, "", out, cancel, errJob, true), using up to `threads` threads. */
static bool ScanParallel(const char *root, EntryList *out, volatile int *cancel, ShsSyncJob *errJob, int threads)
{
    if (threads > SCAN_MAX_THR) threads = SCAN_MAX_THR;
    if (threads <= 1) return ScanDir(root, "", out, cancel, errJob, true);

    char **cur = (char **)malloc(sizeof *cur);               /* folders whose children are still to be listed */
    if (!cur) return ScanDir(root, "", out, cancel, errJob, true);
    cur[0] = (char *)calloc(1, 1);
    size_t nCur = cur[0] ? 1 : 0;
    if (!nCur) { free(cur); return ScanDir(root, "", out, cancel, errJob, true); }

    for (int level = 0; level < 4 && nCur > 0 && nCur < SCAN_FANOUT && !*cancel; level++) {
        char **nxt = NULL; size_t nNxt = 0, capNxt = 0;
        for (size_t k = 0; k < nCur; k++) {
            size_t before = out->n;
            if (!ScanDir(root, cur[k], out, cancel, errJob, false)) { ScanFreeDirs(cur, nCur); ScanFreeDirs(nxt, nNxt); return false; }
            for (size_t e = before; e < out->n; e++) {
                if (!out->v[e].isDir) continue;
                if (nNxt == capNxt) {
                    size_t nc = capNxt ? capNxt * 2 : 16;
                    char **g = (char **)realloc(nxt, nc * sizeof *g);
                    if (!g) { ScanFreeDirs(cur, nCur); ScanFreeDirs(nxt, nNxt); return false; }
                    nxt = g; capNxt = nc;
                }
                char *copy = (char *)malloc(strlen(out->v[e].rel) + 1);
                if (!copy) { ScanFreeDirs(cur, nCur); ScanFreeDirs(nxt, nNxt); return false; }
                strcpy(copy, out->v[e].rel);
                nxt[nNxt++] = copy;
            }
        }
        ScanFreeDirs(cur, nCur);
        cur = nxt; nCur = nNxt;
    }
    if (nCur == 0 || *cancel) { ScanFreeDirs(cur, nCur); return true; }

    ScanShared sh = { root, errJob, cancel, cur, nCur, 0 };
    MutexInit(&sh.lock);
    int nThr = (int)(nCur < (size_t)threads ? nCur : (size_t)threads);
    ScanWorker workers[SCAN_MAX_THR];
    ShsThread th[SCAN_MAX_THR];
    bool started[SCAN_MAX_THR];
    for (int k = 0; k < nThr; k++) {
        memset(&workers[k], 0, sizeof workers[k]);
        workers[k].sh = &sh;
        workers[k].ok = true;
        started[k] = k > 0 && ThreadStart(&th[k], ScanWorkerMain, &workers[k]);
    }
    ScanWorkerMain(&workers[0]);                            /* this thread works too */
    for (int k = 1; k < nThr; k++) {
        if (started[k]) ThreadJoin(th[k]);
        else            ScanWorkerMain(&workers[k]);        /* could not start it: run its share here */
    }
    bool ok = true;
    for (int k = 0; k < nThr; k++) {
        if (!workers[k].ok) ok = false;
        if (!ListMove(out, &workers[k].out)) ok = false;
        ListFree(&workers[k].out);
    }
    MutexDestroy(&sh.lock);
    ScanFreeDirs(cur, nCur);
    return ok;
}

typedef struct { const char *root; EntryList list; volatile int *cancel; int threads; bool ok; int err; } TargetScan;

THREAD_FN(TargetScanMain)
{
    TargetScan *t = (TargetScan *)arg;
    t->ok = ScanParallel(t->root, &t->list, t->cancel, NULL, t->threads);
    t->err = t_scanErr;
    return THREAD_RESULT;
}

typedef struct {
    const Entry   *e;
    char          *dstRel;   /* target path when it differs from e->rel (Keep both), else NULL; freed by RunJob */
    uint32_t       id;
    bool           fresh;    /* nothing exists at the target path: a partial file can safely be left behind and removed on failure */
    unsigned char *buf;      /* whole file when it was read ahead, else NULL (streamed by the writer) */
    size_t         len;
} CopyItem;

#define SMALL_DIRECT (1u << 20)   /* new files up to this size are written straight to their final name (no temp file + rename) */

typedef struct {
    ShsSyncJob *j;
    CopyItem   *items;
    size_t      n;
    ShsMutex    lock;               /* guards everything below */
    ShsCond     cv;                 /* broadcast whenever anything below changes */
    size_t      produced;          /* items [0, produced) are ready for a writer */
    size_t      taken;              /* next item a writer will take */
    uint64_t    buffered, budget;   /* bytes held in read-ahead buffers / allowed */
    uint64_t    maxPrefetchFile;    /* only files up to this size are read ahead */
    int         readerDone;
    int         bigLimit, bigActive;/* how many large files may stream at once (see SameVolume) */
} Pipe;

#define BIG_FILE (16u << 20)        /* files above this stream in large blocks; their concurrency is limited */

/* True when both folders live on the same volume / device. Large files copied in parallel on one disk only fight
   over the same heads or flash channels, so there they are copied one at a time. */
static bool SameVolume(const char *a, const char *b)
{
#ifdef _WIN32
    wchar_t *wa = Widen(a), *wb = Widen(b);
    wchar_t va[MAX_PATH] = L"", vb[MAX_PATH] = L"";
    bool ok = wa && wb && GetVolumePathNameW(wa, va, MAX_PATH) && GetVolumePathNameW(wb, vb, MAX_PATH);
    bool same = ok && _wcsicmp(va, vb) == 0;
    free(wa); free(wb);
    return same;
#else
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev;
#endif
}

/* True when the target lives on a removable drive (USB stick, SD card). Only detected on Windows. */
static bool TargetIsRemovable(const char *path)
{
#ifdef _WIN32
    wchar_t *w = Widen(path);
    wchar_t vol[MAX_PATH] = L"";
    bool removable = w && GetVolumePathNameW(w, vol, MAX_PATH) && GetDriveTypeW(vol) == DRIVE_REMOVABLE;
    free(w);
    return removable;
#else
    (void)path;
    return false;
#endif
}

/* Chunk size grows with the file: small files in one go, big files in large sequential blocks. */
static size_t ChunkFor(uint64_t size)
{
    if (size < (1u << 20))   return size < 65536 ? 65536 : (size_t)size;
    if (size < (32u << 20))  return 1u << 20;
    if (size < (512u << 20)) return 4u << 20;
    return 8u << 20;
}

/* Like FsOpen, but unbuffered (we already move large blocks) and with an access hint on Windows. */
static FILE *FsOpenFast(const char *path, bool write)
{
    FILE *f = FsOpen(path, write);
    if (f) setvbuf(f, NULL, _IONBF, 0);
    return f;
}

static unsigned char *ReadWhole(const ShsSyncJob *j, const Entry *e, size_t *outLen)
{
    char src[SHS_SYNC_PATH_MAX * 2];
    JoinPath(src, sizeof src, j->source, e->rel);
    FILE *in = FsOpenFast(src, false);
    if (!in) return NULL;
    size_t want = (size_t)e->size;
    unsigned char *buf = (unsigned char *)malloc(want ? want : 1);
    if (!buf) { fclose(in); return NULL; }
    size_t n = fread(buf, 1, want, in);
    bool ok = n == want && !ferror(in);
    fclose(in);
    if (!ok) { free(buf); return NULL; }              /* the writer will stream the file instead */
    *outLen = n;
    return buf;
}

/* Fast 64-bit content hash (8 bytes per step) used by Verify. It detects corruption; it is not a cryptographic hash.
   The result does not depend on how the data is split into chunks. */
#define HASH_SEED 0x9E3779B97F4A7C15ull

typedef struct { uint64_t h, total; unsigned char tail[8]; size_t tailLen; } Hasher;

static void HashInit(Hasher *s) { s->h = HASH_SEED; s->total = 0; s->tailLen = 0; }

static void HashMix(Hasher *s, uint64_t v)
{
    s->h = (s->h ^ v) * 0xFF51AFD7ED558CCDull;
    s->h ^= s->h >> 32;
}

static void HashAdd(Hasher *s, const unsigned char *p, size_t n)
{
    s->total += n;
    if (s->tailLen) {                                       /* finish a partial 8-byte word from the previous call */
        while (n && s->tailLen < 8) { s->tail[s->tailLen++] = *p++; n--; }
        if (s->tailLen == 8) { uint64_t v; memcpy(&v, s->tail, 8); HashMix(s, v); s->tailLen = 0; }
    }
    while (n >= 8) { uint64_t v; memcpy(&v, p, 8); HashMix(s, v); p += 8; n -= 8; }
    while (n) { s->tail[s->tailLen++] = *p++; n--; }       /* fewer than 8 bytes left */
}

static uint64_t HashFinal(const Hasher *s)
{
    uint64_t tail = 0;
    memcpy(&tail, s->tail, s->tailLen);
    uint64_t h = (s->h ^ tail ^ (s->total << 3)) * 0xC4CEB9FE1A85EC53ull;
    return h ^ (h >> 29);
}

enum { VERIFY_MISMATCH = 0, VERIFY_OK = 1, VERIFY_ABORTED = 2 };

/* Reads `path` back and compares its hash with `expected`. */
static int VerifyFile(ShsSyncJob *j, uint32_t id, const char *path, uint64_t size, uint64_t expected)
{
    FILE *in = FsOpenFast(path, false);
    if (!in) return VERIFY_MISMATCH;
    size_t chunk = ChunkFor(size);
    unsigned char *buf = (unsigned char *)malloc(chunk);
    if (!buf) { fclose(in); return VERIFY_MISMATCH; }
    Hasher hs; HashInit(&hs);
    int result = VERIFY_OK;
    for (;;) {
        if (j->cancel || j->skipId == (int)id) { result = VERIFY_ABORTED; break; }
        if (!Checkpoint(j)) { result = VERIFY_ABORTED; break; }
        size_t n = fread(buf, 1, chunk, in);
        if (n == 0) { if (ferror(in)) result = VERIFY_MISMATCH; break; }
        HashAdd(&hs, buf, n);
    }
    free(buf);
    fclose(in);
    if (result == VERIFY_OK && HashFinal(&hs) != expected) result = VERIFY_MISMATCH;
    return result;
}

static void ReleaseItem(Pipe *p, CopyItem *it)
{
    if (!it->buf) return;
    free(it->buf);
    it->buf = NULL;
    MutexLock(&p->lock); p->buffered -= it->len; CondBroadcast(&p->cv); MutexUnlock(&p->lock);
}

/* ---- overlapped read / write for large streamed files -------------------------------------------------------------
   A helper thread reads the next blocks of the source while the copy thread writes the previous ones, so a copy between
   two drives runs at the speed of the slower drive instead of read time + write time. */

#define RA_SLOTS 3

typedef struct {
    FILE          *in;
    size_t         chunk;
    unsigned char *buf[RA_SLOTS];
    size_t         len[RA_SLOTS];
    int            head, tail, count;    /* ring of filled slots */
    int            done, err, stop;
    ShsMutex       m;
    ShsCond        cv;
} ReadAhead;

THREAD_FN(ReadAheadMain)
{
    ReadAhead *r = (ReadAhead *)arg;
    for (;;) {
        MutexLock(&r->m);
        while (r->count == RA_SLOTS && !r->stop) CondWait(&r->cv, &r->m, 50);
        if (r->stop) { MutexUnlock(&r->m); break; }
        int idx = r->head;
        MutexUnlock(&r->m);

        size_t n = fread(r->buf[idx], 1, r->chunk, r->in);

        MutexLock(&r->m);
        if (n == 0) { r->done = 1; r->err = ferror(r->in) ? 1 : 0; CondBroadcast(&r->cv); MutexUnlock(&r->m); break; }
        r->len[idx] = n;
        r->head = (r->head + 1) % RA_SLOTS;
        r->count++;
        CondBroadcast(&r->cv);
        MutexUnlock(&r->m);
    }
    return THREAD_RESULT;
}

enum { COPY_FAILED = 0, COPY_OK = 1, COPY_SKIPPED = 2, COPY_FAILED_REPORTED = 3 /* the error was already recorded */ };

static int CopyFileItem(ShsSyncJob *j, CopyItem *it)
{
    const Entry *e = it->e;
    char src[SHS_SYNC_PATH_MAX * 2], dst[SHS_SYNC_PATH_MAX * 2], tmp[SHS_SYNC_PATH_MAX * 2 + 16];
    JoinPath(src, sizeof src, j->source, e->rel);
    JoinPath(dst, sizeof dst, j->target, it->dstRel ? it->dstRel : e->rel);
    const bool direct = j->direct || (it->fresh && e->size <= SMALL_DIRECT);
    if (direct) snprintf(tmp, sizeof tmp, "%s", dst);             /* removable target, or a small new file: write the final name, skip the rename */
    else           snprintf(tmp, sizeof tmp, "%s.shs-tmp", dst);

    const bool verify = j->verify != 0;           /* decided per file, when it starts */
    Hasher hs; HashInit(&hs);
    uint64_t copied = 0;
    bool skipped = false;

    RecentPush(j, e->rel, e->size, SHS_FILE_COPYING, it->id);
    MutexLock(&j->lock);
    snprintf(j->st.currentFile, sizeof j->st.currentFile, "%s", e->rel);
    MutexUnlock(&j->lock);

    FILE *out = FsOpenFast(tmp, true);
    bool ok = out != NULL;
    if (ok && it->buf) {                                           /* read ahead: write from memory */
        if (verify) HashAdd(&hs, it->buf, it->len);
        const size_t step = 1u << 20;                              /* 1 MiB: pause / cancel / progress granularity */
        size_t off = 0;
        while (ok && off < it->len) {
            if (j->skipId == (int)it->id) { skipped = true; ok = false; break; }
            if (!Checkpoint(j)) { ok = false; break; }
            size_t n = it->len - off < step ? it->len - off : step;
            if (fwrite(it->buf + off, 1, n, out) != n) { ok = false; break; }
            off += n; copied += n;
            AddBytes(j, n, it->id);
        }
    } else if (ok) {                                               /* stream with a size-appropriate chunk */
        FILE *in = FsOpenFast(src, false);
        size_t chunk = ChunkFor(e->size);
        ok = in != NULL;
        bool overlapped = false;
        if (ok && e->size >= (4u << 20)) {                         /* large file: read the next block while the previous one is written */
            ReadAhead ra;
            memset(&ra, 0, sizeof ra);
            ra.in = in; ra.chunk = chunk;
            bool bufsOk = true;
            for (int k = 0; k < RA_SLOTS; k++) { ra.buf[k] = (unsigned char *)malloc(chunk); if (!ra.buf[k]) bufsOk = false; }
            ShsThread rt;
            bool started = false;
            if (bufsOk) {
                MutexInit(&ra.m); CondInit(&ra.cv);
                started = ThreadStart(&rt, ReadAheadMain, &ra);
                if (!started) { MutexDestroy(&ra.m); CondDestroy(&ra.cv); }
            }
            if (started) {
                overlapped = true;
                while (ok) {
                    if (j->skipId == (int)it->id) { skipped = true; ok = false; break; }
                    if (!Checkpoint(j)) { ok = false; break; }
                    MutexLock(&ra.m);
                    while (ra.count == 0 && !ra.done && !j->cancel && j->skipId != (int)it->id) CondWait(&ra.cv, &ra.m, 50);
                    if (ra.count == 0) {
                        bool finished = ra.done != 0, failed = ra.err != 0;
                        MutexUnlock(&ra.m);
                        if (!finished) continue;                   /* woken by cancel / skip: the checks at the top handle it */
                        ok = !failed;
                        break;
                    }
                    int idx = ra.tail;
                    size_t n = ra.len[idx];
                    MutexUnlock(&ra.m);

                    if (verify) HashAdd(&hs, ra.buf[idx], n);
                    if (fwrite(ra.buf[idx], 1, n, out) != n) { ok = false; break; }
                    copied += n;
                    AddBytes(j, n, it->id);

                    MutexLock(&ra.m);
                    ra.tail = (ra.tail + 1) % RA_SLOTS;
                    ra.count--;
                    CondBroadcast(&ra.cv);
                    MutexUnlock(&ra.m);
                }
                MutexLock(&ra.m); ra.stop = 1; CondBroadcast(&ra.cv); MutexUnlock(&ra.m);
                ThreadJoin(rt);
                MutexDestroy(&ra.m); CondDestroy(&ra.cv);
            }
            for (int k = 0; k < RA_SLOTS; k++) free(ra.buf[k]);
        }
        char *buf = (in && !overlapped) ? (char *)malloc(chunk) : NULL;
        if (!overlapped) ok = in != NULL && buf != NULL;
        while (ok && !overlapped) {
            if (j->skipId == (int)it->id) { skipped = true; ok = false; break; }
            if (!Checkpoint(j)) { ok = false; break; }
            size_t n = fread(buf, 1, chunk, in);
            if (n == 0) { ok = !ferror(in); break; }
            if (verify) HashAdd(&hs, (const unsigned char *)buf, n);
            if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
            copied += n;
            AddBytes(j, n, it->id);
        }
        free(buf);
        if (in) fclose(in);
    }
    bool mtimeDone = false;
#ifdef _WIN32
    if (ok && out) {                                               /* set the date on the handle we already have: one metadata call fewer per file */
        unsigned long long t = ((unsigned long long)(e->mtime + 11644473600LL)) * 10000000ULL;
        FILETIME ft = { (DWORD)(t & 0xFFFFFFFFu), (DWORD)(t >> 32) };
        mtimeDone = SetFileTime((HANDLE)_get_osfhandle(_fileno(out)), NULL, NULL, &ft) != 0;
    }
#endif
    if (out && fclose(out) != 0) ok = false;
    if (!ok && !skipped && !j->cancel)
        DbgLog("copy failed: %s -> %s (%s, os error %d, errno %d)", src, dst, out ? "write/read" : "cannot create the temp file", LastOsError(), errno);

    if (skipped) {                                                 /* drop the partial copy and take it out of the totals */
        RemoveFile(tmp);
        MutexLock(&j->lock);
        if (j->st.filesTotal) j->st.filesTotal--;
        j->st.bytesTotal = j->st.bytesTotal >= e->size ? j->st.bytesTotal - e->size : 0;
        j->st.bytesDone  = j->st.bytesDone  >= copied  ? j->st.bytesDone  - copied  : 0;
        j->st.filesSkipped++;
        RecSetLocked(j, it->id, SHS_FILE_SKIPPED);
        ShsSyncFileInfo *rf = RecentFind(j, it->id);
        if (rf) rf->state = SHS_FILE_SKIPPED;
        MutexUnlock(&j->lock);
        return COPY_SKIPPED;
    }

    if (ok && verify) {                                            /* read the written copy back and compare */
        RecentSetState(j, it->id, SHS_FILE_VERIFYING);
        int v = VerifyFile(j, it->id, tmp, e->size, HashFinal(&hs));
        if (v == VERIFY_MISMATCH) { RemoveFile(tmp); RecentFinish(j, it->id, false); RecordError(j, "Verify failed", e->rel); return COPY_FAILED_REPORTED; }
        else if (v == VERIFY_OK) { MutexLock(&j->lock); j->st.filesVerified++; MutexUnlock(&j->lock); }
        else if (j->cancel) ok = false;                            /* aborted by cancel; a skipped verify keeps the copy */
        if (j->skipId == (int)it->id) j->skipId = 0;
    }

    if (ok && !direct) ok = FsReplace(tmp, dst);
    if (ok && !mtimeDone) SetMtime(dst, e->mtime);
    if (!ok) RemoveFile(tmp);
    RecentFinish(j, it->id, ok);
    return ok ? COPY_OK : COPY_FAILED;
}
THREAD_FN(ReaderMain)
{
    Pipe *p = (Pipe *)arg;
    ShsSyncJob *j = p->j;
    for (size_t i = 0; i < p->n && !j->cancel; i++) {
        if (!Checkpoint(j)) break;
        CopyItem *it = &p->items[i];
        uint64_t size = it->e->size;
        if (size > 0 && size <= p->maxPrefetchFile) {
            MutexLock(&p->lock);                                   /* wait until the budget has room */
            while (p->buffered + size > p->budget && !j->cancel) CondWait(&p->cv, &p->lock, 50);
            MutexUnlock(&p->lock);
            if (j->cancel) break;
            size_t len = 0;
            it->buf = ReadWhole(j, it->e, &len);
            if (it->buf) { it->len = len; MutexLock(&p->lock); p->buffered += len; MutexUnlock(&p->lock); }
        }
        MutexLock(&p->lock); p->produced = i + 1; CondBroadcast(&p->cv); MutexUnlock(&p->lock);
    }
    MutexLock(&p->lock); p->readerDone = 1; CondBroadcast(&p->cv); MutexUnlock(&p->lock);
    return THREAD_RESULT;
}

THREAD_FN(WriterMain)
{
    Pipe *p = (Pipe *)arg;
    ShsSyncJob *j = p->j;
    for (;;) {
        MutexLock(&p->lock);
        size_t idx = (size_t)-1;
        bool finished = false;
        for (;;) {                                                   /* sleep until an item is ready or the reader is done */
            if (p->taken < p->produced) { idx = p->taken++; break; }
            if (p->readerDone) { finished = true; break; }
            CondWait(&p->cv, &p->lock, 50);
        }
        MutexUnlock(&p->lock);
        if (finished) break;

        CopyItem *it = &p->items[idx];
        bool big = !it->buf && it->e->size > BIG_FILE;               /* streamed large file: take a slot first */
        if (big) {
            MutexLock(&p->lock);
            while (p->bigActive >= p->bigLimit && !j->cancel) CondWait(&p->cv, &p->lock, 50);
            big = p->bigActive < p->bigLimit;
            if (big) p->bigActive++;
            MutexUnlock(&p->lock);
        }
        if (!j->cancel) {
            int r = CopyFileItem(j, it);
            if (r == COPY_OK) { MutexLock(&j->lock); j->st.filesDone++; MutexUnlock(&j->lock); }
            else if (r == COPY_FAILED && !j->cancel) RecordError(j, "Copy failed", it->e->rel);
        }
        if (big) { MutexLock(&p->lock); p->bigActive--; CondBroadcast(&p->cv); MutexUnlock(&p->lock); }
        ReleaseItem(p, it);
    }
    return THREAD_RESULT;
}

static void RunCopies(ShsSyncJob *j, CopyItem *items, size_t n)
{
    if (n == 0) return;
    Pipe p;
    memset(&p, 0, sizeof p);
    p.j = j; p.items = items; p.n = n;
    MutexInit(&p.lock);
    CondInit(&p.cv);
    p.budget = (uint64_t)g_cfgBufMiB << 20;
    p.maxPrefetchFile = p.budget / 4 < (32u << 20) ? p.budget / 4 : (32u << 20);

    int threads = g_cfgThreads;
    j->direct = TargetIsRemovable(j->target);
    if (j->direct && threads > 1) {            /* a USB stick is one slow flash device: parallel writers only slow it down */
        DbgLog("target is a removable drive: using 1 writer instead of %d", threads);
        threads = 1;
    }
    if ((size_t)threads > n) threads = (int)n;
    if (threads < 1) threads = 1;
    p.bigLimit = SameVolume(j->source, j->target) ? 1 : (threads < 3 ? threads : 3);

    ShsThread reader, writers[MAX_COPY_THREADS];
    bool readerStarted = false;
    if (p.maxPrefetchFile >= 64 * 1024) readerStarted = ThreadStart(&reader, ReaderMain, &p);
    if (!readerStarted) { p.produced = n; p.readerDone = 1; }        /* no read-ahead: writers stream everything */

    int started = 0;
    for (int k = 0; k < threads; k++) if (ThreadStart(&writers[started], WriterMain, &p)) started++;
    if (started == 0) WriterMain(&p);                                /* could not start threads: copy on this one */

    for (int k = 0; k < started; k++) ThreadJoin(writers[k]);
    if (readerStarted) ThreadJoin(reader);

    for (size_t i = 0; i < n; i++) ReleaseItem(&p, &items[i]);      /* anything left after a cancel */
    MutexDestroy(&p.lock);
    CondDestroy(&p.cv);
}
static bool NeedsCopy(const Entry *s, const Entry *t)
{
    if (!t || t->isDir) return true;
    if (s->size != t->size) return true;
    int64_t d = s->mtime - t->mtime;
    return d > MTIME_SLACK_S || d < -MTIME_SLACK_S;
}

/* Whether a source file is copied. Sync tasks copy new and changed files; Quick Copy copies missing files and the
   existing ones the conflict decision `act` says to replace or keep alongside. */
static bool WillCopy(const Entry *s, const Entry *t, int act, bool quick)
{
    if (!quick) return NeedsCopy(s, t);
    if (!t || t->isDir) return true;
    return act == SHS_CONFLICT_OVERWRITE || act == SHS_CONFLICT_KEEP_BOTH || act == SHS_CONFLICT_RENAME_TARGET;
}

/* Size, mtime and kind of one path. */
static bool StatPath(const char *path, uint64_t *size, int64_t *mtime, bool *isDir)
{
#ifdef _WIN32
    wchar_t *w = Widen(path);
    if (!w) return false;
    WIN32_FILE_ATTRIBUTE_DATA d;
    BOOL ok = GetFileAttributesExW(w, GetFileExInfoStandard, &d);
    free(w);
    if (!ok) return false;
    unsigned long long ft = ((unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    *mtime = (int64_t)(ft / 10000000ULL) - 11644473600LL;
    *size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    *isDir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *mtime = (int64_t)st.st_mtime;
    *size = (uint64_t)st.st_size;
    *isDir = S_ISDIR(st.st_mode);
    return true;
#endif
}

static const char *BaseNameOf(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++) if ((*p == '/' || *p == '\\') && p[1]) b = p + 1;
    return b;
}

/* Points the job at the next batch: a folder (its contents -> target; added folders go to <target>/<name>) or a single file. */
static bool SetupBatch(ShsSyncJob *j, const char *path, bool added)
{
    uint64_t sz; int64_t mt; bool isDir;
    j->onlyName[0] = 0; j->prefix[0] = 0; j->batchMirror = false;
    if (!StatPath(path, &sz, &mt, &isDir)) { RecordError(j, "Cannot read source", path); return false; }

    char clean[SHS_SYNC_PATH_MAX];
    snprintf(clean, sizeof clean, "%s", path);
    for (char *s = clean + strlen(clean); s > clean + 1 && (s[-1] == '/' || s[-1] == '\\'); ) *--s = 0;
    const char *base = BaseNameOf(clean);

    if (isDir) {
        snprintf(j->source, sizeof j->source, "%s", clean);
        if (added) {
            char name[SHS_SYNC_PATH_MAX];                            /* a drive root "D:" becomes the folder "D" */
            size_t k = 0;
            for (const char *c = base; *c && k + 1 < sizeof name; c++) if (*c != ':' && *c != '/' && *c != '\\') name[k++] = *c;
            name[k] = 0;
            if (!name[0]) snprintf(name, sizeof name, "Copy");
            JoinPath(j->target, sizeof j->target, j->baseTarget, name);
            snprintf(j->prefix, sizeof j->prefix, "%s/", name);
        } else {
            snprintf(j->target, sizeof j->target, "%s", j->baseTarget);
            j->batchMirror = j->mirror;
        }
    } else {
        size_t cut = (size_t)(base - clean);                         /* the parent folder, keeping a root's separator */
        if (cut > 0 && !(cut == 3 && clean[1] == ':') && cut != 1) cut--;
        snprintf(j->source, sizeof j->source, "%.*s", (int)cut, clean);
        snprintf(j->onlyName, sizeof j->onlyName, "%s", base);
        snprintf(j->target, sizeof j->target, "%s", j->baseTarget);
    }
    return true;
}

/* Decides what happens to a source file whose target already exists. Asks the UI when the policy is ASK. */
static int DecideConflict(ShsSyncJob *j, const Entry *s, const Entry *t, const char *srcPath, const char *dstPath)
{
    int pol = j->policy;
    if (pol == SHS_CONFLICT_ASK) {
        MutexLock(&j->lock);
        snprintf(j->st.conflictSrc, sizeof j->st.conflictSrc, "%s", srcPath);
        snprintf(j->st.conflictDst, sizeof j->st.conflictDst, "%s", dstPath);
        j->st.conflictSrcSize = s->size; j->st.conflictDstSize = t->size;
        j->st.conflictSrcMtime = s->mtime; j->st.conflictDstMtime = t->mtime;
        j->resolveSet = 0;
        j->st.conflictPending = true;
        MutexUnlock(&j->lock);
        DbgLog("conflict: %s already exists, waiting for an answer", dstPath);
        while (!j->resolveSet && !j->cancel && j->policy == SHS_CONFLICT_ASK) SleepMs(30);
        MutexLock(&j->lock); j->st.conflictPending = false; MutexUnlock(&j->lock);
        if (j->cancel) return SHS_CONFLICT_SKIP;
        pol = j->policy != SHS_CONFLICT_ASK ? j->policy : j->resolveAction;   /* "all" may have been set meanwhile */
    }
    const int64_t d = s->mtime - t->mtime;                          /* > 0: the target is older than the source */
    switch (pol) {
    case SHS_CONFLICT_OVERWRITE:          return SHS_CONFLICT_OVERWRITE;
    case SHS_CONFLICT_KEEP_BOTH:          return SHS_CONFLICT_KEEP_BOTH;
    case SHS_CONFLICT_RENAME_TARGET:      return SHS_CONFLICT_RENAME_TARGET;
    case SHS_CONFLICT_OVERWRITE_OLDER:    return d > MTIME_SLACK_S ? SHS_CONFLICT_OVERWRITE : SHS_CONFLICT_SKIP;
    case SHS_CONFLICT_REPLACE_SMALLER:    return t->size < s->size ? SHS_CONFLICT_OVERWRITE : SHS_CONFLICT_SKIP;
    case SHS_CONFLICT_REPLACE_DIFF_SIZE:  return t->size != s->size ? SHS_CONFLICT_OVERWRITE : SHS_CONFLICT_SKIP;
    case SHS_CONFLICT_RENAME_OLDER_TARGET:return d > MTIME_SLACK_S ? SHS_CONFLICT_RENAME_TARGET : SHS_CONFLICT_SKIP;
    default:                              return SHS_CONFLICT_SKIP;
    }
}

/* "dir/name.ext" -> "dir/name (2).ext" (or (3), ...) such that nothing exists at that name in the current target. */
static char *UniqueRel(const ShsSyncJob *j, const char *rel)
{
    size_t len = strlen(rel);
    const char *slash = strrchr(rel, '/');
    const char *nameStart = slash ? slash + 1 : rel;
    const char *dot = strrchr(nameStart, '.');
    size_t stem = dot && dot != nameStart ? (size_t)(dot - rel) : len;
    char *out = (char *)malloc(len + 16);
    if (!out) return NULL;
    for (int n = 2; n < 10000; n++) {
        snprintf(out, len + 16, "%.*s (%d)%s", (int)stem, rel, n, rel + stem);
        char full[SHS_SYNC_PATH_MAX * 2];
        uint64_t sz; int64_t mt; bool isDir;
        JoinPath(full, sizeof full, j->target, out);
        if (!StatPath(full, &sz, &mt, &isDir)) return out;
    }
    free(out);
    return NULL;
}

static void RunJob(ShsSyncJob *j)
{
    EntryList src = {0}, dst = {0};
    SetState(j, SHS_SYNC_SCANNING);
    DbgLog("batch: source=%s target=%s%s%s", j->source, j->target, j->onlyName[0] ? " file=" : "", j->onlyName);

    if (!MakeDirs(j->target)) { RecordError(j, "Cannot create target", j->target); DbgLog("cannot create target %s (error %d)", j->target, LastOsError()); return; }
    if (j->onlyName[0]) {                                            /* a single file: no folder scan */
        char sp[SHS_SYNC_PATH_MAX * 2], tp[SHS_SYNC_PATH_MAX * 2];
        JoinPath(sp, sizeof sp, j->source, j->onlyName);
        JoinPath(tp, sizeof tp, j->target, j->onlyName);
        uint64_t sz; int64_t mt; bool isDir;
        if (StatPath(sp, &sz, &mt, &isDir)) ListPush(&src, j->onlyName, sz, mt, false);
        if (StatPath(tp, &sz, &mt, &isDir)) ListPush(&dst, j->onlyName, isDir ? 0 : sz, mt, isDir);
    } else {
        /* Scan the source and the target at the same time, each with a few threads (one on a removable drive). */
        const int srcThr = TargetIsRemovable(j->source) ? 1 : SCAN_MAX_THR, dstThr = TargetIsRemovable(j->target) ? 1 : SCAN_MAX_THR;
        TargetScan ts; memset(&ts, 0, sizeof ts);
        ts.root = j->target; ts.cancel = &j->cancel; ts.threads = dstThr;
        ShsThread tsThread;
        bool tsStarted = ThreadStart(&tsThread, TargetScanMain, &ts);
        bool srcOk = ScanParallel(j->source, &src, &j->cancel, j, srcThr);
        int srcErr = t_scanErr;
        if (tsStarted) ThreadJoin(tsThread); else TargetScanMain(&ts);
        dst = ts.list;
        if (!srcOk && !j->cancel) {
            char what[64]; snprintf(what, sizeof what, "Cannot read source (error %d)", srcErr);
            RecordError(j, what, j->source);
            ListFree(&src); ListFree(&dst); return;
        }
        if (!ts.ok && !j->cancel) {
            char what[64]; snprintf(what, sizeof what, "Cannot read target (error %d)", ts.err);
            RecordError(j, what, j->target);
            ListFree(&src); ListFree(&dst); return;
        }
    }
    if (j->cancel) { ListFree(&src); ListFree(&dst); return; }

    qsort(src.v, src.n, sizeof(Entry), EntryCmp);
    qsort(dst.v, dst.n, sizeof(Entry), EntryCmp);

    /* Quick Copy: settle every file that already exists in the target first (this may wait for the user). */
    unsigned char *act = NULL;                                       /* per source entry: the ShsConflictAction chosen, 0 = none */
    if (j->quick) {
        act = (unsigned char *)calloc(src.n ? src.n : 1, 1);
        for (size_t i = 0; act && i < src.n && !j->cancel; i++) {
            const Entry *s = &src.v[i], *t;
            if (s->isDir || !(t = ListFind(&dst, s->rel)) || t->isDir) continue;
            char sp[SHS_SYNC_PATH_MAX * 2], tp[SHS_SYNC_PATH_MAX * 2];
            JoinPath(sp, sizeof sp, j->source, s->rel);
            JoinPath(tp, sizeof tp, j->target, s->rel);
            act[i] = (unsigned char)DecideConflict(j, s, t, sp, tp);
        }
        if (j->cancel) { free(act); ListFree(&src); ListFree(&dst); return; }
    }

    /* Plan: count what needs copying. */
    uint64_t files = 0, bytes = 0;
    for (size_t i = 0; i < src.n; i++) {
        if (src.v[i].isDir) continue;
        if (WillCopy(&src.v[i], ListFind(&dst, src.v[i].rel), act ? act[i] : 0, j->quick)) { files++; bytes += src.v[i].size; }
    }
    MutexLock(&j->lock);
    j->st.filesTotal += files; j->st.bytesTotal += bytes;       /* batches add up */
    MutexUnlock(&j->lock);
    DbgLog("plan: %zu source entries, %zu target entries, %llu file(s) / %llu byte(s) to copy", src.n, dst.n,
           (unsigned long long)files, (unsigned long long)bytes);
    j->winStart = NowSeconds(); j->winBytes = 0;
    SetState(j, SHS_SYNC_RUNNING);

    /* Pass 1a: create directories (sorted order => parents before children) and collect the files to copy. */
    CopyItem *items = (CopyItem *)calloc(files ? (size_t)files : 1, sizeof(CopyItem));
    size_t nItems = 0;
    for (size_t i = 0; i < src.n && items && Checkpoint(j); i++) {
        const Entry *s = &src.v[i];
        const Entry *t = ListFind(&dst, s->rel);
        char path[SHS_SYNC_PATH_MAX * 2];
        JoinPath(path, sizeof path, j->target, s->rel);

        if (s->isDir) {
            if (t && !t->isDir) RemoveFile(path);       /* file in the way of a directory */
            if (!MakeDirs(path)) RecordError(j, "Cannot create folder", s->rel);
            continue;
        }
        if (!WillCopy(s, t, act ? act[i] : 0, j->quick)) continue;
        if (t && t->isDir) { RecordError(j, "Folder blocks file", s->rel); continue; }
        items[nItems].e = s;
        items[nItems].fresh = !t || (act && (act[i] == SHS_CONFLICT_KEEP_BOTH || act[i] == SHS_CONFLICT_RENAME_TARGET));
        if (act && act[i] == SHS_CONFLICT_KEEP_BOTH) {
            items[nItems].dstRel = UniqueRel(j, s->rel);
            DbgLog("keep both: %s will be copied as %s", s->rel, items[nItems].dstRel ? items[nItems].dstRel : "(no free name)");
            if (!items[nItems].dstRel) { RecordError(j, "No free name for", s->rel); continue; }
        } else if (act && act[i] == SHS_CONFLICT_RENAME_TARGET) {
            char *nr = UniqueRel(j, s->rel), oldp[SHS_SYNC_PATH_MAX * 2], newp[SHS_SYNC_PATH_MAX * 2];
            JoinPath(oldp, sizeof oldp, j->target, s->rel);
            if (nr) JoinPath(newp, sizeof newp, j->target, nr);
            bool renamed = nr && FsReplace(oldp, newp);
            DbgLog("rename target: %s -> %s %s", oldp, nr ? newp : "?", renamed ? "ok" : "FAILED");
            free(nr);
            if (!renamed) { RecordError(j, "Could not rename the existing file", s->rel); continue; }
        }
        items[nItems].id = RecAdd(j, items[nItems].dstRel ? items[nItems].dstRel : s->rel, s->size);
        nItems++;
    }

    /* Pass 1b: copy them: read-ahead thread + parallel writers. */
    if (items && !j->cancel) RunCopies(j, items, nItems);
    if (items) for (size_t i = 0; i < nItems; i++) free(items[i].dstRel);
    free(items);
    free(act);
    /* Pass 2 (mirror): delete target items missing from source, children first. */
    if (j->batchMirror && !j->cancel) {
        for (size_t i = dst.n; i-- > 0 && Checkpoint(j); ) {
            const Entry *t = &dst.v[i];
            const Entry *s = ListFind(&src, t->rel);
            if (s && s->isDir == t->isDir) continue;
            if (s) continue;   /* type mismatch was handled in pass 1 */
            char path[SHS_SYNC_PATH_MAX * 2];
            JoinPath(path, sizeof path, j->target, t->rel);
            bool ok = t->isDir ? RemoveDirEmpty(path) : RemoveFile(path);
            if (ok) { MutexLock(&j->lock); j->st.filesDeleted++; MutexUnlock(&j->lock); if (!t->isDir) RecentPush(j, t->rel, t->size, SHS_FILE_REMOVED, 0); }
            else    RecordError(j, "Delete failed", t->rel);
        }
    }

    ListFree(&src); ListFree(&dst);
}

/* The first batch, then whatever ShsSyncAddPath queued while it ran. The final state is set under the same lock that
   AddPath uses, so an item is either picked up here or refused. */
static void RunAll(ShsSyncJob *j)
{
    if (SetupBatch(j, j->firstSource, j->firstAdded)) RunJob(j);
    for (;;) {
        char path[SHS_SYNC_PATH_MAX];
        MutexLock(&j->lock);
        if (!j->cancel && j->pendN > 0) {
            snprintf(path, sizeof path, "%s", j->pend[0]);
            RecSetLocked(j, j->pendRec[0], SHS_FILE_HIDDEN);          /* its real files are listed from here on */
            memmove(j->pend[0], j->pend[1], (size_t)(j->pendN - 1) * sizeof j->pend[0]);
            memmove(&j->pendRec[0], &j->pendRec[1], (size_t)(j->pendN - 1) * sizeof j->pendRec[0]);
            j->pendN--;
            MutexUnlock(&j->lock);
            if (SetupBatch(j, path, true)) RunJob(j);
            continue;
        }
        j->st.rateMBps = 0;
        j->st.currentFile[0] = 0;
        j->st.state = j->cancel ? SHS_SYNC_CANCELLED : (j->st.errorCount ? SHS_SYNC_FAILED : SHS_SYNC_DONE);
        j->closing = 1;
        MutexUnlock(&j->lock);
        break;
    }
}

#ifdef _WIN32
static DWORD WINAPI WorkerMain(LPVOID p) { RunAll((ShsSyncJob *)p); return 0; }
#else
static void *WorkerMain(void *p) { RunAll((ShsSyncJob *)p); return NULL; }
#endif

/* ---- public API -------------------------------------------------------- */

static ShsSyncJob *StartJob(const char *source, const char *target, bool mirror, bool firstAdded, bool quick, int policy)
{
    if (!source || !target) return NULL;
    ShsSyncJob *j = (ShsSyncJob *)calloc(1, sizeof *j);
    if (!j) return NULL;
    j->firstAdded = firstAdded;
    j->quick = quick;
    j->policy = policy;
    snprintf(j->firstSource, sizeof j->firstSource, "%s", source);
    snprintf(j->baseTarget, sizeof j->baseTarget, "%s", target);
    /* strip trailing separators so JoinPath doesn't double them */
    for (char *s = j->firstSource + strlen(j->firstSource); s > j->firstSource + 1 && (s[-1] == '/' || s[-1] == '\\'); ) *--s = 0;
    for (char *s = j->baseTarget + strlen(j->baseTarget); s > j->baseTarget + 1 && (s[-1] == '/' || s[-1] == '\\'); ) *--s = 0;
    j->mirror = mirror;
    MutexInit(&j->lock);
    j->st.state = SHS_SYNC_PENDING;

#ifdef _WIN32
    j->thread = CreateThread(NULL, 0, WorkerMain, j, 0, NULL);
    j->threadStarted = j->thread != NULL;
#else
    j->threadStarted = pthread_create(&j->thread, NULL, WorkerMain, j) == 0;
#endif
    if (!j->threadStarted) { MutexDestroy(&j->lock); free(j); return NULL; }
    return j;
}

ShsSyncJob *ShsSyncStart(const char *source, const char *target, bool mirror)  { return StartJob(source, target, mirror, false, false, SHS_CONFLICT_SKIP); }
ShsSyncJob *ShsSyncStartQuick(const char *source, const char *target, ShsConflictAction policy) { return StartJob(source, target, false, false, true, (int)policy); }
ShsSyncJob *ShsSyncStartItem(const char *path, const char *target, ShsConflictAction policy)    { return StartJob(path, target, false, true, true, (int)policy); }

void ShsSyncResolve(ShsSyncJob *j, ShsConflictAction action, bool applyToAll)
{
    if (applyToAll) j->policy = (int)action;
    j->resolveAction = (int)action;
    j->resolveSet = 1;
}

void ShsSyncGetStatus(ShsSyncJob *j, ShsSyncStatus *out)
{
    MutexLock(&j->lock); *out = j->st; MutexUnlock(&j->lock);
}

int ShsSyncFileCount(ShsSyncJob *j)
{
    MutexLock(&j->lock);
    int n = 0;
    for (size_t i = 0; i < j->recN; i++) if (j->recs[i].state != SHS_FILE_HIDDEN) n++;
    MutexUnlock(&j->lock);
    return n;
}

int ShsSyncGetFiles(ShsSyncJob *j, unsigned stateMask, int first, int max, ShsSyncFileRow *out, int *total)
{
    int n = 0, seen = 0, matched = 0;
    MutexLock(&j->lock);
    for (size_t i = 0; i < j->recN; i++) {
        if (j->recs[i].state == SHS_FILE_HIDDEN || !((stateMask >> j->recs[i].state) & 1u)) continue;
        matched++;
        if (seen++ < first || n >= max) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", j->recs[i].name);
        out[n].size = j->recs[i].size;
        out[n].state = j->recs[i].state;
        n++;
    }
    MutexUnlock(&j->lock);
    if (total) *total = matched;
    return n;
}

bool ShsSyncAddPath(ShsSyncJob *j, const char *path)
{
    if (!path || !path[0]) return false;
    bool ok = false;
    MutexLock(&j->lock);
    if (!j->closing && !j->cancel) {
        if (j->pendN == j->pendCap) {
            int nc = j->pendCap ? j->pendCap * 2 : 8;
            void *nv = realloc(j->pend, (size_t)nc * sizeof j->pend[0]);
            if (nv) { j->pend = (char (*)[SHS_SYNC_PATH_MAX])nv; j->pendCap = nc; }
            void *nr = realloc(j->pendRec, (size_t)nc * sizeof j->pendRec[0]);
            if (nr) j->pendRec = (uint32_t *)nr; else j->pendCap = j->pendN;     /* keep the arrays the same size */
        }
        if (j->pendN < j->pendCap) {
            /* a placeholder row so the item shows in the file list right away; hidden once its own files are listed */
            uint64_t sz = 0; int64_t mt = 0; bool isDir = false;
            StatPath(path, &sz, &mt, &isDir);
            char label[SHS_SYNC_PATH_MAX + 16];
            snprintf(label, sizeof label, "%s%s (queued)", BaseNameOf(path), isDir ? "/" : "");
            j->pendRec[j->pendN] = RecPushLocked(j, label, isDir ? 0 : sz, SHS_FILE_PENDING);
            snprintf(j->pend[j->pendN++], SHS_SYNC_PATH_MAX, "%s", path);
            ok = true;
        }
    }
    MutexUnlock(&j->lock);
    return ok;
}

void ShsSyncSetPaused(ShsSyncJob *j, bool paused) { j->paused = paused ? 1 : 0; }
void ShsSyncSetVerify(ShsSyncJob *j, bool verify) { j->verify = verify ? 1 : 0; }

void ShsSyncSkipCurrent(ShsSyncJob *j)
{
    MutexLock(&j->lock);
    for (int i = 0; i < j->st.recentCount; i++)                    /* newest in-flight file first */
        if (j->st.recent[i].state == SHS_FILE_COPYING || j->st.recent[i].state == SHS_FILE_VERIFYING) { j->skipId = (int)j->st.recent[i].id; break; }
    MutexUnlock(&j->lock);
}
void ShsSyncCancel(ShsSyncJob *j)                 { j->cancel = 1; }

bool ShsSyncIsFinished(ShsSyncJob *j)
{
    ShsSyncStatus s; ShsSyncGetStatus(j, &s);
    return s.state == SHS_SYNC_DONE || s.state == SHS_SYNC_FAILED || s.state == SHS_SYNC_CANCELLED;
}

void ShsSyncFree(ShsSyncJob *j)
{
    if (!j) return;
    j->cancel = 1;
#ifdef _WIN32
    WaitForSingleObject(j->thread, INFINITE);
    CloseHandle(j->thread);
#else
    pthread_join(j->thread, NULL);
#endif
    MutexDestroy(&j->lock);
    for (size_t i = 0; i < j->recN; i++) free(j->recs[i].name);
    free(j->recs);
    free(j->pend);
    free(j->pendRec);
    free(j);
}

/* ---- dev setup: mock folders ------------------------------------------- */

static bool WriteMock(const char *root, const char *rel, size_t bytes, int64_t mtime)
{
    char path[SHS_SYNC_PATH_MAX * 2];
    JoinPath(path, sizeof path, root, rel);

    char dir[SHS_SYNC_PATH_MAX * 2];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, SHS_SEP);
    if (slash) { *slash = 0; MakeDirs(dir); }

    FILE *f = FsOpen(path, true);
    if (!f) return false;
    char block[4096];
    for (size_t i = 0; i < sizeof block; i++) block[i] = (char)('A' + (i + bytes) % 26);
    for (size_t left = bytes; left > 0; ) {
        size_t n = left < sizeof block ? left : sizeof block;
        if (fwrite(block, 1, n, f) != n) { fclose(f); return false; }
        left -= n;
    }
    fclose(f);
    return SetMtime(path, mtime);
}

bool ShsSyncSetupDevFolders(const char *testDir)
{
    char src[SHS_SYNC_PATH_MAX], dst[SHS_SYNC_PATH_MAX];
    snprintf(src, sizeof src, "%s/source_folder", testDir);
    snprintf(dst, sizeof dst, "%s/target_folder", testDir);
    if (!MakeDirs(src) || !MakeDirs(dst)) return false;

    int64_t now = (int64_t)time(NULL);
    bool ok = true;

    ok &= WriteMock(src, "readme.txt",               1200,            now - 3600);
    ok &= WriteMock(src, "docs/notes.txt",           8 * 1024,        now - 7200);
    ok &= WriteMock(src, "docs/report.pdf",          350 * 1024,      now - 86400);
    ok &= WriteMock(src, "photos/img_001.jpg",       2 * 1024 * 1024, now - 172800);
    ok &= WriteMock(src, "photos/img_002.jpg",       3 * 1024 * 1024, now - 172000);
    ok &= WriteMock(src, "photos/2024/trip/map.png", 640 * 1024,      now - 400000);
    ok &= WriteMock(src, "backup/big_archive.bin",   64 * 1024 * 1024, now - 600);   /* visible progress */

    /* Target: one up-to-date file, one outdated (size differs), one stale extra. */
    ok &= WriteMock(dst, "readme.txt",               1200,            now - 3600);   /* identical: skipped */
    ok &= WriteMock(dst, "docs/notes.txt",           2 * 1024,        now - 999999); /* outdated: updated */
    ok &= WriteMock(dst, "old/obsolete.txt",         512,             now - 999999); /* removed in mirror mode */
    return ok;
}
