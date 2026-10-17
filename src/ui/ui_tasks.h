#ifndef UI_TASKS_H
#define UI_TASKS_H

#include "raylib.h"

#define SHS_TASK_PATH_MAX   260
#define SHS_TASK_NAME_MAX   64
#define SHS_TASK_MSG_MAX    160
#define SHS_TASK_VIEW_MAX   512   /* max cards with animated hover state */

typedef enum {
    SHS_TASK_IDLE = 0,   /* green, "Up-to-date" */
    SHS_TASK_SYNCING,    /* blue, spinner + progress bar */
    SHS_TASK_ERROR,      /* red box with warning text */
    SHS_TASK_PAUSED      /* amber badge */
} ShsTaskState;

#define SHS_TASK_FILES      8
#define SHS_TASK_FILE_NAME  120

/* One row of the expanded file list. state: 0 copying, 1 done, 2 failed, 3 removed. */
typedef struct {
    char               name[SHS_TASK_FILE_NAME];
    unsigned long long size, done;
    int                state;
} ShsTaskFile;

/* One row of the Quick Copy file list. state: 0 copying, 1 done, 2 failed, 3 removed, 4 skipped, 5 verifying, 6 queued. */
typedef struct {
    char               name[SHS_TASK_PATH_MAX];
    unsigned long long size;
    int                state;
} ShsTaskRow;

/* Supplies rows to the file list: `filter` 0 all, 1 queued / running, 2 done, 3 failed / skipped. Fills up to `max` rows
   starting at the `first`-th matching one and stores the number of matching rows in *total. Returns rows filled. */
typedef int (*ShsTaskRowsFn)(void *ctx, int filter, int first, int max, ShsTaskRow *out, int *total);

#define SHS_TASK_GRAPH   96
#define SHS_TASK_LOG     24
#define SHS_TASK_LOG_LEN 140

typedef struct {
    int          id;                          /* stable per-task index; keys the card's expanded state */
    char         name[SHS_TASK_NAME_MAX];
    char         source[SHS_TASK_PATH_MAX];
    char         target[SHS_TASK_PATH_MAX];
    ShsTaskState state;
    bool         mirror;                      /* shows the "Mirror" chip */
    bool         autoSync;                    /* watching the source folder; highlights the eye button */
    bool         quick;                       /* a one-off Quick Copy: drawn as a copy panel instead of a task card */

    /* Quick Copy panel */
    unsigned long long bytesDone, bytesTotal;         /* whole job */
    unsigned long long curSize, curDone;              /* the file being copied (or the last one) */
    char         curName[SHS_TASK_FILE_NAME];         /* its path relative to the source folder; empty = none yet */
    int          curState;                            /* ShsTaskFile.state of that file; 5 = verifying */
    unsigned long long filesVerified, filesSkipped;
    bool         verify, unattended;                  /* the two checkboxes */
    int          result;                              /* finished job: 0 none / running, 1 done, 2 failed, 3 stopped */
    float        progress;                    /* 0..1, SYNCING only */
    float        rateMBps;                    /* SYNCING only */
    float        etaSeconds;                  /* SYNCING only, <0 = unknown */
    char         meta[96];                    /* e.g. "Last synced: 2m ago | 14 MB" (non-syncing) */
    char         currentFile[SHS_TASK_PATH_MAX];  /* SYNCING only */
    char         errorMsg[SHS_TASK_MSG_MAX];  /* ERROR only */

    /* file activity shown when the card is expanded */
    ShsTaskFile  files[SHS_TASK_FILES];       /* newest first */
    int          fileCount;
    bool         lastRun;                     /* the list is from the last finished run, not a live one */
    unsigned long long filesDone, filesTotal, filesRemoved, errors;

    /* Quick Copy expanded panel */
    ShsTaskRowsFn rows;                       /* full file list; NULL = none */
    void         *rowsCtx;
    float        graph[SHS_TASK_GRAPH];       /* recent speed samples (MB/s), oldest first */
    int          graphCount;
    float        elapsedSec, avgMBps;
    int          activeFiles;                 /* files being copied right now */
    char         targetInfo[120];             /* e.g. "Free space: 16.3 GB" */
    char         optionsInfo[120];            /* e.g. "4 files at once, 64 MB read-ahead (Settings)" */
    char         log[SHS_TASK_LOG][SHS_TASK_LOG_LEN];   /* oldest first */
    int          logCount;
} ShsTask;

