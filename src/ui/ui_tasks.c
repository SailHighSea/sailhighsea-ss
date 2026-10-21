#include "ui_tasks.h"
#include "shs_theme.h"
#include "shs_icons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CARD_PAD     16
#define CARD_GAP     12
#define CARD_RADIUS_PX 16.0f
#define CARD_H_SMALL  92.0f    /* idle / paused: header, then paths + progress on one row */
#define CARD_H_SYNC  118.0f    /* syncing: plus the speed / remaining / file line */
#define CARD_H_ERROR 140.0f    /* error: plus the error box */
#define CARD_H_QUICK 142.0f    /* Quick Copy panel: two progress rows and a button row */
#define SCROLLBAR_W  6
#define WHEEL_STEP   60.0f
#define AVATAR       32                          /* app-logo play / pause button */
#define TEXT_X       (CARD_PAD + AVATAR + 12)   /* left edge of the text column inside a card */
#define BTN          24                          /* icon button size */
#define TOP_Y        12.0f                       /* top of the content area: logo, name and pill start here */
#define BTN_Y        56.0f                       /* button group top; its centre lines up with the speed / progress row */
#define BTN_GAP      4
#define REMOVE_ARM_S 3.0

/* ---- helpers ---------------------------------------------------------- */

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static Color LerpColor(Color a, Color b, float t)
{
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        (unsigned char)(a.a + (b.a - a.a) * t)
    };
}

static Vector2 MousePos(const ShsTaskView *v)
{
    return v->blocked ? (Vector2){ -10000.0f, -10000.0f } : GetMousePosition();
}

static bool Pressed(const ShsTaskView *v) { return !v->blocked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

/* Copy `text` into `out`, ellipsising the start (paths keep their tail) or end
   so it fits `maxW` pixels at `size`. */
static const char *FitText(char *out, size_t cap, const char *text, int size, float maxW, bool keepTail)
{
    if (ShsMeasureText(text, size) <= maxW) { snprintf(out, cap, "%s", text); return out; }
    size_t len = strlen(text);
    for (size_t cut = 1; cut < len; cut++) {
        if (keepTail) snprintf(out, cap, "...%s", text + cut);
        else          snprintf(out, cap, "%.*s...", (int)(len - cut), text);
        if (ShsMeasureText(out, size) <= maxW) return out;
    }
    snprintf(out, cap, "...");
    return out;
}

/* ---- file paths in lists ---------------------------------------------------------------------------------------------
   The file name is what matters, so it stays whole and the folders around it shrink instead:
     D:\Work\241 Suburban Rail Loop\Tender preparation\10 Subcontractor Packs\10.2 Quality\Report_rev1.pdf
     D:\..\10.2 Quality\Report_rev1.pdf   ->   D:\..\10.2 Qual..\Report_rev1.pdf   ->   ..\10.2 Q..\Report_rev1.pdf   ->   ..\Report_rev1.pdf */

#ifdef _WIN32
  #define PATH_SEP "\\"
#else
  #define PATH_SEP "/"
#endif

/* Appends `seg` cut to `keep` characters (UTF-8 safe) plus ".."; keep 0 = the whole segment. */
static void AppendCut(char *out, size_t cap, const char *seg, size_t segLen, int keep)
{
    size_t n = strlen(out), take = segLen;
    bool cut = false;
    if (keep > 0) {
        size_t chars = 0, i = 0;
        while (i < segLen && chars < (size_t)keep) { i++; while (i < segLen && ((unsigned char)seg[i] & 0xC0) == 0x80) i++; chars++; }
        if (i < segLen) { take = i; cut = true; }
    }
    if (n + take + 3 >= cap) return;
    memcpy(out + n, seg, take);
    out[n + take] = 0;
    if (cut) strcat(out, "..");
}

/* Middle-ellipsis for a name that is still too wide: "very_long_na..ame_rev1.pdf" (keeps the start and the extension). */
static void MidEllipsis(char *out, size_t cap, const char *name, int size, float maxW)
{
    size_t len = strlen(name);
    for (size_t head = len; head-- > 1; ) {
        char buf[SHS_TASK_PATH_MAX + 8];
        size_t tail = 0;
        while (head + 2 + tail < len && tail < 12) tail++;
        size_t t0 = len - tail;
        while (t0 < len && ((unsigned char)name[t0] & 0xC0) == 0x80) t0++;
        size_t h = head;
        while (h > 0 && ((unsigned char)name[h] & 0xC0) == 0x80) h--;
        snprintf(buf, sizeof buf, "%.*s..%s", (int)h, name, name + t0);
        if (ShsMeasureText(buf, size) <= maxW) { snprintf(out, cap, "%s", buf); return; }
    }
    snprintf(out, cap, "..");
}

static const char *ShortenPath(char *out, size_t cap, const char *path, int size, float maxW)
{
    if (ShsMeasureText(path, size) <= maxW) { snprintf(out, cap, "%s", path); return out; }

    const char *segS[64]; size_t segL[64]; int n = 0;                       /* split on / and \ */
    const char *p = path;
    while (*p && n < 64) {
        const char *e = p;
        while (*e && *e != '/' && *e != '\\') e++;
        if (e > p) { segS[n] = p; segL[n] = (size_t)(e - p); n++; }
        p = *e ? e + 1 : e;
    }
    if (n < 2) { MidEllipsis(out, cap, path, size, maxW); return out; }

    const char *file = segS[n - 1]; const size_t fileL = segL[n - 1];
    const char *par  = segS[n - 2]; const size_t parL  = segL[n - 2];
    static const int KEEP[] = { 0, 28, 20, 14, 9, 5 };                       /* how many characters of each folder name survive */
    for (size_t k = 0; k < sizeof KEEP / sizeof KEEP[0]; k++) {
        for (int withFirst = 1; withFirst >= 0; withFirst--) {
            if (withFirst && n < 3) continue;
            out[0] = 0;
            if (withFirst) {
                AppendCut(out, cap, segS[0], segL[0], KEEP[k]);
                strcat(out, PATH_SEP);
                if (n > 3) { strcat(out, ".."); strcat(out, PATH_SEP); }       /* folders in between are left out */
                if (n > 2) { AppendCut(out, cap, par, parL, KEEP[k]); strcat(out, PATH_SEP); }
            } else {
                strcat(out, ".."); strcat(out, PATH_SEP);
                if (n > 2) { AppendCut(out, cap, par, parL, KEEP[k]); strcat(out, PATH_SEP); }
            }
            AppendCut(out, cap, file, fileL, 0);
            if (ShsMeasureText(out, size) <= maxW) return out;
        }
    }
    char name[SHS_TASK_PATH_MAX];                                            /* last resort: ..\<name shortened in the middle> */
    snprintf(name, sizeof name, "%.*s", (int)fileL, file);
    const float lead = (float)ShsMeasureText(".." PATH_SEP, size);
    MidEllipsis(out, cap, name, size, maxW - lead);
    char tmp[SHS_TASK_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, ".." PATH_SEP "%s", out);
    snprintf(out, cap, "%s", tmp);
    return out;
}

static void DrawPathFit(const char *path, float x, float y, int size, float maxW, Color c)
{
    char buf[SHS_TASK_PATH_MAX + 16];
    ShsDrawText(ShortenPath(buf, sizeof buf, path, size, maxW), (int)x, (int)y, size, c);
}

static void DrawTextFit(const char *text, float x, float y, int size, float maxW, Color c, bool keepTail)
{
    char buf[SHS_TASK_PATH_MAX + 8];
    ShsDrawText(FitText(buf, sizeof buf, text, size, maxW, keepTail), (int)x, (int)y, size, c);
}

static Color StateColor(ShsTaskState s)
{
    switch (s) {
        case SHS_TASK_SYNCING: return SHS_COLOR_PRIMARY;
        case SHS_TASK_ERROR:   return SHS_COLOR_DANGER;
        case SHS_TASK_PAUSED:  return SHS_COLOR_WARNING;
        default:               return SHS_COLOR_SUCCESS;
    }
}

/* ---- badge ------------------------------------------------------------ */

static float DrawBadge(ShsTaskState state, float rightX, float y)      /* returns the badge's left edge */
{
    const char *label;
    switch (state) {
        case SHS_TASK_SYNCING: label = "Syncing";    break;
        case SHS_TASK_ERROR:   label = "Error";      break;
        case SHS_TASK_PAUSED:  label = "Paused";     break;
        default:               label = "Up-to-date"; break;
    }
    Color col = StateColor(state);
    const int fs = 12, iconW = 13, padX = 8;
    float w = (float)(padX * 2 + iconW + ShsMeasureText(label, fs));
    Rectangle r = { rightX - w, y, w, 18 };                /* slim pill */
    ShsRoundRec(r, 1.0f, 12, Fade(col, 0.16f));
    ShsRoundLines(r, 1.0f, 12, 1.0f, Fade(col, 0.55f));

    float cx = r.x + padX + 3.5f, cy = r.y + r.height / 2;
    if (state == SHS_TASK_SYNCING) {
        float a = (float)GetTime() * 360.0f;            /* one turn per second */
        DrawRing((Vector2){ cx, cy }, 2.6f, 4.6f, a, a + 270.0f, 24, col);
    } else {
        DrawCircleV((Vector2){ cx, cy }, 3.2f, col);
    }
    ShsDrawText(label, (int)(r.x + padX + iconW), (int)(r.y + 2), fs, col);
    return r.x;
}
/* ---- path row: [folder] source ---> [folder] target -------------------- */

/* "D:\Work\Projects\Working Files" -> "D:\...\Working Files": the drive (or share / root) and the last folder name.
   Paths with no middle part are returned unchanged. */
static void ShortPath(char *out, size_t cap, const char *path)
{
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/')) len--;          /* ignore trailing separators */

    size_t rootEnd = 0;                                                                   /* length of the root part */
    if (len >= 2 && path[1] == ':') rootEnd = (len >= 3 && (path[2] == '\\' || path[2] == '/')) ? 3 : 2;      /* D:\ */
    else if (len >= 2 && path[0] == '\\' && path[1] == '\\') {                            /* \\server\share\ */
        int seps = 0;
        for (size_t i = 2; i < len && seps < 2; i++) if (path[i] == '\\') { seps++; rootEnd = i + 1; }
        if (seps < 2) rootEnd = len;
    } else if (path[0] == '/' || path[0] == '\\') rootEnd = 1;                            /* / */

    size_t lastSep = len;                                                                 /* start of the last component */
    for (size_t i = len; i > rootEnd; i--) if (path[i - 1] == '\\' || path[i - 1] == '/') { lastSep = i; break; }
    if (lastSep >= len || lastSep <= rootEnd) { snprintf(out, cap, "%.*s", (int)len, path); return; }   /* nothing to hide */

    char sep = path[lastSep - 1];
    snprintf(out, cap, "%.*s...%c%.*s", (int)rootEnd, path, sep, (int)(len - lastSep), path + lastSep);
}

/* Draws the row; reports a hovered path (full text + its screen rect) through tipText / tipRect for a tooltip. */
static void DrawPathRow(const ShsTask *t, float x, float y, float w, Vector2 mouse, bool inside,
                        const char **tipText, Rectangle *tipRect)
{
    const int fs = 15;
    const float arrowW = 34, iconW = 22;
    char shortSrc[SHS_TASK_PATH_MAX], shortDst[SHS_TASK_PATH_MAX];
    ShortPath(shortSrc, sizeof shortSrc, t->source);
    ShortPath(shortDst, sizeof shortDst, t->target);

    float srcW = (float)ShsMeasureText(shortSrc, fs), dstW = (float)ShsMeasureText(shortDst, fs);
    float avail = w - arrowW - 2 * iconW - 28;
    if (avail < 80) avail = 80;
    float textW = avail / 2;
    /* give the shorter path's unused space to the longer one */
    float sW = srcW < textW ? srcW : (dstW < textW ? fminf(srcW, avail - dstW) : textW);
    float dW = dstW < textW ? dstW : (srcW < textW ? fminf(dstW, avail - srcW) : textW);

    ShsDrawIcon(SHS_ICON_FOLDER, x + 8, y + 9, 16.0f, SHS_COLOR_TEXT_MUTED);
    DrawTextFit(shortSrc, x + iconW, y, fs, sW, SHS_COLOR_TEXT_MUTED, true);
    Rectangle srcRect = { x, y - 2, iconW + sW, 22 };

    float ax = x + iconW + sW + 14, ay = y + 10;
    DrawLineEx((Vector2){ ax, ay }, (Vector2){ ax + arrowW - 3, ay }, 2.0f, SHS_COLOR_PRIMARY);
    DrawTriangle((Vector2){ ax + arrowW, ay }, (Vector2){ ax + arrowW - 9, ay - 5 }, (Vector2){ ax + arrowW - 9, ay + 5 }, SHS_COLOR_PRIMARY);

    float tx = ax + arrowW + 14;
    ShsDrawIcon(SHS_ICON_FOLDER, tx + 8, y + 9, 16.0f, SHS_COLOR_PRIMARY);
    DrawTextFit(shortDst, tx + iconW, y, fs, dW, SHS_COLOR_TEXT_MUTED, true);
    Rectangle dstRect = { tx, y - 2, iconW + dW, 22 };

    if (inside && CheckCollisionPointRec(mouse, srcRect))      { *tipText = t->source; *tipRect = srcRect; }
    else if (inside && CheckCollisionPointRec(mouse, dstRect)) { *tipText = t->target; *tipRect = dstRect; }
}
/* ---- progress bar ----------------------------------------------------- */

#define PCT_W 46.0f             /* room for "100%" before the bar */

/* Row under the title: "Speed: 45.0 MB/s  |  Remaining: 3m 20s  |  68% [bar]". Shown on every card; the bar fills
   while syncing, is full (green) once synced, and empty if never synced. */
static void DrawStatsRow(const ShsTask *t, float x, float rowY, float maxRight)
{
    float p = Clampf(t->progress, 0.0f, 1.0f);

    char eta[24] = "--";
    if (t->state == SHS_TASK_SYNCING && t->etaSeconds >= 0) {
        int s = (int)t->etaSeconds;
        if (s >= 60) snprintf(eta, sizeof eta, "%dm %02ds", s / 60, s % 60);
        else         snprintf(eta, sizeof eta, "%ds", s);
    }
    char text[96];
    snprintf(text, sizeof text, "Speed: %.1f MB/s  |  Remaining: %s  |", t->state == SHS_TASK_SYNCING ? t->rateMBps : 0.0f, eta);
    int tw = ShsMeasureText(text, 15);
    if (maxRight - x - tw - PCT_W - 90.0f < 0) {                      /* narrow card: drop the remaining-time part */
        snprintf(text, sizeof text, "Speed: %.1f MB/s  |", t->state == SHS_TASK_SYNCING ? t->rateMBps : 0.0f);
        tw = ShsMeasureText(text, 15);
    }
    ShsDrawText(text, (int)x, (int)rowY + 1, 15, SHS_COLOR_TEXT_MUTED);

    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(p * 100.0f + 0.5f));
    float px = x + tw + 12;
    ShsDrawText(pct, (int)px, (int)rowY + 1, 15, SHS_COLOR_TEXT_MAIN);

    float barX = px + PCT_W;
    float barW = fminf(260.0f, maxRight - barX);
    if (barW < 40.0f) return;
    if (ShsDrawProgress((Rectangle){ barX, rowY + 3, barW, 14 }, p, StateColor(t->state))) return;     /* retro themes */
    Rectangle track = { barX, rowY + 6, barW, 8 };
    ShsRoundRec(track, 1.0f, 8, SHS_COLOR_BG);
    if (p > 0.0f)
        ShsRoundRec((Rectangle){ barX, track.y, fmaxf(track.height, barW * p), track.height }, 1.0f, 8, StateColor(t->state));
}

