#include "shs_icons.h"

#include <math.h>

#define DEG2RADF 0.017453292f

static Vector2 V(float cx, float cy, float u, float x, float y)
{
    return (Vector2){ cx + x * u, cy + y * u };
}

/* Arrowhead on a circle of radius r (in units u) at `deg`, pointing clockwise. */
static void ArcArrow(float cx, float cy, float u, float r, float deg, Color c)
{
    float a = deg * DEG2RADF, co = cosf(a), si = sinf(a);
    Vector2 p = { cx + r * u * co, cy + r * u * si };
    Vector2 tan = { -si, co }, rad = { co, si };
    Vector2 tip = { p.x + tan.x * 3.6f * u, p.y + tan.y * 3.6f * u };
    Vector2 b1 = { p.x + rad.x * 3.2f * u, p.y + rad.y * 3.2f * u };
    Vector2 b2 = { p.x - rad.x * 3.2f * u, p.y - rad.y * 3.2f * u };
    DrawTriangle(tip, b2, b1, c);      /* raylib culls by winding, so draw both orders */
    DrawTriangle(tip, b1, b2, c);
}

void ShsDrawIcon(ShsIcon icon, float cx, float cy, float size, Color c)
{
    const float u = size / 16.0f;          /* 1 unit = 1 px at the nominal 16 px size */
    const float t = 1.8f * u;              /* stroke */

    switch (icon) {
    case SHS_ICON_PLUS:
        DrawLineEx(V(cx, cy, u, -6, 0), V(cx, cy, u, 6, 0), t, c);
        DrawLineEx(V(cx, cy, u, 0, -6), V(cx, cy, u, 0, 6), t, c);
        break;

    case SHS_ICON_SEARCH:
        DrawRing(V(cx, cy, u, -1.5f, -1.5f), 4.0f * u, 5.8f * u, 0, 360, 28, c);
        DrawLineEx(V(cx, cy, u, 2.8f, 2.8f), V(cx, cy, u, 7, 7), t * 1.2f, c);
        break;

    case SHS_ICON_GEAR: {
        for (int i = 0; i < 8; i++) {
            float a = i * 45.0f;
            Rectangle r = { cx, cy, 3.0f * u, 3.2f * u };       /* tooth at radius 4.3..7.5 */
            DrawRectanglePro(r, (Vector2){ 1.5f * u, 7.5f * u }, a, c);
        }
        DrawRing((Vector2){ cx, cy }, 3.0f * u, 5.6f * u, 0, 360, 28, c);
    } break;

    case SHS_ICON_TASKS:
        for (int i = 0; i < 3; i++) {
            float y = -5.0f + i * 5.0f;
            DrawCircleV(V(cx, cy, u, -6, y), 1.3f * u, c);
            DrawLineEx(V(cx, cy, u, -2, y), V(cx, cy, u, 7, y), t, c);
        }
        break;

    case SHS_ICON_TRAFFIC: {
        Vector2 p[6] = { V(cx, cy, u, -8, 0), V(cx, cy, u, -4, 0), V(cx, cy, u, -2, -6),
                         V(cx, cy, u, 2, 6),  V(cx, cy, u, 4, 0),  V(cx, cy, u, 8, 0) };
        for (int i = 0; i < 5; i++) DrawLineEx(p[i], p[i + 1], t, c);
    } break;

    case SHS_ICON_LOGS: {
        Rectangle r = { cx - 5.5f * u, cy - 7.5f * u, 11 * u, 15 * u };
        DrawRectangleLinesEx(r, t, c);
        for (int i = 0; i < 3; i++)
            DrawLineEx(V(cx, cy, u, -2.5f, -3.0f + i * 3.5f), V(cx, cy, u, 2.5f, -3.0f + i * 3.5f), 1.4f * u, c);
    } break;

    case SHS_ICON_INFO:
        DrawRing((Vector2){ cx, cy }, 5.6f * u, 7.4f * u, 0, 360, 32, c);
        DrawCircleV(V(cx, cy, u, 0, -3.1f), 1.1f * u, c);
        DrawLineEx(V(cx, cy, u, 0, -0.6f), V(cx, cy, u, 0, 3.6f), t, c);
        break;

    case SHS_ICON_PAUSE:
        DrawRectangleRec((Rectangle){ cx - 5.0f * u, cy - 6.0f * u, 3.4f * u, 12 * u }, c);
        DrawRectangleRec((Rectangle){ cx + 1.6f * u, cy - 6.0f * u, 3.4f * u, 12 * u }, c);
        break;

    case SHS_ICON_PLAY:
        DrawTriangle(V(cx, cy, u, -4, -6.5f), V(cx, cy, u, -4, 6.5f), V(cx, cy, u, 6, 0), c);
        break;

    case SHS_ICON_SYNC:
        DrawRing((Vector2){ cx, cy }, 4.6f * u, 6.4f * u, 200, 330, 16, c);
        DrawRing((Vector2){ cx, cy }, 4.6f * u, 6.4f * u, 20, 150, 16, c);
        ArcArrow(cx, cy, u, 5.5f, 326, c);
        ArcArrow(cx, cy, u, 5.5f, 146, c);
        break;

    case SHS_ICON_SORT:
        DrawLineEx(V(cx, cy, u, -7, -4.5f), V(cx, cy, u, 7, -4.5f), t, c);
        DrawLineEx(V(cx, cy, u, -7, 0), V(cx, cy, u, 3, 0), t, c);
        DrawLineEx(V(cx, cy, u, -7, 4.5f), V(cx, cy, u, -1, 4.5f), t, c);
        break;

    case SHS_ICON_GRID:
        for (int i = 0; i < 4; i++) {
            float x = (i & 1) ? 1.0f : -6.0f, y = (i & 2) ? 1.0f : -6.0f;
            DrawRectangleRec((Rectangle){ cx + x * u, cy + y * u, 5.0f * u, 5.0f * u }, c);
        }
        break;

    case SHS_ICON_WARN: {
        Vector2 a = V(cx, cy, u, 0, -7), b = V(cx, cy, u, -7.5f, 6), d = V(cx, cy, u, 7.5f, 6);
        DrawLineEx(a, b, t, c); DrawLineEx(b, d, t, c); DrawLineEx(d, a, t, c);
        DrawLineEx(V(cx, cy, u, 0, -2.5f), V(cx, cy, u, 0, 1.5f), t, c);
        DrawCircleV(V(cx, cy, u, 0, 3.8f), 1.0f * u, c);
    } break;

    case SHS_ICON_PENCIL:
        DrawLineEx(V(cx, cy, u, -3, 3), V(cx, cy, u, 5.5f, -5.5f), 3.6f * u, c);
        DrawTriangle(V(cx, cy, u, -7, 7), V(cx, cy, u, -2.5f, 5.5f), V(cx, cy, u, -5.5f, 2.5f), c);
        break;

    case SHS_ICON_TRASH:
        DrawLineEx(V(cx, cy, u, -7, -4.5f), V(cx, cy, u, 7, -4.5f), t, c);
        DrawRectangleLinesEx((Rectangle){ cx - 5 * u, cy - 3.5f * u, 10 * u, 11 * u }, t, c);
        DrawLineEx(V(cx, cy, u, -2, -7), V(cx, cy, u, 2, -7), t, c);
        break;

    case SHS_ICON_CLOSE:
        DrawLineEx(V(cx, cy, u, -5.5f, -5.5f), V(cx, cy, u, 5.5f, 5.5f), t * 1.1f, c);
        DrawLineEx(V(cx, cy, u, 5.5f, -5.5f), V(cx, cy, u, -5.5f, 5.5f), t * 1.1f, c);
        break;

    case SHS_ICON_CHECK:
        DrawLineEx(V(cx, cy, u, -6, 0.5f), V(cx, cy, u, -2, 4.5f), t * 1.2f, c);
        DrawLineEx(V(cx, cy, u, -2, 4.5f), V(cx, cy, u, 6.5f, -5), t * 1.2f, c);
        break;

    case SHS_ICON_FOLDER:
        DrawRectangleLinesEx((Rectangle){ cx - 7.5f * u, cy - 4 * u, 15 * u, 11 * u }, t, c);
        DrawRectangleRec((Rectangle){ cx - 7.5f * u, cy - 6.5f * u, 6 * u, 3 * u }, c);
        break;

    case SHS_ICON_CHEVRON_DOWN:
        DrawLineEx(V(cx, cy, u, -5, -2.5f), V(cx, cy, u, 0, 2.5f), t, c);
        DrawLineEx(V(cx, cy, u, 0, 2.5f), V(cx, cy, u, 5, -2.5f), t, c);
        break;

    case SHS_ICON_CHEVRON_UP:
        DrawLineEx(V(cx, cy, u, -5, 2.5f), V(cx, cy, u, 0, -2.5f), t, c);
        DrawLineEx(V(cx, cy, u, 0, -2.5f), V(cx, cy, u, 5, 2.5f), t, c);
        break;

    case SHS_ICON_WATCH:
        DrawEllipseLines((int)cx, (int)cy, 8.0f * u, 5.0f * u, c);
        DrawEllipseLines((int)cx, (int)cy, 7.0f * u, 4.0f * u, c);
        DrawCircleV((Vector2){ cx, cy }, 2.6f * u, c);
        break;

    case SHS_ICON_COPY:
        /* back page: only the parts not covered by the front page */
        DrawLineEx(V(cx, cy, u, -7, 3), V(cx, cy, u, -7, -7), t, c);
        DrawLineEx(V(cx, cy, u, -7, -7), V(cx, cy, u, 3, -7), t, c);
        DrawLineEx(V(cx, cy, u, 3, -7), V(cx, cy, u, 3, -3), t, c);
        DrawLineEx(V(cx, cy, u, -7, 3), V(cx, cy, u, -3, 3), t, c);
        DrawRectangleLinesEx((Rectangle){ cx - 3 * u, cy - 3 * u, 10 * u, 10 * u }, t, c);
        break;

    default: break;
    }
}
