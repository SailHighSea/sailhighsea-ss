#include "task_store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copies `s` with tabs/newlines flattened so one record stays on one line. */
static void Flatten(char *dst, size_t cap, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) dst[i] = (s[i] == '\t' || s[i] == '\n' || s[i] == '\r') ? ' ' : s[i];
    dst[i] = 0;
}

bool ShsStoreSave(const char *path, const ShsTaskDef *defs, int count)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;

    for (int i = 0; i < count; i++) {
        char name[SHS_DEF_NAME_MAX], src[SHS_DEF_PATH_MAX], dst[SHS_DEF_PATH_MAX];
        Flatten(name, sizeof name, defs[i].name);
        Flatten(src, sizeof src, defs[i].source);
        Flatten(dst, sizeof dst, defs[i].target);
        char del[SHS_DEF_PATH_MAX], usbId[sizeof defs[i].usbId];
        Flatten(del, sizeof del, defs[i].deletedDir);
        Flatten(usbId, sizeof usbId, defs[i].usbId);
        char excl[sizeof defs[i].exclude];
        Flatten(excl, sizeof excl, defs[i].exclude);
        fprintf(f, "%s\t%s\t%s\t%d\t%lld\t%lld\t%d\t%d\t%s\t%d\t%s\t%d\t%s\n", name, src, dst, defs[i].mirror ? 1 : 0,
                defs[i].lastSync, defs[i].lastBytes, defs[i].autoSync ? 1 : 0, defs[i].usbSync ? 1 : 0, del, defs[i].includeFolder ? 1 : 0, usbId, defs[i].usbMark ? 1 : 0, excl);
    }
    bool ok = fclose(f) == 0;
    if (ok) {
        remove(path);                 /* rename() will not overwrite on Windows */
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    return ok;
}

int ShsStoreLoad(const char *path, ShsTaskDef *out, int max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    int n = 0;
    char line[4096];
    while (n < max && fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;

        char *field[13] = { 0 };
        int nf = 0;
        for (char *p = line; nf < 13; ) {
            field[nf++] = p;
            char *tab = strchr(p, '\t');
            if (!tab) break;
            *tab = 0;
            p = tab + 1;
        }
        if (nf < 3 || !field[0][0] || !field[1][0] || !field[2][0]) continue;

        ShsTaskDef *d = &out[n++];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%s", field[0]);
        snprintf(d->source, sizeof d->source, "%s", field[1]);
        snprintf(d->target, sizeof d->target, "%s", field[2]);
        d->mirror    = nf > 3 && atoi(field[3]) != 0;
        d->lastSync  = nf > 4 ? atoll(field[4]) : 0;
        d->lastBytes = nf > 5 ? atoll(field[5]) : 0;
        d->autoSync  = nf > 6 && atoi(field[6]) != 0;
        d->usbSync   = nf > 7 && atoi(field[7]) != 0;
        if (nf > 8) snprintf(d->deletedDir, sizeof d->deletedDir, "%s", field[8]);
        d->includeFolder = nf > 9 && atoi(field[9]) != 0;
        d->usbMark = nf > 11 ? atoi(field[11]) != 0 : true;
        if (nf > 12) snprintf(d->exclude, sizeof d->exclude, "%s", field[12]);
        if (nf > 10) {                                          /* a 16-digit hex id; anything else (an older build's number) is dropped */
            size_t k = 0;
            while (field[10][k] && isxdigit((unsigned char)field[10][k])) k++;
            if (k == 16 && !field[10][k]) snprintf(d->usbId, sizeof d->usbId, "%s", field[10]);
        }
    }
    fclose(f);
    return n;
}