/* Line under the stats row while syncing: the file being copied. */
static void DrawFileLine(const ShsTask *t, float x, float y, float w)
{
    ShsDrawText("File:", (int)x, (int)y, 13, SHS_COLOR_TEXT_MUTED);
    int fw = ShsMeasureText("File: ", 13);
    DrawPathFit(t->currentFile, x + fw, y, 13, w - fw, SHS_COLOR_TEXT_MUTED);
}
/* ---- icon buttons ----------------------------------------------------- */

typedef struct { Rectangle r; ShsIcon icon; ShsTaskAction act; const char *tip; bool primary, armed; } CardButton;

/* Draws one button; returns true when clicked. Records the hovered button for a tooltip. */
static bool IconButton(const ShsTaskView *v, const CardButton *b, const CardButton **hoverOut, bool inside)
{
    bool hot = inside && CheckCollisionPointRec(MousePos(v), b->r);
    Color bg, ink;
    if (b->armed)        { bg = SHS_COLOR_DANGER;  ink = SHS_COLOR_ON_PRIMARY; }
    else if (b->primary) { bg = hot ? SHS_COLOR_PRIMARY_HOVER : SHS_COLOR_PRIMARY; ink = SHS_COLOR_ON_PRIMARY; }
    else                 { bg = hot ? SHS_COLOR_CARD_HOVER : SHS_COLOR_BG; ink = hot ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED; }
    ShsDrawButtonBox(b->r, 7.0f, bg, (b->primary || b->armed) ? bg : SHS_COLOR_BORDER, hot && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    ShsDrawIcon(b->icon, b->r.x + b->r.width / 2, b->r.y + b->r.height / 2, 14.0f, ink);
    if (hot) *hoverOut = b;
    return hot && Pressed(v);
}

static void DrawTooltip(const char *tip, Rectangle target, bool below, float limitRight)
{
    int tw = ShsMeasureText(tip, 13);
    Rectangle t = { target.x + target.width / 2 - (tw + 20) / 2.0f, below ? target.y + target.height + 6 : target.y - 30, (float)(tw + 20), 24 };
    if (t.x + t.width > limitRight) t.x = limitRight - t.width;
    if (t.x < target.x - 4 && below) t.x = target.x - 4;
    ShsRoundRec(t, 0.4f, 8, SHS_COLOR_TEXT_MAIN);
    ShsDrawText(tip, (int)t.x + 10, (int)t.y + 4, 13, SHS_COLOR_BG);
}

/* ---- expanded file list ------------------------------------------------- */

#define FILE_ROW_H   26.0f
#define PANEL_HEAD   30.0f

static void FormatSize(char *out, size_t cap, unsigned long long b)
{
    if (b >= 1073741824ULL)  snprintf(out, cap, "%.2f GB", (double)b / 1073741824.0);
    else if (b >= 1048576ULL) snprintf(out, cap, "%.1f MB", (double)b / 1048576.0);
    else if (b >= 1024ULL)    snprintf(out, cap, "%.0f KB", (double)b / 1024.0);
    else                      snprintf(out, cap, "%llu B", b);
}

/* Extra height an expanded card needs below its normal content. */
static float s_listH = 600.0f;                                  /* height of the task list viewport (set each frame) */

/* The Quick Copy panel fills the viewport below the card, between a minimum and the full layout. */
static float QuickPanelHeight(void) { return fminf(6.0f + 38.0f + 296.0f + 6.0f, fmaxf(250.0f, s_listH - 142.0f - 4.0f)); }

#define SUM_ROWS_MAX 8   /* rows visible at once in the list behind a summary tab */

static float PanelHeight(const ShsTask *t)
{
    if (t->quick) return QuickPanelHeight();
    int rows = t->selRows > 0 ? (t->selRows < SUM_ROWS_MAX ? t->selRows : SUM_ROWS_MAX) : (t->fileCount > 0 ? t->fileCount : 1);
    return 12.0f + PANEL_HEAD + rows * FILE_ROW_H + 8.0f;
}

static float CardHeightNow(const ShsTaskView *v, const ShsTask *t)
{
    float h = ShsTaskCardHeight(t);
    int id = (t->id >= 0 && t->id < SHS_TASK_VIEW_MAX) ? t->id : 0;
    return h + PanelHeight(t) * v->expandT[id];
}


/* One row of the file list: state icon, name, size and (while copying) a progress bar. */
static void DrawFileRow(const char *name, unsigned long long size, unsigned long long done, int state, float x, float ry, float w, float rightPad)
{
    float cy = ry + FILE_ROW_H / 2;
    Color col; ShsIcon ic;
    switch (state) {
        case 1:  col = SHS_COLOR_SUCCESS;    ic = SHS_ICON_CHECK;  break;
        case 2:  col = SHS_COLOR_DANGER;     ic = SHS_ICON_WARN;   break;
        case 3:  col = SHS_COLOR_TEXT_MUTED; ic = SHS_ICON_TRASH;  break;
        case 4:  col = SHS_COLOR_TEXT_MUTED; ic = SHS_ICON_CLOSE;  break;       /* skipped */
        case 8:  col = SHS_COLOR_TEXT_MUTED; ic = SHS_ICON_CLOSE;  break;       /* temp / lock file left out */
        default: col = SHS_COLOR_PRIMARY;    ic = SHS_ICON_SYNC;   break;
    }
    ShsDrawIcon(ic, x + 10, cy, 14.0f, col);

    w -= rightPad;
    char sz[24];
    FormatSize(sz, sizeof sz, size);
    int sizeW = ShsMeasureText(sz, 13);
    ShsDrawText(sz, (int)(x + w - sizeW), (int)cy - 7, 13, SHS_COLOR_TEXT_MUTED);

    const bool busy = state == 0 || state == 5;
    float nameW = w - 30 - sizeW - 16 - (busy ? 190.0f : 0.0f);
    DrawPathFit(name, x + 30, cy - 8, 14, nameW, (state == 3 || state == 8) ? SHS_COLOR_TEXT_MUTED : SHS_COLOR_TEXT_MAIN);

    if (busy) {                                                   /* per-file progress */
        float p = size ? (float)((double)done / (double)size) : 0.0f;
        float bx = x + w - sizeW - 16 - 170.0f;
        Rectangle track = { bx, cy - 4, 120, 8 };
        ShsRoundRec(track, 1.0f, 8, SHS_COLOR_BG);
        if (p > 0.0f) ShsRoundRec((Rectangle){ bx, cy - 4, fmaxf(8.0f, 120.0f * Clampf(p, 0, 1)), 8 }, 1.0f, 8, SHS_COLOR_PRIMARY);
        char pct[12];
        snprintf(pct, sizeof pct, "%d%%", (int)(Clampf(p, 0, 1) * 100.0f + 0.5f));
        ShsDrawText(pct, (int)(bx + 128), (int)cy - 7, 13, SHS_COLOR_TEXT_MAIN);
    }
}

static void DrawFilePanel(ShsTaskView *v, const ShsTask *t, Rectangle r, float baseH, float x, float w, bool inside, bool *pressedAny)
{
    const int id = (t->id >= 0 && t->id < SHS_TASK_VIEW_MAX) ? t->id : 0;
    float py = r.y + baseH;
    DrawRectangle((int)x, (int)py, (int)w, 1, Fade(SHS_COLOR_BORDER, 0.8f));

    char head[SHS_TASK_PATH_MAX], seg[4][64];
    snprintf(head, sizeof head, "%s", t->lastRun ? "LAST RUN" : "FILES");
    ShsDrawText(head, (int)x, (int)py + 12, 12, SHS_COLOR_TEXT_MUTED);

    /* summary: each part is a tab that lists those files (copied -> filter 2, removed -> 4, errors -> 3) */
    snprintf(seg[0], sizeof seg[0], "%llu of %llu copied", t->filesDone, t->filesTotal);
    snprintf(seg[1], sizeof seg[1], "%llu removed", t->filesRemoved);
    snprintf(seg[2], sizeof seg[2], "%llu error%s", t->errors, t->errors == 1 ? "" : "s");
    snprintf(seg[3], sizeof seg[3], "%llu temp file%s skipped", t->filesIgnored, t->filesIgnored == 1 ? "" : "s");
    const bool tabs = t->rows != NULL;
    float sx = x + w;
    for (int k = 3; k >= 0; k--) {
        int tw = ShsMeasureText(seg[k], 13);
        sx -= (float)tw;
        Rectangle tr = { sx - 6, py + 5, (float)tw + 12, 24 };
        bool hot = tabs && inside && CheckCollisionPointRec(MousePos(v), tr);
        bool sel = tabs && v->sumSel[id] == k + 1;
        Color c = sel ? SHS_COLOR_PRIMARY : (k == 2 && t->errors ? SHS_COLOR_DANGER : (hot ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED));
        ShsDrawText(seg[k], (int)sx, (int)py + 11, 13, c);
        if (sel || hot) DrawRectangle((int)sx, (int)py + 27, tw, 2, sel ? SHS_COLOR_PRIMARY : Fade(c, 0.6f));
        if (hot && Pressed(v)) { v->sumSel[id] = sel ? 0 : k + 1; v->sumTop[id] = 0.0f; *pressedAny = true; }
        if (k > 0) { sx -= 12; ShsDrawText("|", (int)sx, (int)py + 11, 13, Fade(SHS_COLOR_TEXT_MUTED, 0.6f)); sx -= 12; }
    }

    float ry = py + PANEL_HEAD;
    const int sel = tabs ? v->sumSel[id] : 0;
    if (sel > 0 && t->selRows > 0) {                              /* one of the lists: scrolls with the wheel */
        static const int FILTER[4] = { 2, 4, 3, 5 };
        const int rowsShown = t->selRows < SUM_ROWS_MAX ? t->selRows : SUM_ROWS_MAX;
        Rectangle list = { x, ry, w, rowsShown * FILE_ROW_H };
        ShsTaskRow rows[SUM_ROWS_MAX];
        int total = 0;
        t->rows(t->rowsCtx, FILTER[sel - 1], 0, 0, rows, &total);
        if (inside && CheckCollisionPointRec(MousePos(v), list) && Pressed(v)) *pressedAny = true;     /* clicking a row must not collapse the card */
        const bool scrollable = total > rowsShown;
        float top = v->sumTop[id];
        if (scrollable && inside && CheckCollisionPointRec(MousePos(v), list)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) top -= wheel * 3.0f;
            v->innerRect = list;
        }
        top = Clampf(top, 0.0f, (float)(scrollable ? total - rowsShown : 0));
        v->sumTop[id] = top;
        int n = total > 0 ? t->rows(t->rowsCtx, FILTER[sel - 1], (int)top, rowsShown, rows, &total) : 0;
        if (total == 0 && ry + FILE_ROW_H <= r.y + r.height)
            ShsDrawText("Nothing in this list.", (int)x, (int)ry + 5, 14, SHS_COLOR_TEXT_MUTED);
        for (int k = 0; k < n; k++) {
            float rowY = ry + k * FILE_ROW_H;
            if (rowY + FILE_ROW_H > r.y + r.height - 2) break;
            DrawFileRow(rows[k].name, rows[k].size, 0, rows[k].state, x, rowY, w, scrollable ? 12.0f : 0.0f);
        }
        if (scrollable) {
            float th = fmaxf(24.0f, list.height * rowsShown / total);
            float tyb = list.y + (list.height - th) * (top / (float)(total - rowsShown));
            ShsRoundRec((Rectangle){ x + w - 6, list.y, 5, list.height }, 1.0f, 6, Fade(SHS_COLOR_BG, 0.8f));
            ShsRoundRec((Rectangle){ x + w - 6, tyb, 5, th }, 1.0f, 6, SHS_COLOR_BORDER);
        }
        return;
    }

    if (t->fileCount == 0) {
        if (ry + FILE_ROW_H <= r.y + r.height)
            ShsDrawText("No file activity yet. Start a sync to see files here.", (int)x, (int)ry + 5, 14, SHS_COLOR_TEXT_MUTED);
        return;
    }
    for (int i = 0; i < t->fileCount; i++, ry += FILE_ROW_H) {
        if (ry + FILE_ROW_H > r.y + r.height - 2) break;          /* still animating open */
        const ShsTaskFile *f = &t->files[i];
        DrawFileRow(f->name, f->size, f->done, f->state, x, ry, w, 0.0f);
    }
}

