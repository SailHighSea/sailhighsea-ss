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

typedef enum { SHS_THEME_AUTO = 0, SHS_THEME_LIGHT, SHS_THEME_DARK } ShsThemeMode;

void         ShsThemeSetMode(ShsThemeMode mode);   /* AUTO follows the operating system */
ShsThemeMode ShsThemeGetMode(void);
bool         ShsThemeIsDark(void);                 /* the palette currently in use */
void         ShsThemeUpdate(void);                 /* call once per frame; re-checks the OS in AUTO mode */

/* System UI font (Segoe UI / Helvetica / DejaVu ...), falling back to raylib's built-in font.
   Call ShsFontsInit() after InitWindow() and ShsFontsShutdown() before CloseWindow(). */
void ShsFontsInit(void);
void ShsFontsShutdown(void);
/* Rounded box with a crisp 1 px border: border-coloured rounded rect with the fill inset by 1 px
   (avoids the uneven corners of DrawRectangleRoundedLines). `radius` is in pixels. */
void ShsDrawRoundedBox(Rectangle r, float radius, Color fill, Color border);

const char *ShsFontName(void);                     /* file name of the font in use, or "built-in" */
void ShsDrawText(const char *text, int x, int y, int size, Color color);
int  ShsMeasureText(const char *text, int size);

#endif /* SHS_THEME_H */
