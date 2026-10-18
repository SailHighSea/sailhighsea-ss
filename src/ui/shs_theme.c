#include "shs_theme.h"
#include "platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- palettes ----------------------------------------------------------- */

#define RGB(r, g, b) { (r), (g), (b), 0xFF }

static const ShsTheme THEME_DARK = {
    .bg = RGB(0x0B, 0x0B, 0x0C),        .card = RGB(0x16, 0x16, 0x18),
    .cardHover = RGB(0x22, 0x22, 0x25), .border = RGB(0x2E, 0x2E, 0x32), .titleBar = RGB(0x00, 0x00, 0x00),
    .primary = RGB(0x02, 0x84, 0xC7),   .primaryHover = RGB(0x03, 0x69, 0xA1),
    .textMain = RGB(0xF4, 0xF4, 0xF5),  .textMuted = RGB(0x9C, 0x9C, 0xA3),
    .danger = RGB(0xDC, 0x26, 0x26),    .success = RGB(0x22, 0xC5, 0x5E),
    .warning = RGB(0xF5, 0x9E, 0x0B),
};

static const ShsTheme THEME_LIGHT = {
    .bg = RGB(0xF1, 0xF5, 0xF9),        .card = RGB(0xFF, 0xFF, 0xFF),
    .cardHover = RGB(0xE8, 0xEE, 0xF5), .border = RGB(0xCB, 0xD5, 0xE1), .titleBar = RGB(0x17, 0x25, 0x54),
    .primary = RGB(0x02, 0x84, 0xC7),   .primaryHover = RGB(0x03, 0x69, 0xA1),
    .textMain = RGB(0x0F, 0x17, 0x2A),  .textMuted = RGB(0x5B, 0x6B, 0x82),
    .danger = RGB(0xDC, 0x26, 0x26),    .success = RGB(0x16, 0xA3, 0x4A),
    .warning = RGB(0xC2, 0x6A, 0x05),
};

static const ShsTheme THEME_FLAT98 = {
    .bg = RGB(0xC0, 0xC0, 0xC0),        .card = RGB(0xFF, 0xFF, 0xFF),
    .cardHover = RGB(0xDF, 0xDF, 0xDF), .border = RGB(0x40, 0x40, 0x40), .titleBar = RGB(0x00, 0x00, 0x80),
    .primary = RGB(0x00, 0x00, 0x80),   .primaryHover = RGB(0x00, 0x00, 0xA8),
    .textMain = RGB(0x00, 0x00, 0x00),  .textMuted = RGB(0x40, 0x40, 0x40),
    .danger = RGB(0xB0, 0x00, 0x00),    .success = RGB(0x00, 0x64, 0x00),
    .warning = RGB(0x8A, 0x5A, 0x00),
};

static const ShsTheme THEME_WIN98 = {
    .bg = RGB(0xC0, 0xC0, 0xC0),        .card = RGB(0xFF, 0xFF, 0xFF),
    .cardHover = RGB(0xF0, 0xF0, 0xF0), .border = RGB(0x80, 0x80, 0x80), .titleBar = RGB(0x00, 0x00, 0x80),
    .primary = RGB(0x00, 0x00, 0x80),   .primaryHover = RGB(0x00, 0x00, 0xA8),
    .textMain = RGB(0x00, 0x00, 0x00),  .textMuted = RGB(0x40, 0x40, 0x40),
    .danger = RGB(0xB0, 0x00, 0x00),    .success = RGB(0x00, 0x64, 0x00),
    .warning = RGB(0x8A, 0x5A, 0x00),
};

static const ShsTheme THEME_MAC7 = {
    .bg = RGB(0xFF, 0xFF, 0xFF),        .card = RGB(0xFF, 0xFF, 0xFF),
    .cardHover = RGB(0xEE, 0xEE, 0xEE), .border = RGB(0x00, 0x00, 0x00), .titleBar = RGB(0xFF, 0xFF, 0xFF),
    .primary = RGB(0x00, 0x00, 0x00),   .primaryHover = RGB(0x33, 0x33, 0x33),
    .textMain = RGB(0x00, 0x00, 0x00),  .textMuted = RGB(0x4A, 0x4A, 0x4A),
    .danger = RGB(0xA0, 0x00, 0x00),    .success = RGB(0x00, 0x64, 0x00),
    .warning = RGB(0x7A, 0x52, 0x00),
};

