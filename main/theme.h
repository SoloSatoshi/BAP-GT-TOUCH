#ifndef THEME_H
#define THEME_H

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    UI_THEME_BITAXE_RED = 0,
    UI_THEME_MONOCHROME,
    UI_THEME_SPACE,
    UI_THEME_CYBERPUNK,
    UI_THEME_COUNT
} ui_theme_t;

void ui_theme_init(void);
ui_theme_t ui_theme_get_current(void);
void ui_theme_set_current(ui_theme_t theme);
bool ui_theme_save_current(void);
const char *ui_theme_get_options(void);
uint16_t ui_theme_get_dropdown_index(ui_theme_t theme);
ui_theme_t ui_theme_from_dropdown_index(uint16_t index);

lv_color_t ui_theme_get_background_color(void);
lv_color_t ui_theme_get_card_bg_color(void);
lv_color_t ui_theme_get_accent_color(void);
lv_color_t ui_theme_get_red_color(void);
lv_color_t ui_theme_get_text_primary_color(void);
lv_color_t ui_theme_get_text_secondary_color(void);
lv_color_t ui_theme_get_text_on_accent_color(void);
lv_color_t ui_theme_get_border_color(void);
lv_color_t ui_theme_get_nav_bg_color(void);
lv_color_t ui_theme_get_nav_icon_color(void);
lv_color_t ui_theme_get_surface_fill_color(void);
lv_opa_t ui_theme_get_surface_fill_opa(void);
lv_color_t ui_theme_get_surface_outline_color(void);
bool ui_theme_uses_wallpaper(void);

#endif
