#ifndef SHS_ICONS_H
#define SHS_ICONS_H

#include "raylib.h"

/* Small line symbols drawn with raylib shapes, so they scale and follow the theme colours. */
typedef enum {
    SHS_ICON_NONE = -1,
    SHS_ICON_PLUS,
    SHS_ICON_SEARCH,
    SHS_ICON_GEAR,
    SHS_ICON_TASKS,      /* list */
    SHS_ICON_TRAFFIC,    /* pulse line */
    SHS_ICON_LOGS,       /* document */
    SHS_ICON_INFO,
    SHS_ICON_PAUSE,
    SHS_ICON_PLAY,
    SHS_ICON_SYNC,       /* two circular arrows */
    SHS_ICON_SORT,
    SHS_ICON_GRID,
    SHS_ICON_WARN,
    SHS_ICON_PENCIL,
    SHS_ICON_TRASH,
    SHS_ICON_CLOSE,
    SHS_ICON_CHECK,
    SHS_ICON_FOLDER,
    SHS_ICON_CHEVRON_DOWN,
    SHS_ICON_CHEVRON_UP,
    SHS_ICON_WATCH,      /* eye: auto-sync / watching */
    SHS_ICON_COPY        /* two overlapping pages */
} ShsIcon;

/* Draws `icon` centred on (cx, cy) inside a size x size box. */
void ShsDrawIcon(ShsIcon icon, float cx, float cy, float size, Color color);

#endif /* SHS_ICONS_H */