static const ShsTheme THEME_PLATINUM = {
    .bg = RGB(0xDD, 0xDD, 0xDD),        .card = RGB(0xFF, 0xFF, 0xFF),
    .cardHover = RGB(0xF4, 0xF4, 0xF4), .border = RGB(0x66, 0x66, 0x66), .titleBar = RGB(0xCC, 0xCC, 0xCC),
    .primary = RGB(0x2F, 0x6F, 0xCF),   .primaryHover = RGB(0x4A, 0x86, 0xE0),
    .textMain = RGB(0x00, 0x00, 0x00),  .textMuted = RGB(0x55, 0x55, 0x55),
    .danger = RGB(0xB4, 0x23, 0x18),    .success = RGB(0x1B, 0x7F, 0x1B),
    .warning = RGB(0x9A, 0x67, 0x00),
};

static const ShsTheme THEME_CDE = {
    .bg = RGB(0xAE, 0xB2, 0xC3),        .card = RGB(0xC9, 0xCC, 0xD9),
    .cardHover = RGB(0xD4, 0xD7, 0xE3), .border = RGB(0x50, 0x53, 0x6A), .titleBar = RGB(0x71, 0x8B, 0xA6),
    .primary = RGB(0x71, 0x8B, 0xA6),   .primaryHover = RGB(0x81, 0x99, 0xB3),
    .textMain = RGB(0x00, 0x00, 0x00),  .textMuted = RGB(0x3B, 0x3E, 0x52),
    .danger = RGB(0x9A, 0x1B, 0x1B),    .success = RGB(0x1F, 0x6B, 0x2B),
    .warning = RGB(0x7A, 0x52, 0x00),
};

static const ShsTheme THEME_GNOME2 = {
    .bg = RGB(0xED, 0xED, 0xEB),        .card = RGB(0xFF, 0xFF, 0xFF),
    .cardHover = RGB(0xF6, 0xF5, 0xF4), .border = RGB(0x8C, 0x8C, 0x86), .titleBar = RGB(0xE8, 0xB7, 0x68),
    .primary = RGB(0xE0, 0x7A, 0x1F),   .primaryHover = RGB(0xF0, 0x8A, 0x2F),
    .textMain = RGB(0x2E, 0x34, 0x36),  .textMuted = RGB(0x6B, 0x70, 0x68),
    .danger = RGB(0xCC, 0x00, 0x00),    .success = RGB(0x4E, 0x9A, 0x06),
    .warning = RGB(0xC1, 0x7D, 0x11),
};

/* ---- classic styles: how each retro theme draws its chrome ----------------- */

enum { TITLE_SOLID, TITLE_GRAD_H, TITLE_RAISED, TITLE_STRIPES_BW, TITLE_PINSTRIPE, TITLE_GRAD_V };
enum { PROG_DEFAULT, PROG_BLOCKS, PROG_SOLID, PROG_STRIPES_BW, PROG_POLE, PROG_GRAD };

typedef struct {
    const ShsTheme *pal;
    bool   square;                 /* no anti-aliasing, small fonts: fixed at start-up */
    int    bevel;                  /* 3D edge layers (0 = none) */
    Color  rTL[3], rBR[3], sTL[3], sBR[3];     /* raised / sunken edge colours, outermost first */
    float  radius;                 /* corner scale for round themes */
    int    title;
    Color  titleA, titleB, titleInk, titleBack;
    bool   fauxBold, gradBtn;
    Color  btnTop, btnBot, priTop, priBot;
    Color  tabOn, tabOff, tabAccent;
    int    progress;
    const char *name, *label;
} Style;

