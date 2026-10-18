#include "raylib.h"
#include "ui/shs_theme.h"
#include "ui/ui_tasks.h"
#include "ui/icon_data.h"
#include "ui/shs_icons.h"
#include "engine/sync_engine.h"
#include "engine/task_store.h"
#include "platform/platform.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SHS_APP_TITLE   "SailHighSea SimpleSync"
#ifndef SHS_VERSION
#define SHS_VERSION     "0.0.1"   /* normally injected by CMake */
#endif
#define TITLEBAR_H      48
#define STATUSBAR_H     30
#define SIDEBAR_W       64      /* icon rail */
#define BTN_W           46
#define MAX_TASKS       64

#if defined(_WIN32)
  #define ABOUT_OS "Windows"
#elif defined(__APPLE__)
  #define ABOUT_OS "macOS"
#else
  #define ABOUT_OS "Linux"
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  #define ABOUT_PLATFORM ABOUT_OS " arm64"
#else
  #define ABOUT_PLATFORM ABOUT_OS " x64"
#endif
#define LOG_LINES       200
#define LOG_LEN         220

typedef enum { FILTER_ALL, FILTER_ACTIVE, FILTER_ERRORS, FILTER_PAUSED } Filter;
typedef enum { NAV_TASKS, NAV_TRAFFIC, NAV_LOGS, NAV_SETTINGS, NAV_ABOUT, NAV_COUNT } Nav;

static const char *NAV_LABEL[NAV_COUNT] = { "Tasks", "Live Traffic", "Event Logs", "Settings", "About" };

/* ---- application state -------------------------------------------------- */

typedef struct {
    ShsTaskDef     def;
    ShsSyncJob    *job;          /* non-NULL while a sync is in flight */
    ShsSyncStatus  last;         /* most recent poll of `job` */
    ShsSyncStatus  lastRun;      /* final status of the previous run (feeds the expanded file list) */
    bool           hasLastRun;
    bool           quick;        /* one-off Quick Copy: not saved, not part of Sync All */
    bool           quickVerify, quickUnattended;
    double         dismissAt;    /* unattended Quick Copy: when to remove the card (0 = never) */
    int            lastResult;   /* 1 done, 2 failed, 3 stopped */
    ShsWatch      *watch;        /* folder watcher while auto-sync is on */
    bool           pending;      /* a change was seen; sync once the folder has been quiet for a moment */
    double         lastChange;   /* when the last change was seen */
    double         nextWatchTry; /* retry time if the source folder could not be watched */
    bool           failed;       /* last run ended with errors */
    char           resultMsg[SHS_TASK_MSG_MAX];

    /* Quick Copy extras */
    ShsSyncJob    *doneJob;      /* the finished job, kept so its file list stays on the card */
    double         startTime, endTime;
    float          hist[SHS_TASK_GRAPH]; int histN; double histAt;      /* speed samples for the graph */
    double         freeAt;       float freeGiB;                          /* cached free space of the target */
    char           log[SHS_TASK_LOG][SHS_TASK_LOG_LEN]; int logN;       /* this copy's own log, oldest first */
} AppTask;

static AppTask g_app[MAX_TASKS];
static int     g_count = 0;
static int     g_sel = -1;                 /* selected task (index into g_app) */
static char    g_appDir[512];
static char    g_tasksFile[600];

static char    g_log[LOG_LINES][LOG_LEN];
static int     g_logCount = 0;             /* total ever written */

typedef struct {
    bool open;
    int  editIdx;                          /* -1 = adding a new task */
    char name[SHS_DEF_NAME_MAX], src[SHS_DEF_PATH_MAX], dst[SHS_DEF_PATH_MAX];
    bool mirror;
    bool autoSync;
    int  focus;                            /* 0 name, 1 source, 2 target */
    bool drag;                             /* drag & drop zones instead of the folder text fields */
    bool quick;                            /* Quick Copy: one-off copy, nothing saved */
    char err[120];
} Dialog;

static bool      g_autostart = false;        /* registered to start with the system (read from the OS) */
static bool      g_startHidden = false;      /* ...and then start hidden in the tray */
static bool      g_closeToTray = false;      /* close button hides the window instead of quitting */
static bool      g_dragDialog = false;       /* the New/Edit Task dialog uses drag & drop zones instead of text fields */
static bool      g_quickVerify = false;      /* Quick Copy: verify copied files (default for new copies) */
static bool      g_quickUnattended = false;  /* Quick Copy: dismiss the card by itself when the copy finishes */
static int       g_copyThreads = 4;          /* files copied in parallel (1..16) */
static int       g_copyBufMiB = 64;          /* read-ahead buffer for small files, MiB (0 = off) */
static bool      g_hidden = false;           /* window currently hidden in the tray */
static bool      g_launchedHidden = false;   /* started with --minimized */
static bool      g_multi = false;            /* started with --multi (testing): must not touch the start-up entry */
static bool      g_restart = false;          /* theme switch: start a fresh copy after closing */
static float     g_winR = 12.0f;             /* window corner radius; 0 when maximized */
static Texture2D g_logo;
static Texture2D g_logoGray;               /* grey copy of the logo, shown on paused tasks */                   /* title bar logo, from the embedded icon */
static bool g_block = false;               /* a modal dialog owns the mouse */

static Vector2 Mouse(void)   { return g_block ? (Vector2){ -10000.0f, -10000.0f } : GetMousePosition(); }
static bool    Pressed(void) { return !g_block && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

/* ---- log ---------------------------------------------------------------- */

static char g_debugFile[512];                /* simplesync-debug.log next to the exe; empty until set up */

/* Appends one timestamped line to the debug log file. */
static void DebugLine(const char *msg)
{
    if (!g_debugFile[0]) return;
    FILE *f = fopen(g_debugFile, "ab");
    if (!f) return;
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char stamp[24] = "----/--/-- --:--:--";
    if (tmv) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tmv);
    fprintf(f, "[%s] %s\n", stamp, msg);
    fclose(f);
}

static void LogAdd(const char *fmt, ...)
{
    char msg[LOG_LEN - 12];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char stamp[16] = "--:--:--";
    if (tmv) strftime(stamp, sizeof stamp, "%H:%M:%S", tmv);
    snprintf(g_log[g_logCount % LOG_LINES], LOG_LEN, "[%s] %s", stamp, msg);
    g_logCount++;
    DebugLine(msg);
}

/* Adds a line to a Quick Copy card's own log (and to the event log). */
static void TaskLog(AppTask *a, const char *fmt, ...)
{
    char msg[SHS_TASK_LOG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (a->logN == SHS_TASK_LOG) { memmove(a->log[0], a->log[1], sizeof a->log[0] * (SHS_TASK_LOG - 1)); a->logN--; }
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char stamp[16] = "--:--:--";
    if (tmv) strftime(stamp, sizeof stamp, "%H:%M:%S", tmv);
    snprintf(a->log[a->logN++], SHS_TASK_LOG_LEN, "[%s] %s", stamp, msg);
    LogAdd("%s: %s", a->def.name, msg);
}

/* ---- small helpers ------------------------------------------------------ */

static void AppPath(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", g_appDir, name);
}

static void FormatBytes(char *out, size_t cap, double b)
{
    if (b >= 1073741824.0)      snprintf(out, cap, "%.1f GB", b / 1073741824.0);
    else if (b >= 1048576.0)    snprintf(out, cap, "%.0f MB", b / 1048576.0);
    else if (b >= 1024.0)       snprintf(out, cap, "%.0f KB", b / 1024.0);
    else                        snprintf(out, cap, "%.0f B", b);
}

static void FormatAgo(char *out, size_t cap, long long then)
{
    long long d = (long long)time(NULL) - then;
    if (d < 0) d = 0;
    if (d < 60)         snprintf(out, cap, "just now");
    else if (d < 3600)  snprintf(out, cap, "%lldm ago", d / 60);
    else if (d < 86400) snprintf(out, cap, "%lldh ago", d / 3600);
    else                snprintf(out, cap, "%lldd ago", d / 86400);
}

static void NormPath(char *o, size_t cap, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) {
        char c = s[i] == '\\' ? '/' : s[i];
        o[i] = (char)tolower((unsigned char)c);
    }
    o[i] = 0;
    while (i > 1 && o[i - 1] == '/') o[--i] = 0;
}

/* True if `a` is the same folder as `b` or lies inside it. */
static bool SameOrInside(const char *a, const char *b)
{
    char x[600], y[600];
    NormPath(x, sizeof x, a);
    NormPath(y, sizeof y, b);
    size_t n = strlen(y);
    return n > 0 && strncmp(x, y, n) == 0 && (x[n] == 0 || x[n] == '/');
}

static bool ContainsNoCase(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

static int StatusRank(ShsTaskState s)
{
    switch (s) { case SHS_TASK_ERROR: return 0; case SHS_TASK_SYNCING: return 1; case SHS_TASK_PAUSED: return 2; default: return 3; }
}

/* ---- rounded shapes ------------------------------------------------------ */

static float RoundNess(Rectangle r, float radius)
{
    float m = r.width < r.height ? r.width : r.height;
    return m > 0 ? fminf(1.0f, 2.0f * radius / m) : 0.0f;
}

static void FillRound(Rectangle r, float radius, Color c)
{
    if (radius < 0.5f) DrawRectangleRec(r, c);
    else               ShsRoundRec(r, RoundNess(r, radius), 16, c);
}

/* Rounded on top only (title bar) / bottom only (status bar). */
static void FillRoundTop(Rectangle r, float radius, Color c)
{
    FillRound(r, radius, c);
    if (radius >= 0.5f) DrawRectangleRec((Rectangle){ r.x, r.y + r.height - radius, r.width, radius }, c);
}

static void FillRoundBottom(Rectangle r, float radius, Color c)
{
    FillRound(r, radius, c);
    if (radius >= 0.5f) DrawRectangleRec((Rectangle){ r.x, r.y, r.width, radius }, c);
}

/* ---- widgets ------------------------------------------------------------ */

static bool ButtonI(Rectangle r, const char *label, ShsIcon icon, bool primary, bool active)
{
    bool hot = CheckCollisionPointRec(Mouse(), r);
    Color bg = primary ? (hot ? SHS_COLOR_PRIMARY_HOVER : SHS_COLOR_PRIMARY)
                       : (active ? SHS_COLOR_PRIMARY : (hot ? SHS_COLOR_CARD_HOVER : SHS_COLOR_CARD));
    ShsDrawButtonBox(r, 9.0f, bg, (primary || active) ? bg : SHS_COLOR_BORDER, active || (hot && !g_block && IsMouseButtonDown(MOUSE_BUTTON_LEFT)));
    Color ink = (primary || active) ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN;

    int tw = ShsMeasureText(label, 15);
    float iconW = icon != SHS_ICON_NONE ? 16.0f : 0.0f;
    float gap = (icon != SHS_ICON_NONE && tw > 0) ? 8.0f : 0.0f;
    float x = r.x + (r.width - (iconW + gap + (float)tw)) / 2;
    if (icon != SHS_ICON_NONE) ShsDrawIcon(icon, x + 8.0f, r.y + r.height / 2, 16.0f, ink);
    ShsDrawText(label, (int)(x + iconW + gap), (int)(r.y + (r.height - 15) / 2), 15, ink);
    return hot && Pressed();
}

static bool Button(Rectangle r, const char *label, bool primary, bool active)
{
    return ButtonI(r, label, SHS_ICON_NONE, primary, active);
}

/* Appends typed characters / paste / backspace to `buf` (UTF-8 aware). */
static void HandleTyping(char *buf, size_t cap)
{
    size_t len = strlen(buf);
    int c;
    while ((c = GetCharPressed()) != 0) {
        if (c < 32 || c == 127) continue;
        int n = 0;
        const char *u = CodepointToUTF8(c, &n);
        if (n > 0 && len + (size_t)n < cap) { memcpy(buf + len, u, (size_t)n); len += (size_t)n; buf[len] = 0; }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len) {
        do { len--; } while (len && ((unsigned char)buf[len] & 0xC0) == 0x80);
        buf[len] = 0;
    }
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        const char *cb = GetClipboardText();
        for (; cb && *cb && len + 1 < cap; cb++)
            if (*cb != '\r' && *cb != '\n' && *cb != '\t') { buf[len++] = *cb; buf[len] = 0; }
    }
}

static void DrawFieldEx(Rectangle r, const char *text, const char *placeholder, bool focused, ShsIcon icon, bool onTitle)
{
    const bool tint = onTitle && !ShsThemeClassic();         /* retro themes keep a white field with dark text on the title bar */
    const Color ink    = tint ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN;
    const Color muted  = tint ? Fade(SHS_COLOR_ON_PRIMARY, 0.65f) : SHS_COLOR_TEXT_MUTED;
    const float round  = onTitle ? 0.5f : 0.25f;
    (void)round;
    if (tint) {
        ShsRoundRec(r, 0.5f, 16, Fade(SHS_COLOR_ON_PRIMARY, focused ? 0.20f : 0.12f));
        if (focused) ShsRoundLines(r, 0.5f, 16, 1.5f, Fade(SHS_COLOR_ON_PRIMARY, 0.75f));
    } else {
        ShsDrawRoundedBox(r, 9.0f, ShsThemeClassic() ? SHS_COLOR_CARD : SHS_COLOR_BG, focused ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER);
    }
    const int pad = icon != SHS_ICON_NONE ? 38 : 12;
    if (icon != SHS_ICON_NONE)
        ShsDrawIcon(icon, r.x + 20, r.y + r.height / 2, 16.0f, tint ? ink : (focused ? SHS_COLOR_PRIMARY : muted));
    const char *p = text;
    while (*p && ShsMeasureText(p, 15) > (int)r.width - pad - 12) {    /* keep the tail visible */
        p++;
        while (((unsigned char)*p & 0xC0) == 0x80) p++;
    }
    int ty = (int)(r.y + (r.height - 15) / 2);
    if (*text) ShsDrawText(p, (int)r.x + pad, ty, 15, ink);
    else       ShsDrawText(placeholder, (int)r.x + pad, ty, 15, muted);
    if (focused && ((int)(GetTime() * 2) & 1))
        DrawRectangle((int)r.x + pad + (*text ? ShsMeasureText(p, 15) + 1 : 0), ty - 1, 1, 17, ink);
}

