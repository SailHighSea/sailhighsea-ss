#ifndef SYNC_ENGINE_H
#define SYNC_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#define SHS_SYNC_PATH_MAX 520
#define SHS_SYNC_MSG_MAX  200

typedef enum {
    SHS_SYNC_PENDING = 0,  /* thread started, not scanning yet */
    SHS_SYNC_SCANNING,
    SHS_SYNC_RUNNING,      /* copying / deleting */
    SHS_SYNC_PAUSED,
    SHS_SYNC_DONE,         /* finished with no errors */
    SHS_SYNC_FAILED,       /* finished, at least one error (see errorMsg) */
    SHS_SYNC_CANCELLED
} ShsSyncState;

#define SHS_SYNC_RECENT    8     /* how many recent file operations a snapshot carries */
#define SHS_SYNC_NAME_MAX  120

typedef enum {
    SHS_FILE_COPYING = 0, SHS_FILE_DONE, SHS_FILE_FAILED, SHS_FILE_REMOVED,
    SHS_FILE_SKIPPED,      /* the user skipped it while it was copying */
    SHS_FILE_VERIFYING,    /* written; being read back and compared */
    SHS_FILE_PENDING,      /* planned, not started yet (only in the full file list) */
    SHS_FILE_HIDDEN        /* internal: a queued item's placeholder row once its files are listed; never returned */
} ShsFileState;

/* One recent file operation (newest first in ShsSyncStatus.recent). */
typedef struct {
    char     name[SHS_SYNC_NAME_MAX];   /* UTF-8, relative to the root; long paths keep their tail */
    uint64_t size, done;                /* bytes (done == size once finished) */
    int      state;                     /* ShsFileState */
    uint32_t id;                        /* identifies the operation while several files copy at once */
} ShsSyncFileInfo;

/* What to do when a file to copy already exists in the target (Quick Copy). Everything except KEEP_BOTH and
   RENAME_TARGET ends up as copy or skip; the rule actions decide per file and are normally applied to all files. */
typedef enum {
    SHS_CONFLICT_ASK = 0,           /* ask for every existing file */
    SHS_CONFLICT_OVERWRITE,         /* replace the target file */
    SHS_CONFLICT_SKIP,              /* leave the target file, do not copy */
    SHS_CONFLICT_KEEP_BOTH,         /* copy under a new name: "name (2).ext" */
    SHS_CONFLICT_RENAME_TARGET,     /* give the existing target file a new name, then copy */
    SHS_CONFLICT_OVERWRITE_OLDER,   /* replace only target files older than the source */
    SHS_CONFLICT_REPLACE_SMALLER,   /* replace only target files smaller than the source */
    SHS_CONFLICT_REPLACE_DIFF_SIZE, /* replace only target files whose size differs */
    SHS_CONFLICT_RENAME_OLDER_TARGET/* rename target files older than the source, then copy; skip the others */
} ShsConflictAction;

/* Snapshot of a job's progress; safe to poll every frame from the UI thread. */
typedef struct {
    ShsSyncState state;
    bool     conflictPending;                 /* the job waits for ShsSyncResolve: the fields below describe the file */
    char     conflictSrc[SHS_SYNC_PATH_MAX], conflictDst[SHS_SYNC_PATH_MAX];   /* full paths */
    uint64_t conflictSrcSize, conflictDstSize;
    int64_t  conflictSrcMtime, conflictDstMtime;                              /* unix seconds */
    uint64_t filesTotal, filesDone;   /* files that need copying */
    uint64_t bytesTotal, bytesDone;
    uint64_t filesDeleted;
    uint64_t filesVerified, filesSkipped;
    uint64_t errorCount;
    float    rateMBps;
    char     currentFile[SHS_SYNC_PATH_MAX];  /* UTF-8, relative to source root */
    char     errorMsg[SHS_SYNC_MSG_MAX];      /* first error encountered */
    ShsSyncFileInfo recent[SHS_SYNC_RECENT];  /* latest copies / removals, newest first */
    int      recentCount;
} ShsSyncStatus;

typedef struct ShsSyncJob ShsSyncJob;