/* ---- one card --------------------------------------------------------- */

float ShsTaskCardHeight(const ShsTask *task)
{
    if (task->quick) return CARD_H_QUICK;
    switch (task->state) {
        case SHS_TASK_SYNCING: return CARD_H_SYNC;
        case SHS_TASK_ERROR:   return CARD_H_ERROR;
        default:               return CARD_H_SMALL;
    }
}

/* ---- Quick Copy panel ----------------------------------------------------------------------------------------------
   Two progress rows in the style of a copy manager:
     [Copying]  <current source file>                n / N     176 MB    1.3 GB    12.9 %
     [Target ]  <its target path>                 1.1 GB/s - 3 s   3.2 GB    6.7 GB   68.2 %
     [Pause] [Skip] [Stop]                                          [ ] Unattended  [x] Verify                          */

static bool TextButton(const ShsTaskView *v, Rectangle r, const char *label, bool enabled, bool inside, bool primary)
{
    bool hot = enabled && inside && CheckCollisionPointRec(MousePos(v), r);
    Color bg = !enabled ? Fade(SHS_COLOR_BG, 0.5f) : (primary ? (hot ? SHS_COLOR_PRIMARY_HOVER : SHS_COLOR_PRIMARY) : (hot ? SHS_COLOR_CARD_HOVER : SHS_COLOR_BG));
    ShsDrawButtonBox(r, 8.0f, bg, primary && enabled ? bg : SHS_COLOR_BORDER, hot && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    Color ink = !enabled ? Fade(SHS_COLOR_TEXT_MUTED, 0.5f) : (primary ? SHS_COLOR_ON_PRIMARY : SHS_COLOR_TEXT_MAIN);
    int tw = ShsMeasureText(label, 15);
    ShsDrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 15) / 2 - 1), 15, ink);
    return hot && Pressed(v);
}

