/* Headless tests for the sync engine and task store.
   Usage: engine_test <scratch-dir>   (the dir is created and cleaned up; never point it at real data) */
#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE
  #define _DARWIN_C_SOURCE
#endif
#include "sync_engine.h"
#include "task_store.h"
#include "updater.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <direct.h>
  #define SLEEP_MS(ms) Sleep(ms)
  #define RMDIR(p) _rmdir(p)
  #define MKDIR(p) _mkdir(p)
#else
  #include <unistd.h>
  #define SLEEP_MS(ms) usleep((ms) * 1000)
  #define RMDIR(p) rmdir(p)
  #define MKDIR(p) mkdir((p), 0755)
#endif

static int g_fail = 0;

#define CHECK(cond) do { if (!(cond)) { printf("  FAIL line %d: %s\n", __LINE__, #cond); g_fail++; } } while (0)

static const char *FILES[] = {
    "readme.txt", "docs/notes.txt", "docs/report.pdf", "photos/img_001.jpg", "photos/img_002.jpg",
    "photos/2024/trip/map.png", "backup/big_archive.bin", "old/obsolete.txt", NULL };
static const char *DIRS[] = {
    "photos/2024/trip", "photos/2024", "photos", "docs", "backup", "old", NULL };

static char g_src[600], g_dst[600], g_base[512];

static void CleanRoot(const char *root)
{
    char p[800];
    for (int i = 0; FILES[i]; i++) {
        snprintf(p, sizeof p, "%s/%s", root, FILES[i]);         remove(p);
        snprintf(p, sizeof p, "%s/%s.shs-tmp", root, FILES[i]); remove(p);
    }
    for (int i = 0; DIRS[i]; i++) { snprintf(p, sizeof p, "%s/%s", root, DIRS[i]); RMDIR(p); }
    RMDIR(root);
}

static void CleanAll(void) { CleanRoot(g_src); CleanRoot(g_dst); RMDIR(g_base); }

static long long SizeOf(const char *root, const char *rel)
{
    char p[800]; struct stat st;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

static ShsSyncStatus RunSyncPaths(const char *src, const char *dst, bool mirror)
{
    ShsSyncJob *j = ShsSyncStart(src, dst, mirror);
    ShsSyncStatus s; memset(&s, 0, sizeof s);
    if (!j) { printf("  FAIL: ShsSyncStart returned NULL\n"); g_fail++; return s; }
    for (int i = 0; i < 6000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
    ShsSyncGetStatus(j, &s);
    CHECK(ShsSyncIsFinished(j));
    ShsSyncFree(j);
    return s;
}

static ShsSyncStatus RunSync(bool mirror) { return RunSyncPaths(g_src, g_dst, mirror); }

/* ---- bulk copy test helpers ------------------------------------------------------------------------------ */

static void MakeFilled(const char *path, size_t size, unsigned seed)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  FAIL: cannot create %s\n", path); g_fail++; return; }
    unsigned x = seed * 2654435761u + 12345u;
    unsigned char block[4096];
    for (size_t left = size; left > 0; ) {
        size_t n = left < sizeof block ? left : sizeof block;
        for (size_t i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; block[i] = (unsigned char)(x >> 24); }
        fwrite(block, 1, n, f);
        left -= n;
    }
    fclose(f);
}

static bool SameFile(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    bool same = fa && fb;
    static unsigned char ba[65536], bb[65536];
    while (same) {
        size_t na = fread(ba, 1, sizeof ba, fa), nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na) != 0) same = false;
        if (na == 0) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static void RemoveTree(const char *path)           /* test scratch data only */
{
    char cmd[1200];
#ifdef _WIN32
    char win[1000]; snprintf(win, sizeof win, "%s", path);
    for (char *c = win; *c; c++) if (*c == '/') *c = '\\';
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\" 2>nul", win);
#else
    snprintf(cmd, sizeof cmd, "rm -rf \"%s\"", path);
#endif
    if (system(cmd) != 0) { /* nothing to remove */ }
}

#define BULK_DIRS   20
#define BULK_PER    15

static void BulkRelName(char *out, size_t cap, int d, int k) { snprintf(out, cap, "d%02d/f%03d.bin", d, k); }