static void DrawField(Rectangle r, const char *text, const char *placeholder, bool focused, ShsIcon icon)
{
    DrawFieldEx(r, text, placeholder, focused, icon, false);
}

/* ---- persistence -------------------------------------------------------- */

/* Settings file, one "key=value" per line: theme=auto|light|dark, startHidden=0|1, closeToTray=0|1, dragDialog=0|1, copyThreads=N, copyBufMB=N, quickVerify=0|1, quickUnattended=0|1.
   Whether the program starts with the system is read back from the OS, not stored here. */
static void SaveSettings(void)
{
    char path[600];
    AppPath(path, sizeof path, "simplesync-settings.txt");
    FILE *f = fopen(path, "wb");
    if (!f) return;
    ShsThemeMode m = ShsThemeGetMode();
    fprintf(f, "theme=%s\nstartHidden=%d\ncloseToTray=%d\ndragDialog=%d\ncopyThreads=%d\ncopyBufMB=%d\nquickVerify=%d\nquickUnattended=%d\n",
            ShsThemeName(m), g_startHidden ? 1 : 0, g_closeToTray ? 1 : 0,
            g_dragDialog ? 1 : 0, g_copyThreads, g_copyBufMiB, g_quickVerify ? 1 : 0, g_quickUnattended ? 1 : 0);
    fclose(f);
}

static void LoadSettings(void)
{
    char path[600], line[96];
    ShsThemeMode mode = SHS_THEME_AUTO;
    AppPath(path, sizeof path, "simplesync-settings.txt");
    FILE *f = fopen(path, "rb");
    while (f && fgets(line, sizeof line, f)) {
        if (!strncmp(line, "theme=", 6)) {
            char nm[32] = "";
            sscanf(line + 6, "%31[a-z0-9]", nm);
            mode = ShsThemeFromName(nm);
        }
        else if (strstr(line, "startHidden=1"))   g_startHidden = true;
        else if (strstr(line, "closeToTray=1"))   g_closeToTray = true;
        else if (strstr(line, "dragDialog=1"))    g_dragDialog = true;
        else if (strstr(line, "quickVerify=1"))     g_quickVerify = true;
        else if (strstr(line, "quickUnattended=1")) g_quickUnattended = true;
        else if (!strncmp(line, "copyThreads=", 12)) g_copyThreads = atoi(line + 12);
        else if (!strncmp(line, "copyBufMB=", 10))   g_copyBufMiB = atoi(line + 10);
    }
    if (f) fclose(f);
    ShsThemeSetMode(mode);
    g_autostart = ShsAutostartEnabled();
    if (g_copyThreads < 1) g_copyThreads = 1;
    if (g_copyThreads > 16) g_copyThreads = 16;
    if (g_copyBufMiB < 0) g_copyBufMiB = 0;
    if (g_copyBufMiB > 1024) g_copyBufMiB = 1024;
    ShsSyncConfigure(g_copyThreads, g_copyBufMiB);
    if (g_autostart && !g_multi && ShsAutostartRepair(g_startHidden)) LogAdd("Start-up entry pointed at a missing file; it now points at this program");
}

/* ---- tray / hide ---------------------------------------------------------- */

/* The tray icon exists while "close to tray" is on, or when the program was started hidden. */
static void ApplyTray(void)
{
    if (!ShsTraySupported()) return;
    if (g_closeToTray || g_launchedHidden) {
        if (!ShsTrayActive() && !ShsTrayCreate("SailHighSea SimpleSync")) LogAdd("Could not create the tray icon");
    } else {
        if (g_hidden) { ClearWindowState(FLAG_WINDOW_HIDDEN); g_hidden = false; }
        ShsTrayDestroy();
    }
}

static void HideToTray(void)
{
    if (!ShsTrayActive()) return;
    SetWindowState(FLAG_WINDOW_HIDDEN);
    g_hidden = true;
}

static void ShowFromTray(void)
{
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    if (IsWindowMinimized()) RestoreWindow();
    SetWindowFocused();
    g_hidden = false;
}

/* Settings-page number control:  label   [-]  value  [+]   hint.  Returns true when the value changed. */
static bool Stepper(float x, float y, const char *label, int *value, int lo, int hi, int step, const char *valueText, const char *hint)
{
    ShsDrawText(label, (int)x, (int)y + 6, 15, SHS_COLOR_TEXT_MAIN);
    float bx = x + 170;
    bool changed = false;
    if (Button((Rectangle){ bx, y, 30, 30 }, "-", false, false) && *value > lo) { *value = *value - step < lo ? lo : *value - step; changed = true; }
    int tw = ShsMeasureText(valueText, 15);
    ShsDrawText(valueText, (int)(bx + 30 + (84 - tw) / 2), (int)y + 6, 15, SHS_COLOR_TEXT_MAIN);
    if (Button((Rectangle){ bx + 114, y, 30, 30 }, "+", false, false) && *value < hi) { *value = *value + step > hi ? hi : *value + step; changed = true; }
    ShsDrawText(hint, (int)(bx + 164), (int)y + 8, 13, SHS_COLOR_TEXT_MUTED);
    return changed;
}

/* Settings-page switch: label + hint, returns true when clicked (and flips *value). */
static bool Toggle(float x, float y, float w, const char *label, const char *hint, bool *value)
{
    Rectangle row = { x, y, w, 46 };
    Rectangle sw = { x, y + 3, 44, 24 };
    ShsRoundRec(sw, 1.0f, 16, *value ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER);
    DrawCircleV((Vector2){ sw.x + (*value ? 33.0f : 11.0f), sw.y + 12 }, 8.5f, SHS_COLOR_ON_PRIMARY);
    ShsDrawText(label, (int)x + 58, (int)y + 1, 16, SHS_COLOR_TEXT_MAIN);
    ShsDrawText(hint, (int)x + 58, (int)y + 23, 13, SHS_COLOR_TEXT_MUTED);
    if (CheckCollisionPointRec(Mouse(), row) && Pressed()) { *value = !*value; return true; }
    return false;
}

static void SaveTasks(void)
{
    ShsTaskDef defs[MAX_TASKS];
    int n = 0;
    for (int i = 0; i < g_count; i++) if (!g_app[i].quick) defs[n++] = g_app[i].def;      /* Quick Copy cards are never saved */
    if (!ShsStoreSave(g_tasksFile, defs, n)) LogAdd("Could not save %s", g_tasksFile);
}

static void LoadTasks(void)
{
    ShsTaskDef defs[MAX_TASKS];
    int n = ShsStoreLoad(g_tasksFile, defs, MAX_TASKS);
    for (int i = 0; i < n; i++) { memset(&g_app[i], 0, sizeof g_app[i]); g_app[i].def = defs[i]; }
    g_count = n;
    if (n) LogAdd("Loaded %d task(s)", n);
}

/* ---- task control ------------------------------------------------------- */

static const char *ValidateTask(const char *src, const char *dst)
{
    if (!src[0] || !dst[0])          return "Choose both a source and a target folder.";
    if (!DirectoryExists(src))       return "The source folder does not exist.";
    if (SameOrInside(src, dst))      return "Source and target overlap (source is inside the target).";
    if (SameOrInside(dst, src))      return "Source and target overlap (target is inside the source).";
    return NULL;
}

/* Starts a task. For a Quick Copy, `item` (a dropped file or folder) is copied into the target instead of the source folder. */
static void StartTaskItem(int i, const char *item)
{
    AppTask *a = &g_app[i];
    if (a->job) return;
    const char *bad = item ? NULL : ValidateTask(a->def.source, a->def.target);
    if (item && SameOrInside(a->def.target, item)) bad = "The target is inside the dropped folder.";
    if (bad) {
        a->failed = true;
        snprintf(a->resultMsg, sizeof a->resultMsg, "%s", bad);
        LogAdd("%s: %s", a->def.name, bad);
        return;
    }
    if (a->doneJob) { ShsSyncFree(a->doneJob); a->doneJob = NULL; }
    if (item)          a->job = ShsSyncStartItem(item, a->def.target, SHS_CONFLICT_ASK);
    else if (a->quick) a->job = ShsSyncStartItem(a->def.source, a->def.target, SHS_CONFLICT_ASK);   /* the folder itself lands in the target, like dropped folders */
    else               a->job = ShsSyncStart(a->def.source, a->def.target, a->def.mirror);
    if (!a->job) {
        a->failed = true;
        snprintf(a->resultMsg, sizeof a->resultMsg, "Could not start the sync thread.");
        LogAdd("%s: could not start", a->def.name);
        return;
    }
    a->failed = false;
    a->resultMsg[0] = 0;
    a->dismissAt = 0;
    memset(&a->last, 0, sizeof a->last);
    a->startTime = GetTime(); a->endTime = 0; a->histN = 0; a->histAt = 0;
    if (a->quick) {
        ShsSyncSetVerify(a->job, a->quickVerify);
        TaskLog(a, "Copy started: %s -> %s", item ? item : a->def.source, a->def.target);
    } else {
        LogAdd("%s: sync started%s", a->def.name, a->def.mirror ? " (mirror)" : "");
    }
}

static void StartTask(int i) { StartTaskItem(i, NULL); }

/* Files or folders dropped on a Quick Copy card: queued on the running copy, or a new copy when it is not running. */
static void QuickAddPaths(int i, const FilePathList *fl)
{
    AppTask *a = &g_app[i];
    for (unsigned k = 0; k < fl->count; k++) {
        const char *p = fl->paths[k];
        if (a->job) {
            if (ShsSyncAddPath(a->job, p)) TaskLog(a, "Added %s", p);
            else                           TaskLog(a, "The copy has just finished; drop %s again to copy it", p);
        } else {
            StartTaskItem(i, p);
            if (!a->job) TaskLog(a, "Could not start copying %s: %s", p, a->resultMsg);
        }
    }
}

/* File list callback for the Quick Copy panel. */
static int QueryRows(void *ctx, int filter, int first, int max, ShsTaskRow *out, int *total)
{
    AppTask *a = (AppTask *)ctx;
    ShsSyncJob *j = a->job ? a->job : a->doneJob;
    *total = 0;
    if (!j) return 0;
    static const unsigned MASK[4] = {
        ~0u,
        (1u << SHS_FILE_PENDING) | (1u << SHS_FILE_COPYING) | (1u << SHS_FILE_VERIFYING),
        1u << SHS_FILE_DONE,
        (1u << SHS_FILE_FAILED) | (1u << SHS_FILE_SKIPPED) };
    static ShsSyncFileRow tmp[16];
    if (max > 16) max = 16;
    int n = ShsSyncGetFiles(j, MASK[filter & 3], first, max, tmp, total);
    for (int k = 0; k < n; k++) {
        snprintf(out[k].name, sizeof out[k].name, "%s", tmp[k].name);
        out[k].size = tmp[k].size;
        out[k].state = tmp[k].state;                 /* ShsFileState values match the widget's row states */
    }
    return n;
}

static void FinishTask(int i)
{
    AppTask *a = &g_app[i];
    ShsSyncStatus s;
    ShsSyncGetStatus(a->job, &s);
    if (a->quick) a->doneJob = a->job;               /* keep it: the card's file list reads from it */
    else          ShsSyncFree(a->job);
    a->job = NULL;
    a->endTime = GetTime();
    a->lastRun = s;
    a->hasLastRun = true;
    a->lastResult = s.state == SHS_SYNC_DONE ? 1 : (s.state == SHS_SYNC_FAILED ? 2 : 3);
    if (a->quick && a->quickUnattended && s.state == SHS_SYNC_DONE) a->dismissAt = GetTime() + 4.0;   /* unattended: no click needed */

    if (s.state == SHS_SYNC_DONE) {
        a->def.lastSync = (long long)time(NULL);
        a->def.lastBytes = (long long)s.bytesDone;
        a->failed = false;
        LogAdd("%s: done - %llu file(s) copied, %llu removed", a->def.name,
               (unsigned long long)s.filesDone, (unsigned long long)s.filesDeleted);
        if (a->quick) TaskLog(a, "Done: %llu file(s) copied%s", (unsigned long long)s.filesDone, s.filesSkipped ? ", some skipped" : "");
    } else if (s.state == SHS_SYNC_FAILED) {
        a->failed = true;
        if (a->quick) TaskLog(a, "Finished with %llu error(s): %s", (unsigned long long)s.errorCount, s.errorMsg);
        if (s.errorCount > 1) snprintf(a->resultMsg, sizeof a->resultMsg, "%s (+%llu more)", s.errorMsg, (unsigned long long)(s.errorCount - 1));
        else                  snprintf(a->resultMsg, sizeof a->resultMsg, "%s", s.errorMsg);
        LogAdd("%s: finished with %llu error(s): %s", a->def.name, (unsigned long long)s.errorCount, s.errorMsg);
    } else {
        a->failed = false;
        LogAdd("%s: cancelled", a->def.name);
    }
    SaveTasks();
}