#define C(r, g, b) { (r), (g), (b), 0xFF }
#define NONE       { 0, 0, 0, 0 }

static const Style STYLES[SHS_THEME_COUNT] = {
    [SHS_THEME_AUTO]  = { .name = "auto",  .label = "System", .radius = 1.0f },
    [SHS_THEME_LIGHT] = { .name = "light", .label = "Light",  .radius = 1.0f },
    [SHS_THEME_DARK]  = { .name = "dark",  .label = "Dark",   .radius = 1.0f },
    [SHS_THEME_FLAT98] = { .pal = &THEME_FLAT98, .square = true, .title = TITLE_SOLID, .titleA = C(0, 0, 0x80), .titleInk = C(255, 255, 255),
        .tabOn = C(255, 255, 255), .tabOff = C(0xC0, 0xC0, 0xC0), .tabAccent = C(0, 0, 0x80), .name = "flat98", .label = "Flat 98" },
    [SHS_THEME_WIN98] = { .pal = &THEME_WIN98, .square = true, .bevel = 2,
        .rTL = { C(255, 255, 255), C(0xDF, 0xDF, 0xDF) }, .rBR = { C(0, 0, 0), C(0x80, 0x80, 0x80) },
        .sTL = { C(0x80, 0x80, 0x80), C(0, 0, 0) },       .sBR = { C(255, 255, 255), C(0xDF, 0xDF, 0xDF) },
        .title = TITLE_GRAD_H, .titleA = C(0, 0, 0x80), .titleB = C(0x10, 0x84, 0xD0), .titleInk = C(255, 255, 255), .fauxBold = true,
        .tabOn = C(0xC0, 0xC0, 0xC0), .tabOff = C(0xC0, 0xC0, 0xC0), .progress = PROG_BLOCKS, .name = "win98", .label = "Win98" },
    [SHS_THEME_MAC7] = { .pal = &THEME_MAC7, .square = true, .title = TITLE_STRIPES_BW, .titleA = C(255, 255, 255), .titleB = C(0, 0, 0),
        .titleInk = C(0, 0, 0), .titleBack = C(255, 255, 255), .fauxBold = true,
        .tabOn = C(0, 0, 0), .tabOff = C(255, 255, 255), .progress = PROG_STRIPES_BW, .name = "mac7", .label = "Mac System 7" },
    [SHS_THEME_PLATINUM] = { .pal = &THEME_PLATINUM, .radius = 0.45f, .title = TITLE_PINSTRIPE, .titleA = C(0xC8, 0xC8, 0xC8), .titleB = C(0xF4, 0xF4, 0xF4),
        .titleInk = C(0, 0, 0), .titleBack = C(0xE4, 0xE4, 0xE4), .gradBtn = true,
        .btnTop = C(255, 255, 255), .btnBot = C(0xC4, 0xC4, 0xC4), .priTop = C(0x8D, 0xC0, 0xF5), .priBot = C(0x2F, 0x6F, 0xCF),
        .tabOn = C(0xF0, 0xF0, 0xF0), .tabOff = C(0xBB, 0xBB, 0xBB), .progress = PROG_POLE, .name = "platinum", .label = "Mac Platinum" },
    [SHS_THEME_CDE] = { .pal = &THEME_CDE, .square = true, .bevel = 3,
        .rTL = { C(0xE2, 0xE4, 0xEE), C(0xD4, 0xD7, 0xE3), C(0xC0, 0xC3, 0xD2) }, .rBR = { C(0x3A, 0x3C, 0x50), C(0x50, 0x53, 0x6A), C(0x6A, 0x6D, 0x85) },
        .sTL = { C(0x3A, 0x3C, 0x50), C(0x50, 0x53, 0x6A), C(0x6A, 0x6D, 0x85) }, .sBR = { C(0xE2, 0xE4, 0xEE), C(0xD4, 0xD7, 0xE3), C(0xC0, 0xC3, 0xD2) },
        .title = TITLE_RAISED, .titleA = C(0x71, 0x8B, 0xA6), .titleInk = C(255, 255, 255), .fauxBold = true,
        .tabOn = C(0xAE, 0xB2, 0xC3), .tabOff = C(0xAE, 0xB2, 0xC3), .progress = PROG_SOLID, .name = "cde", .label = "Linux CDE" },
    [SHS_THEME_GNOME2] = { .pal = &THEME_GNOME2, .radius = 0.4f, .title = TITLE_GRAD_V, .titleA = C(0xF9, 0xEC, 0xD2), .titleB = C(0xE8, 0xB7, 0x68),
        .titleInk = C(0x2E, 0x34, 0x36), .fauxBold = true, .gradBtn = true,
        .btnTop = C(255, 255, 255), .btnBot = C(0xDC, 0xDC, 0xD6), .priTop = C(0xF4, 0xA8, 0x5C), .priBot = C(0xE0, 0x7A, 0x1F),
        .tabOn = C(255, 255, 255), .tabOff = C(0xDC, 0xDC, 0xD6), .tabAccent = C(0xE0, 0x7A, 0x1F), .progress = PROG_GRAD, .name = "gnome2", .label = "Linux GNOME 2" },
};