static bool CheckBox(const ShsTaskView *v, float x, float y, const char *label, bool on, bool inside, float *widthOut)
{
    int lw = ShsMeasureText(label, 15);
    Rectangle box = { x, y + 5, 20, 20 };
    Rectangle hit = { x - 4, y, 20 + 10 + (float)lw + 8, 30 };
    bool hot = inside && CheckCollisionPointRec(MousePos(v), hit);
    ShsDrawRoundedBox(box, 5.0f, on ? SHS_COLOR_PRIMARY : SHS_COLOR_BG, on ? SHS_COLOR_PRIMARY : (hot ? SHS_COLOR_TEXT_MUTED : SHS_COLOR_BORDER));
    if (on) {
        DrawLineEx((Vector2){ box.x + 5, box.y + 10.5f }, (Vector2){ box.x + 8.5f, box.y + 14 }, 2.0f, SHS_COLOR_ON_PRIMARY);
        DrawLineEx((Vector2){ box.x + 8.5f, box.y + 14 }, (Vector2){ box.x + 15, box.y + 6 }, 2.0f, SHS_COLOR_ON_PRIMARY);
    }
    ShsDrawText(label, (int)x + 30, (int)y + 6, 15, SHS_COLOR_TEXT_MAIN);
    if (widthOut) *widthOut = hit.width;
    return hot && Pressed(v);
}

static void Chip(float x, float y, float w, const char *label, Color tint, bool tinted)
{
    Rectangle r = { x, y, w, 28 };
    ShsDrawRoundedBox(r, 8.0f, tinted ? Fade(tint, 0.18f) : SHS_COLOR_BG, tinted ? Fade(tint, 0.5f) : SHS_COLOR_BORDER);
    int tw = ShsMeasureText(label, 14);
    ShsDrawText(label, (int)(x + (w - tw) / 2), (int)y + 6, 14, tinted ? tint : SHS_COLOR_TEXT_MUTED);
}

static void RightText(const char *text, float rightX, float y, int size, Color c)
{
    ShsDrawText(text, (int)(rightX - ShsMeasureText(text, size)), (int)y, size, c);
}

static void ThinBar(float x, float y, float w, float p, Color fill)
{
    ShsRoundRec((Rectangle){ x, y, w, 4 }, 1.0f, 6, SHS_COLOR_BORDER);
    p = Clampf(p, 0.0f, 1.0f);
    if (p > 0.0f) ShsRoundRec((Rectangle){ x, y, fmaxf(4.0f, w * p), 4 }, 1.0f, 6, fill);
}

/* ---- Quick Copy expanded panel: File list | Target | Options | Status | Log ---------------------------------------- */

#define QP_TAB_H   38.0f
#define QP_BODY_H  296.0f
#define QP_ROW_H   26.0f
#define QP_TOOL_H  38.0f
#define QP_TABS    5

static void Tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    DrawTriangle(a, b, c, col);          /* raylib culls by winding: draw both orders */
    DrawTriangle(a, c, b, col);
}

/* Speed history as a filled area, newest sample at the right edge. */
static void DrawSpeedGraph(Rectangle r, const float *g, int n, Color c)
{
    if (n < 2) return;
    float peak = 1.0f;
    for (int i = 0; i < n; i++) if (g[i] > peak) peak = g[i];
    float step = r.width / (SHS_TASK_GRAPH - 1), base = r.y + r.height;
    for (int i = 0; i + 1 < n; i++) {
        float x0 = r.x + r.width - (float)(n - 1 - i) * step, x1 = x0 + step;
        float y0 = base - r.height * g[i] / peak * 0.95f, y1 = base - r.height * g[i + 1] / peak * 0.95f;
        Tri((Vector2){ x0, y0 }, (Vector2){ x0, base }, (Vector2){ x1, base }, c);
        Tri((Vector2){ x0, y0 }, (Vector2){ x1, base }, (Vector2){ x1, y1 }, c);
    }
}

static void FormatDuration(char *out, size_t cap, float secs)
{
    int s = (int)secs;
    if (s >= 3600) snprintf(out, cap, "%dh %02dm", s / 3600, (s / 60) % 60);
    else if (s >= 60) snprintf(out, cap, "%dm %02ds", s / 60, s % 60);
    else snprintf(out, cap, "%d s", s);
}

static void FormatRate(char *out, size_t cap, float mbps)
{
    if (mbps >= 1024.0f) snprintf(out, cap, "%.2f GB/s", mbps / 1024.0f);
    else                 snprintf(out, cap, "%.1f MB/s", mbps);
}

static void DrawRowState(int state, float cx, float cy)
{
    switch (state) {
    case 1: ShsDrawIcon(SHS_ICON_CHECK, cx, cy, 14.0f, SHS_COLOR_SUCCESS); break;
    case 2: ShsDrawIcon(SHS_ICON_WARN,  cx, cy, 14.0f, SHS_COLOR_DANGER);  break;
    case 4: ShsDrawIcon(SHS_ICON_CLOSE, cx, cy, 12.0f, SHS_COLOR_TEXT_MUTED); break;
    case 0: case 5: {                                                      /* copying / verifying: spinning arc */
        float a = (float)GetTime() * 280.0f;
        DrawRing((Vector2){ cx, cy }, 5.0f, 6.6f, a, a + 250.0f, 20, SHS_COLOR_PRIMARY);
    } break;
    default: DrawRing((Vector2){ cx, cy }, 4.2f, 5.4f, 0, 360, 20, Fade(SHS_COLOR_TEXT_MUTED, 0.6f)); break;   /* queued */
    }
}