/* Debug log: the engine reports what it scans, plans and why things fail (with OS error codes) through this callback.
   It may be called from any engine thread, one call at a time; `line` has no timestamp or newline. NULL turns it off. */
typedef void (*ShsSyncLogFn)(const char *line);
void ShsSyncSetLogger(ShsSyncLogFn fn);

/* Copy performance, applied to jobs started afterwards. `threads` (1..16): files copied in parallel.
   `bufferMiB` (0..1024): memory used to read small files ahead, in directory order, while the writers drain it
   (0 turns read-ahead off). Defaults: 4 threads, 64 MiB. */
void ShsSyncConfigure(int threads, int bufferMiB);

/* Starts a background one-way sync source -> target (UTF-8 paths).
   A file is copied when it is missing in the target or when its size differs or
   its mtime differs by more than 2 seconds (FAT granularity).
   If `mirror` is true, items in the target that are absent from the source are deleted.
   Returns NULL if the thread could not be started. Never blocks the caller. */
ShsSyncJob *ShsSyncStart(const char *source, const char *target, bool mirror);

/* Quick Copy: copy only (never deletes). Files that already exist in the target are handled by `policy`; with
   SHS_CONFLICT_ASK the job pauses while scanning until the UI answers each one with ShsSyncResolve (see
   ShsSyncStatus.conflictPending). Plain ShsSyncStart never asks: it copies new and changed files. */
ShsSyncJob *ShsSyncStartQuick(const char *source, const char *target, ShsConflictAction policy);

/* Like ShsSyncStartQuick, but for a single file or folder dropped into `target`: a file lands in <target>, a folder in
   <target>/<its name>. Further items can be added with ShsSyncAddPath. */
ShsSyncJob *ShsSyncStartItem(const char *path, const char *target, ShsConflictAction policy);

/* Answers the pending conflict. With `applyToAll` the action also becomes the policy for every later conflict. */
void ShsSyncResolve(ShsSyncJob *job, ShsConflictAction action, bool applyToAll);

void ShsSyncGetStatus(ShsSyncJob *job, ShsSyncStatus *out);

/* Verify: while on, every file copied afterwards is hashed as it is read, then read back from the target and compared
   (a mismatch fails the file and removes the bad copy). Can be switched at any time. */
void ShsSyncSetVerify(ShsSyncJob *job, bool verify);

/* Skip: abandons the file that started copying most recently (its partial copy is removed, totals are adjusted). */
void ShsSyncSkipCurrent(ShsSyncJob *job);

/* The full list of files this job copies (every file that needed copying, in order, including those added with
   ShsSyncAddPath). Rows are numbered from 0; `name` is relative to the target folder. */
typedef struct {
    char     name[SHS_SYNC_PATH_MAX];
    uint64_t size;
    int      state;                    /* ShsFileState */
} ShsSyncFileRow;

int  ShsSyncFileCount(ShsSyncJob *job);

/* Fills up to `max` rows starting at the `first`-th row whose state is in `stateMask` (bit n = ShsFileState n; ~0 = all).
   Returns how many rows were filled; *total (optional) receives how many rows match the mask in all. */
int  ShsSyncGetFiles(ShsSyncJob *job, unsigned stateMask, int first, int max, ShsSyncFileRow *out, int *total);

/* Queues one more file or folder to copy into the job's target while it runs (a folder lands in <target>/<its name>,
   a file directly in <target>). Returns false when the job has already finished or been cancelled; start a new job then. */
bool ShsSyncAddPath(ShsSyncJob *job, const char *path);

void ShsSyncSetPaused(ShsSyncJob *job, bool paused);
void ShsSyncCancel(ShsSyncJob *job);            /* asynchronous request */
bool ShsSyncIsFinished(ShsSyncJob *job);        /* DONE, FAILED or CANCELLED */

/* Cancels if needed, joins the worker thread, and frees the job. */
void ShsSyncFree(ShsSyncJob *job);

/* Creates <testDir>/source_folder and <testDir>/target_folder populated with mock
   files (including an outdated file and a stale extra file in the target, plus one
   large file so progress is visible). Safe to call repeatedly.
   Returns false on failure. */
bool ShsSyncSetupDevFolders(const char *testDir);

#endif /* SYNC_ENGINE_H */
