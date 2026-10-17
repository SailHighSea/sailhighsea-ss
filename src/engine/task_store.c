#include "task_store.h"

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
        fprintf(f, "%s\t%s\t%s\t%d\t%lld\t%lld\t%d\n", name, src, dst, defs[i].mirror ? 1 : 0,
                defs[i].lastSync, defs[i].lastBytes, defs[i].autoSync ? 1 : 0);
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
    char line[1024];
    while (n < max && fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;

        char *field[7] = { 0 };
        int nf = 0;
        for (char *p = line; nf < 7; ) {
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
    }
    fclose(f);
    return n;
}
