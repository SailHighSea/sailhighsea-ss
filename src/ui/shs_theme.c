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
static double       g_nextCheck = 0;

#ifdef _WIN32
  #define OS_CHECK_SECONDS 2.0      /* registry read: cheap */
#else
  #define OS_CHECK_SECONDS 15.0     /* spawns a helper process */
#endif

static void Apply(bool dark)
{
    g_dark = dark;
    g_shsTheme = dark ? THEME_DARK : THEME_LIGHT;
}

void ShsThemeSetMode(ShsThemeMode mode)
{
    g_mode = mode;
    if (mode == SHS_THEME_AUTO) { Apply(ShsSystemPrefersDark()); g_nextCheck = GetTime() + OS_CHECK_SECONDS; }
    else                        Apply(mode == SHS_THEME_DARK);
}

ShsThemeMode ShsThemeGetMode(void) { return g_mode; }
bool         ShsThemeIsDark(void)  { return g_dark; }

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
    const char *win = getenv("WINDIR");
    for (int i = 0; names[i]; i++) {
        char p[600];
        snprintf(p, sizeof p, "%s/Fonts/%s", win ? win : "C:/Windows", names[i]);
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
            if (sf->ok) SetTextureFilter(sf->font.texture, TEXTURE_FILTER_BILINEAR);
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
    AddRange(160, 383);         /* Latin-1 + Latin Extended-A */
    AddRange(0x370, 0x3FF);     /* Greek */
    AddRange(0x400, 0x4FF);     /* Cyrillic */
    AddRange(0x2010, 0x2027);   /* dashes, quotes, bullets, ellipsis */
    AddRange(0x20AC, 0x20AC);   /* euro sign */

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

/* ---- rounded boxes ------------------------------------------------------ */

static float RoundNess(Rectangle r, float radius)
{
    float m = r.width < r.height ? r.width : r.height;
    return m > 0 ? fminf(1.0f, 2.0f * radius / m) : 0.0f;
}

void ShsDrawRoundedBox(Rectangle r, float radius, Color fill, Color border)
{
    if (border.a) DrawRectangleRounded(r, RoundNess(r, radius), 20, border);
    Rectangle in = { r.x + 1, r.y + 1, r.width - 2, r.height - 2 };
    if (in.width > 0 && in.height > 0)
        DrawRectangleRounded(in, RoundNess(in, radius - 1), 20, fill);
}
