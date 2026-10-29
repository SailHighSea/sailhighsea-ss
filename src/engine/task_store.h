#ifndef TASK_STORE_H
#define TASK_STORE_H

#include <stdbool.h>

#define SHS_DEF_NAME_MAX 64
#define SHS_DEF_PATH_MAX 260

/* A saved sync task (what the user configured, not its live progress). */
typedef struct {
    char      name[SHS_DEF_NAME_MAX];
    char      source[SHS_DEF_PATH_MAX];
    char      target[SHS_DEF_PATH_MAX];
    bool      mirror;       /* delete target items missing from source */
    long long lastSync;     /* unix seconds, 0 = never */
    long long lastBytes;    /* bytes copied by the last run */
    bool      autoSync;     /* watch the source folder and sync automatically when it changes */
    bool      usbSync;      /* sync when the drive holding the target is connected (USB stick plugged in) */
    bool      includeFolder; /* copy the source folder itself into <target>/<name> instead of only its contents */
    char      usbId[24];    /* USB sync: id written to the target folder's .simplesync-id marker ("" = no marker written yet) */
    bool      usbMark;      /* USB sync: write the hidden ID file into the target (on by default; older task files load as on) */
    char      deletedDir[SHS_DEF_PATH_MAX];   /* mirror: move removed target files here instead of deleting them ("" = delete) */
    char      exclude[1024]; /* excluded source folders, '|' separated ("" = none) */
} ShsTaskDef;

/* Plain text, one task per line: name \t source \t target \t mirror \t lastSync \t lastBytes \t autoSync \t usbSync \t deletedDir \t includeFolder \t usbId \t usbMark \t exclude.
   Older files with fewer columns still load.
   Returns the number of tasks read (0 if the file is missing). */
int  ShsStoreLoad(const char *path, ShsTaskDef *out, int max);
bool ShsStoreSave(const char *path, const ShsTaskDef *defs, int count);

#endif /* TASK_STORE_H */