static const char *FILTER_LABEL[4] = { "All items", "Queued", "Completed", "Failed / skipped" };

static ShsTaskAction DrawQuickPanel(ShsTaskView *v, const ShsTask *t, Rectangle r, float baseH, float x, float w, bool inside,
                                    bool *pressedAny)
{
    ShsTaskAction act = SHS_ACT_NONE;
    const int id = (t->id >= 0 && t->id < SHS_TASK_VIEW_MAX) ? t->id : 0;
    const float py = r.y + baseH;
    DrawRectangle((int)x, (int)py, (int)w, 1, Fade(SHS_COLOR_BORDER, 0.8f));
    if (v->expandT[id] < 0.98f) return act;                    /* still opening: content appears once it has room */

    Rectangle panel = { r.x, py, r.width, r.height - baseH };
    if (inside && CheckCollisionPointRec(MousePos(v), panel) && Pressed(v)) *pressedAny = true;   /* clicks here never collapse the card */

    /* tab strip */
    const float ty = py + 6;
    float tx = x;
    for (int i = 0; i < QP_TABS; i++) {
        static const char *label[QP_TABS] = { "File list", "Target", "Options", "Status", "Log" };
        float tw = (float)ShsMeasureText(label[i], 15) + 28.0f;
        Rectangle tr = { tx, ty, tw, QP_TAB_H - 6 };
        bool hot = inside && CheckCollisionPointRec(MousePos(v), tr);
        bool sel = v->tab[id] == i;
        ShsDrawText(label[i], (int)(tx + 14), (int)ty + 8, 15, sel ? SHS_COLOR_PRIMARY : (hot ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED));
        if (sel) DrawRectangle((int)tx + 8, (int)(ty + QP_TAB_H - 8), (int)tw - 16, 2, SHS_COLOR_PRIMARY);
        if (hot && Pressed(v)) v->tab[id] = i;
        tx += tw;
    }

    const float by = ty + QP_TAB_H, bh = QuickPanelHeight() - 6.0f - QP_TAB_H - 6.0f - 8.0f;
    char buf[SHS_TASK_PATH_MAX + 64], a[40], b[40];

    if (v->tab[id] == 0) {
        /* toolbar: Add folder, hint, Auto-scroll, filter */
        if (TextButton(v, (Rectangle){ x, by, 112, 28 }, "Add folder", true, inside, false)) act = SHS_ACT_ADD_FOLDER;
        ShsDrawText("or drop files and folders here", (int)x + 124, (int)by + 6, 14, SHS_COLOR_TEXT_MUTED);

        Rectangle fb = { x + w - 150, by, 150, 28 };
        bool fhot = inside && CheckCollisionPointRec(MousePos(v), fb);
        ShsDrawButtonBox(fb, 8.0f, fhot ? SHS_COLOR_CARD_HOVER : SHS_COLOR_BG, SHS_COLOR_BORDER, fhot && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        ShsDrawText(FILTER_LABEL[v->filter[id] & 3], (int)fb.x + 12, (int)fb.y + 6, 14, SHS_COLOR_TEXT_MAIN);
        ShsDrawIcon(SHS_ICON_CHEVRON_DOWN, fb.x + fb.width - 16, fb.y + 14, 12.0f, SHS_COLOR_TEXT_MUTED);
        if (fhot && Pressed(v)) { v->filter[id] = (v->filter[id] + 1) & 3; v->listTop[id] = 0; }

        float cw = 0;
        bool autoOn = !v->freeScroll[id];
        float cx = fb.x - 14 - (ShsMeasureText("Auto-scroll", 15) + 30);
        if (CheckBox(v, cx, by - 1, "Auto-scroll", autoOn, inside, &cw)) v->freeScroll[id] = autoOn;      /* toggles */

        /* rows */
        Rectangle list = { x, by + QP_TOOL_H, w, bh - QP_TOOL_H };
        const int visible = (int)(list.height / QP_ROW_H);
        int total = 0;
        ShsTaskRow rows[16];
        const int filter = v->filter[id] & 3;
        if (t->rows) t->rows(t->rowsCtx, filter, 0, 0, rows, &total);

        float top = v->listTop[id];
        bool scrollable = total > visible;
        if (scrollable && CheckCollisionPointRec(MousePos(v), list)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) { top -= wheel * 3.0f; v->freeScroll[id] = true; }
            v->innerRect = list;
        }
        if (!v->freeScroll[id]) {                                             /* follow the copy: current file near the bottom */
            int focus = filter == 0 ? (int)(t->filesDone + t->filesSkipped + t->errors) : total;
            top = (float)(focus - (visible - 2));
            if (t->state != SHS_TASK_SYNCING && t->state != SHS_TASK_PAUSED) top = (float)(total - visible);
        }
        top = Clampf(top, 0.0f, (float)(total > visible ? total - visible : 0));
        v->listTop[id] = top;

        int n = 0;
        if (t->rows && total > 0) n = t->rows(t->rowsCtx, filter, (int)top, visible < 16 ? visible : 16, rows, &total);
        for (int k = 0; k < n; k++) {
            float ry = list.y + k * QP_ROW_H, cy = ry + QP_ROW_H / 2;
            const ShsTaskRow *f = &rows[k];
            if (f->state == 0 || f->state == 5) ShsRoundRec((Rectangle){ x, ry + 1, w - (scrollable ? 12.0f : 0.0f), QP_ROW_H - 2 }, 0.3f, 6, Fade(SHS_COLOR_PRIMARY, 0.10f));
            ShsDrawIcon(SHS_ICON_LOGS, x + 14, cy, 13.0f, SHS_COLOR_TEXT_MUTED);
            FormatSize(a, sizeof a, f->size);
            float right = x + w - (scrollable ? 14.0f : 4.0f);
            DrawRowState(f->state, right - 12, cy);
            RightText(a, right - 34, cy - 8, 14, SHS_COLOR_TEXT_MUTED);
            DrawPathFit(f->name, x + 32, cy - 8, 14, right - 34 - 80 - (x + 32), f->state == 4 ? SHS_COLOR_TEXT_MUTED : SHS_COLOR_TEXT_MAIN);
        }
        if (total == 0) {
            const char *msg = filter == 0 ? "Drop files or folders here to copy them to the target"
                                          : "Nothing in this list";
            ShsDrawText(msg, (int)(list.x + (list.width - ShsMeasureText(msg, 15)) / 2), (int)(list.y + list.height / 2 - 10), 15, SHS_COLOR_TEXT_MUTED);
        }
        if (scrollable) {
            float th = fmaxf(24.0f, list.height * visible / total);
            float tyb = list.y + (list.height - th) * (top / (float)(total - visible));
            ShsRoundRec((Rectangle){ x + w - 6, list.y, 5, list.height }, 1.0f, 6, Fade(SHS_COLOR_BG, 0.8f));
            ShsRoundRec((Rectangle){ x + w - 6, tyb, 5, th }, 1.0f, 6, SHS_COLOR_BORDER);
        }
    } else if (v->tab[id] == 1) {
        ShsDrawText("Target folder", (int)x, (int)by + 6, 13, SHS_COLOR_TEXT_MUTED);
        DrawTextFit(t->target, x, by + 24, 16, w, SHS_COLOR_TEXT_MAIN, true);
        ShsDrawText("Source", (int)x, (int)by + 48, 13, SHS_COLOR_TEXT_MUTED);
        DrawTextFit(t->source, x, by + 66, 16, w, SHS_COLOR_TEXT_MAIN, true);
        if (t->targetInfo[0]) ShsDrawText(t->targetInfo, (int)x, (int)by + 98, 14, SHS_COLOR_TEXT_MUTED);
        ShsDrawText("Files and folders dropped on this card are copied into the target folder.", (int)x, (int)by + 120, 14, SHS_COLOR_TEXT_MUTED);
        if (TextButton(v, (Rectangle){ x, by + 148, 160, 32 }, "Open target folder", true, inside, false)) act = SHS_ACT_OPEN_TARGET;
        if (TextButton(v, (Rectangle){ x + 170, by + 148, 160, 32 }, "Open source folder", true, inside, false)) act = SHS_ACT_OPEN_SOURCE;
    } else if (v->tab[id] == 2) {
        float cw = 0;
        char vlabel[48];
        if (t->filesVerified) snprintf(vlabel, sizeof vlabel, "Verify copied files (%llu OK)", t->filesVerified);
        else                  snprintf(vlabel, sizeof vlabel, "Verify copied files");
        if (CheckBox(v, x, by, vlabel, t->verify, inside, &cw)) act = SHS_ACT_TOGGLE_VERIFY;
        ShsDrawText("Reads every file back from the target and compares it with the source.", (int)x + 30, (int)by + 30, 13, SHS_COLOR_TEXT_MUTED);
        if (CheckBox(v, x, by + 54, "Unattended", t->unattended, inside, &cw)) act = SHS_ACT_TOGGLE_UNATTENDED;
        ShsDrawText("Removes this card a few seconds after the copy finishes without errors.", (int)x + 30, (int)by + 84, 13, SHS_COLOR_TEXT_MUTED);
        if (t->optionsInfo[0]) ShsDrawText(t->optionsInfo, (int)x, (int)by + 116, 14, SHS_COLOR_TEXT_MUTED);
    } else if (v->tab[id] == 3) {
        struct { const char *k; char v[64]; } cell[8];
        int c = 0;
        snprintf(cell[c].v, sizeof cell[c].v, "%llu of %llu", t->filesDone, t->filesTotal);                cell[c++].k = "Files copied";
        FormatSize(a, sizeof a, t->bytesDone); FormatSize(b, sizeof b, t->bytesTotal);
        snprintf(cell[c].v, sizeof cell[c].v, "%s of %s", a, b);                                           cell[c++].k = "Data";
        FormatRate(cell[c].v, sizeof cell[c].v, t->rateMBps);                                              cell[c++].k = "Speed now";
        FormatRate(cell[c].v, sizeof cell[c].v, t->avgMBps);                                               cell[c++].k = "Average speed";
        FormatDuration(cell[c].v, sizeof cell[c].v, t->elapsedSec);                                        cell[c++].k = "Elapsed";
        if (t->etaSeconds >= 0) FormatDuration(cell[c].v, sizeof cell[c].v, t->etaSeconds); else snprintf(cell[c].v, sizeof cell[c].v, "--");
        cell[c++].k = "Remaining";
        snprintf(cell[c].v, sizeof cell[c].v, "%llu verified, %llu skipped", t->filesVerified, t->filesSkipped); cell[c++].k = "Checks";
        snprintf(cell[c].v, sizeof cell[c].v, "%llu", t->errors);                                          cell[c++].k = "Errors";
        for (int i = 0; i < c; i++) {
            float cxp = x + (i % 2) * (w / 2), cyp = by + 4 + (i / 2) * 26.0f;
            ShsDrawText(cell[i].k, (int)cxp, (int)cyp, 14, SHS_COLOR_TEXT_MUTED);
            ShsDrawText(cell[i].v, (int)cxp + 120, (int)cyp, 14, (i == 7 && t->errors) ? SHS_COLOR_DANGER : SHS_COLOR_TEXT_MAIN);
        }
        Rectangle gr = { x, by + 110, w, bh - 110 };
        ShsDrawRoundedBox(gr, 10.0f, SHS_COLOR_BG, SHS_COLOR_BORDER);
        DrawSpeedGraph((Rectangle){ gr.x + 2, gr.y + 22, gr.width - 4, gr.height - 24 }, t->graph, t->graphCount, Fade(SHS_COLOR_PRIMARY, 0.45f));
        ShsDrawText("Speed", (int)gr.x + 12, (int)gr.y + 5, 13, SHS_COLOR_TEXT_MUTED);
    } else {
        const int lines = (int)(bh / 20.0f);
        int first = t->logCount > lines ? t->logCount - lines : 0;
        if (t->logCount == 0) ShsDrawText("Nothing logged yet.", (int)x, (int)by + 6, 14, SHS_COLOR_TEXT_MUTED);
        for (int i = first; i < t->logCount; i++)
            DrawTextFit(t->log[i], x, by + 4 + (i - first) * 20.0f, 14, w, i == t->logCount - 1 ? SHS_COLOR_TEXT_MAIN : SHS_COLOR_TEXT_MUTED, false);
    }
    if (act != SHS_ACT_NONE) *pressedAny = true;
    return act;
}