ShsTheme g_shsTheme = {
    .bg = RGB(0x0B, 0x0B, 0x0C),        .card = RGB(0x16, 0x16, 0x18),
    .cardHover = RGB(0x22, 0x22, 0x25), .border = RGB(0x2E, 0x2E, 0x32), .titleBar = RGB(0x00, 0x00, 0x00),
    .primary = RGB(0x02, 0x84, 0xC7),   .primaryHover = RGB(0x03, 0x69, 0xA1),
    .textMain = RGB(0xF4, 0xF4, 0xF5),  .textMuted = RGB(0x9C, 0x9C, 0xA3),
    .danger = RGB(0xDC, 0x26, 0x26),    .success = RGB(0x22, 0xC5, 0x5E),
    .warning = RGB(0xF5, 0x9E, 0x0B),
};

static ShsThemeMode g_mode = SHS_THEME_AUTO;
static bool         g_dark = true;
static bool         g_flat = false;                 /* square corners, no anti-aliasing, small fonts */
static bool         g_bevel = false;                /* 3D raised / sunken edges */
static const Style *g_style = &STYLES[SHS_THEME_AUTO];
static double       g_nextCheck = 0;

#ifdef _WIN32
  #define OS_CHECK_SECONDS 2.0      /* registry read: cheap */
#else
  #define OS_CHECK_SECONDS 15.0     /* spawns a helper process */
#endif

static void Apply(bool dark)
{
    g_dark = dark;
    g_shsTheme = g_style->pal ? *g_style->pal : (dark ? THEME_DARK : THEME_LIGHT);
}

void ShsThemeSetMode(ShsThemeMode mode)
{
    if ((int)mode < 0 || mode >= SHS_THEME_COUNT) mode = SHS_THEME_AUTO;
    g_mode = mode;
    g_style = &STYLES[mode];
    g_flat = g_style->square;
    g_bevel = g_style->bevel > 0;
    if (mode == SHS_THEME_AUTO) { Apply(ShsSystemPrefersDark()); g_nextCheck = GetTime() + OS_CHECK_SECONDS; }
    else                        Apply(mode == SHS_THEME_DARK);
}

ShsThemeMode ShsThemeGetMode(void) { return g_mode; }
bool         ShsThemeIsDark(void)  { return g_dark; }
bool         ShsThemeFlat(void)    { return g_flat; }
bool         ShsThemeBevel(void)   { return g_bevel; }
bool         ShsThemeClassic(void) { return g_style->pal != NULL; }
const char  *ShsThemeName(ShsThemeMode m)  { return (int)m >= 0 && m < SHS_THEME_COUNT ? STYLES[m].name : "auto"; }
const char  *ShsThemeLabel(ShsThemeMode m) { return (int)m >= 0 && m < SHS_THEME_COUNT ? STYLES[m].label : "System"; }