static void PollJobs(void)
{
    for (int i = 0; i < g_count; i++) {
        AppTask *q = &g_app[i];
        if (q->quick && GetTime() - q->freeAt >= 2.0) {                 /* target free space for the Target tab */
            q->freeAt = GetTime();
            q->freeGiB = (float)ShsFreeDiskGiB(q->def.target);
        }
        if (!g_app[i].job) continue;
        ShsSyncGetStatus(g_app[i].job, &g_app[i].last);
        AppTask *a = &g_app[i];
        double now = GetTime();
        if (a->quick && now - a->histAt >= 0.5) {                      /* speed history for the graph */
            a->histAt = now;
            if (a->histN == SHS_TASK_GRAPH) { memmove(a->hist, a->hist + 1, sizeof a->hist[0] * (SHS_TASK_GRAPH - 1)); a->histN--; }
            a->hist[a->histN++] = a->last.state == SHS_SYNC_PAUSED ? 0.0f : a->last.rateMBps;
        }
        ShsSyncState st = g_app[i].last.state;
        if (st == SHS_SYNC_DONE || st == SHS_SYNC_FAILED || st == SHS_SYNC_CANCELLED) FinishTask(i);
    }
}

static void StopWatch(int i)
{
    if (g_app[i].watch) { ShsWatchStop(g_app[i].watch); g_app[i].watch = NULL; }
    g_app[i].pending = false;
}

/* Auto-sync: watch each enabled task's source folder; sync once after changes stop for AUTO_QUIET_S seconds,
   and once when watching starts so the target is up to date after the app (re)starts. */
#define AUTO_QUIET_S 3.0
static void WatchTasks(void)
{
    double now = GetTime();
    for (int i = 0; i < g_count; i++) {
        AppTask *a = &g_app[i];
        if (!a->def.autoSync) { if (a->watch) StopWatch(i); continue; }
        if (!a->watch) {
            if (now < a->nextWatchTry) continue;
            a->watch = ShsWatchStart(a->def.source);
            if (!a->watch) { a->nextWatchTry = now + 5.0; continue; }          /* folder missing: try again in a few seconds */
            LogAdd("%s: auto-sync watching %s", a->def.name, a->def.source);
            a->pending = true;
            a->lastChange = now - AUTO_QUIET_S;                               /* initial sync right away */
        }
        if (ShsWatchTake(a->watch)) { a->pending = true; a->lastChange = now; }
        if (a->pending && !a->job && now - a->lastChange >= AUTO_QUIET_S) {
            a->pending = false;
            LogAdd("%s: change detected, syncing", a->def.name);
            StartTask(i);
        }
    }
}

static void RemoveTask(int i)
{
    StopWatch(i);
    if (g_app[i].job) { ShsSyncFree(g_app[i].job); g_app[i].job = NULL; }
    if (g_app[i].doneJob) { ShsSyncFree(g_app[i].doneJob); g_app[i].doneJob = NULL; }
    LogAdd("%s: removed", g_app[i].def.name);
    for (int k = i; k + 1 < g_count; k++) g_app[k] = g_app[k + 1];
    g_count--;
    if (g_sel == i) g_sel = -1;
    else if (g_sel > i) g_sel--;
    SaveTasks();
}

/* Builds the card model the list widget draws. */
static void BuildView(const AppTask *a, ShsTask *v)
{
    memset(v, 0, sizeof *v);
    snprintf(v->name, sizeof v->name, "%s", a->def.name);
    snprintf(v->source, sizeof v->source, "%s", a->def.source);
    snprintf(v->target, sizeof v->target, "%s", a->def.target);
    v->etaSeconds = -1;
    v->mirror = a->def.mirror;
    v->autoSync = a->def.autoSync;
    v->quick = a->quick;

    if (a->quick) {                                                   /* Quick Copy expanded panel data */
        v->rows = QueryRows;
        v->rowsCtx = (void *)a;
        memcpy(v->graph, a->hist, sizeof v->graph);
        v->graphCount = a->histN;
        memcpy(v->log, a->log, sizeof v->log);
        v->logCount = a->logN;
        double end = a->job ? GetTime() : a->endTime;
        v->elapsedSec = a->startTime > 0 && end > a->startTime ? (float)(end - a->startTime) : 0.0f;
        if (a->freeGiB > 0) snprintf(v->targetInfo, sizeof v->targetInfo, "Free space on the target drive: %.1f GB", a->freeGiB);
        snprintf(v->optionsInfo, sizeof v->optionsInfo, "Copies %d file%s at once with a %d MB read-ahead buffer (change in Settings).",
                 g_copyThreads, g_copyThreads == 1 ? "" : "s", g_copyBufMiB);
    }

    /* file list for the expanded card: the live run, or else the previous run */
    const ShsSyncStatus *fs = a->job ? &a->last : (a->hasLastRun ? &a->lastRun : NULL);
    v->verify = a->quickVerify;
    v->unattended = a->quickUnattended;
    v->result = a->job ? 0 : a->lastResult;
    if (fs) {
        v->bytesDone = fs->bytesDone; v->bytesTotal = fs->bytesTotal;
        v->filesVerified = fs->filesVerified; v->filesSkipped = fs->filesSkipped;
        int ci = -1;                                                  /* the biggest file in flight (steadier than the newest), else the newest */
        for (int k = 0; k < fs->recentCount; k++)
            if ((fs->recent[k].state == SHS_FILE_COPYING || fs->recent[k].state == SHS_FILE_VERIFYING) &&
                (ci < 0 || fs->recent[k].size > fs->recent[ci].size)) ci = k;
        if (ci < 0 && fs->recentCount) ci = 0;
        if (ci >= 0) {
            snprintf(v->curName, sizeof v->curName, "%s", fs->recent[ci].name);
            v->curSize = fs->recent[ci].size; v->curDone = fs->recent[ci].done; v->curState = fs->recent[ci].state;
        }
        v->lastRun = !a->job;
        for (int k = 0; k < fs->recentCount; k++)
            if (fs->recent[k].state == SHS_FILE_COPYING || fs->recent[k].state == SHS_FILE_VERIFYING) v->activeFiles++;
        if (v->elapsedSec > 1.0f) v->avgMBps = (float)((double)fs->bytesDone / 1048576.0 / v->elapsedSec);
        v->filesDone = fs->filesDone; v->filesTotal = fs->filesTotal;
        v->filesRemoved = fs->filesDeleted; v->errors = fs->errorCount;
        v->fileCount = fs->recentCount < SHS_TASK_FILES ? fs->recentCount : SHS_TASK_FILES;
        for (int k = 0; k < v->fileCount; k++) {
            snprintf(v->files[k].name, sizeof v->files[k].name, "%s", fs->recent[k].name);
            v->files[k].size = fs->recent[k].size;
            v->files[k].done = fs->recent[k].done;
            v->files[k].state = fs->recent[k].state;
        }
    }

    if (a->job) {
        const ShsSyncStatus *s = &a->last;
        float p = s->bytesTotal ? (float)((double)s->bytesDone / (double)s->bytesTotal)
                                : (s->filesTotal ? (float)((double)s->filesDone / (double)s->filesTotal) : 0.0f);
        v->progress = p;
        v->rateMBps = s->rateMBps;
        if (s->state == SHS_SYNC_PAUSED) {
            v->state = SHS_TASK_PAUSED;
            snprintf(v->meta, sizeof v->meta, "Paused | %llu of %llu file(s)",
                     (unsigned long long)s->filesDone, (unsigned long long)s->filesTotal);
        } else {
            v->state = SHS_TASK_SYNCING;
            if (s->state == SHS_SYNC_PENDING || s->state == SHS_SYNC_SCANNING) snprintf(v->currentFile, sizeof v->currentFile, "scanning...");
            else snprintf(v->currentFile, sizeof v->currentFile, "%s", s->currentFile);
            if (s->rateMBps > 0.1f && s->bytesTotal > s->bytesDone)
                v->etaSeconds = (float)((double)(s->bytesTotal - s->bytesDone) / 1048576.0 / s->rateMBps);
        }
        return;
    }

    char meta[96];
    snprintf(meta, sizeof meta, "%s", a->quick ? "Quick copy - not copied yet" : "Never synced");
    if (a->def.lastSync) {
        char ago[24], sz[24];
        FormatAgo(ago, sizeof ago, a->def.lastSync);
        FormatBytes(sz, sizeof sz, (double)a->def.lastBytes);
        snprintf(meta, sizeof meta, "%s: %s | %s", a->quick ? "Quick copy done" : "Last synced", ago, sz);
    }
    if (a->def.mirror) strncat(meta, " | Mirror", sizeof meta - strlen(meta) - 1);
    if (a->def.autoSync) strncat(meta, " | Auto-sync", sizeof meta - strlen(meta) - 1);
    snprintf(v->meta, sizeof v->meta, "%s", meta);

    if (a->failed) {
        v->state = SHS_TASK_ERROR;
        snprintf(v->errorMsg, sizeof v->errorMsg, "%s", a->resultMsg);
    } else {
        v->state = SHS_TASK_IDLE;
        v->progress = a->def.lastSync ? 1.0f : 0.0f;       /* full once synced, empty if never synced */
    }
}

/* ---- test sandbox ------------------------------------------------------- */

static void CreateSandbox(void)
{
    if (!ShsSyncSetupDevFolders(g_appDir)) { LogAdd("Could not create the test folders in %s", g_appDir); return; }
    for (int i = 0; i < g_count; i++) if (!strcmp(g_app[i].def.name, "Test Sandbox")) { LogAdd("Test folders refreshed"); return; }
    if (g_count >= MAX_TASKS) return;
    AppTask *a = &g_app[g_count++];
    memset(a, 0, sizeof *a);
    snprintf(a->def.name, sizeof a->def.name, "Test Sandbox");
    snprintf(a->def.source, sizeof a->def.source, "%s/source_folder", g_appDir);
    snprintf(a->def.target, sizeof a->def.target, "%s/target_folder", g_appDir);
    a->def.mirror = true;
    SaveTasks();
    LogAdd("Test folders created in %s", g_appDir);
}

/* ---- add / edit dialog ---------------------------------------------------- */

static const char *LastFolder(const char *path);

static void DialogOpen(Dialog *d, int editIdx)
{
    memset(d, 0, sizeof *d);
    d->open = true;
    d->editIdx = editIdx;
    d->drag = g_dragDialog;
    if (editIdx >= 0) {
        const ShsTaskDef *t = &g_app[editIdx].def;
        snprintf(d->name, sizeof d->name, "%s", t->name);
        snprintf(d->src, sizeof d->src, "%s", t->source);
        snprintf(d->dst, sizeof d->dst, "%s", t->target);
        d->mirror = t->mirror;
        d->autoSync = t->autoSync;
    }
}

static void DialogOpenQuick(Dialog *d)
{
    DialogOpen(d, -1);
    d->quick = true;
    d->focus = 1;
}

/* A temporary Quick Copy card: copy only (no deletions), nothing saved. */
static void StartQuickCopy(const char *src, const char *dst)
{
    AppTask *q = &g_app[g_count++];
    memset(q, 0, sizeof *q);
    q->quick = true;
    q->quickVerify = g_quickVerify;
    q->quickUnattended = g_quickUnattended;
    snprintf(q->def.name, sizeof q->def.name, "Quick copy: %s", LastFolder(src));
    snprintf(q->def.source, sizeof q->def.source, "%s", src);
    snprintf(q->def.target, sizeof q->def.target, "%s", dst);
    g_sel = g_count - 1;
    StartTask(g_count - 1);
}

static void DialogSubmit(Dialog *d)
{
    const char *bad = ValidateTask(d->src, d->dst);
    if (bad) { snprintf(d->err, sizeof d->err, "%s", bad); return; }

    if (d->quick) {                                  /* Quick Copy: a temporary card, copy only (no deletions), nothing saved */
        if (g_count >= MAX_TASKS) { snprintf(d->err, sizeof d->err, "Task limit reached."); return; }
        StartQuickCopy(d->src, d->dst);
        d->open = false;
        return;
    }

    AppTask *a;
    if (d->editIdx >= 0) {
        a = &g_app[d->editIdx];
    } else {
        if (g_count >= MAX_TASKS) { snprintf(d->err, sizeof d->err, "Task limit reached."); return; }
        a = &g_app[g_count++];
        memset(a, 0, sizeof *a);
        g_sel = g_count - 1;
    }
    char fallback[SHS_DEF_NAME_MAX];
    snprintf(fallback, sizeof fallback, "Task %d", g_count);
    snprintf(a->def.name, sizeof a->def.name, "%s", d->name[0] ? d->name : fallback);
    snprintf(a->def.source, sizeof a->def.source, "%s", d->src);
    snprintf(a->def.target, sizeof a->def.target, "%s", d->dst);
    a->def.mirror = d->mirror;
    a->def.autoSync = d->autoSync;
    StopWatch((int)(a - g_app));                 /* restart the watcher on the (possibly new) source folder */
    a->failed = false;
    LogAdd("%s: %s", a->def.name, d->editIdx >= 0 ? "updated" : "added");
    SaveTasks();
    d->open = false;
}

/* Checkbox + label row; clicking anywhere on the row toggles it. */
static void DialogCheck(float x, float y, float w, const char *label, bool *value)
{
    Rectangle cb = { x, y + 4, 22, 22 };
    ShsRoundRec(cb, 0.25f, 6, *value ? SHS_COLOR_PRIMARY : SHS_COLOR_BG);
    ShsRoundLines(cb, 0.25f, 6, 1.0f, SHS_COLOR_BORDER);
    if (*value) {
        DrawLineEx((Vector2){ cb.x + 5, cb.y + 11 }, (Vector2){ cb.x + 10, cb.y + 16 }, 2, SHS_COLOR_ON_PRIMARY);
        DrawLineEx((Vector2){ cb.x + 10, cb.y + 16 }, (Vector2){ cb.x + 17, cb.y + 6 }, 2, SHS_COLOR_ON_PRIMARY);
    }
    ShsDrawText(label, (int)x + 32, (int)y + 7, 15, SHS_COLOR_TEXT_MAIN);
    Rectangle hit = { x, y, w, 30 };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), hit)) *value = !*value;
}

/* ---- drag & drop zones ------------------------------------------------------ */