static ShsTaskAction DrawQuickCard(ShsTaskView *view, int index, const ShsTask *t, Rectangle r, float hover, bool inside,
                                   bool *pressedAny)
{
    (void)index;
    ShsDrawRoundedBox(r, CARD_RADIUS_PX, LerpColor(SHS_COLOR_CARD, SHS_COLOR_CARD_HOVER, hover),
                      LerpColor(SHS_COLOR_BORDER, SHS_COLOR_PRIMARY, hover * 0.5f));
    if (t->state == SHS_TASK_SYNCING && t->graphCount > 1)                   /* speed silhouette behind the right-hand columns */
        DrawSpeedGraph((Rectangle){ r.x + r.width - 330, r.y + 8, 316, CARD_H_QUICK - 24 }, t->graph, t->graphCount,
                       Fade(SHS_COLOR_PRIMARY, 0.14f));

    const bool running  = t->state == SHS_TASK_SYNCING;
    const bool paused   = t->state == SHS_TASK_PAUSED;
    const bool active   = running || paused;
    const float x = r.x + 16, R = r.x + r.width - 16;
    const float cPct = R, cSize = R - 84, cDone = R - 168, cCount = R - 252;       /* right edges of the columns */
    const float textX = x + 104, textMax = cCount - 150 - textX;

    /* ---- row A: current file ---- */
    const char *chipA = paused ? "Paused" : (running ? (t->curState == 5 ? "Verifying" : "Copying")
                       : (t->state == SHS_TASK_ERROR ? "Failed" : (t->result == 3 ? "Stopped" : "Done")));
    Color tintA = paused ? SHS_COLOR_WARNING : (t->state == SHS_TASK_ERROR ? SHS_COLOR_DANGER : (active ? SHS_COLOR_PRIMARY : SHS_COLOR_SUCCESS));
    Chip(x, r.y + 12, 92, chipA, tintA, !running);

    char buf[SHS_TASK_PATH_MAX + 64], a[40], b[40];
    float fileP = t->curSize ? (float)((double)t->curDone / (double)t->curSize) : (active ? 0.0f : 1.0f);
    if (t->curName[0]) {
        char full[SHS_TASK_PATH_MAX * 2];
        snprintf(full, sizeof full, "%s\\%s", t->source, t->curName);
        DrawPathFit(full, textX, r.y + 17, 15, textMax, SHS_COLOR_TEXT_MAIN);
    } else if (t->state == SHS_TASK_ERROR) {
        DrawTextFit(t->errorMsg[0] ? t->errorMsg : "Copy failed", textX, r.y + 17, 15, textMax, SHS_COLOR_DANGER, false);
    } else {
        DrawTextFit(t->source, textX, r.y + 17, 15, textMax, SHS_COLOR_TEXT_MAIN, true);
    }
    unsigned long long idx = t->filesDone + (active && t->filesDone < t->filesTotal ? 1 : 0);
    snprintf(buf, sizeof buf, "%llu / %llu", idx, t->filesTotal);
    RightText(buf, cCount, r.y + 17, 15, SHS_COLOR_TEXT_MUTED);
    FormatSize(a, sizeof a, t->curDone);  RightText(a, cDone, r.y + 17, 15, SHS_COLOR_TEXT_MUTED);
    FormatSize(b, sizeof b, t->curSize);  RightText(b, cSize, r.y + 17, 15, SHS_COLOR_TEXT_MUTED);
    snprintf(buf, sizeof buf, "%.1f %%", fileP * 100.0f);
    RightText(buf, cPct, r.y + 17, 15, SHS_COLOR_TEXT_MAIN);
    ThinBar(x, r.y + 46, R - x, fileP, tintA);

    /* ---- row B: target and overall progress ---- */
    Chip(x, r.y + 56, 92, "Target", SHS_COLOR_PRIMARY, false);
    if (t->curName[0]) snprintf(buf, sizeof buf, "%s\\%s", t->target, t->curName);
    else               snprintf(buf, sizeof buf, "%s", t->target);
    DrawTextFit(buf, textX, r.y + 61, 15, textMax, SHS_COLOR_TEXT_MAIN, true);

    float allP = t->bytesTotal ? (float)((double)t->bytesDone / (double)t->bytesTotal) : (active ? 0.0f : 1.0f);
    if (running && t->rateMBps > 0.05f) {
        char sp[32], eta[24] = "";
        if (t->rateMBps >= 1024.0f) snprintf(sp, sizeof sp, "%.1f GB/s", t->rateMBps / 1024.0f);
        else                        snprintf(sp, sizeof sp, "%.1f MB/s", t->rateMBps);
        if (t->etaSeconds >= 0) {
            int s = (int)t->etaSeconds;
            if (s >= 60) snprintf(eta, sizeof eta, " - %dm %02ds", s / 60, s % 60); else snprintf(eta, sizeof eta, " - %d s", s);
        }
        snprintf(buf, sizeof buf, "%s%s", sp, eta);
    } else {
        snprintf(buf, sizeof buf, "%s", paused ? "paused" : "--");
    }
    RightText(buf, cCount, r.y + 61, 15, SHS_COLOR_TEXT_MUTED);
    FormatSize(a, sizeof a, t->bytesDone);   RightText(a, cDone, r.y + 61, 15, SHS_COLOR_TEXT_MUTED);
    FormatSize(b, sizeof b, t->bytesTotal);  RightText(b, cSize, r.y + 61, 15, SHS_COLOR_TEXT_MUTED);
    snprintf(buf, sizeof buf, "%.1f %%", allP * 100.0f);
    RightText(buf, cPct, r.y + 61, 15, SHS_COLOR_TEXT_MAIN);
    ThinBar(x, r.y + 90, R - x, allP, SHS_COLOR_PRIMARY);

    /* ---- row C: buttons and options ---- */
    ShsTaskAction act = SHS_ACT_NONE;
    const float by = r.y + 100;
    if (active) {
        if (TextButton(view, (Rectangle){ x, by, 100, 30 }, paused ? "Resume" : "Pause", true, inside, false)) act = paused ? SHS_ACT_RESUME : SHS_ACT_PAUSE;
        if (TextButton(view, (Rectangle){ x + 108, by, 100, 30 }, "Skip", t->curName[0] != 0, inside, false)) act = SHS_ACT_SKIP;
        if (TextButton(view, (Rectangle){ x + 216, by, 100, 30 }, "Stop", true, inside, false)) act = SHS_ACT_CANCEL;
    } else {
        if (TextButton(view, (Rectangle){ x, by, 110, 30 }, "Copy again", true, inside, true)) act = SHS_ACT_SYNC;
        if (TextButton(view, (Rectangle){ x + 118, by, 130, 30 }, "Open target", true, inside, false)) act = SHS_ACT_OPEN_TARGET;
        if (TextButton(view, (Rectangle){ x + 256, by, 100, 30 }, "Dismiss", true, inside, false)) act = SHS_ACT_REMOVE;
    }
    char vlabel[48];
    if (t->filesVerified) snprintf(vlabel, sizeof vlabel, "Verify (%llu OK)", t->filesVerified);
    else                  snprintf(vlabel, sizeof vlabel, "Verify");
    float w1 = 0, w2 = 0;
    float xv = R - (ShsMeasureText(vlabel, 15) + 30);
    float xu = xv - 28 - (ShsMeasureText("Unattended", 15) + 30);
    if (CheckBox(view, xu, by, "Unattended", t->unattended, inside, &w1)) act = SHS_ACT_TOGGLE_UNATTENDED;
    if (CheckBox(view, xv, by, vlabel, t->verify, inside, &w2)) act = SHS_ACT_TOGGLE_VERIFY;

    if (act != SHS_ACT_NONE) *pressedAny = true;

    /* a dismissed Quick Copy does not need a confirmation click; everything else is a plain action */
    float baseH = ShsTaskCardHeight(t);
    int xid = (t->id >= 0 && t->id < SHS_TASK_VIEW_MAX) ? t->id : 0;
    ShsDrawIcon(view->expanded[xid] ? SHS_ICON_CHEVRON_UP : SHS_ICON_CHEVRON_DOWN, r.x + r.width / 2, r.y + baseH - 8, 14.0f,
                Fade(SHS_COLOR_TEXT_MUTED, hover > 0.5f ? 1.0f : 0.55f));
    if (view->expandT[xid] > 0.01f) {
        ShsTaskAction pa = DrawQuickPanel(view, t, r, baseH, x, r.width - 32, inside, pressedAny);
        if (pa != SHS_ACT_NONE) act = pa;
    }
    return act;
}
/* Draws the card and its buttons; returns the action pressed (SHS_ACT_NONE for the body). */
static ShsTaskAction DrawCard(ShsTaskView *view, int index, const ShsTask *t, Rectangle r, float hover, bool inside,
                              bool *pressedAny)
{
    if (t->quick) return DrawQuickCard(view, index, t, r, hover, inside, pressedAny);
    ShsDrawRoundedBox(r, CARD_RADIUS_PX, LerpColor(SHS_COLOR_CARD, SHS_COLOR_CARD_HOVER, hover),
                      LerpColor(SHS_COLOR_BORDER, SHS_COLOR_PRIMARY, hover * 0.5f));

    float x = r.x + CARD_PAD, w = r.width - CARD_PAD * 2;

    /* play / pause button (replaces the status square): start or resume, or pause a running sync */
    /* The app logo is the play / pause button: full colour normally, grey while paused. Hovering shows what a click does.
       It sits in the top-left corner of the content area, level with the task name. */
    Rectangle av = { x, r.y + TOP_Y + 1, AVATAR, AVATAR };
    ShsIcon playIcon = (t->state == SHS_TASK_SYNCING) ? SHS_ICON_PAUSE : SHS_ICON_PLAY;
    ShsTaskAction playAct = (t->state == SHS_TASK_SYNCING) ? SHS_ACT_PAUSE : (t->state == SHS_TASK_PAUSED ? SHS_ACT_RESUME : SHS_ACT_SYNC);
    const char *playTip = (t->state == SHS_TASK_SYNCING) ? "Pause sync" : (t->state == SHS_TASK_PAUSED ? "Resume sync" :
                          (t->state == SHS_TASK_ERROR ? "Retry sync" : "Sync now"));
    bool avHot = inside && CheckCollisionPointRec(MousePos(view), av);
    Texture2D logoTex = (t->state == SHS_TASK_PAUSED && view->logoGray.id) ? view->logoGray : view->logo;
    if (logoTex.id) DrawTexturePro(logoTex, (Rectangle){ 0, 0, (float)logoTex.width, (float)logoTex.height }, av, (Vector2){ 0, 0 }, 0.0f, WHITE);
    else            ShsDrawRoundedBox(av, 8.0f, Fade(StateColor(t->state), 0.3f), Fade(StateColor(t->state), 0.6f));
    if (avHot) {
        ShsRoundRec(av, 0.3f, 10, Fade(BLACK, 0.6f));
        ShsDrawIcon(playIcon, av.x + AVATAR / 2.0f + (playIcon == SHS_ICON_PLAY ? 1.0f : 0.0f), av.y + AVATAR / 2.0f, 16.0f, SHS_COLOR_ON_PRIMARY);
    }
    /* title row: name, meta text, then the status pill; the paths go on the next line */
    float badgeLeft = DrawBadge(t->state, r.x + r.width - CARD_PAD, r.y + 14);      /* centred on the name line */
    float tx = r.x + TEXT_X;
    float avail = badgeLeft - 18 - tx;
    float nameW = fminf((float)ShsMeasureText(t->name, 20), avail * 0.5f);
    DrawTextFit(t->name, tx, r.y + TOP_Y, 20, nameW, SHS_COLOR_TEXT_MAIN, false);

    /* meta text sits on the title row after the name (same baseline: 20 px text at y+11 vs 13 px text at y+17) */
    char meta[SHS_TASK_PATH_MAX];
    if (t->state == SHS_TASK_SYNCING) snprintf(meta, sizeof meta, "%s", t->mirror ? "Mirror mode" : "Copying new and changed files");
    else                              snprintf(meta, sizeof meta, "%s", t->meta);
    float metaX = tx + nameW + 16;
    if (badgeLeft - 18 - metaX > 60) DrawTextFit(meta, metaX, r.y + TOP_Y + 6, 13, badgeLeft - 18 - metaX, SHS_COLOR_TEXT_MUTED, false);

    /* buttons, right-aligned on the path row */
    CardButton b[6];
    int n = 0;
    const bool armed = false;                               /* removing is confirmed in the app's own dialog (it can also offer to delete the target files) */
    if (!t->quick)
        b[n++] = (CardButton){ {0}, SHS_ICON_WATCH, SHS_ACT_TOGGLE_AUTO,
                               t->autoSync ? "Auto-sync is on - click to turn off" : "Watch the source folder and sync automatically",
                               t->autoSync, false };
    if (t->state == SHS_TASK_SYNCING) {
        b[n++] = (CardButton){ {0}, SHS_ICON_CLOSE,  SHS_ACT_CANCEL,      "Cancel", false, false };
        b[n++] = (CardButton){ {0}, SHS_ICON_FOLDER, SHS_ACT_OPEN_TARGET, "Open target folder", false, false };
    } else if (t->state == SHS_TASK_PAUSED) {
        b[n++] = (CardButton){ {0}, SHS_ICON_CLOSE,  SHS_ACT_CANCEL,      "Cancel", false, false };
        b[n++] = (CardButton){ {0}, SHS_ICON_FOLDER, SHS_ACT_OPEN_TARGET, "Open target folder", false, false };
    } else {
        if (!t->quick) b[n++] = (CardButton){ {0}, SHS_ICON_PENCIL, SHS_ACT_EDIT,        "Edit task", false, false };
        b[n++] = (CardButton){ {0}, SHS_ICON_FOLDER, SHS_ACT_OPEN_TARGET, "Open target folder", false, false };
        b[n++] = (CardButton){ {0}, armed ? SHS_ICON_CHECK : SHS_ICON_TRASH, SHS_ACT_REMOVE,
                               armed ? "Click again to remove" : (t->quick ? "Dismiss" : "Remove task"), false, armed };
    }
    float bx = r.x + r.width - CARD_PAD;
    float by = r.y + BTN_Y;                                 /* well below the pill, level with the speed / progress row */
    for (int i = n - 1; i >= 0; i--) { bx -= BTN; b[i].r = (Rectangle){ bx, by, BTN, BTN }; bx -= BTN_GAP; }
    float btnLeft = bx + BTN_GAP;

    /* source -> target on the line below: shortened (drive + last folder) and kept clear of the buttons */
    const char *pathTip = NULL; Rectangle pathTipRect = { 0 };
    DrawPathRow(t, tx, r.y + 37, btnLeft - 18 - tx, MousePos(view), inside, &pathTip, &pathTipRect);

    /* third row: speed, remaining time and progress, then the buttons */
    DrawStatsRow(t, tx, r.y + 60, btnLeft - 18);

    ShsTaskAction act = SHS_ACT_NONE;
    const CardButton *hovered = NULL;
    if (avHot && Pressed(view)) { act = playAct; *pressedAny = true; }
    for (int i = 0; i < n; i++) {
        if (IconButton(view, &b[i], &hovered, inside)) {
            act = b[i].act;
            *pressedAny = true;
        }
    }
    if (hovered) DrawTooltip(hovered->tip, hovered->r, false, r.x + r.width - 4);
    else if (avHot) DrawTooltip(playTip, av, true, r.x + r.width - 4);
    else if (pathTip) DrawTooltip(pathTip, pathTipRect, true, r.x + r.width - 4);

    /* expand affordance, and the file list when open */
    float baseH = ShsTaskCardHeight(t);
    int xid = (t->id >= 0 && t->id < SHS_TASK_VIEW_MAX) ? t->id : 0;
    ShsDrawIcon(view->expanded[xid] ? SHS_ICON_CHEVRON_UP : SHS_ICON_CHEVRON_DOWN, r.x + r.width / 2, r.y + baseH - 9, 14.0f,
                Fade(SHS_COLOR_TEXT_MUTED, hover > 0.5f ? 1.0f : 0.55f));
    if (view->expandT[xid] > 0.01f) DrawFilePanel(view, t, r, baseH, x, w, inside, pressedAny);

    /* footer */
    if (t->state == SHS_TASK_SYNCING) DrawFileLine(t, tx, r.y + 88, r.x + r.width - CARD_PAD - tx);
    if (t->state == SHS_TASK_ERROR) {
        Rectangle box = { x, r.y + 90, w, 32 };
        ShsRoundRec(box, 0.3f, 8, Fade(SHS_COLOR_DANGER, 0.18f));
        ShsRoundLines(box, 0.3f, 8, 1.0f, Fade(SHS_COLOR_DANGER, 0.7f));
        ShsDrawIcon(SHS_ICON_WARN, box.x + 18, box.y + box.height / 2, 16.0f, SHS_COLOR_DANGER);
        DrawTextFit(t->errorMsg[0] ? t->errorMsg : "Sync failed", box.x + 36, box.y + 8, 15, box.width - 48, SHS_COLOR_TEXT_MAIN, false);
    }
    return act;
}