ShsThemeMode ShsThemeFromName(const char *name)
{
    for (int i = 0; i < SHS_THEME_COUNT; i++)
        if (!strcmp(name, STYLES[i].name)) return (ShsThemeMode)i;
    return SHS_THEME_AUTO;
}

void ShsThemeUpdate(void)
{
    if (g_mode != SHS_THEME_AUTO || GetTime() < g_nextCheck) return;
    g_nextCheck = GetTime() + OS_CHECK_SECONDS;
    bool dark = ShsSystemPrefersDark();
    if (dark != g_dark) Apply(dark);
}

/* ---- fonts -------------------------------------------------------------- */

#define MAX_FONT_PX 96

typedef struct { Font font; bool tried, ok; } SizedFont;

static SizedFont g_fonts[MAX_FONT_PX + 1];      /* indexed by logical text size */
static char      g_fontPath[512];
static float     g_dpi = 1.0f;
static int      *g_glyphs = NULL;
static int       g_glyphCount = 0;

static bool FirstExisting(const char *const *list, char *out, size_t cap)
{
    for (; *list; list++)
        if (FileExists(*list)) { snprintf(out, cap, "%s", *list); return true; }
    return false;
}

static bool FindSystemFont(char *out, size_t cap)
{
#if defined(_WIN32)
    static const char *const names[] = { "segoeui.ttf", "tahoma.ttf", "arial.ttf", NULL };
    static const char *const flatNames[] = { "tahoma.ttf", "arial.ttf", NULL };     /* Flat 98 */
    const char *win = getenv("WINDIR");
    const char *const *list = g_flat ? flatNames : names;
    for (int i = 0; list[i]; i++) {
        char p[600];
        snprintf(p, sizeof p, "%s/Fonts/%s", win ? win : "C:/Windows", list[i]);
        if (FileExists(p)) { snprintf(out, cap, "%s", p); return true; }
    }
    return false;
#elif defined(__APPLE__)
    static const char *const list[] = {
        "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/SFNS.ttf", NULL };
    return FirstExisting(list, out, cap);
#else
    static const char *const list[] = {
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL };
    return FirstExisting(list, out, cap);
#endif
}

static void AddRange(int from, int to)
{
    for (int c = from; c <= to; c++) g_glyphs[g_glyphCount++] = c;
}

static SizedFont *GetFont(int size)
{
    if (size < 1) size = 1;
    if (size > MAX_FONT_PX) size = MAX_FONT_PX;
    SizedFont *sf = &g_fonts[size];
    if (!sf->tried) {
        sf->tried = true;
        if (g_fontPath[0]) {
            int px = (int)lroundf((float)size * g_dpi);
            sf->font = LoadFontEx(g_fontPath, px, g_glyphs, g_glyphCount);
            sf->ok = sf->font.texture.id != 0 && sf->font.glyphCount > 0;
            if (sf->ok) SetTextureFilter(sf->font.texture, g_flat ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
        }
    }
    return sf;
}

void ShsFontsInit(void)
{
    g_dpi = GetWindowScaleDPI().x;
    if (g_dpi < 1.0f) g_dpi = 1.0f;

    g_glyphs = (int *)malloc(1400 * sizeof(int));
    if (!g_glyphs) return;
    g_glyphCount = 0;
    AddRange(32, 126);          /* Basic Latin */
    if (g_flat) AddRange(160, 255);   /* Flat 98: Latin-1 only keeps the glyph atlases small */
    else {
    AddRange(160, 383);         /* Latin-1 + Latin Extended-A */
    AddRange(0x370, 0x3FF);     /* Greek */
    AddRange(0x400, 0x4FF);     /* Cyrillic */
    AddRange(0x2010, 0x2027);   /* dashes, quotes, bullets, ellipsis */
    AddRange(0x20AC, 0x20AC);   /* euro sign */
    }

    if (!FindSystemFont(g_fontPath, sizeof g_fontPath)) g_fontPath[0] = 0;

    static const int common[] = { 12, 13, 14, 15, 16, 18, 20, 22, 26, 0 };
    for (int i = 0; common[i]; i++) GetFont(common[i]);     /* load now, not mid-frame */
}

void ShsFontsShutdown(void)
{
    for (int i = 0; i <= MAX_FONT_PX; i++)
        if (g_fonts[i].ok) UnloadFont(g_fonts[i].font);
    memset(g_fonts, 0, sizeof g_fonts);
    free(g_glyphs);
    g_glyphs = NULL;
}

const char *ShsFontName(void)
{
    return g_fontPath[0] ? GetFileName(g_fontPath) : "built-in";
}

void ShsDrawText(const char *text, int x, int y, int size, Color color)
{
    SizedFont *sf = GetFont(size);
    if (sf->ok) DrawTextEx(sf->font, text, (Vector2){ (float)x, (float)y }, (float)size, 0.0f, color);
    else        DrawText(text, x, y, size, color);
}

int ShsMeasureText(const char *text, int size)
{
    SizedFont *sf = GetFont(size);
    if (sf->ok) return (int)ceilf(MeasureTextEx(sf->font, text, (float)size, 0.0f).x);
    return MeasureText(text, size);
}

/* ---- boxes, bevels, buttons -------------------------------------------------- */

static float RoundNess(Rectangle r, float radius)
{
    float m = r.width < r.height ? r.width : r.height;
    return m > 0 ? fminf(1.0f, 2.0f * radius / m) : 0.0f;
}

static bool SameRGB(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static Color Mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), 0xFF };
}

