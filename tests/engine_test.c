/* Headless tests for the sync engine and task store.
   Usage: engine_test <scratch-dir>   (the dir is created and cleaned up; never point it at real data) */
#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE
  #define _DARWIN_C_SOURCE
#endif
#include "sync_engine.h"
#include "task_store.h"

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

    CleanAll();
    printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
