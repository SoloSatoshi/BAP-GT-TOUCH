#ifndef NAV_ICONS_H
#define NAV_ICONS_H

#include "lvgl.h"
#include <stdbool.h>

#define NAV_ICON_BITCOIN "@btc"
#define NAV_ICON_WEATHER "@weather"
#define NAV_ICON_CHART "@chart"

bool nav_icon_render(lv_obj_t *btn, const char *symbol, lv_color_t color);

#endif