void ShsDrawRoundedBox(Rectangle r, float radius, Color fill, Color border)
{
    if (g_bevel && fill.a == 255) { ShsDrawBevel(r, true, fill); return; }     /* bevel themes: panels and fields are sunken */
    if (g_flat) {                                    /* square, 1 px border */
        DrawRectangleRec(r, border.a ? border : fill);
        if (border.a) DrawRectangleRec((Rectangle){ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, fill);
        return;
    }
    radius *= g_style->radius;
    if (border.a) DrawRectangleRounded(r, RoundNess(r, radius), 20, border);
    Rectangle in = { r.x + 1, r.y + 1, r.width - 2, r.height - 2 };
    if (in.width > 0 && in.height > 0)
        DrawRectangleRounded(in, RoundNess(in, radius - 1), 20, fill);
}

void ShsRoundRec(Rectangle r, float roundness, int segments, Color c)
{
    if (g_flat) DrawRectangleRec(r, c);
    else        DrawRectangleRounded(r, roundness * g_style->radius, segments, c);
}

void ShsRoundLines(Rectangle r, float roundness, int segments, float thick, Color c)
{
    if (g_flat) DrawRectangleLinesEx(r, thick, c);
    else        DrawRectangleRoundedLinesEx(r, roundness * g_style->radius, segments, thick, c);
}

void ShsDrawBevelFrame(Rectangle r, bool sunken)
{
    const Style *s = g_style;
    const Color *tl = sunken ? s->sTL : s->rTL, *br = sunken ? s->sBR : s->rBR;
    int x = (int)r.x, y = (int)r.y, w = (int)r.width, h = (int)r.height;
    for (int k = 0; k < s->bevel && k < 3; k++) {
        int iw = w - 2 * k, ih = h - 2 * k;
        if (iw < 3 || ih < 3) break;
        DrawRectangle(x + k, y + k, iw - 1, 1, tl[k]);   DrawRectangle(x + k, y + k, 1, ih - 1, tl[k]);
        DrawRectangle(x + k, y + h - 1 - k, iw, 1, br[k]); DrawRectangle(x + w - 1 - k, y + k, 1, ih, br[k]);
    }
}

void ShsDrawBevel(Rectangle r, bool sunken, Color fill)
{
    DrawRectangleRec(r, fill);
    ShsDrawBevelFrame(r, sunken);
}

void ShsDrawButtonBox(Rectangle r, float radius, Color fill, Color border, bool pressed)
{
    const Style *s = g_style;
    const ShsTheme *t = &g_shsTheme;
    if (g_bevel) {
        if (SameRGB(fill, t->card) || (fill.r >= 0xDF && fill.g >= 0xDF && fill.b >= 0xDF)) fill = t->bg;   /* plain buttons are the surface grey */
        ShsDrawBevel(r, pressed, fill);
        return;
    }
    if (s->gradBtn && fill.a == 255 && !SameRGB(fill, t->danger)) {
        bool pri = SameRGB(fill, t->primary) || SameRGB(fill, t->primaryHover);
        Color top = pri ? s->priTop : s->btnTop, bot = pri ? s->priBot : s->btnBot;
        if (!pri && SameRGB(fill, t->cardHover)) { top = Mix(top, WHITE, 0.6f); bot = Mix(bot, WHITE, 0.4f); }   /* hover */
        if (pressed) { Color tmp = top; top = bot; bot = tmp; }
        Color edge = pri ? Mix(s->priBot, BLACK, 0.35f) : border;
        DrawRectangleRounded(r, RoundNess(r, radius * s->radius), 12, edge);
        DrawRectangleGradientV((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, (int)r.height - 2, top, bot);
        return;
    }
    ShsDrawRoundedBox(r, radius, fill, border);
}

/* ---- title bar, tabs, progress ----------------------------------------------- */

Color ShsThemeTitleInk(void)   { return g_style->titleInk; }
Color ShsThemeTitleBack(void)  { return g_style->titleBack; }
bool  ShsThemeFauxBold(void)   { return g_style->fauxBold; }
bool  ShsThemeFolderTabs(void) { return g_style->pal && g_style->title != TITLE_SOLID; }
Color ShsThemeTabFill(void)    { return g_bevel ? g_shsTheme.bg : g_style->tabOn; }

void ShsDrawTitleBar(int w, int h)
{
    const Style *s = g_style;
    switch (s->title) {
        case TITLE_GRAD_H: DrawRectangleGradientH(0, 0, w, h, s->titleA, s->titleB); break;
        case TITLE_RAISED:
            DrawRectangle(0, 0, w, h, s->titleA);
            ShsDrawBevelFrame((Rectangle){ 0, 0, (float)w, (float)h }, false);
            break;
        case TITLE_STRIPES_BW:
            DrawRectangle(0, 0, w, h, s->titleA);
            for (int y = 6; y < h - 8; y += 2) DrawRectangle(4, y, w - 8, 1, s->titleB);
            DrawRectangle(0, h - 2, w, 2, s->titleB);
            break;
        case TITLE_PINSTRIPE:
            DrawRectangle(0, 0, w, h, s->titleA);
            for (int y = 1; y < h - 1; y += 2) DrawRectangle(0, y, w, 1, s->titleB);
            DrawRectangle(0, h - 1, w, 1, g_shsTheme.border);
            break;
        case TITLE_GRAD_V:
            DrawRectangleGradientV(0, 0, w, h, s->titleA, s->titleB);
            DrawRectangle(0, h - 1, w, 1, Mix(s->titleB, BLACK, 0.4f));
            break;
        default: DrawRectangle(0, 0, w, h, s->titleA); break;
    }
}

/* Draws one page tab; returns the text colour to use on it. */
Color ShsDrawTab(Rectangle r, bool active)
{
    const Style *s = g_style;
    const ShsTheme *t = &g_shsTheme;
    if (g_bevel) { ShsDrawBevel(r, false, t->bg); return t->textMain; }
    Color fill = active ? s->tabOn : s->tabOff;
    Color ink = (s->progress == PROG_STRIPES_BW && active) ? (Color){ 255, 255, 255, 255 } : t->textMain;
    if (g_flat) {
        ShsDrawRoundedBox(r, 0, fill, t->border);
    } else {
        ShsDrawRoundedBox((Rectangle){ r.x, r.y, r.width, r.height + 8 }, 7.0f, fill, t->border);
        DrawRectangle((int)r.x, (int)(r.y + r.height), (int)r.width, 9, t->bg);   /* square off the bottom */
    }
    if (s->tabAccent.a && active) DrawRectangle((int)r.x + 2, (int)r.y + 1, (int)r.width - 4, 2, s->tabAccent);
    return ink;
}

bool ShsDrawProgress(Rectangle tr, float p, Color col)
{
    const Style *s = g_style;
    const ShsTheme *t = &g_shsTheme;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    switch (s->progress) {
        case PROG_BLOCKS: {
            ShsDrawBevel(tr, true, t->card);
            float fillW = (tr.width - 6.0f) * p;
            for (float bx = tr.x + 3; bx + 7.0f <= tr.x + 3 + fillW + 0.5f; bx += 8.0f)
                DrawRectangle((int)bx, (int)tr.y + 3, 7, (int)tr.height - 6, col);
        } return true;
        case PROG_SOLID:
            ShsDrawBevel(tr, true, t->card);
            DrawRectangle((int)tr.x + 3, (int)tr.y + 3, (int)((tr.width - 6.0f) * p), (int)tr.height - 6, col);
            return true;
        case PROG_STRIPES_BW: {
            DrawRectangleRec(tr, t->border);
            DrawRectangle((int)tr.x + 1, (int)tr.y + 1, (int)tr.width - 2, (int)tr.height - 2, WHITE);
            float fillW = (tr.width - 6.0f) * p;
            for (float bx = tr.x + 3; bx + 2.0f <= tr.x + 3 + fillW + 0.5f; bx += 4.0f)
                DrawRectangle((int)bx, (int)tr.y + 3, 2, (int)tr.height - 6, t->border);
        } return true;
        case PROG_POLE: {
            ShsDrawRoundedBox(tr, 14.0f, WHITE, t->border);
            if (p <= 0) return true;
            Rectangle f = { tr.x + 2, tr.y + 2, fmaxf(tr.height - 4, (tr.width - 4) * p), tr.height - 4 };
            DrawRectangleRounded(f, RoundNess(f, f.height / 2), 12, col);
            Color lt = Mix(col, WHITE, 0.35f);
            for (float sx = f.x - f.height; sx < f.x + f.width; sx += 11.0f) {          /* diagonal stripes, clipped by hand */
                float t0 = fmaxf(0.0f, f.x - sx), t1 = fminf(f.height, f.x + f.width - sx);
                if (t1 - t0 < 1.0f) continue;
                DrawLineEx((Vector2){ sx + t0, f.y + f.height - t0 }, (Vector2){ sx + t1, f.y + f.height - t1 }, 3.0f, lt);
            }
        } return true;
        case PROG_GRAD: {
            ShsDrawRoundedBox(tr, 6.0f, WHITE, t->border);
            if (p <= 0) return true;
            DrawRectangleGradientV((int)tr.x + 2, (int)tr.y + 2, (int)fmaxf(tr.height - 4, (tr.width - 4) * p), (int)tr.height - 4, Mix(col, WHITE, 0.4f), col);
        } return true;
        default: return false;
    }
}

/* Moving the window to a monitor with a different scale leaves the glyph atlases at the old pixel size, which draws
   jagged text (point filtering in the retro themes). Rebuild them when the scale changes. */
void ShsFontsCheckDpi(void)
{
    float dpi = GetWindowScaleDPI().x;
    if (dpi < 1.0f) dpi = 1.0f;
    if (!g_glyphs || fabsf(dpi - g_dpi) < 0.01f) return;
    for (int i = 0; i <= MAX_FONT_PX; i++)
        if (g_fonts[i].ok) UnloadFont(g_fonts[i].font);
    memset(g_fonts, 0, sizeof g_fonts);
    g_dpi = dpi;
    static const int common[] = { 12, 13, 14, 15, 16, 18, 20, 22, 26, 0 };
    for (int i = 0; common[i]; i++) GetFont(common[i]);
}
