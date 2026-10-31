#ifndef SHS_THEME_H
#define SHS_THEME_H

#include "raylib.h"

/* Runtime palette: SHS_COLOR_* read the active theme, so call sites do not change when the
   user (or the OS) switches between light and dark. Dark values are shared with SailHighSea-FireWall. */
typedef struct {
    Color bg, card, cardHover, border;
    Color titleBar;                     /* brand-blue title bar, drawn with white text */
    Color primary, primaryHover;
    Color textMain, textMuted;
    Color danger, success, warning;
} ShsTheme;

extern ShsTheme g_shsTheme;

#define SHS_COLOR_BG            (g_shsTheme.bg)
#define SHS_COLOR_CARD          (g_shsTheme.card)
#define SHS_COLOR_CARD_HOVER    (g_shsTheme.cardHover)
#define SHS_COLOR_BORDER        (g_shsTheme.border)
#define SHS_COLOR_TITLEBAR      (g_shsTheme.titleBar)
#define SHS_COLOR_PRIMARY       (g_shsTheme.primary)
#define SHS_COLOR_PRIMARY_HOVER (g_shsTheme.primaryHover)
#define SHS_COLOR_TEXT_MAIN     (g_shsTheme.textMain)
#define SHS_COLOR_TEXT_MUTED    (g_shsTheme.textMuted)
#define SHS_COLOR_DANGER        (g_shsTheme.danger)
#define SHS_COLOR_SUCCESS       (g_shsTheme.success)
#define SHS_COLOR_WARNING       (g_shsTheme.warning)

/* Text drawn on top of primary / danger fills; white in both themes. */
#define SHS_COLOR_ON_PRIMARY    CLITERAL(Color){ 0xFF, 0xFF, 0xFF, 0xFF }

typedef enum {
    SHS_THEME_AUTO = 0, SHS_THEME_LIGHT, SHS_THEME_DARK,
    SHS_THEME_FLAT98, SHS_THEME_WIN98, SHS_THEME_MAC7, SHS_THEME_PLATINUM, SHS_THEME_CDE, SHS_THEME_GNOME2,
    SHS_THEME_COUNT
} ShsThemeMode;

void         ShsThemeSetMode(ShsThemeMode mode);   /* AUTO follows the operating system */
ShsThemeMode ShsThemeGetMode(void);
bool         ShsThemeIsDark(void);                 /* the palette currently in use */
bool         ShsThemeFlat(void);                   /* Flat 98 and Win98: square corners, no anti-aliasing (needs a restart to switch on or off) */
bool         ShsThemeBevel(void);                  /* Win98 / CDE: raised and sunken 3D edges */
bool         ShsThemeClassic(void);                /* any retro theme (Flat 98 .. GNOME 2) */
const char  *ShsThemeName(ShsThemeMode m);         /* settings-file key */
const char  *ShsThemeLabel(ShsThemeMode m);        /* name shown in Settings */
ShsThemeMode ShsThemeFromName(const char *name);   /* SHS_THEME_AUTO when unknown */
void         ShsThemeUpdate(void);                 /* call once per frame; re-checks the OS in AUTO mode */

/* System UI font (Segoe UI / Helvetica / DejaVu ...), falling back to raylib's built-in font.
   Call ShsFontsInit() after InitWindow() and ShsFontsShutdown() before CloseWindow(). */
void ShsFontsInit(void);
void ShsFontsShutdown(void);
void ShsFontsCheckDpi(void);                       /* call each frame: rebuilds the glyph atlases when the window moves to a monitor with another scale */
/* Rounded box with a crisp 1 px border: border-coloured rounded rect with the fill inset by 1 px
   (avoids the uneven corners of DrawRectangleRoundedLines). `radius` is in pixels. */
void ShsDrawRoundedBox(Rectangle r, float radius, Color fill, Color border);

/* Win98 3D edges. ShsDrawBevel fills and frames; ShsDrawBevelFrame frames only. */
void ShsDrawBevel(Rectangle r, bool sunken, Color fill);
void ShsDrawBevelFrame(Rectangle r, bool sunken);
/* Retro chrome (title bar, page tabs, progress bar) for the classic themes. */
void  ShsDrawTitleBar(int w, int h);
Color ShsThemeTitleInk(void);
Color ShsThemeTitleBack(void);      /* patch drawn behind the title text on striped bars (alpha 0 = none) */
bool  ShsThemeFauxBold(void);
bool  ShsThemeFolderTabs(void);     /* tabs drawn as folder tabs instead of an underline */
Color ShsThemeTabFill(void);        /* colour of the selected tab, to open it into the page */
Color ShsDrawTab(Rectangle r, bool active);                 /* returns the text colour */
bool  ShsDrawProgress(Rectangle track, float p, Color col); /* false: the caller draws the default bar */

/* A push button: ShsDrawRoundedBox normally, a raised / pressed 3D or gradient button in the retro themes. */
void ShsDrawButtonBox(Rectangle r, float radius, Color fill, Color border, bool pressed);

/* Drop-ins for DrawRectangleRounded / DrawRectangleRoundedLinesEx: plain rectangles in the flat theme. */
void ShsRoundRec(Rectangle r, float roundness, int segments, Color c);
void ShsRoundLines(Rectangle r, float roundness, int segments, float thick, Color c);

const char *ShsFontName(void);                     /* file name of the font in use, or "built-in" */
void ShsDrawText(const char *text, int x, int y, int size, Color color);
int  ShsMeasureText(const char *text, int size);

#endif /* SHS_THEME_H */
