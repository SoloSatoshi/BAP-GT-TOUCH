#include "keyboard_theme.h"
#include "home.h"

void keyboard_theme_apply(lv_obj_t *kb)
{
    if (!kb)
    {
        return;
    }

    lv_color_t panel_bg = COLOR_CARD_BG;
    lv_color_t key_bg = ui_theme_get_surface_fill_color();
    lv_color_t key_border = COLOR_NAV_ICON;
    lv_color_t key_text = COLOR_TEXT_PRIMARY;
    lv_color_t active_bg = COLOR_NAV_ICON;
    lv_color_t active_text = COLOR_TEXT_ON_ACCENT;

    lv_obj_set_style_bg_color(kb, panel_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_80, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, key_border, LV_PART_ITEMS);
    lv_obj_set_style_border_opa(kb, LV_OPA_70, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, key_text, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(kb, active_bg, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(kb, active_bg, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, active_text, LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_set_style_bg_color(kb, active_bg, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(kb, active_bg, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, active_text, LV_PART_ITEMS | LV_STATE_CHECKED);
}