/* Last folder name of a path ("D:\Work\Photos\" -> "Photos"); returns a pointer into a static buffer. */
static const char *LastFolder(const char *path)
{
    static char buf[SHS_DEF_NAME_MAX * 2];
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/')) len--;
    size_t start = len;
    while (start > 0 && path[start - 1] != '\\' && path[start - 1] != '/') start--;
    snprintf(buf, sizeof buf, "%.*s", (int)(len - start), path + start);
    if (!buf[0]) snprintf(buf, sizeof buf, "%.*s", (int)len, path);                    /* a drive root such as D:\ */
    return buf;
}

/* Draws `text` right-aligned to fit `maxW`, dropping characters from the start ("...tail"). */
static void DrawTailFit(const char *text, int x, int y, int size, int maxW, Color c)
{
    const char *p = text;
    char buf[SHS_DEF_PATH_MAX + 8];
    if (ShsMeasureText(p, size) > maxW) {
        while (*p && ShsMeasureText(p, size) + ShsMeasureText("...", size) > maxW) { p++; while (((unsigned char)*p & 0xC0) == 0x80) p++; }
        snprintf(buf, sizeof buf, "...%s", p);
        p = buf;
    }
    ShsDrawText(p, x, y, size, c);
}

/* Dashed outline that follows a rounded rectangle: the straight sides and the corner arcs are one continuous path,
   walked with a fixed dash / gap pattern so the dashes also curve around the corners. */
static void DashedRoundRect(Rectangle r, float rad, Color c)
{
    enum { ARC = 12 };
    Vector2 pts[4 * (ARC + 1) + 1];
    int n = 0;
    const float cx[4] = { r.x + r.width - rad, r.x + r.width - rad, r.x + rad, r.x + rad };       /* corner centres, clockwise from top-right */
    const float cy[4] = { r.y + rad, r.y + r.height - rad, r.y + r.height - rad, r.y + rad };
    const float a0[4] = { -90.0f, 0.0f, 90.0f, 180.0f };
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= ARC; i++) {
            float a = (a0[k] + 90.0f * (float)i / ARC) * 0.017453292f;
            pts[n++] = (Vector2){ cx[k] + rad * cosf(a), cy[k] + rad * sinf(a) };
        }
    pts[n++] = pts[0];                                                                           /* close the loop */

    const float dash = 6.0f, gap = 4.0f;
    bool on = true;
    float left = dash;
    for (int i = 0; i + 1 < n; i++) {
        Vector2 a = pts[i], b = pts[i + 1];
        float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)), t = 0.0f;
        while (t < len) {
            float step = fminf(left, len - t);
            if (on) {
                Vector2 p = { a.x + (b.x - a.x) * (t / len), a.y + (b.y - a.y) * (t / len) };
                Vector2 q = { a.x + (b.x - a.x) * ((t + step) / len), a.y + (b.y - a.y) * ((t + step) / len) };
                DrawLineEx(p, q, 1.4f, c);
            }
            t += step; left -= step;
            if (left <= 0.001f) { on = !on; left = on ? dash : gap; }
        }
    }
}
/* One drop zone. Returns 1 when clicked (open the folder picker) and 2 when its clear button is clicked. */
static int DropZone(Rectangle r, const char *label, const char *path, const char *what)
{
    Vector2 m = GetMousePosition();
    bool hot = CheckCollisionPointRec(m, r);
    bool filled = path[0] != 0;
    Color edge = hot ? SHS_COLOR_PRIMARY : (filled ? SHS_COLOR_TEXT_MUTED : SHS_COLOR_BORDER);
    FillRound(r, 12.0f, hot ? Fade(SHS_COLOR_PRIMARY, 0.10f) : SHS_COLOR_BG);
    DashedRoundRect((Rectangle){ r.x + 0.7f, r.y + 0.7f, r.width - 1.4f, r.height - 1.4f }, 11.3f, edge);
    ShsDrawText(label, (int)r.x + 14, (int)r.y + 10, 12, SHS_COLOR_TEXT_MUTED);

    Rectangle clr = { r.x + r.width - 32, r.y + 6, 24, 24 };
    bool clrHot = filled && CheckCollisionPointRec(m, clr);
    if (!filled) {
        float cx = r.x + r.width / 2;
        ShsDrawIcon(SHS_ICON_FOLDER, cx, r.y + 50, 30.0f, hot ? SHS_COLOR_PRIMARY : SHS_COLOR_TEXT_MUTED);
        char line[96];
        snprintf(line, sizeof line, "Drag the %s folder here", what);
        ShsDrawText(line, (int)(cx - ShsMeasureText(line, 15) / 2), (int)r.y + 74, 15, SHS_COLOR_TEXT_MAIN);
        const char *sub = "or click to browse";
        ShsDrawText(sub, (int)(cx - ShsMeasureText(sub, 13) / 2), (int)r.y + 96, 13, SHS_COLOR_TEXT_MUTED);
    } else {
        ShsDrawIcon(SHS_ICON_FOLDER, r.x + 30, r.y + 58, 26.0f, SHS_COLOR_PRIMARY);
        DrawTailFit(LastFolder(path), (int)r.x + 54, (int)r.y + 44, 18, (int)r.width - 54 - 40, SHS_COLOR_TEXT_MAIN);
        DrawTailFit(path, (int)r.x + 16, (int)r.y + 88, 13, (int)r.width - 32, SHS_COLOR_TEXT_MUTED);
        ShsRoundRec(clr, 0.4f, 8, clrHot ? SHS_COLOR_CARD_HOVER : Fade(SHS_COLOR_CARD, 0.8f));
        ShsDrawIcon(SHS_ICON_CLOSE, clr.x + clr.width / 2, clr.y + clr.height / 2, 12.0f, clrHot ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED);
    }
    if (hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return clrHot ? 2 : 1;
    return 0;
}

/* Folders dragged onto the window while the dialog is open. Over a zone / field: that one; otherwise the first empty one.
   Two or more at once: the first becomes the source and the second the target. */
static void DialogHandleDrop(Dialog *d, Rectangle srcR, Rectangle dstR)
{
    if (!IsFileDropped()) return;
    FilePathList fl = LoadDroppedFiles();
    Vector2 m = GetMousePosition();
    for (unsigned i = 0; i < fl.count; i++) {
        char dir[SHS_DEF_PATH_MAX];
        snprintf(dir, sizeof dir, "%s", DirectoryExists(fl.paths[i]) ? fl.paths[i] : GetDirectoryPath(fl.paths[i]));
        char *dest = NULL;
        if (fl.count == 1) {
            if (CheckCollisionPointRec(m, srcR))      dest = d->src;
            else if (CheckCollisionPointRec(m, dstR)) dest = d->dst;
            else                                       dest = d->src[0] ? d->dst : d->src;
        } else {
            dest = i == 0 ? d->src : (i == 1 ? d->dst : NULL);
        }
        if (dest) snprintf(dest, SHS_DEF_PATH_MAX, "%s", dir);
    }
    UnloadDroppedFiles(fl);
    if (!d->name[0] && d->src[0] && d->editIdx < 0) snprintf(d->name, sizeof d->name, "%s", LastFolder(d->src));
    d->err[0] = 0;
}

/* ---- "Destination file already exists" dialog (Quick Copy) -------------------------------------------------------------- */

/* Ellipsises the start of `text` until it fits `maxW` px at `size` (paths keep their tail). */
static const char *FitTail(char *out, size_t cap, const char *text, int size, float maxW)
{
    if (ShsMeasureText(text, size) <= maxW) { snprintf(out, cap, "%s", text); return out; }
    for (size_t cut = 1; cut < strlen(text); cut++) {
        snprintf(out, cap, "...%s", text + cut);
        if (ShsMeasureText(out, size) <= maxW) return out;
    }
    snprintf(out, cap, "...");
    return out;
}

static void SplitPath(const char *path, char *dir, size_t dcap, const char *name[1])
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    size_t n = (size_t)(b - path);
    if (n >= dcap) n = dcap - 1;
    memcpy(dir, path, n); dir[n] = 0;
    name[0] = b;
}

static void FormatStamp(char *out, size_t cap, long long t)
{
    time_t tt = (time_t)t;
    struct tm *tmv = localtime(&tt);
    if (tmv) strftime(out, cap, "%b %d, %Y - %H:%M", tmv); else snprintf(out, cap, "unknown date");
}

/* Arrow glyph for the big buttons: dir 0 = down, 1 = right; `all` adds a tick across the shaft. */
static void ArrowGlyph(int dir, bool all, float cx, float cy, float s, Color c)
{
    Vector2 d = dir == 0 ? (Vector2){ 0, 1 } : (Vector2){ 1, 0 }, n = { -d.y, d.x };
    Vector2 a = { cx - d.x * s, cy - d.y * s }, b = { cx + d.x * s, cy + d.y * s };
    DrawLineEx(a, b, 3.0f, c);
    Vector2 h1 = { b.x - d.x * s * 0.8f + n.x * s * 0.8f, b.y - d.y * s * 0.8f + n.y * s * 0.8f };
    Vector2 h2 = { b.x - d.x * s * 0.8f - n.x * s * 0.8f, b.y - d.y * s * 0.8f - n.y * s * 0.8f };
    DrawLineEx(b, h1, 3.0f, c); DrawLineEx(b, h2, 3.0f, c);
    if (all) {
        Vector2 m = { cx - d.x * s * 0.15f, cy - d.y * s * 0.15f };
        DrawLineEx((Vector2){ m.x - n.x * s * 0.6f, m.y - n.y * s * 0.6f }, (Vector2){ m.x + n.x * s * 0.6f, m.y + n.y * s * 0.6f }, 3.0f, c);
    }
}

static void ConflictDraw(int ti, int W, int H)
{
    AppTask *a = &g_app[ti];
    const ShsSyncStatus *s = &a->last;
    g_block = false;
    FillRound((Rectangle){ 0, 0, (float)W, (float)H }, g_winR, Fade(BLACK, 0.6f));

    const float pw = 700, ph = 492;
    Rectangle p = { (float)((W - pw) / 2), (float)((H - ph) / 2), pw, ph };
    if (ShsThemeBevel()) ShsDrawBevel(p, false, SHS_COLOR_BG);
    else { ShsRoundRec(p, 0.03f, 10, SHS_COLOR_CARD); ShsRoundLines(p, 0.03f, 10, 1.0f, SHS_COLOR_BORDER); }
    ShsDrawText("Destination file already exists", (int)p.x + 24, (int)p.y + 18, 20, SHS_COLOR_TEXT_MAIN);

    Rectangle closeR = { p.x + pw - 44, p.y + 12, 32, 32 };               /* the X stops the copy */
    bool closeHot = CheckCollisionPointRec(Mouse(), closeR);
    if (closeHot) FillRound(closeR, 8.0f, SHS_COLOR_CARD_HOVER);
    ShsDrawIcon(SHS_ICON_CLOSE, closeR.x + 16, closeR.y + 16, 14.0f, SHS_COLOR_TEXT_MUTED);
    if (closeHot && Pressed()) { ShsSyncCancel(a->job); return; }

    char dir[SHS_SYNC_PATH_MAX], fit[SHS_SYNC_PATH_MAX + 8], when[40], sz[40];
    const char *name;
    const float x = p.x + 24, R = p.x + pw - 24;

    /* source file (the one being copied) */
    SplitPath(s->conflictSrc, dir, sizeof dir, &name);
    ShsDrawIcon(SHS_ICON_LOGS, x + 16, p.y + 90, 30.0f, SHS_COLOR_PRIMARY);
    ShsDrawText(FitTail(fit, sizeof fit, name, 18, R - x - 56 - 170), (int)x + 48, (int)p.y + 70, 18, SHS_COLOR_TEXT_MAIN);
    ShsDrawText(FitTail(fit, sizeof fit, dir, 14, R - x - 56 - 170), (int)x + 48, (int)p.y + 98, 14, SHS_COLOR_TEXT_MUTED);
    FormatStamp(when, sizeof when, s->conflictSrcMtime);
    FormatBytes(sz, sizeof sz, (double)s->conflictSrcSize);
    ShsDrawText(when, (int)(R - ShsMeasureText(when, 15)), (int)p.y + 70, 15, SHS_COLOR_TEXT_MAIN);
    ShsDrawText(sz, (int)(R - ShsMeasureText(sz, 14)), (int)p.y + 98, 14, SHS_COLOR_TEXT_MUTED);

    /* the five answers for this file */
    static const struct { const char *label; ShsConflictAction act; bool all; int icon; } BTN[5] = {
        { "Overwrite",     SHS_CONFLICT_OVERWRITE,  false, 0 }, { "Overwrite all", SHS_CONFLICT_OVERWRITE, true, 0 },
        { "Skip",          SHS_CONFLICT_SKIP,       false, 1 }, { "Skip all",      SHS_CONFLICT_SKIP,      true, 1 },
        { "Keep both",     SHS_CONFLICT_KEEP_BOTH,  false, 2 } };
    const float by = p.y + 128, bh = 92, bw = pw / 5.0f;
    DrawRectangle((int)p.x + 1, (int)by, (int)pw - 2, (int)bh, SHS_COLOR_BG);
    for (int i = 0; i < 5; i++) {
        Rectangle r = { p.x + i * bw, by, bw, bh };
        bool hot = CheckCollisionPointRec(Mouse(), r);
        if (i == 0 || hot) DrawRectangleRec((Rectangle){ r.x + (i == 0 ? 1.0f : 0.0f), r.y, r.width - (i == 0 ? 0.0f : 0.0f), r.height }, i == 0 ? (hot ? SHS_COLOR_PRIMARY_HOVER : SHS_COLOR_PRIMARY) : SHS_COLOR_CARD_HOVER);
        Color ink = i == 0 ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN;
        float cx = r.x + r.width / 2, cy = r.y + 34;
        if (BTN[i].icon == 2) ShsDrawIcon(SHS_ICON_COPY, cx, cy, 26.0f, ink);
        else ArrowGlyph(BTN[i].icon, BTN[i].all, cx, cy, 11.0f, ink);
        ShsDrawText(BTN[i].label, (int)(cx - ShsMeasureText(BTN[i].label, 15) / 2.0f), (int)(r.y + 66), 15, ink);
        if (hot && Pressed()) { ShsSyncResolve(a->job, BTN[i].act, BTN[i].all); return; }
    }

    /* the file that is already there */
    SplitPath(s->conflictDst, dir, sizeof dir, &name);
    const float dy = by + bh + 14;
    ShsDrawIcon(SHS_ICON_LOGS, x + 16, dy + 22, 30.0f, SHS_COLOR_TEXT_MUTED);
    ShsDrawText(FitTail(fit, sizeof fit, name, 18, R - x - 56 - 150), (int)x + 48, (int)dy + 2, 18, SHS_COLOR_TEXT_MAIN);
    ShsDrawText(FitTail(fit, sizeof fit, dir, 14, R - x - 56 - 150), (int)x + 48, (int)dy + 30, 14, SHS_COLOR_TEXT_MUTED);
    long long dd = s->conflictSrcMtime - s->conflictDstMtime;
    const char *dateTxt = (dd <= 2 && dd >= -2) ? "Same date" : (dd < 0 ? "Newer" : "Older");
    const char *sizeTxt = s->conflictDstSize == s->conflictSrcSize ? "Same size" : (s->conflictDstSize > s->conflictSrcSize ? "Larger" : "Smaller");
    bool dateSame = dd <= 2 && dd >= -2, sizeSame = s->conflictDstSize == s->conflictSrcSize;
    ShsDrawText(dateTxt, (int)(R - ShsMeasureText(dateTxt, 15)), (int)dy + 2, 15, dateSame ? SHS_COLOR_TEXT_MUTED : SHS_COLOR_WARNING);
    ShsDrawText(sizeTxt, (int)(R - ShsMeasureText(sizeTxt, 14)), (int)dy + 30, 14, sizeSame ? SHS_COLOR_TEXT_MUTED : SHS_COLOR_WARNING);

    /* rules applied to this and all later conflicts */
    const float my = dy + 70;
    ShsDrawText("More options, applicable to all files", (int)x, (int)my, 14, SHS_COLOR_TEXT_MUTED);
    static const struct { const char *label; ShsConflictAction act; } MORE[6] = {
        { "Overwrite all older files",           SHS_CONFLICT_OVERWRITE_OLDER },
        { "Rename all copied files",             SHS_CONFLICT_KEEP_BOTH },
        { "Replace all smaller files",           SHS_CONFLICT_REPLACE_SMALLER },
        { "Rename all target files",             SHS_CONFLICT_RENAME_TARGET },
        { "Replace all files if size different", SHS_CONFLICT_REPLACE_DIFF_SIZE },
        { "Rename all older target files",       SHS_CONFLICT_RENAME_OLDER_TARGET } };
    const float colW = (R - x - 10) / 2.0f;
    for (int i = 0; i < 6; i++) {
        Rectangle r = { x + (i % 2) * (colW + 10), my + 26 + (i / 2) * 42.0f, colW, 34 };
        if (Button(r, MORE[i].label, false, false)) { ShsSyncResolve(a->job, MORE[i].act, true); return; }
    }
}