static void RunBulkCase(const char *label, int threads, int bufMiB, const char *bsrc, const char *bdst)
{
    printf("bulk copy: %s (threads=%d, buffer=%d MiB)\n", label, threads, bufMiB);
    RemoveTree(bdst);
    ShsSyncConfigure(threads, bufMiB);
    ShsSyncStatus s = RunSyncPaths(bsrc, bdst, false);
    CHECK(s.state == SHS_SYNC_DONE);
    CHECK(s.errorCount == 0);
    CHECK(s.filesDone == BULK_DIRS * BULK_PER + 3);
    int bad = 0;
    for (int d = 0; d < BULK_DIRS; d++) for (int k = 0; k < BULK_PER; k++) {
        char rel[64], a[900], b[900];
        BulkRelName(rel, sizeof rel, d, k);
        snprintf(a, sizeof a, "%s/%s", bsrc, rel); snprintf(b, sizeof b, "%s/%s", bdst, rel);
        if (!SameFile(a, b)) bad++;
    }
    const char *big[] = { "mid12.bin", "big5.bin", "big45.bin" };
    for (int i = 0; i < 3; i++) {
        char a[900], b[900];
        snprintf(a, sizeof a, "%s/%s", bsrc, big[i]); snprintf(b, sizeof b, "%s/%s", bdst, big[i]);
        if (!SameFile(a, b)) bad++;
    }
    CHECK(bad == 0);
    s = RunSyncPaths(bsrc, bdst, false);                           /* second pass copies nothing */
    CHECK(s.filesDone == 0);
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: engine_test <scratch-dir>\n"); return 2; }
    snprintf(g_base, sizeof g_base, "%s", argv[1]);
    snprintf(g_src, sizeof g_src, "%s/source_folder", g_base);
    snprintf(g_dst, sizeof g_dst, "%s/target_folder", g_base);
    MKDIR(g_base);
    CleanAll();
    MKDIR(g_base);

    printf("setup mock folders\n");
    CHECK(ShsSyncSetupDevFolders(g_base));
    CHECK(SizeOf(g_src, "backup/big_archive.bin") == 64LL * 1024 * 1024);

    printf("copy-only sync\n");
    ShsSyncStatus s = RunSync(false);
    CHECK(s.state == SHS_SYNC_DONE);
    CHECK(s.errorCount == 0);
    CHECK(s.filesDone == 6);                 /* 7 source files, readme.txt is already identical */
    CHECK(s.filesDeleted == 0);
    CHECK(SizeOf(g_dst, "docs/notes.txt") == 8 * 1024);                /* outdated file was replaced */
    CHECK(SizeOf(g_dst, "photos/2024/trip/map.png") == 640 * 1024);
    CHECK(SizeOf(g_dst, "backup/big_archive.bin") == 64LL * 1024 * 1024);
    CHECK(SizeOf(g_dst, "old/obsolete.txt") == 512);                   /* kept without mirror */
    CHECK(s.recentCount == 6);                                         /* the six copies are listed */
    CHECK(s.recent[0].state == SHS_FILE_DONE && s.recent[0].done == s.recent[0].size && s.recent[0].name[0]);

    printf("second run is a no-op\n");
    s = RunSync(false);
    CHECK(s.state == SHS_SYNC_DONE);
    CHECK(s.filesDone == 0);

    printf("paths with a trailing separator (drive roots like E:\\)\n");
    {
        char a[620], b[620];
        snprintf(a, sizeof a, "%s/", g_src);
        snprintf(b, sizeof b, "%s/", g_dst);
        s = RunSyncPaths(a, b, false);
        CHECK(s.state == SHS_SYNC_DONE);
        CHECK(s.errorCount == 0);
    }

    printf("mirror removes extras\n");
    s = RunSync(true);
    CHECK(s.state == SHS_SYNC_DONE);
    CHECK(s.filesDeleted >= 1);
    CHECK(SizeOf(g_dst, "old/obsolete.txt") == -1);
    CHECK(s.recentCount >= 1 && s.recent[0].state == SHS_FILE_REMOVED);   /* the removal is listed */
    CHECK(SizeOf(g_src, "docs/notes.txt") == 8 * 1024);                /* source untouched */

    printf("cancel leaves no temp file\n");
    {
        char p[800];
        snprintf(p, sizeof p, "%s/backup/big_archive.bin", g_dst); remove(p);
        ShsSyncJob *j = ShsSyncStart(g_src, g_dst, false);
        CHECK(j != NULL);
        if (j) {
            ShsSyncCancel(j);
            for (int i = 0; i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
            ShsSyncGetStatus(j, &s);
            CHECK(s.state == SHS_SYNC_CANCELLED || s.state == SHS_SYNC_DONE);   /* DONE if it won the race */
            ShsSyncFree(j);
        }
        CHECK(SizeOf(g_dst, "backup/big_archive.bin.shs-tmp") == -1);
    }

    printf("task store round trip\n");
    {
        char path[600];
        snprintf(path, sizeof path, "%s/tasks.tsv", g_base);
        ShsTaskDef in[2], out[4];
        memset(in, 0, sizeof in);
        snprintf(in[0].name, sizeof in[0].name, "My\ttask");     /* tab must not break the record */
        snprintf(in[0].source, sizeof in[0].source, "C:/a b/src");
        snprintf(in[0].target, sizeof in[0].target, "D:/dst");
        in[0].mirror = true; in[0].lastSync = 1700000000LL; in[0].lastBytes = 12345; in[0].autoSync = true;
        in[0].usbSync = true; in[0].includeFolder = true; snprintf(in[0].usbId, sizeof in[0].usbId, "0123456789abcdef"); in[0].usbMark = true; snprintf(in[0].deletedDir, sizeof in[0].deletedDir, "D:/dst/Deleted files");
        snprintf(in[1].name, sizeof in[1].name, "Second");
        snprintf(in[1].source, sizeof in[1].source, "/x");
        snprintf(in[1].target, sizeof in[1].target, "/y");
        CHECK(ShsStoreSave(path, in, 2));
        int n = ShsStoreLoad(path, out, 4);
        CHECK(n == 2);
        if (n == 2) {
            CHECK(strcmp(out[0].name, "My task") == 0);
            CHECK(strcmp(out[0].source, "C:/a b/src") == 0);
            CHECK(out[0].mirror && out[0].lastSync == 1700000000LL && out[0].lastBytes == 12345);
            CHECK(!out[1].mirror && out[1].lastSync == 0 && !out[1].autoSync);
            CHECK(out[0].autoSync);
            CHECK(out[0].usbSync && strcmp(out[0].deletedDir, "D:/dst/Deleted files") == 0);
            CHECK(!out[1].usbSync && out[1].deletedDir[0] == 0);
            CHECK(out[0].includeFolder && !out[1].includeFolder);
            CHECK(strcmp(out[0].usbId, "0123456789abcdef") == 0 && out[1].usbId[0] == 0 && out[0].usbMark && !out[1].usbMark);   /* the mark option is saved per task */
        }
        {                                                         /* a task file from an older version (7 columns) still loads */
            FILE *old = fopen(path, "wb");
            if (old) { fputs("Old\t/s\t/t\t1\t5\t6\t1\n", old); fclose(old); }
            int on = ShsStoreLoad(path, out, 4);
            CHECK(on == 1 && out[0].mirror && out[0].autoSync && !out[0].usbSync && out[0].deletedDir[0] == 0);
        }
        CHECK(ShsStoreLoad("does/not/exist.tsv", out, 4) == 0);
        remove(path);
    }

    /* ---- bulk, content-verified copies with different thread / buffer settings ---- */
    {
        char bsrc[700], bdst[700];
        snprintf(bsrc, sizeof bsrc, "%s/bulk_src", g_base);
        snprintf(bdst, sizeof bdst, "%s/bulk_dst", g_base);
        RemoveTree(bsrc);
        MKDIR(bsrc);
        for (int d = 0; d < BULK_DIRS; d++) {
            char dir[800]; snprintf(dir, sizeof dir, "%s/d%02d", bsrc, d); MKDIR(dir);
            for (int k = 0; k < BULK_PER; k++) {
                char rel[64], path[900]; BulkRelName(rel, sizeof rel, d, k);
                snprintf(path, sizeof path, "%s/%s", bsrc, rel);
                MakeFilled(path, (size_t)((d * 37 + k * 1013) % 50000), (unsigned)(d * 100 + k));   /* includes empty files */
            }
        }
        char p[900];
        snprintf(p, sizeof p, "%s/mid12.bin", bsrc);  MakeFilled(p, 12u << 20, 901);     /* read ahead */
        snprintf(p, sizeof p, "%s/big5.bin", bsrc);   MakeFilled(p, 5u << 20, 902);
        snprintf(p, sizeof p, "%s/big45.bin", bsrc);  MakeFilled(p, 45u << 20, 903);     /* above the read-ahead limit: streamed */

        RunBulkCase("4 threads + read-ahead", 4, 64, bsrc, bdst);
        RunBulkCase("1 thread, no read-ahead", 1, 0, bsrc, bdst);
        RunBulkCase("8 threads, tiny buffer", 8, 8, bsrc, bdst);
        RunBulkCase("16 threads, large buffer", 16, 256, bsrc, bdst);

        /* Verify on: every file is hashed while copied, read back and compared */
        {
            printf("bulk copy with Verify\n");
            RemoveTree(bdst);
            ShsSyncConfigure(4, 64);
            ShsSyncJob *vj = ShsSyncStart(bsrc, bdst, false);
            CHECK(vj != NULL);
            if (vj) {
                ShsSyncSetVerify(vj, true);
                for (int i = 0; i < 6000 && !ShsSyncIsFinished(vj); i++) SLEEP_MS(10);
                ShsSyncStatus vs; ShsSyncGetStatus(vj, &vs);
                ShsSyncFree(vj);
                CHECK(vs.state == SHS_SYNC_DONE);
                CHECK(vs.errorCount == 0);
                CHECK(vs.filesDone == BULK_DIRS * BULK_PER + 3);
                /* the job may have started a few files before Verify was switched on, so allow for that */
                CHECK(vs.filesVerified >= (BULK_DIRS * BULK_PER + 3) - 20);
                CHECK(vs.filesVerified <= vs.filesDone);
            }
        }
        ShsSyncConfigure(4, 64);

        RemoveTree(bsrc); RemoveTree(bdst);
    }

    /* ---- Skip: abandon a large file while it is copying ---- */
    {
        printf("skip the file being copied\n");
        char ssrc[700], sdst[700], p[900];
        snprintf(ssrc, sizeof ssrc, "%s/skip_src", g_base);
        snprintf(sdst, sizeof sdst, "%s/skip_dst", g_base);
        RemoveTree(ssrc); RemoveTree(sdst);
        MKDIR(ssrc);
        snprintf(p, sizeof p, "%s/a_big.bin", ssrc); MakeFilled(p, 300u << 20, 777);          /* sorts first, copied first */
        for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/small%d.bin", ssrc, i); MakeFilled(p, 20000 + (size_t)i * 1000, (unsigned)(800 + i)); }
        ShsSyncConfigure(1, 0);                                                                   /* one thread, no read-ahead: deterministic */
        ShsSyncJob *sj = ShsSyncStart(ssrc, sdst, false);
        CHECK(sj != NULL);
        if (sj) {
            ShsSyncStatus ss; bool seen = false;
            for (int i = 0; i < 3000 && !seen; i++) {                                              /* wait until the big file is copying */
                ShsSyncGetStatus(sj, &ss);
                seen = ss.recentCount > 0 && ss.recent[0].state == SHS_FILE_COPYING && ss.recent[0].done > 0;
                if (!seen) SLEEP_MS(2);
            }
            CHECK(seen);
            ShsSyncSkipCurrent(sj);
            for (int i = 0; i < 6000 && !ShsSyncIsFinished(sj); i++) SLEEP_MS(10);
            ShsSyncGetStatus(sj, &ss);
            ShsSyncFree(sj);
            CHECK(ss.state == SHS_SYNC_DONE);
            CHECK(ss.errorCount == 0);
            CHECK(ss.filesSkipped == 1);
            CHECK(ss.filesDone == 3);
            CHECK(ss.filesTotal == 3);                                                             /* the skipped file left the totals */
            CHECK(ss.bytesTotal == 20000u + 21000u + 22000u);
            CHECK(ss.bytesDone == ss.bytesTotal);
            CHECK(SizeOf(sdst, "a_big.bin") == -1);                                                /* nothing was left behind */
            CHECK(SizeOf(sdst, "a_big.bin.shs-tmp") == -1);
            CHECK(SizeOf(sdst, "small2.bin") == 22000);
        }
        ShsSyncConfigure(4, 64);
        RemoveTree(ssrc); RemoveTree(sdst);
    }

    /* ---- Add files and folders to a running copy ---- */
    {
        printf("add a folder and a file to a running copy\n");
        char asrc[700], adst[700], xdir[700], p[900];
        snprintf(asrc, sizeof asrc, "%s/add_src", g_base);
        snprintf(adst, sizeof adst, "%s/add_dst", g_base);
        snprintf(xdir, sizeof xdir, "%s/add_extra", g_base);
        RemoveTree(asrc); RemoveTree(adst); RemoveTree(xdir);
        MKDIR(asrc); MKDIR(xdir);
        snprintf(p, sizeof p, "%s/sub", xdir); MKDIR(p);
        snprintf(p, sizeof p, "%s/a_big.bin", asrc);   MakeFilled(p, 200u << 20, 31);
        snprintf(p, sizeof p, "%s/a.txt", xdir);       MakeFilled(p, 1000, 32);
        snprintf(p, sizeof p, "%s/sub/b.txt", xdir);   MakeFilled(p, 2000, 33);
        char single[800]; snprintf(single, sizeof single, "%s/add_single.bin", g_base);
        MakeFilled(single, 3000, 34);

        ShsSyncConfigure(1, 0);
        ShsSyncJob *aj = ShsSyncStart(asrc, adst, false);
        CHECK(aj != NULL);
        if (aj) {
            CHECK(ShsSyncAddPath(aj, xdir));
            CHECK(ShsSyncAddPath(aj, single));
            for (int i = 0; i < 6000 && !ShsSyncIsFinished(aj); i++) SLEEP_MS(10);
            ShsSyncStatus as; ShsSyncGetStatus(aj, &as);
            CHECK(as.state == SHS_SYNC_DONE);
            CHECK(as.errorCount == 0);
            CHECK(as.filesDone == 4 && as.filesTotal == 4);
            CHECK(ShsSyncFileCount(aj) == 4);
            ShsSyncFileRow rows[8];
            int total = 0;
            int nr = ShsSyncGetFiles(aj, ~0u, 0, 8, rows, &total);
            CHECK(nr == 4 && total == 4);
            CHECK(ShsSyncGetFiles(aj, 1u << SHS_FILE_FAILED, 0, 8, rows, &total) == 0 && total == 0);
            nr = ShsSyncGetFiles(aj, ~0u, 0, 8, rows, &total);
            for (int i = 0; i < nr; i++) CHECK(rows[i].state == SHS_FILE_DONE);
            CHECK(nr == 4 && strcmp(rows[1].name, "add_extra/a.txt") == 0);
            CHECK(!ShsSyncAddPath(aj, xdir));                                   /* finished: refused */
            ShsSyncFree(aj);
        }
        CHECK(SizeOf(adst, "a_big.bin") == 200LL << 20);
        CHECK(SizeOf(adst, "add_extra/a.txt") == 1000);
        CHECK(SizeOf(adst, "add_extra/sub/b.txt") == 2000);
        CHECK(SizeOf(adst, "add_single.bin") == 3000);

        /* a file as the first source copies just that file into the target folder */
        RemoveTree(adst);
        ShsSyncStatus fs = RunSyncPaths(single, adst, false);
        CHECK(fs.state == SHS_SYNC_DONE && fs.filesDone == 1);
        CHECK(SizeOf(adst, "add_single.bin") == 3000);

        ShsSyncConfigure(4, 64);
        remove(single);
        RemoveTree(asrc); RemoveTree(adst); RemoveTree(xdir);
    }

    /* ---- Quick Copy: files that already exist in the target ---- */
    {
        printf("quick copy conflict policies\n");
        char csrc[700], cdst[700], p[900];
        snprintf(csrc, sizeof csrc, "%s/cf_src", g_base);
        snprintf(cdst, sizeof cdst, "%s/cf_dst", g_base);
        static const struct { ShsConflictAction pol; long long aSize; bool aCopy2; bool bCopied; } CASE[] = {
            { SHS_CONFLICT_OVERWRITE,         5000, false, true },   /* a.txt replaced */
            { SHS_CONFLICT_SKIP,              1000, false, true },   /* a.txt untouched */
            { SHS_CONFLICT_KEEP_BOTH,         1000, true,  true },   /* a.txt untouched, copy is "a (2).txt" */
            { SHS_CONFLICT_RENAME_TARGET,     5000, true,  true },   /* old a.txt renamed, new one in place */
            { SHS_CONFLICT_REPLACE_SMALLER,   5000, false, true },
            { SHS_CONFLICT_REPLACE_DIFF_SIZE, 5000, false, true },
        };
        for (size_t c = 0; c < sizeof CASE / sizeof CASE[0]; c++) {
            RemoveTree(csrc); RemoveTree(cdst);
            MKDIR(csrc); MKDIR(cdst);
            snprintf(p, sizeof p, "%s/a.txt", csrc); MakeFilled(p, 5000, 61);
            snprintf(p, sizeof p, "%s/b.txt", csrc); MakeFilled(p, 3000, 62);
            snprintf(p, sizeof p, "%s/a.txt", cdst); MakeFilled(p, 1000, 63);
            ShsSyncJob *cj = ShsSyncStartQuick(csrc, cdst, CASE[c].pol);
            CHECK(cj != NULL);
            if (!cj) continue;
            for (int i = 0; i < 3000 && !ShsSyncIsFinished(cj); i++) SLEEP_MS(10);
            ShsSyncStatus cs; ShsSyncGetStatus(cj, &cs);
            ShsSyncFree(cj);
            CHECK(cs.state == SHS_SYNC_DONE && cs.errorCount == 0);
            CHECK(SizeOf(cdst, "a.txt") == CASE[c].aSize);
            CHECK((SizeOf(cdst, "a (2).txt") != -1) == CASE[c].aCopy2);
            CHECK((SizeOf(cdst, "b.txt") == 3000) == CASE[c].bCopied);
            if (CASE[c].pol == SHS_CONFLICT_KEEP_BOTH) CHECK(SizeOf(cdst, "a (2).txt") == 5000);
            if (CASE[c].pol == SHS_CONFLICT_RENAME_TARGET) CHECK(SizeOf(cdst, "a (2).txt") == 1000);
        }

        /* ASK: the job waits for the answer; "skip" for this file, then it carries on */
        RemoveTree(csrc); RemoveTree(cdst);
        MKDIR(csrc); MKDIR(cdst);
        snprintf(p, sizeof p, "%s/a.txt", csrc); MakeFilled(p, 5000, 61);
        snprintf(p, sizeof p, "%s/b.txt", csrc); MakeFilled(p, 3000, 62);
        snprintf(p, sizeof p, "%s/a.txt", cdst); MakeFilled(p, 1000, 63);
        snprintf(p, sizeof p, "%s/b.txt", cdst); MakeFilled(p, 1000, 64);
        ShsSyncJob *aj = ShsSyncStartQuick(csrc, cdst, SHS_CONFLICT_ASK);
        CHECK(aj != NULL);
        if (aj) {
            ShsSyncStatus as; bool asked = false;
            for (int i = 0; i < 1000 && !asked; i++) { ShsSyncGetStatus(aj, &as); asked = as.conflictPending; if (!asked) SLEEP_MS(5); }
            CHECK(asked);
            CHECK(asked && as.conflictSrcSize == 5000 && as.conflictDstSize == 1000 && strstr(as.conflictDst, "a.txt") != NULL);
            SLEEP_MS(200);
            CHECK(!ShsSyncIsFinished(aj));                                  /* still waiting */
            ShsSyncResolve(aj, SHS_CONFLICT_SKIP, false);                   /* a.txt: skip */
            asked = false;
            for (int i = 0; i < 1000 && !asked; i++) { ShsSyncGetStatus(aj, &as); asked = as.conflictPending && strstr(as.conflictDst, "b.txt"); if (!asked) SLEEP_MS(5); }
            CHECK(asked);
            ShsSyncResolve(aj, SHS_CONFLICT_OVERWRITE, true);               /* b.txt: overwrite (and everything after) */
            for (int i = 0; i < 3000 && !ShsSyncIsFinished(aj); i++) SLEEP_MS(10);
            ShsSyncGetStatus(aj, &as);
            ShsSyncFree(aj);
            CHECK(as.state == SHS_SYNC_DONE && as.filesDone == 1);
            CHECK(SizeOf(cdst, "a.txt") == 1000 && SizeOf(cdst, "b.txt") == 3000);
        }
        RemoveTree(csrc); RemoveTree(cdst);
    }

    /* ---- many small files in a deep tree: parallel scan finds everything, direct writes leave no temp files ---- */
    {
        printf("many small files (parallel scan, direct writes)\n");
        char msrc[700], mdst[700], p[900];
        snprintf(msrc, sizeof msrc, "%s/many_src", g_base);
        snprintf(mdst, sizeof mdst, "%s/many_dst", g_base);
        RemoveTree(msrc); RemoveTree(mdst);
        MKDIR(msrc);
        int made = 0;
        for (int a = 0; a < 6; a++) {
            snprintf(p, sizeof p, "%s/d%d", msrc, a); MKDIR(p);
            for (int b = 0; b < 6; b++) {
                snprintf(p, sizeof p, "%s/d%d/e%d", msrc, a, b); MKDIR(p);
                for (int c = 0; c < 2; c++) {
                    snprintf(p, sizeof p, "%s/d%d/e%d/f%d", msrc, a, b, c); MKDIR(p);
                    for (int k = 0; k < 8; k++) {
                        snprintf(p, sizeof p, "%s/d%d/e%d/f%d/s%d.bin", msrc, a, b, c, k);
                        MakeFilled(p, 100 + (size_t)(a * 31 + b * 7 + c * 3 + k) * 13, (unsigned)(made + 100));
                        made++;
                    }
                }
            }
        }
        ShsSyncConfigure(4, 64);
        clock_t t0 = clock();
        ShsSyncStatus ms = RunSyncPaths(msrc, mdst, false);
        printf("  %d files copied in %ld ms\n", made, (long)((clock() - t0) * 1000 / CLOCKS_PER_SEC));
        CHECK(ms.state == SHS_SYNC_DONE && ms.errorCount == 0);
        CHECK(ms.filesDone == (uint64_t)made && ms.filesTotal == (uint64_t)made);
        CHECK(SizeOf(mdst, "d0/e0/f0/s0.bin") == 100);
        CHECK(SizeOf(mdst, "d5/e5/f1/s7.bin") == 100 + (5 * 31 + 5 * 7 + 1 * 3 + 7) * 13);
        CHECK(SizeOf(mdst, "d3/e2/f1/s4.bin.shs-tmp") < 0);                        /* no temp files left behind */
        ShsSyncStatus again = RunSyncPaths(msrc, mdst, false);                   /* second run: nothing to copy */
        CHECK(again.state == SHS_SYNC_DONE && again.filesTotal == 0);
        snprintf(p, sizeof p, "%s/d2/e3/f0/s1.bin", msrc); MakeFilled(p, 5000, 7);   /* a changed file goes through the temp file + rename */
        ShsSyncStatus chg = RunSyncPaths(msrc, mdst, false);
        CHECK(chg.state == SHS_SYNC_DONE && chg.filesDone == 1);
        CHECK(SizeOf(mdst, "d2/e3/f0/s1.bin") == 5000);
        snprintf(p, sizeof p, "%s/d1/e1/f1/s1.bin", msrc); remove(p);               /* mirror deletes what is gone from the source */
        ShsSyncStatus mir = RunSyncPaths(msrc, mdst, true);
        CHECK(mir.state == SHS_SYNC_DONE && mir.filesDeleted == 1);
        CHECK(SizeOf(mdst, "d1/e1/f1/s1.bin") < 0);
        RemoveTree(msrc); RemoveTree(mdst);
    }

    /* ---- mirror with a deleted-files folder: removed files are moved there, not deleted ---- */
    {
        printf("mirror keeps removed files in the deleted-files folder\n");
        char ksrc[700], kdst[700], kdel[700], p[900];
        snprintf(ksrc, sizeof ksrc, "%s/keep_src", g_base);
        snprintf(kdst, sizeof kdst, "%s/keep_dst", g_base);
        snprintf(kdel, sizeof kdel, "%s/keep_dst/Deleted files", g_base);          /* inside the target */
        RemoveTree(ksrc); RemoveTree(kdst);
        MKDIR(g_base); MKDIR(ksrc); MKDIR(kdst);
        snprintf(p, sizeof p, "%s/sub", ksrc); MKDIR(p);
        snprintf(p, sizeof p, "%s/a.txt", ksrc);     MakeFilled(p, 3000, 61);
        snprintf(p, sizeof p, "%s/sub/b.txt", ksrc); MakeFilled(p, 2000, 62);
        snprintf(p, sizeof p, "%s/sub/c.txt", ksrc); MakeFilled(p, 1000, 63);

        ShsSyncJob *j = ShsSyncStartEx(ksrc, kdst, true, kdel, false);
        for (int i = 0; j && i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
        ShsSyncStatus s; memset(&s, 0, sizeof s);
        if (j) { ShsSyncGetStatus(j, &s); ShsSyncFree(j); }
        CHECK(s.state == SHS_SYNC_DONE && SizeOf(kdst, "sub/b.txt") == 2000);

        snprintf(p, sizeof p, "%s/sub/b.txt", ksrc); remove(p);                    /* delete one file from the source... */
        snprintf(p, sizeof p, "%s/a.txt", ksrc);     remove(p);
        for (int round = 0; round < 2; round++) {                                  /* ...second round: the kept copy must survive another mirror run */
            j = ShsSyncStartEx(ksrc, kdst, true, kdel, false);
            for (int i = 0; j && i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
            memset(&s, 0, sizeof s);
            if (j) { ShsSyncGetStatus(j, &s); ShsSyncFree(j); }
            CHECK(s.state == SHS_SYNC_DONE && s.errorCount == 0);
            CHECK(round == 0 ? s.filesDeleted >= 2 : s.filesDeleted == 0);
            CHECK(SizeOf(kdst, "a.txt") < 0 && SizeOf(kdst, "sub/b.txt") < 0);      /* gone from the target */
            CHECK(SizeOf(kdst, "sub/c.txt") == 1000);
            CHECK(SizeOf(kdel, "a.txt") == 3000);                                   /* ...but kept, with its relative path */
            CHECK(SizeOf(kdel, "sub/b.txt") == 2000);
        }

        snprintf(p, sizeof p, "%s/a.txt", ksrc); MakeFilled(p, 3000, 64);          /* same name comes and goes again: the old kept copy is not overwritten */
        ShsSyncStatus back = RunSyncPaths(ksrc, kdst, false);
        CHECK(back.state == SHS_SYNC_DONE && SizeOf(kdst, "a.txt") == 3000);
        remove(p);
        j = ShsSyncStartEx(ksrc, kdst, true, kdel, false);
        for (int i = 0; j && i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
        if (j) ShsSyncFree(j);
        CHECK(SizeOf(kdel, "a.txt") == 3000 && SizeOf(kdst, "a.txt") < 0);

        RemoveTree(ksrc); RemoveTree(kdst);
    }

    /* ---- include the folder itself: <target>/<folder name>/..., mirrored only inside that folder ---- */
    {
        printf("sync task can include the source folder itself\n");
        char isrc[700], idst[700], p[900];
        snprintf(isrc, sizeof isrc, "%s/Photos", g_base);
        snprintf(idst, sizeof idst, "%s/incl_dst", g_base);
        RemoveTree(isrc); RemoveTree(idst);
        MKDIR(g_base); MKDIR(isrc); MKDIR(idst);
        snprintf(p, sizeof p, "%s/sub", isrc); MKDIR(p);
        snprintf(p, sizeof p, "%s/a.txt", isrc);     MakeFilled(p, 1500, 71);
        snprintf(p, sizeof p, "%s/sub/b.txt", isrc); MakeFilled(p, 2500, 72);
        snprintf(p, sizeof p, "%s/other.txt", idst); MakeFilled(p, 700, 73);       /* outside the copied folder: mirror must leave it */

        ShsSyncJob *j = ShsSyncStartEx(isrc, idst, true, NULL, true);
        for (int i = 0; j && i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
        ShsSyncStatus s; memset(&s, 0, sizeof s);
        if (j) { ShsSyncGetStatus(j, &s); ShsSyncFree(j); }
        CHECK(s.state == SHS_SYNC_DONE && s.errorCount == 0 && s.filesDone == 2);
        CHECK(SizeOf(idst, "Photos/a.txt") == 1500 && SizeOf(idst, "Photos/sub/b.txt") == 2500);
        CHECK(SizeOf(idst, "a.txt") < 0);                                           /* not copied loose into the target */
        CHECK(SizeOf(idst, "other.txt") == 700);

        snprintf(p, sizeof p, "%s/a.txt", isrc); remove(p);                         /* mirror removes it from <target>/Photos only */
        j = ShsSyncStartEx(isrc, idst, true, NULL, true);
        for (int i = 0; j && i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
        memset(&s, 0, sizeof s);
        if (j) { ShsSyncGetStatus(j, &s); ShsSyncFree(j); }
        CHECK(s.state == SHS_SYNC_DONE && s.filesDeleted == 1);
        CHECK(SizeOf(idst, "Photos/a.txt") < 0 && SizeOf(idst, "Photos/sub/b.txt") == 2500 && SizeOf(idst, "other.txt") == 700);
        RemoveTree(isrc); RemoveTree(idst);
    }

    /* ---- purge: delete what a task copied into its target, nothing else ---- */
    {
        printf("purge deletes only what the task copied\n");
        char psrc[700], pdst[700], p[900];
        snprintf(psrc, sizeof psrc, "%s/Docs", g_base);
        snprintf(pdst, sizeof pdst, "%s/purge_dst", g_base);
        RemoveTree(psrc); RemoveTree(pdst);
        MKDIR(g_base); MKDIR(psrc); MKDIR(pdst);
        snprintf(p, sizeof p, "%s/sub", psrc); MKDIR(p);
        snprintf(p, sizeof p, "%s/a.txt", psrc);     MakeFilled(p, 1000, 91);
        snprintf(p, sizeof p, "%s/sub/b.txt", psrc); MakeFilled(p, 2000, 92);

        /* without "include the folder": files that also exist in the source go, the rest of the target stays */
        ShsSyncStatus s1 = RunSyncPaths(psrc, pdst, false);
        CHECK(s1.state == SHS_SYNC_DONE && SizeOf(pdst, "sub/b.txt") == 2000);
        snprintf(p, sizeof p, "%s/keep.txt", pdst); MakeFilled(p, 300, 93);                         /* someone else's file */
        snprintf(p, sizeof p, "%s/sub/mine_now.txt", pdst); MakeFilled(p, 400, 94);                 /* in a synced folder, but not from the source */
        ShsPurgeJob *pj = ShsPurgeStart(psrc, pdst, false);
        uint64_t del = 0, fail = 0;
        for (int i = 0; pj && i < 3000 && !ShsPurgeStatus(pj, &del, &fail); i++) SLEEP_MS(10);
        CHECK(pj && ShsPurgeStatus(pj, &del, &fail) && del == 2 && fail == 0);
        ShsPurgeFree(pj);
        CHECK(SizeOf(pdst, "a.txt") < 0 && SizeOf(pdst, "sub/b.txt") < 0);
        CHECK(SizeOf(pdst, "keep.txt") == 300 && SizeOf(pdst, "sub/mine_now.txt") == 400);          /* untouched, and so is the folder that still holds one */

        /* with "include the folder": its own folder goes completely, the target around it stays */
        snprintf(p, sizeof p, "%s/purge_dst2", g_base);
        RemoveTree(p); MKDIR(p);
        ShsSyncJob *sj = ShsSyncStartEx(psrc, p, false, NULL, true);
        for (int i = 0; sj && i < 3000 && !ShsSyncIsFinished(sj); i++) SLEEP_MS(10);
        if (sj) ShsSyncFree(sj);
        CHECK(SizeOf(p, "Docs/sub/b.txt") == 2000);
        char q[900]; snprintf(q, sizeof q, "%s/other.txt", p); MakeFilled(q, 123, 95);
        snprintf(q, sizeof q, "%s/Docs/extra_leftover.txt", p); MakeFilled(q, 77, 96);              /* not in the source, but inside the task's folder */
        pj = ShsPurgeStart(psrc, p, true);
        for (int i = 0; pj && i < 3000 && !ShsPurgeStatus(pj, &del, &fail); i++) SLEEP_MS(10);
        CHECK(pj && ShsPurgeStatus(pj, &del, &fail) && del == 3 && fail == 0);
        ShsPurgeFree(pj);
        CHECK(SizeOf(p, "Docs/a.txt") < 0 && SizeOf(p, "Docs/extra_leftover.txt") < 0);
        CHECK(SizeOf(p, "other.txt") == 123);
        snprintf(q, sizeof q, "%s/Docs", p);
        { struct stat st; CHECK(stat(q, &st) != 0); }                                               /* the folder itself is gone */
        RemoveTree(psrc); RemoveTree(pdst); RemoveTree(p);
    }

    /* ---- Office / LibreOffice lock files are left out; removed files show up in the file list ---- */
    {
        printf("lock files are skipped, removed files are listed\n");
        char lsrc[700], ldst[700], p[900];
        snprintf(lsrc, sizeof lsrc, "%s/lock_s", g_base);
        snprintf(ldst, sizeof ldst, "%s/lock_d", g_base);
        RemoveTree(lsrc); RemoveTree(ldst);
        MKDIR(g_base); MKDIR(lsrc); MKDIR(ldst);
        const char *names[] = { "real.txt", "~keep.txt", "~$Report.xlsx", "~WRL0001.tmp", ".~lock.Notes.odt#", "~scratch.TMP" };
        for (int i = 0; i < 6; i++) { snprintf(p, sizeof p, "%s/%s", lsrc, names[i]); MakeFilled(p, 500 + (size_t)i, 80u + (unsigned)i); }
        snprintf(p, sizeof p, "%s/gone.txt", ldst); MakeFilled(p, 300, 90);

        ShsSyncJob *j = ShsSyncStart(lsrc, ldst, true);
        for (int i = 0; j && i < 3000 && !ShsSyncIsFinished(j); i++) SLEEP_MS(10);
        ShsSyncStatus s; memset(&s, 0, sizeof s);
        if (j) ShsSyncGetStatus(j, &s);
        CHECK(s.state == SHS_SYNC_DONE && s.errorCount == 0 && s.filesDone == 2);
        CHECK(s.filesIgnored == 4);                                          /* the four lock files are counted... */
        CHECK(SizeOf(ldst, "real.txt") == 500 && SizeOf(ldst, "~keep.txt") == 501);
        CHECK(SizeOf(ldst, "~$Report.xlsx") < 0 && SizeOf(ldst, "~WRL0001.tmp") < 0);
        CHECK(SizeOf(ldst, ".~lock.Notes.odt#") < 0 && SizeOf(ldst, "~scratch.TMP") < 0);
        CHECK(SizeOf(ldst, "gone.txt") < 0 && s.filesDeleted == 1);
        int removed = 0, doneN = 0, ignoredN = 0;
        if (j) {
            ShsSyncFileRow rows[8];
            ShsSyncGetFiles(j, 1u << SHS_FILE_REMOVED, 0, 8, rows, &removed);
            ShsSyncGetFiles(j, 1u << SHS_FILE_DONE, 0, 8, rows, &doneN);
            ShsSyncGetFiles(j, 1u << SHS_FILE_IGNORED, 0, 8, rows, &ignoredN);            /* ...and listed, so the UI can show them */
            if (ignoredN == 4) { int hasReport = 0; for (int k = 0; k < 4; k++) if (strstr(rows[k].name, "~$Report.xlsx")) hasReport = 1; CHECK(hasReport); }
            ShsSyncFree(j);
        }
        CHECK(removed == 1 && doneN == 2 && ignoredN == 4);
        RemoveTree(lsrc); RemoveTree(ldst);
    }

#ifdef _WIN32
    /* ---- paths beyond Windows' 260-character limit (deep folders + the target's own folder name) must copy ---- */
    {
        printf("paths longer than 260 characters\n");
        char base[MAX_PATH], lsrc[MAX_PATH], ldst[MAX_PATH], deep[1100], leaf[1200];
        if (_fullpath(base, g_base, sizeof base)) {
            snprintf(lsrc, sizeof lsrc, "%s\\long_src", base);
            snprintf(ldst, sizeof ldst, "%s\\long_dst", base);
            snprintf(deep, sizeof deep, "%s", lsrc);
            for (int i = 0; i < 9; i++) strcat(deep, "\\dir_with_a_fairly_long_name_");       /* 9 x 29 chars: the path ends up near 350 */
            snprintf(leaf, sizeof leaf, "%s\\file_in_the_deep_folder.bin", deep);
            CHECK(strlen(leaf) > 300);

            char prefixed[1300];
            snprintf(prefixed, sizeof prefixed, "\\\\?\\%s", lsrc);
            RemoveTree(prefixed);
            snprintf(prefixed, sizeof prefixed, "\\\\?\\%s", ldst);
            RemoveTree(prefixed);

            wchar_t wl[1300];
            wcscpy(wl, L"\\\\?\\");
            MultiByteToWideChar(CP_UTF8, 0, deep, -1, wl + 4, 1290);
            for (wchar_t *c = wl + 7; ; c++) {                                              /* every level, starting after "\\?\X:\" */
                if (*c == L'\\' || *c == 0) {
                    wchar_t keep = *c; *c = 0;
                    CreateDirectoryW(wl, NULL);
                    *c = keep;
                    if (!keep) break;
                }
            }
            wchar_t wf[1400];
            wcscpy(wf, L"\\\\?\\");
            MultiByteToWideChar(CP_UTF8, 0, leaf, -1, wf + 4, 1290);
            FILE *f = _wfopen(wf, L"wb");
            CHECK(f != NULL);
            if (f) { for (int i = 0; i < 5000; i++) fputc(i & 0xFF, f); fclose(f); }

            ShsSyncStatus ls = RunSyncPaths(lsrc, ldst, false);
            CHECK(ls.state == SHS_SYNC_DONE && ls.errorCount == 0 && ls.filesDone == 1);

            char dleaf[1300];
            snprintf(dleaf, sizeof dleaf, "\\\\?\\%s%s", ldst, leaf + strlen(lsrc));
            wchar_t wd[1400];
            MultiByteToWideChar(CP_UTF8, 0, dleaf, -1, wd, 1400);
            WIN32_FILE_ATTRIBUTE_DATA info;
            CHECK(GetFileAttributesExW(wd, GetFileExInfoStandard, &info) && info.nFileSizeLow == 5000);   /* arrived intact */

            ShsPurgeJob *lp = ShsPurgeStart(lsrc, ldst, false);                              /* ...and it can be deleted again (Explorer cannot) */
            uint64_t ld = 0, lf = 0;
            for (int i = 0; lp && i < 3000 && !ShsPurgeStatus(lp, &ld, &lf); i++) SLEEP_MS(10);
            CHECK(lp && ShsPurgeStatus(lp, &ld, &lf) && ld == 1 && lf == 0);
            ShsPurgeFree(lp);
            CHECK(!GetFileAttributesExW(wd, GetFileExInfoStandard, &info));

            snprintf(prefixed, sizeof prefixed, "\\\\?\\%s", lsrc);
            RemoveTree(prefixed);
            snprintf(prefixed, sizeof prefixed, "\\\\?\\%s", ldst);
            RemoveTree(prefixed);
        }
    }
#endif

    /* ---- USB marker file: a hidden .simplesync-id in the target names the task, so the stick is found on any drive letter ---- */
    {
        printf("marker file identifies the target folder\n");
        char mdir[700], p[900];
        snprintf(mdir, sizeof mdir, "%s/marker_dir", g_base);
        RemoveTree(mdir);
        MKDIR(g_base); MKDIR(mdir);
        CHECK(!ShsMarkerHas(mdir, "00112233445566aa"));                      /* nothing there yet */
        CHECK(ShsMarkerAdd(mdir, "00112233445566aa") && ShsMarkerHas(mdir, "00112233445566aa"));
        CHECK(!ShsMarkerHas(mdir, "00112233445566ab"));                      /* a different task's id does not match */
        CHECK(ShsMarkerAdd(mdir, "00112233445566aa") && ShsMarkerAdd(mdir, "ffeeddccbbaa9988"));     /* adding again is harmless; a second task shares the folder */
        CHECK(ShsMarkerHas(mdir, "00112233445566aa") && ShsMarkerHas(mdir, "ffeeddccbbaa9988"));
        CHECK(!ShsMarkerHas(mdir, "00112233445566a") && !ShsMarkerHas(mdir, ""));      /* no partial or empty matches */
        snprintf(p, sizeof p, "%s/.simplesync-id", mdir);
        CHECK(SizeOf(mdir, ".simplesync-id") > 0);
#ifdef _WIN32
        {
            wchar_t w[900]; MultiByteToWideChar(CP_UTF8, 0, p, -1, w, 900);
            CHECK((GetFileAttributesW(w) & FILE_ATTRIBUTE_HIDDEN) != 0);       /* hidden in Explorer */
            SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);
        }
#endif
        remove(p);
        RemoveTree(mdir);
        char vols[8][SHS_VOLUME_PATH];
        int nv = ShsListVolumes(vols, 8);
#ifdef _WIN32
        CHECK(nv >= 1 && vols[0][1] == ':');                                  /* at least the system drive */
#else
        CHECK(nv >= 0);
#endif
    }

    /* ---- an unreadable folder in the target (like "System Volume Information" on a USB stick) must not fail the copy ---- */
#ifdef _WIN32
    {
        printf("unreadable folder in the target is skipped\n");
        char usrc[700], udst[700], p[900], cmd[1600];
        snprintf(usrc, sizeof usrc, "%s/lock_src", g_base);
        snprintf(udst, sizeof udst, "%s/lock_dst", g_base);
        RemoveTree(usrc); RemoveTree(udst);
        MKDIR(usrc); MKDIR(udst);
        snprintf(p, sizeof p, "%s/a.txt", usrc); MakeFilled(p, 4000, 51);
        snprintf(p, sizeof p, "%s/locked", udst); MKDIR(p);
        char win[900]; snprintf(win, sizeof win, "%s", p); for (char *c = win; *c; c++) if (*c == '/') *c = '\\';
        snprintf(cmd, sizeof cmd, "icacls \"%s\" /deny \"%%USERNAME%%:(OI)(CI)(F)\" >nul 2>nul", win);
        int denied = system(cmd);

        ShsSyncStatus us = RunSyncPaths(usrc, udst, false);
        CHECK(us.state == SHS_SYNC_DONE);
        CHECK(us.errorCount == 0);
        CHECK(SizeOf(udst, "a.txt") == 4000);

        snprintf(cmd, sizeof cmd, "icacls \"%s\" /remove:d \"%%USERNAME%%\" >nul 2>nul", win);   /* undo so the scratch folder can be removed */
        if (system(cmd) != 0) { /* best effort */ }
        if (denied != 0) printf("  (could not deny access; the skip itself was not exercised)\n");
        RemoveTree(usrc); RemoveTree(udst);
    }
#endif

    /* ---- update check helpers and drive detection (no network) ---- */
    {
        printf("version compare and volume roots\n");
        CHECK(ShsVersionNewer("v0.0.4", "0.0.3"));
        CHECK(ShsVersionNewer("0.1.0", "0.0.9"));
        CHECK(ShsVersionNewer("v1.0.0", "0.9.9"));
        CHECK(!ShsVersionNewer("v0.0.3", "0.0.3"));
        CHECK(!ShsVersionNewer("v0.0.2", "0.0.3"));
        CHECK(ShsVersionNewer("v0.0.3", "0.0.3-dev-2"));        /* the final release is newer than its own dev builds */
        CHECK(!ShsVersionNewer("v0.0.3-dev-1", "0.0.3"));
        CHECK(ShsVersionNewer("v0.0.4-dev-3", "0.0.4-dev-2") && !ShsVersionNewer("v0.0.4-dev-2", "0.0.4-dev-10"));
        CHECK(ShsVersionNewer("v0.0.4-dev-1", "0.0.3") && !ShsVersionNewer("v0.0.4-dev-1", "0.0.4"));
        CHECK(!ShsVersionCoreNewer("v0.0.1", "0.0.1-dev-90") && ShsVersionCoreNewer("v0.0.2", "0.0.1-dev-90"));   /* stable-only: a dev build of 0.0.1 is not outdated by 0.0.1 */
        char root[300];
#ifdef _WIN32
        CHECK(ShsVolumeRoot("E:\\Backups\\x", root, sizeof root) && strcmp(root, "E:\\") == 0);
        CHECK(ShsVolumeRoot("e:/Backups", root, sizeof root) && strcmp(root, "e:\\") == 0);
        CHECK(ShsVolumeRoot("\\\\nas\\share\\dir\\x", root, sizeof root) && strcmp(root, "\\\\nas\\share") == 0);
        CHECK(!ShsVolumeMounted("") && ShsVolumeMounted("C:\\"));
#else
        CHECK(ShsVolumeRoot("/Volumes/STICK/Backups", root, sizeof root) && strcmp(root, "/Volumes/STICK") == 0);
        CHECK(ShsVolumeRoot("/media/me/STICK/x/y", root, sizeof root) && strcmp(root, "/media/me/STICK") == 0);
        CHECK(!ShsVolumeRoot("/home/me/x", root, sizeof root));
#endif
    }

    CleanAll();
    printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