/* What the user pressed on a card this frame. */
typedef enum {
    SHS_ACT_NONE = 0,    /* the card body was clicked (select) */
    SHS_ACT_SYNC,
    SHS_ACT_PAUSE,
    SHS_ACT_RESUME,
    SHS_ACT_CANCEL,
    SHS_ACT_EDIT,
    SHS_ACT_REMOVE,      /* already confirmed (second click) */
    SHS_ACT_TOGGLE_AUTO, /* turn auto-sync (folder watching) on or off */
    SHS_ACT_SKIP,        /* Quick Copy: skip the current file */
    SHS_ACT_TOGGLE_VERIFY,
    SHS_ACT_TOGGLE_UNATTENDED,
    SHS_ACT_OPEN_SOURCE,
    SHS_ACT_OPEN_TARGET,
    SHS_ACT_ADD_FOLDER   /* Quick Copy: "Add folder..." in the file list (the caller opens the folder picker) */
} ShsTaskAction;

typedef struct {
    int           index;   /* card index in the list passed in, or -1 when nothing happened */
    ShsTaskAction action;
} ShsTaskEvent;

/* Persistent UI state for one scrolling task list. Zero-initialise it. */
typedef struct {
    float  scroll;         /* current (smoothed) offset in px */
    float  scrollTarget;   /* wheel-driven target offset */
    float  hover[SHS_TASK_VIEW_MAX];  /* 0..1 hover transition per card */
    int    selected;       /* card index drawn highlighted; the caller sets it every frame, -1 = none */
    bool   blocked;        /* a modal dialog is open: ignore the mouse */
    Texture2D logo;        /* app logo drawn as the play / pause button; set by the caller */
    Texture2D logoGray;    /* grey version, used while a task is paused */
    bool   expanded[SHS_TASK_VIEW_MAX];   /* card shows its file list (indexed by ShsTask.id) */
    float  expandT[SHS_TASK_VIEW_MAX];    /* 0..1 animation of the above */
    int    armedRemove;    /* card whose Remove button was clicked once, -1 = none (set by the widget) */
    double armedAt;

    /* Quick Copy panel state, indexed by ShsTask.id like `expanded` */
    int    tab[SHS_TASK_VIEW_MAX];          /* 0 file list, 1 target, 2 options, 3 status, 4 log */
    int    filter[SHS_TASK_VIEW_MAX];       /* file list filter, see ShsTaskRowsFn */
    float  listTop[SHS_TASK_VIEW_MAX];      /* first visible row of the file list */
    bool   freeScroll[SHS_TASK_VIEW_MAX];   /* auto-scroll turned off */
    int    reveal;                          /* card id + 1 that was just expanded: scroll it to the top while it opens (0 = none) */
    Rectangle innerRect, innerPrev;        /* screen rect of a scrollable file list (this / previous frame): the wheel scrolls it */
} ShsTaskView;

/* Call after task `id` is removed so the expanded flags of the later tasks shift down with them. */
void ShsTaskViewRemoved(ShsTaskView *view, int id);

/* Height of one collapsed card for the given task (px). */
float ShsTaskCardHeight(const ShsTask *task);

/* Handles input and draws the clipped, scrollable list inside `bounds`. */
ShsTaskEvent ShsTaskListDraw(ShsTaskView *view, const ShsTask *tasks, int count, Rectangle bounds);

#endif /* UI_TASKS_H */