static void DialogDraw(Dialog *d, int W, int H)
{
    g_block = false;
    FillRound((Rectangle){ 0, 0, (float)W, (float)H }, g_winR, Fade(BLACK, 0.6f));

    const float pw = 640, ph = d->quick ? 372 : 428;
    Rectangle p = { (W - pw) / 2, (H - ph) / 2, pw, ph };
    if (ShsThemeBevel()) ShsDrawBevel(p, false, SHS_COLOR_BG);
    else { ShsRoundRec(p, 0.04f, 10, SHS_COLOR_CARD); ShsRoundLines(p, 0.04f, 10, 1.0f, SHS_COLOR_BORDER); }
    ShsDrawText(d->quick ? "Quick Copy" : (d->editIdx >= 0 ? "Edit Sync Task" : "New Sync Task"), (int)p.x + 24, (int)p.y + 20, 22, SHS_COLOR_TEXT_MAIN);

    /* mode switch, top right: Drag & drop folders [switch] */
    {
        const char *swLabel = "Drag & drop folders";
        int lw = ShsMeasureText(swLabel, 14);
        Rectangle sw = { p.x + pw - 24 - 44, p.y + 22, 44, 24 };
        ShsRoundRec(sw, 1.0f, 16, d->drag ? SHS_COLOR_PRIMARY : SHS_COLOR_BORDER);
        DrawCircleV((Vector2){ sw.x + (d->drag ? 33.0f : 11.0f), sw.y + 12 }, 8.5f, SHS_COLOR_ON_PRIMARY);
        ShsDrawText(swLabel, (int)(sw.x - 12 - lw), (int)p.y + 26, 14, SHS_COLOR_TEXT_MUTED);
        Rectangle hit = { sw.x - 16 - lw, sw.y - 4, 16 + lw + 44, 32 };
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), hit)) {
            d->drag = !d->drag;
            g_dragDialog = d->drag;
            if (d->drag) d->focus = 0;
            SaveSettings();
        }
    }

    float x = p.x + 24, w = pw - 48, y = p.y + 62;
    Rectangle fName = { x, y + 20, w, 34 };
    Rectangle fSrc  = { x, y + 82, w - 100, 34 };
    Rectangle fDst  = { x, y + 144, w - 100, 34 };
    Rectangle bSrc  = { x + w - 90, y + 82, 90, 34 };
    Rectangle bDst  = { x + w - 90, y + 144, 90, 34 };
    const float zw = (w - 16) / 2;
    Rectangle zSrc = { x, y + 62, zw, 122 };
    Rectangle zDst = { x + zw + 16, y + 62, zw, 122 };

    DialogHandleDrop(d, d->drag ? zSrc : fSrc, d->drag ? zDst : fDst);

    if (d->quick) {
        ShsDrawText("Copies new and changed files once. Nothing is saved, and nothing is deleted from the target.", (int)x, (int)y + 14, 14, SHS_COLOR_TEXT_MUTED);
    } else {
        ShsDrawText("NAME (optional)", (int)x, (int)y + 2, 12, SHS_COLOR_TEXT_MUTED);
    }
    char *bufs[3] = { d->name, d->src, d->dst };
    const size_t caps[3] = { sizeof d->name, sizeof d->src, sizeof d->dst };
    Rectangle fields[3] = { fName, fSrc, fDst };
    const char *hint[3] = { "e.g. Photos backup", "Folder to copy from", "Folder to copy to" };

    if (d->drag) {
        d->focus = 0;
        if (!d->quick) {
            HandleTyping(d->name, sizeof d->name);
            DrawField(fName, d->name, hint[0], true, SHS_ICON_NONE);
        }

        int a = DropZone(zSrc, "SOURCE FOLDER", d->src, "source");
        int b = DropZone(zDst, "TARGET FOLDER", d->dst, "target");
        char pick[SHS_DEF_PATH_MAX];
        if (a == 2) d->src[0] = 0;
        else if (a == 1 && ShsPickFolder("Select source folder", pick, sizeof pick)) snprintf(d->src, sizeof d->src, "%s", pick);
        if (b == 2) d->dst[0] = 0;
        else if (b == 1 && ShsPickFolder("Select target folder", pick, sizeof pick)) snprintf(d->dst, sizeof d->dst, "%s", pick);
    } else {
        ShsDrawText("SOURCE FOLDER", (int)x, (int)y + 64, 12, SHS_COLOR_TEXT_MUTED);
        ShsDrawText("TARGET FOLDER", (int)x, (int)y + 126, 12, SHS_COLOR_TEXT_MUTED);

        const int first = d->quick ? 1 : 0;                  /* Quick Copy has no name field */
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            for (int i = first; i < 3; i++) if (CheckCollisionPointRec(GetMousePosition(), fields[i])) d->focus = i;
        if (IsKeyPressed(KEY_TAB)) d->focus = (d->focus + ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 2 : 1)) % 3;
        if (d->focus < first) d->focus = first;
        HandleTyping(bufs[d->focus], caps[d->focus]);
        for (int i = first; i < 3; i++) DrawField(fields[i], bufs[i], hint[i], d->focus == i, SHS_ICON_NONE);

        if (ButtonI(bSrc, "Browse", SHS_ICON_FOLDER, false, false)) {
            char pick[SHS_DEF_PATH_MAX];
            if (ShsPickFolder("Select source folder", pick, sizeof pick)) snprintf(d->src, sizeof d->src, "%s", pick);
        }
        if (ButtonI(bDst, "Browse", SHS_ICON_FOLDER, false, false)) {
            char pick[SHS_DEF_PATH_MAX];
            if (ShsPickFolder("Select target folder", pick, sizeof pick)) snprintf(d->dst, sizeof d->dst, "%s", pick);
        }
    }

    if (!d->quick) {
        DialogCheck(x, y + 196, w, "Mirror: delete files in the target that are not in the source", &d->mirror);
        DialogCheck(x, y + 232, w, "Auto-sync: watch the source folder and sync changes automatically", &d->autoSync);
    }

    if (d->err[0]) ShsDrawText(d->err, (int)x, (int)y + (d->quick ? 204 : 276), 15, SHS_COLOR_DANGER);

    Rectangle save = { p.x + pw - 24 - 110, p.y + ph - 56, 110, 36 };
    Rectangle cancel = { save.x - 118, save.y, 110, 36 };
    bool doSave = ButtonI(save, d->quick ? "Copy" : "Save", d->quick ? SHS_ICON_COPY : SHS_ICON_CHECK, true, false);
    bool doCancel = ButtonI(cancel, "Cancel", SHS_ICON_CLOSE, false, false);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) doSave = true;
    if (IsKeyPressed(KEY_ESCAPE)) doCancel = true;

    if (doCancel) d->open = false;
    else if (doSave) DialogSubmit(d);
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    bool multi = false, restart = false;                  /* --multi: allow several copies (for measuring / testing) */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--multi")) multi = true;
        if (!strcmp(argv[i], "--restart")) restart = true;   /* started by a theme switch: the old copy is still closing */
    }
    g_multi = multi;
    if (!multi) {
        bool first = ShsSingleInstance();
        for (int tries = 0; !first && restart && tries < 40; tries++) { ShsSleepMs(100); first = ShsSingleInstance(); }
        if (!first) return 0;                            /* already running: it was asked to come to the front */
    }

    snprintf(g_appDir, sizeof g_appDir, "%s", GetApplicationDirectory());
    for (size_t n = strlen(g_appDir); n > 1 && (g_appDir[n - 1] == '/' || g_appDir[n - 1] == '\\'); ) g_appDir[--n] = 0;
    {   /* square retro themes are chosen before the window exists: they turn anti-aliasing off and load smaller fonts */
        char sp[600], ln[96];
        AppPath(sp, sizeof sp, "simplesync-settings.txt");
        FILE *sf = fopen(sp, "rb");
        while (sf && fgets(ln, sizeof ln, sf)) {
            if (!strncmp(ln, "theme=", 6)) {
                char nm[32] = "";
                sscanf(ln + 6, "%31[a-z0-9]", nm);
                if (ShsThemeFromName(nm) >= SHS_THEME_FLAT98) ShsThemeSetMode(ShsThemeFromName(nm));
            }
        }
        if (sf) fclose(sf);
    }

    /* Undecorated (custom title bar) + high-DPI aware; must be set before InitWindow. */
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT |
                   (ShsThemeFlat() ? 0 : FLAG_MSAA_4X_HINT));   /* MSAA keeps the rounded borders smooth; Flat 98 has none */
    InitWindow(1080, 650, SHS_APP_TITLE);
    Image appIcon = LoadImageFromMemory(".png", SHS_ICON_PNG, (int)sizeof SHS_ICON_PNG);
    if (appIcon.data) {
        SetWindowIcon(appIcon);
        if (ShsThemeFlat()) ImageResize(&appIcon, 128, 128);   /* Flat 98: a small logo texture is plenty */
        g_logo = LoadTextureFromImage(appIcon);          /* title bar logo */
        SetTextureFilter(g_logo, TEXTURE_FILTER_BILINEAR);
        Image gray = ImageCopy(appIcon);                  /* grey logo for paused tasks */
        ImageFormat(&gray, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        for (int i = 0; i < gray.width * gray.height; i++) {   /* desaturate by hand: ImageColorGrayscale() drops the alpha channel */
            unsigned char *px = (unsigned char *)gray.data + (size_t)i * 4;
            unsigned char g8 = (unsigned char)(0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]);
            px[0] = px[1] = px[2] = g8;
        }
        g_logoGray = LoadTextureFromImage(gray);
        SetTextureFilter(g_logoGray, TEXTURE_FILTER_BILINEAR);
        UnloadImage(gray);
        UnloadImage(appIcon);
    }
    if (!ShsThemeFlat()) ShsRoundWindowCorners(GetWindowHandle());
    SetWindowMinSize(860, 540);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    AppPath(g_tasksFile, sizeof g_tasksFile, "simplesync-tasks.tsv");
    AppPath(g_debugFile, sizeof g_debugFile, "simplesync-debug.log");
    {   /* keep the debug log small: start a new file once it passes 1 MB */
        FILE *f = fopen(g_debugFile, "rb");
        if (f) { fseek(f, 0, SEEK_END); long sz = ftell(f); fclose(f); if (sz > (1L << 20)) remove(g_debugFile); }
    }
    ShsSyncSetLogger(DebugLine);

    ShsFontsInit();
    LoadSettings();

    LogAdd("SailHighSea SimpleSync v" SHS_VERSION " started (font: %s)", ShsFontName());
    LoadTasks();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dev")) CreateSandbox();
        if (!strcmp(argv[i], "--minimized")) g_launchedHidden = true;
        if (!strcmp(argv[i], "--quick") && i + 2 < argc && g_count < MAX_TASKS) {      /* --quick <source> <target>: start a Quick Copy */
            if (!ValidateTask(argv[i + 1], argv[i + 2])) StartQuickCopy(argv[i + 1], argv[i + 2]);
            i += 2;
        }
        if (!strcmp(argv[i], "--quick-add") && i + 1 < argc && g_count > 0 && g_app[g_count - 1].quick) {   /* dev: same as dropping a path on the card */
            char *one[1] = { argv[i + 1] };
            FilePathList fl = { 1, 1, one };
            QuickAddPaths(g_count - 1, &fl);
            i += 1;
        }
    }
    ApplyTray();
    if (g_launchedHidden) HideToTray();

    ShsTaskView view = { 0 };
    view.armedRemove = -1;
    Dialog dlg = { 0 };
    Filter filter = FILTER_ALL;
    Nav nav = NAV_TASKS;
    int tasksTab = 0;                                   /* Tasks page tab: 0 Tasks, 1 Quick copies, 2 Schedule, 3 Archive */
    bool sortByStatus = false, searchFocus = false, shouldClose = false, dragging = false;
    char search[64] = "";
    Vector2 dragGrab = { 0 };

    while (!shouldClose && !WindowShouldClose()) {
        int trayEv = ShsTrayPoll();
        if (trayEv & SHS_TRAY_SHOW) ShowFromTray();
        if (trayEv & SHS_TRAY_SYNC_ALL) for (int i = 0; i < g_count; i++) if (!g_app[i].job && !g_app[i].quick) StartTask(i);
        if (trayEv & SHS_TRAY_QUIT) { shouldClose = true; break; }
        if (g_hidden) ShsSleepMs(50);                         /* idle cheaply while hidden */

        bool skipDraw = false;
        {   /* Frame rate follows activity: 60 while the user is interacting, 30 while copying, 20 when nothing happens. */
            static double lastInput = 0;
            static int curFps = 60;
            double nowT = GetTime();
            Vector2 md = GetMouseDelta();
            if (md.x != 0 || md.y != 0 || GetMouseWheelMove() != 0 || IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_TAB) ||
                IsKeyDown(KEY_ESCAPE) || IsKeyDown(KEY_LEFT_CONTROL) || IsFileDropped() || dlg.open)
                lastInput = nowT;
            bool anyJob = false;
            for (int i = 0; i < g_count; i++) if (g_app[i].job) { anyJob = true; break; }
            static unsigned prevSig = 0;                       /* something changed on its own (a job started / ended, a card was added) */
            unsigned sig = (unsigned)g_count * 131u;
            for (int i = 0; i < g_count; i++)
                sig = sig * 31u + (g_app[i].job ? 1u : 0u) + (g_app[i].failed ? 2u : 0u) + (unsigned)g_app[i].lastResult * 4u + (g_app[i].dismissAt > 0 ? 16u : 0u);
            if (sig != prevSig) { prevSig = sig; lastInput = nowT; }
            if (trayEv & SHS_TRAY_SHOW) lastInput = nowT;
            if (IsWindowResized()) lastInput = nowT;
            int fps = nowT - lastInput < 1.0 ? 60 : (anyJob ? 30 : 20);
            if (fps != curFps) { SetTargetFPS(fps); curFps = fps; }
            /* Nothing is moving and nobody is interacting: do not render at all (only once a minute so the "ago"
               texts stay fresh). Events are still polled, so the first mouse move or key wakes the window up. */
            static double lastDraw = 0;
            if (nowT - lastInput >= 1.0 && !anyJob && nowT - lastDraw < 60.0) skipDraw = true;
            else lastDraw = nowT;

            /* Idle for a while: give unused memory back to the OS once (it comes back on demand when the app is used). */
            static bool trimmed = false;
            if (nowT - lastInput < 1.0 || anyJob) trimmed = false;
            else if (!trimmed && nowT - lastInput >= 5.0) { ShsTrimMemory(); trimmed = true; }
        }

        const int W = GetScreenWidth(), H = GetScreenHeight();
        int conflictTask = -1;                                /* a Quick Copy waiting for "overwrite / skip / keep both" */
        for (int i = 0; i < g_count; i++) if (g_app[i].job && g_app[i].last.conflictPending) { conflictTask = i; break; }
        if (conflictTask >= 0 && g_hidden) ShowFromTray();
        g_block = dlg.open || conflictTask >= 0;
        Vector2 mouse = Mouse();
        bool maximized = IsWindowMaximized();

        if (!dlg.open && IsFileDropped()) {
            int qi = -1;                                                       /* an expanded Quick Copy card takes the drop */
            if (nav == NAV_TASKS) for (int i = 0; i < g_count; i++) if (g_app[i].quick && view.expanded[i]) { qi = i; break; }
            if (qi >= 0) {
                FilePathList fl = LoadDroppedFiles();
                QuickAddPaths(qi, &fl);
                UnloadDroppedFiles(fl);
            } else {
                FilePathList fl = LoadDroppedFiles();
                nav = NAV_TASKS;
                if (fl.count > 0 && DirectoryExists(fl.paths[0])) {            /* a folder dropped on the window: Quick Copy, so the folder itself lands in the target */
                    DialogOpenQuick(&dlg);
                    snprintf(dlg.src, sizeof dlg.src, "%s", fl.paths[0]);
                } else if (fl.count > 0) {                                     /* a file: a new task for its folder */
                    DialogOpen(&dlg, -1);
                    snprintf(dlg.src, sizeof dlg.src, "%s", GetDirectoryPath(fl.paths[0]));
                    snprintf(dlg.name, sizeof dlg.name, "%s", LastFolder(dlg.src));
                }
                UnloadDroppedFiles(fl);
            }
        }
        ShsThemeUpdate();
        ShsFontsCheckDpi();
        PollJobs();
        WatchTasks();
        for (int i = 0; i < g_count; i++)                                       /* unattended Quick Copy: remove the finished card */
            if (g_app[i].quick && g_app[i].dismissAt > 0 && !g_app[i].job && GetTime() >= g_app[i].dismissAt) {
                RemoveTask(i);
                ShsTaskViewRemoved(&view, i);
                break;
            }
        static ShsTask views[MAX_TASKS];
        int nActive = 0, nIdle = 0, nErr = 0, nPaused = 0;
        float totalRate = 0;
        for (int i = 0; i < g_count; i++) {
            BuildView(&g_app[i], &views[i]);
            views[i].id = i;
            switch (views[i].state) {
                case SHS_TASK_SYNCING: nActive++; totalRate += views[i].rateMBps; break;
                case SHS_TASK_IDLE:    nIdle++;   break;
                case SHS_TASK_ERROR:   nErr++;    break;
                case SHS_TASK_PAUSED:  nPaused++; break;
            }
        }

        if (skipDraw) {                                           /* idle: the logic above still ran; just wait for events */
            PollInputEvents();
            ShsSleepMs(50);                                       /* not WaitTime(): raylib busy-waits at the end of it */
            continue;
        }
        BeginDrawing();
        g_winR = 0.0f;                                           /* the OS clips the window corners (see ShsRoundWindowCorners) */
        ClearBackground(SHS_COLOR_BG);
        FillRound((Rectangle){ 0, 0, (float)W, (float)H }, g_winR, SHS_COLOR_BG);

        /* ================= title bar ================= */
        if (ShsThemeClassic()) {
            ShsDrawTitleBar(W, TITLEBAR_H);
            Color bk = ShsThemeTitleBack();
            if (bk.a) DrawRectangle(8, 8, ShsMeasureText(SHS_APP_TITLE, 19) + 54, TITLEBAR_H - 16, bk);          /* patch under logo and title on striped bars */
        } else FillRoundTop((Rectangle){ 0, 0, (float)W, TITLEBAR_H }, g_winR, SHS_COLOR_TITLEBAR);
        if (g_logo.id) {
            DrawTexturePro(g_logo, (Rectangle){ 0, 0, (float)g_logo.width, (float)g_logo.height },
                           (Rectangle){ 14, 10, 28, 28 }, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
        const Color titleInk = ShsThemeClassic() ? ShsThemeTitleInk() : SHS_COLOR_ON_PRIMARY;
        ShsDrawText(SHS_APP_TITLE, 52, 14, 19, titleInk);
        if (ShsThemeFauxBold()) ShsDrawText(SHS_APP_TITLE, 53, 14, 19, titleInk);      /* faux bold */

        /* window buttons: 36 x 32 with 6 px gaps and a 12 px right margin, vertically centred in the bar */
        const float wbW = 36, wbH = 32, wbGap = 6, wbY = (TITLEBAR_H - wbH) / 2;
        Rectangle closeB = { (float)W - 12 - wbW, wbY, wbW, wbH };
        Rectangle maxB   = { closeB.x - wbGap - wbW, wbY, wbW, wbH };
        Rectangle minB   = { maxB.x - wbGap - wbW, wbY, wbW, wbH };

        /* search box: centred in the window and in the bar (kept clear of the title on the left and the buttons on the right) */
        float searchW = fminf(520.0f, fmaxf(200.0f, (float)W - 2 * 340.0f));
        Rectangle searchB = { ((float)W - searchW) / 2, wbY, searchW, wbH };

        DrawFieldEx(searchB, search, "Search sync rules...", searchFocus, SHS_ICON_SEARCH, true);


        bool hMin = CheckCollisionPointRec(mouse, minB), hMax = CheckCollisionPointRec(mouse, maxB), hClose = CheckCollisionPointRec(mouse, closeB);
        const bool flat98 = ShsThemeClassic();
        if (flat98) {                                    /* Win98 caption buttons: grey squares with a 1 px border and black glyphs */
            const Rectangle wbs[3] = { minB, maxB, closeB };
            const bool wbHot[3] = { hMin, hMax, hClose };
            for (int i = 0; i < 3; i++)
                ShsDrawButtonBox(wbs[i], ShsThemeFlat() ? 0.0f : 6.0f, (wbHot[i] && !ShsThemeBevel()) ? (i == 2 ? SHS_COLOR_DANGER : SHS_COLOR_CARD_HOVER) : SHS_COLOR_BG, SHS_COLOR_BORDER, wbHot[i] && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        } else {
            if (hMin)   ShsRoundRec(minB,   0.4f, 10, Fade(SHS_COLOR_ON_PRIMARY, 0.18f));
            if (hMax)   ShsRoundRec(maxB,   0.4f, 10, Fade(SHS_COLOR_ON_PRIMARY, 0.18f));
            if (hClose) ShsRoundRec(closeB, 0.4f, 10, SHS_COLOR_DANGER);
        }
        const Color wInk = flat98 ? ((hClose && !ShsThemeBevel()) ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN) : SHS_COLOR_ON_PRIMARY;
        const float mcx = minB.x + wbW / 2,   mcy = minB.y + wbH / 2;
        const float xcx = maxB.x + wbW / 2,   xcy = maxB.y + wbH / 2;
        const float ccx = closeB.x + wbW / 2, ccy = closeB.y + wbH / 2;
        DrawLineEx((Vector2){ mcx - 6, mcy + 3 }, (Vector2){ mcx + 6, mcy + 3 }, 2.0f, wInk);                    /* minimise */
        DrawRectangleLinesEx((Rectangle){ xcx - 6, xcy - 6, 12, 12 }, 2.0f, wInk);                                 /* maximise */
        DrawLineEx((Vector2){ ccx - 6, ccy - 6 }, (Vector2){ ccx + 6, ccy + 6 }, 2.0f, wInk);                       /* close */
        DrawLineEx((Vector2){ ccx + 6, ccy - 6 }, (Vector2){ ccx - 6, ccy + 6 }, 2.0f, wInk);

        bool overTitleBtns = hMin || hMax || hClose || CheckCollisionPointRec(mouse, searchB);
        if (Pressed()) {
            if (hClose) { if (g_closeToTray && ShsTrayActive()) HideToTray(); else shouldClose = true; }
            else if (hMin) MinimizeWindow();
            else if (hMax) { if (maximized) RestoreWindow(); else MaximizeWindow(); }
            else if (CheckCollisionPointRec(mouse, searchB)) searchFocus = true;
            else {
                searchFocus = false;
                if (mouse.y < TITLEBAR_H && !overTitleBtns && !maximized) { dragging = true; dragGrab = mouse; }
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;
        if (dragging) {
            Vector2 win = GetWindowPosition();
            Vector2 now = GetMousePosition();
            SetWindowPosition((int)(win.x + now.x - dragGrab.x), (int)(win.y + now.y - dragGrab.y));
        }
        if (searchFocus && !g_block) {
            HandleTyping(search, sizeof search);
            if (IsKeyPressed(KEY_ESCAPE)) searchFocus = false;
        }

        /* ================= sidebar ================= */
        Rectangle side = { 0, TITLEBAR_H, SIDEBAR_W, (float)(H - TITLEBAR_H - STATUSBAR_H) };
        DrawRectangleRec(side, ShsThemeClassic() ? SHS_COLOR_BG : SHS_COLOR_CARD);
        DrawRectangle(SIDEBAR_W - 1, TITLEBAR_H, 1, (int)side.height, SHS_COLOR_BORDER);

        {   /* a newly added card brings its tab to the front */
            static int prevCount = 0;
            if (g_count > prevCount) { tasksTab = g_app[g_count - 1].quick ? 1 : 0; nav = NAV_TASKS; }
            prevCount = g_count;
        }
        const char *railTip = NULL;
        float railTipY = 0;
        {
            const float bs = 40, bx = (SIDEBAR_W - bs) / 2;
            Rectangle addB = { bx, (float)TITLEBAR_H + 12, bs, bs }, quickB = { bx, (float)TITLEBAR_H + 60, bs, bs };
            if (ButtonI(addB, "", SHS_ICON_PLUS, true, false)) { DialogOpen(&dlg, -1); nav = NAV_TASKS; tasksTab = 0; }
            if (ButtonI(quickB, "", SHS_ICON_COPY, false, false)) { DialogOpenQuick(&dlg); nav = NAV_TASKS; tasksTab = 1; }   /* one-off copy: nothing is saved */
            if (CheckCollisionPointRec(mouse, addB))   { railTip = "Add Task";   railTipY = addB.y + bs / 2; }
            if (CheckCollisionPointRec(mouse, quickB)) { railTip = "Quick Copy"; railTipY = quickB.y + bs / 2; }
            DrawRectangle(14, TITLEBAR_H + 112, SIDEBAR_W - 28, 1, SHS_COLOR_BORDER);
            static const ShsIcon navIcon[NAV_COUNT] = { SHS_ICON_TASKS, SHS_ICON_TRAFFIC, SHS_ICON_LOGS, SHS_ICON_GEAR, SHS_ICON_INFO };
            for (int i = 0; i < NAV_COUNT; i++) {
                Rectangle r = { bx, (float)(TITLEBAR_H + 124 + i * 48), bs, bs };
                bool hot = CheckCollisionPointRec(mouse, r), on = (int)nav == i;
                if (ShsThemeBevel()) ShsDrawBevel(r, on || (hot && !g_block && IsMouseButtonDown(MOUSE_BUTTON_LEFT)), SHS_COLOR_BG);
                else {
                    if (on)       ShsRoundRec(r, 0.25f, 8, Fade(SHS_COLOR_PRIMARY, 0.25f));
                    else if (hot) ShsRoundRec(r, 0.25f, 8, SHS_COLOR_CARD_HOVER);
                    if (on)       DrawRectangle(0, (int)r.y + 10, 3, 20, SHS_COLOR_PRIMARY);
                }
                ShsDrawIcon(navIcon[i], r.x + bs / 2, r.y + bs / 2, 20.0f, on ? SHS_COLOR_PRIMARY : SHS_COLOR_TEXT_MUTED);
                if (hot) {
                    static char tipBuf[48];
                    if (i == NAV_TASKS) snprintf(tipBuf, sizeof tipBuf, "%s (%d)", NAV_LABEL[i], g_count);
                    else                snprintf(tipBuf, sizeof tipBuf, "%s", NAV_LABEL[i]);
                    railTip = tipBuf; railTipY = r.y + bs / 2;
                    if (Pressed()) nav = (Nav)i;
                }
            }
        }

        /* ================= main pane ================= */
        float px = SIDEBAR_W + 20, pw = (float)(W - SIDEBAR_W - 40);
        float py = TITLEBAR_H + 16;

        if (nav == NAV_TASKS) {
            Rectangle activeTabR = { 0 };                       /* Win98: the selected tab is drawn over the divider */
            int nQuick = 0;
            for (int i = 0; i < g_count; i++) if (g_app[i].quick) nQuick++;
            {   /* page tabs */
                char tl[4][32];
                snprintf(tl[0], sizeof tl[0], "Tasks (%d)", g_count - nQuick);
                snprintf(tl[1], sizeof tl[1], "Quick copies (%d)", nQuick);
                snprintf(tl[2], sizeof tl[2], "Schedule");
                snprintf(tl[3], sizeof tl[3], "Archive");
                float tx = px;
                for (int i = 0; i < 4; i++) {
                    float tw = (float)ShsMeasureText(tl[i], 17) + 28;
                    Rectangle tr = { tx, py, tw, 38 };
                    bool hot = CheckCollisionPointRec(mouse, tr), on = tasksTab == i;
                    Color tabInk = on ? SHS_COLOR_PRIMARY : SHS_COLOR_TEXT_MUTED;
                    int tabTextY = (int)py + 9;
                    if (ShsThemeFolderTabs() || ShsThemeBevel()) {       /* retro themes: folder tabs, the selected one a little taller */
                        Rectangle bt = on ? (Rectangle){ tx, py + 1, tw, 40 } : (Rectangle){ tx, py + 5, tw, 34 };
                        tabInk = ShsDrawTab(bt, on);
                        if (on) activeTabR = bt;
                        tabTextY = (int)py + (on ? 9 : 12);
                    } else if (hot && !on) ShsRoundRec((Rectangle){ tr.x, tr.y + 2, tr.width, 32 }, 0.25f, 8, SHS_COLOR_CARD_HOVER);
                    ShsDrawText(tl[i], (int)(tx + 14), tabTextY, 17, tabInk);
                    if (on && !ShsThemeFolderTabs() && !ShsThemeBevel()) DrawRectangle((int)tx, (int)py + 36, (int)tw, 3, SHS_COLOR_PRIMARY);
                    tx += tw + 6;
                }
            }
            Rectangle syncAll = { px + pw - 110, py, 110, 32 };
            Rectangle pauseAll = { syncAll.x - 126, py, 118, 32 };
            if (ButtonI(pauseAll, "Pause All", SHS_ICON_PAUSE, false, false))
                for (int i = 0; i < g_count; i++) if (g_app[i].job) ShsSyncSetPaused(g_app[i].job, true);
            if (ButtonI(syncAll, "Sync All", SHS_ICON_SYNC, true, false)) {
                for (int i = 0; i < g_count; i++) {
                    if (g_app[i].job) ShsSyncSetPaused(g_app[i].job, false);
                    else if (!g_app[i].quick) StartTask(i);
                }
            }
            DrawRectangle((int)px, (int)py + 38, (int)pw, 1, SHS_COLOR_BORDER);
            if (ShsThemeBevel()) DrawRectangle((int)px, (int)py + 39, (int)pw, 1, WHITE);
            if (activeTabR.width > 0) DrawRectangle((int)activeTabR.x + 2, (int)py + 38, (int)activeTabR.width - 4, 3, ShsThemeTabFill());     /* open the active tab into the page */

            const bool listTab = tasksTab < 2;
            int tabAll = 0, tabActive = 0, tabErr = 0, tabPaused = 0;      /* counts for the current tab */
            for (int i = 0; i < g_count; i++) {
                if (g_app[i].quick != (tasksTab == 1)) continue;
                tabAll++;
                switch (views[i].state) {
                    case SHS_TASK_SYNCING: tabActive++; break;
                    case SHS_TASK_ERROR:   tabErr++; break;
                    case SHS_TASK_PAUSED:  tabPaused++; break;
                    default: break;
                }
            }
            struct { const char *name; int count; } fl[4] = {
                { "All", tabAll }, { "Active", tabActive }, { "Errors", tabErr }, { "Paused", tabPaused } };
            float fx = px, fy = py + 50;
            if (listTab) {
                ShsDrawText("Filter:", (int)fx, (int)fy + 8, 14, SHS_COLOR_TEXT_MUTED);
                fx += 56;
                static const ShsIcon filterIcon[4] = { SHS_ICON_GRID, SHS_ICON_SYNC, SHS_ICON_WARN, SHS_ICON_PAUSE };
                for (int i = 0; i < 4; i++) {
                    char lb[32];
                    snprintf(lb, sizeof lb, "%s (%d)", fl[i].name, fl[i].count);
                    float bw = (float)ShsMeasureText(lb, 15) + 28 + 24;
                    if (ButtonI((Rectangle){ fx, fy, bw, 30 }, lb, filterIcon[i], false, filter == (Filter)i)) { filter = (Filter)i; view.scrollTarget = 0; }
                    fx += bw + 8;
                }
                if (ButtonI((Rectangle){ px + pw - 160, fy, 160, 30 }, sortByStatus ? "Sorted by Status" : "Sort by Status", SHS_ICON_SORT, false, sortByStatus))
                    sortByStatus = !sortByStatus;
            } else {
                ShsDrawText(tasksTab == 2 ? "Scheduled syncs are not available yet." : "Archived tasks are not available yet.",
                            (int)px + 8, (int)fy + 16, 16, SHS_COLOR_TEXT_MUTED);
            }
            float hy = fy - 2;                       /* the list starts 42 below this */

            /* filtered (and optionally sorted) copy; shownIdx maps back to g_app */
            static ShsTask shown[MAX_TASKS];
            static int shownIdx[MAX_TASKS];
            int shownCount = 0;
            for (int pass = 0; pass < (sortByStatus ? 4 : 1); pass++)
                for (int i = 0; i < g_count; i++) {
                    const ShsTask *t = &views[i];
                    if (sortByStatus && StatusRank(t->state) != pass) continue;
                    if (!listTab || g_app[i].quick != (tasksTab == 1)) continue;
                    bool ok = filter == FILTER_ALL ||
                              (filter == FILTER_ACTIVE && t->state == SHS_TASK_SYNCING) ||
                              (filter == FILTER_ERRORS && t->state == SHS_TASK_ERROR) ||
                              (filter == FILTER_PAUSED && t->state == SHS_TASK_PAUSED);
                    if (ok && (ContainsNoCase(t->name, search) || ContainsNoCase(t->source, search) ||
                               ContainsNoCase(t->target, search))) {
                        shownIdx[shownCount] = i;
                        shown[shownCount++] = *t;
                    }
                }

            view.selected = -1;
            for (int k = 0; k < shownCount; k++) if (shownIdx[k] == g_sel) view.selected = k;
            view.blocked = g_block;
            view.logo = g_logo;
            view.logoGray = g_logoGray;

            Rectangle listB = { px, hy + 42, pw, (float)(H - STATUSBAR_H) - 12 - (hy + 42) };
            ShsTaskEvent ev = ShsTaskListDraw(&view, shown, shownCount, listB);
            if (ev.index >= 0 && !g_block) {
                int ti = shownIdx[ev.index];
                AppTask *a = &g_app[ti];
                switch (ev.action) {
                    case SHS_ACT_NONE:   g_sel = (g_sel == ti) ? -1 : ti; break;
                    case SHS_ACT_SYNC:   StartTask(ti); break;
                    case SHS_ACT_PAUSE:  if (a->job) ShsSyncSetPaused(a->job, true); break;
                    case SHS_ACT_RESUME: if (a->job) ShsSyncSetPaused(a->job, false); break;
                    case SHS_ACT_CANCEL: if (a->job) ShsSyncCancel(a->job); break;
                    case SHS_ACT_EDIT:   DialogOpen(&dlg, ti); break;
                    case SHS_ACT_SKIP:   if (a->job) ShsSyncSkipCurrent(a->job); break;
                    case SHS_ACT_TOGGLE_VERIFY:
                        a->quickVerify = !a->quickVerify;
                        g_quickVerify = a->quickVerify;               /* remembered for the next Quick Copy */
                        if (a->job) ShsSyncSetVerify(a->job, a->quickVerify);
                        SaveSettings();
                        break;
                    case SHS_ACT_TOGGLE_UNATTENDED:
                        a->quickUnattended = !a->quickUnattended;
                        g_quickUnattended = a->quickUnattended;
                        if (!a->quickUnattended) a->dismissAt = 0;
                        SaveSettings();
                        break;
                    case SHS_ACT_TOGGLE_AUTO:
                        a->def.autoSync = !a->def.autoSync;
                        if (!a->def.autoSync) StopWatch(ti);
                        SaveTasks();
                        LogAdd("%s: auto-sync %s", a->def.name, a->def.autoSync ? "on" : "off");
                        break;
                    case SHS_ACT_REMOVE: RemoveTask(ti); ShsTaskViewRemoved(&view, ti); break;
                    case SHS_ACT_ADD_FOLDER: {
                        char picked[SHS_DEF_PATH_MAX] = "";
                        if (ShsPickFolder("Add a folder to this copy", picked, sizeof picked) && picked[0]) {
                            char *one[1] = { picked };
                            FilePathList fl = { 1, 1, one };
                            QuickAddPaths(ti, &fl);
                        }
                    } break;
                    case SHS_ACT_OPEN_SOURCE:
                        if (!ShsOpenFolder(a->def.source)) LogAdd("%s: could not open %s", a->def.name, a->def.source);
                        break;
                    case SHS_ACT_OPEN_TARGET:
                        if (!ShsOpenFolder(a->def.target)) LogAdd("%s: could not open %s (not created yet?)", a->def.name, a->def.target);
                        break;
                }
            }

            if (shownCount == 0 && g_count == 0)
                ShsDrawText("Click the + button on the left to create your first sync task.", (int)px + 8, (int)listB.y + 40, 16, SHS_COLOR_TEXT_MUTED);
            else if (shownCount == 0 && listTab && tasksTab == 1 && g_count > 0 && tabAll == 0)
                ShsDrawText("No quick copies. Use the copy button on the left to start one.", (int)px + 8, (int)listB.y + 40, 16, SHS_COLOR_TEXT_MUTED);
        } else {
            ShsDrawText(NAV_LABEL[nav], (int)px, (int)py, 26, SHS_COLOR_TEXT_MAIN);
            if (nav == NAV_LOGS) {
                ShsDrawText("Full debug log: simplesync-debug.log, next to the app", (int)px + 160, (int)py + 11, 13, SHS_COLOR_TEXT_MUTED);
            }
            float y = py + 48;
            if (nav == NAV_TRAFFIC) {
                int rows = 0;
                for (int i = 0; i < g_count; i++) {
                    if (!g_app[i].job) continue;
                    char l[400];
                    snprintf(l, sizeof l, "%s   -   %.1f MB/s   -   %s", g_app[i].def.name, views[i].rateMBps,
                             views[i].currentFile[0] ? views[i].currentFile : "(waiting)");
                    ShsDrawText(l, (int)px, (int)y, 16, SHS_COLOR_TEXT_MAIN);
                    y += 28; rows++;
                }
                if (!rows) ShsDrawText("No sync is running right now.", (int)px, (int)y, 16, SHS_COLOR_TEXT_MUTED);
            } else if (nav == NAV_LOGS) {
                int shownLines = g_logCount < LOG_LINES ? g_logCount : LOG_LINES;
                int maxRows = (int)((H - STATUSBAR_H - y - 8) / 22);
                for (int k = 0; k < shownLines && k < maxRows; k++) {
                    const char *line = g_log[(g_logCount - 1 - k) % LOG_LINES];
                    ShsDrawText(line, (int)px, (int)y, 15, k == 0 ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED);
                    y += 22;
                }
            } else if (nav == NAV_SETTINGS) {
                ShsDrawText("APPEARANCE", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
                for (int m = 0; m < SHS_THEME_COUNT; m++) {                 /* two rows of theme buttons */
                    const float bwid = 120;
                    int col = m < 5 ? m : m - 5, row = m < 5 ? 0 : 1;
                    if (Button((Rectangle){ px + col * (bwid + 8), y + 22 + row * 40, bwid, 34 }, ShsThemeLabel((ShsThemeMode)m), false, (int)ShsThemeGetMode() == m)) {
                        bool wasFlat = ShsThemeFlat();
                        ShsThemeSetMode((ShsThemeMode)m);
                        SaveSettings();
                        if (wasFlat != ShsThemeFlat()) { g_restart = true; shouldClose = true; }   /* anti-aliasing and fonts are fixed at start-up */
                    }
                }
                char look[96];
                snprintf(look, sizeof look, "Using the %s theme", ShsThemeClassic() ? ShsThemeLabel(ShsThemeGetMode()) : (ShsThemeIsDark() ? "dark" : "light"));
                ShsDrawText(look, (int)px + 5 * 128 + 8, (int)y + 31, 14, SHS_COLOR_TEXT_MUTED);
                ShsDrawText("Themes with square corners (Flat 98, Win98, Mac System 7, Linux CDE) restart the app when you switch to or from them. They use less memory.", (int)px, (int)y + 102, 13, SHS_COLOR_TEXT_MUTED);
                y += 124;

                ShsDrawText("STARTUP", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
                float ty = y + 24;
                bool v = g_autostart;
                if (Toggle(px, ty, 640, "Start with " ABOUT_OS, "Launches SailHighSea SimpleSync when you sign in (uses this program's current location)", &v)) {
                    if (ShsAutostartSet(v, g_startHidden)) { g_autostart = v; LogAdd("Start with %s: %s", ABOUT_OS, v ? "on" : "off"); }
                    else LogAdd("Could not change the start-with-%s setting", ABOUT_OS);
                    SaveSettings();
                }
                ty += 54;
                if (ShsTraySupported()) {
                    v = g_startHidden;
                    if (Toggle(px, ty, 640, "Start hidden in the system tray", "Opens minimised to the tray icon when started with the system", &v)) {
                        g_startHidden = v;
                        if (g_autostart) ShsAutostartSet(true, g_startHidden);
                        SaveSettings();
                    }
                    ty += 54;
                    v = g_closeToTray;
                    if (Toggle(px, ty, 640, "Close button hides to the system tray", "Syncs keep running in the background; use the tray icon to reopen or quit", &v)) {
                        g_closeToTray = v;
                        ApplyTray();
                        SaveSettings();
                    }
                    ty += 54;
                }
                y = ty + 6;

                ShsDrawText("COPY PERFORMANCE", (int)px, (int)y, 12, SHS_COLOR_TEXT_MUTED);
                char tv[24];
                snprintf(tv, sizeof tv, "%d", g_copyThreads);
                bool perf = Stepper(px, y + 22, "Copy threads", &g_copyThreads, 1, 16, 1, tv, "Files copied at the same time. Use 1-2 on a traditional hard drive.");
                if (g_copyBufMiB == 0) snprintf(tv, sizeof tv, "Off"); else snprintf(tv, sizeof tv, "%d MB", g_copyBufMiB);
                perf |= Stepper(px, y + 58, "Read-ahead buffer", &g_copyBufMiB, 0, 1024, 16, tv, "Small files are read ahead in order, which cuts drive seeks.");
                if (perf) { ShsSyncConfigure(g_copyThreads, g_copyBufMiB); SaveSettings(); }
                y += 100;

                char tf[700];
                snprintf(tf, sizeof tf, "Task list file:  %s", g_tasksFile);
                ShsDrawText(tf, (int)px, (int)y, 14, SHS_COLOR_TEXT_MUTED);
                y += 30;
                if (ButtonI((Rectangle){ px, y, 270, 34 }, "Create / reset test sandbox", SHS_ICON_FOLDER, true, false)) CreateSandbox();
                ShsDrawText("Test folders next to the program, for trying the app on copies instead of real data.", (int)px + 286, (int)y + 9, 13, SHS_COLOR_TEXT_MUTED);
                ShsDrawText("Mirror tasks permanently delete files in the target that are missing from the source.",
                            (int)px, (int)y + 46, 13, SHS_COLOR_WARNING);            } else if (nav == NAV_ABOUT) {
                /* header: icon, name, version, tagline */
                const float icon = 96.0f, tx = px + icon + 24.0f;
                if (g_logo.id)
                    DrawTexturePro(g_logo, (Rectangle){ 0, 0, (float)g_logo.width, (float)g_logo.height },
                                   (Rectangle){ px, y, icon, icon }, (Vector2){ 0, 0 }, 0.0f, WHITE);
                ShsDrawText("SailHighSea SimpleSync", (int)tx, (int)y + 8, 26, SHS_COLOR_TEXT_MAIN);
                ShsDrawText("Version " SHS_VERSION, (int)tx, (int)y + 44, 15, SHS_COLOR_PRIMARY);
                ShsDrawText("A tiny, portable, open-source folder sync tool.", (int)tx, (int)y + 68, 15, SHS_COLOR_TEXT_MUTED);

                float cy = y + icon + 24.0f;
                ShsDrawText("Copies new and changed files from a source folder to a target, with live progress.", (int)px, (int)cy, 15, SHS_COLOR_TEXT_MUTED);
                ShsDrawText("Mirror mode also removes files from the target that are missing in the source.", (int)px, (int)cy + 24, 15, SHS_COLOR_TEXT_MUTED);

                /* details card */
                const float cw = fminf(pw, 760.0f), rowH = 32.0f;
                const char *rows[][2] = {
                    { "Version",    SHS_VERSION },
                    { "Build",      __DATE__ " " __TIME__ },
                    { "Platform",   ABOUT_PLATFORM },
                    { "Graphics",   "raylib " RAYLIB_VERSION },
                    { "Font",       NULL },
                    { "License",    "MIT License" },
                    { "Copyright",  "(c) 2026 SailHighSea contributors" },
                    { "Website",    "github.com/SailHighSea/sailhighsea-ss" },
                    { "Data folder", NULL },
                };
                const int nRows = (int)(sizeof rows / sizeof rows[0]);
                Rectangle card = { px, cy + 62, cw, rowH * nRows + 20 };
                ShsDrawRoundedBox(card, 14.0f, SHS_COLOR_CARD, SHS_COLOR_BORDER);
                for (int i = 0; i < nRows; i++) {
                    float ry = card.y + 10 + i * rowH;
                    ShsDrawText(rows[i][0], (int)card.x + 20, (int)ry + 7, 14, SHS_COLOR_TEXT_MUTED);
                    char val[SHS_DEF_PATH_MAX + 8];
                    const char *v = rows[i][1];
                    if (!v) v = (i == 4) ? ShsFontName() : g_appDir;
                    snprintf(val, sizeof val, "%s", v);
                    while (val[0] && ShsMeasureText(val, 15) > (int)(card.width - 190)) {   /* keep the tail of long paths */
                        memmove(val, val + 1, strlen(val));
                        if (strlen(val) > 3) { val[0] = '.'; val[1] = '.'; val[2] = '.'; }
                    }
                    ShsDrawText(val, (int)card.x + 150, (int)ry + 6, 15, SHS_COLOR_TEXT_MAIN);
                    if (i < nRows - 1) DrawRectangle((int)card.x + 16, (int)(ry + rowH), (int)card.width - 32, 1, Fade(SHS_COLOR_BORDER, 0.6f));
                }

                /* actions */
                float by = card.y + card.height + 18;
                if (ButtonI((Rectangle){ px, by, 150, 36 }, "GitHub page", SHS_ICON_INFO, true, false))
                    if (!ShsOpenUrl("https://github.com/SailHighSea/sailhighsea-ss")) LogAdd("Could not open the browser");
                if (ButtonI((Rectangle){ px + 160, by, 150, 36 }, "Data folder", SHS_ICON_FOLDER, false, false))
                    if (!ShsOpenFolder(g_appDir)) LogAdd("Could not open %s", g_appDir);
                if (ButtonI((Rectangle){ px + 320, by, 150, 36 }, "Copy info", SHS_ICON_LOGS, false, false)) {
                    char info[512];
                    snprintf(info, sizeof info, "SailHighSea SimpleSync v%s\nBuilt %s %s\n%s, raylib %s\nData folder: %s",
                             SHS_VERSION, __DATE__, __TIME__, ABOUT_PLATFORM, RAYLIB_VERSION, g_appDir);
                    SetClipboardText(info);
                    LogAdd("Copied version info to the clipboard");
                }
                ShsDrawText("Settings, tasks and the test folders are kept next to the program.", (int)px, (int)by + 52, 14, SHS_COLOR_TEXT_MUTED);
            }
        }

        if (railTip && !g_block) {                       /* icon rail tooltip, drawn over the page */
            int tw = ShsMeasureText(railTip, 15);
            Rectangle tr = { SIDEBAR_W + 4, railTipY - 14, (float)tw + 20, 28 };
            ShsDrawRoundedBox(tr, 7.0f, SHS_COLOR_CARD, SHS_COLOR_BORDER);
            ShsDrawText(railTip, (int)tr.x + 10, (int)tr.y + 6, 15, SHS_COLOR_TEXT_MAIN);
        }

        /* tray tooltip follows the sync state */
        if (ShsTrayActive()) {
            static char lastTip[96] = "";
            char tip[96];
            if (nActive) snprintf(tip, sizeof tip, "SailHighSea SimpleSync - %d syncing (%.1f MB/s)", nActive, totalRate);
            else if (nErr) snprintf(tip, sizeof tip, "SailHighSea SimpleSync - %d task(s) need attention", nErr);
            else snprintf(tip, sizeof tip, "SailHighSea SimpleSync - idle");
            if (strcmp(tip, lastTip)) { ShsTraySetTooltip(tip); snprintf(lastTip, sizeof lastTip, "%s", tip); }
        }

        /* ================= status bar ================= */
        FillRoundBottom((Rectangle){ 0, (float)(H - STATUSBAR_H), (float)W, STATUSBAR_H }, g_winR, ShsThemeClassic() ? SHS_COLOR_BG : SHS_COLOR_CARD);
        DrawRectangle(0, H - STATUSBAR_H, W, 1, SHS_COLOR_BORDER);
        if (ShsThemeBevel()) {                                      /* Win98: sunken status panels */
            const float spy = (float)(H - STATUSBAR_H + 3), sph = STATUSBAR_H - 5;
            const float verW = (float)ShsMeasureText("SailHighSea Engine v" SHS_VERSION, 14);
            ShsDrawBevelFrame((Rectangle){ 4, spy, 218, sph }, true);
            ShsDrawBevelFrame((Rectangle){ 224, spy, 196, sph }, true);
            ShsDrawBevelFrame((Rectangle){ 424, spy, 230, sph }, true);
            ShsDrawBevelFrame((Rectangle){ (float)W - verW - 30, spy, verW + 26, sph }, true);
        }
        int sy = H - STATUSBAR_H + 8;
        DrawCircle(20, H - STATUSBAR_H / 2, 4.5f, nActive ? SHS_COLOR_PRIMARY : SHS_COLOR_SUCCESS);
        ShsDrawText(nActive ? "Engine Status: SYNCING" : "Engine Status: READY", 32, sy, 14, SHS_COLOR_TEXT_MAIN);
        char sp[64];
        snprintf(sp, sizeof sp, "Active Speed: %.1f MB/s", totalRate);
        ShsDrawText(sp, 230, sy, 14, SHS_COLOR_TEXT_MAIN);
        char disk[64] = "Free Disk: --";
        int diskTask = g_sel >= 0 ? g_sel : (g_count ? 0 : -1);
        if (diskTask >= 0) {
            double gib = ShsFreeDiskGiB(g_app[diskTask].def.target);
            if (gib >= 0) snprintf(disk, sizeof disk, "Free Disk (target): %.1f GB", gib);
        }
        ShsDrawText(disk, 430, sy, 14, SHS_COLOR_TEXT_MUTED);
        const char *ver = "SailHighSea Engine v" SHS_VERSION;
        ShsDrawText(ver, W - ShsMeasureText(ver, 14) - 16, sy, 14, SHS_COLOR_TEXT_MUTED);

        if (dlg.open) DialogDraw(&dlg, W, H);
        if (conflictTask >= 0 && g_app[conflictTask].job) ConflictDraw(conflictTask, W, H);
        g_block = false;

        if (g_winR >= 0.5f) ShsRoundLines((Rectangle){ 0, 0, (float)W, (float)H }, RoundNess((Rectangle){ 0, 0, (float)W, (float)H }, g_winR), 16, 1.0f, SHS_COLOR_BORDER);
        else if (ShsThemeBevel()) ShsDrawBevelFrame((Rectangle){ 0, 0, (float)W, (float)H }, false);
        else                DrawRectangleLines(0, 0, W, H, SHS_COLOR_BORDER);   /* no OS frame */
        EndDrawing();
    }

    for (int i = 0; i < g_count; i++) { StopWatch(i); if (g_app[i].job) { ShsSyncFree(g_app[i].job); g_app[i].job = NULL; } }
    ShsTrayDestroy();
    ShsFontsShutdown();
    if (g_logo.id) UnloadTexture(g_logo);
    if (g_logoGray.id) UnloadTexture(g_logoGray);
    CloseWindow();
    if (g_restart) ShsRelaunch(argc, argv);
    return 0;
}