/* ---- scrolling list --------------------------------------------------- */

void ShsTaskViewRemoved(ShsTaskView *view, int id)
{
    if (id < 0 || id >= SHS_TASK_VIEW_MAX) return;
    for (int k = id; k + 1 < SHS_TASK_VIEW_MAX; k++) {
        view->expanded[k] = view->expanded[k + 1]; view->expandT[k] = view->expandT[k + 1];
        view->tab[k] = view->tab[k + 1]; view->filter[k] = view->filter[k + 1];
        view->listTop[k] = view->listTop[k + 1]; view->freeScroll[k] = view->freeScroll[k + 1];
        view->sumSel[k] = view->sumSel[k + 1]; view->sumTop[k] = view->sumTop[k + 1];
    }
    const int last = SHS_TASK_VIEW_MAX - 1;
    view->expanded[last] = false; view->expandT[last] = 0.0f;
    view->tab[last] = 0; view->filter[last] = 0; view->listTop[last] = 0.0f; view->freeScroll[last] = false;
    view->sumSel[last] = 0; view->sumTop[last] = 0.0f;
}

ShsTaskEvent ShsTaskListDraw(ShsTaskView *view, const ShsTask *tasks, int count, Rectangle bounds)
{
    s_listH = bounds.height;
    float edt = Clampf(GetFrameTime() * 12.0f, 0.0f, 1.0f);
    for (int i = 0; i < count; i++) {
        int id = (tasks[i].id >= 0 && tasks[i].id < SHS_TASK_VIEW_MAX) ? tasks[i].id : 0;
        float target = view->expanded[id] ? 1.0f : 0.0f;
        view->expandT[id] += (target - view->expandT[id]) * edt;
        if (fabsf(target - view->expandT[id]) < 0.003f) view->expandT[id] = target;
    }

    float contentH = 0;
    for (int i = 0; i < count; i++) contentH += CardHeightNow(view, &tasks[i]) + (i ? CARD_GAP : 0);
    float maxScroll = fmaxf(0.0f, contentH - bounds.height);

    Vector2 mouse = MousePos(view);
    bool inside = CheckCollisionPointRec(mouse, bounds);

    if (view->reveal) {                                      /* keep a freshly opened card in view while it grows */
        float off = 0;
        int rid = view->reveal - 1;
        for (int i = 0; i < count; i++) {
            int id = (tasks[i].id >= 0 && tasks[i].id < SHS_TASK_VIEW_MAX) ? tasks[i].id : 0;
            if (id == rid) { view->scrollTarget = off; if (view->expandT[id] >= 0.999f) view->reveal = 0; break; }
            off += CardHeightNow(view, &tasks[i]) + CARD_GAP;
        }
        if (count == 0) view->reveal = 0;
    }
    view->innerPrev = view->innerRect;                       /* a scrollable file list under the cursor takes the wheel */
    view->innerRect = (Rectangle){ 0 };
    if (inside && !CheckCollisionPointRec(mouse, view->innerPrev)) view->scrollTarget -= GetMouseWheelMove() * WHEEL_STEP;
    view->scrollTarget = Clampf(view->scrollTarget, 0.0f, maxScroll);
    view->scroll += (view->scrollTarget - view->scroll) * Clampf(GetFrameTime() * 14.0f, 0.0f, 1.0f);
    view->scroll = Clampf(view->scroll, 0.0f, maxScroll);

    if (view->armedRemove >= count || GetTime() - view->armedAt >= REMOVE_ARM_S) view->armedRemove = -1;

    float cardW = bounds.width - (maxScroll > 0 ? SCROLLBAR_W + 8 : 0);
    float dt = GetFrameTime();
    ShsTaskEvent ev = { -1, SHS_ACT_NONE };
    float y = bounds.y - view->scroll;

    BeginScissorMode((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height);
    for (int i = 0; i < count; i++) {
        float h = CardHeightNow(view, &tasks[i]);
        Rectangle r = { bounds.x, y, cardW, h };
        y += h + CARD_GAP;

        if (r.y + r.height < bounds.y || r.y > bounds.y + bounds.height) {
            if (i < SHS_TASK_VIEW_MAX) view->hover[i] = 0.0f;   /* off-screen: reset */
            continue;
        }
        bool hot = inside && CheckCollisionPointRec(mouse, r);
        float hv = 0.0f;
        if (i < SHS_TASK_VIEW_MAX) {
            float target = hot ? 1.0f : 0.0f;
            view->hover[i] += (target - view->hover[i]) * Clampf(dt * 14.0f, 0.0f, 1.0f);
            hv = view->hover[i];
        } else if (hot) hv = 1.0f;
        if (i == view->selected) hv = 1.0f;

        bool pressedAny = false;
        ShsTaskAction act = DrawCard(view, i, &tasks[i], r, hv, hot, &pressedAny);
        if (pressedAny) {
            if (act != SHS_ACT_NONE) { ev.index = i; ev.action = act; }
        } else if (hot && Pressed(view)) {
            int id = (tasks[i].id >= 0 && tasks[i].id < SHS_TASK_VIEW_MAX) ? tasks[i].id : 0;
            view->expanded[id] = !view->expanded[id];           /* body click: expand / collapse the file list */
            view->reveal = view->expanded[id] ? id + 1 : 0;
            ev.index = i; ev.action = SHS_ACT_NONE;
        }
    }

    if (maxScroll > 0) {
        float trackX = bounds.x + bounds.width - SCROLLBAR_W;
        float thumbH = fmaxf(32.0f, bounds.height * bounds.height / contentH);
        float thumbY = bounds.y + (bounds.height - thumbH) * (view->scroll / maxScroll);
        ShsRoundRec((Rectangle){ trackX, bounds.y, SCROLLBAR_W, bounds.height }, 1.0f, 6, Fade(SHS_COLOR_CARD, 0.6f));
        ShsRoundRec((Rectangle){ trackX, thumbY, SCROLLBAR_W, thumbH }, 1.0f, 6, SHS_COLOR_BORDER);
    }
    EndScissorMode();
    return ev;
}
