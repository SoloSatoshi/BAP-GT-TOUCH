#include "block.h"
#include "home.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "clock.h"
#include "price.h"
#include "weather.h"
#include "mempool.h"
#include "background.h"
#include "nav_icons.h"
#include "navigation_guard.h"
#include "custom_fonts.h"
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *block_screen = NULL;
static lv_obj_t *block_value_card = NULL;
static lv_obj_t *block_height_label = NULL;
static lv_obj_t *block_height_shadow_label = NULL;
static lv_obj_t *block_title_label = NULL;

static char current_block_height_text[24] = "0000000";

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void apply_cached_block_height(void);

void block_screen_create(void)
{
    if (block_screen != NULL)
    {
        return;
    }

    const ui_theme_t theme = ui_theme_get_current();
    const bool cyberpunk = theme == UI_THEME_CYBERPUNK;
    const bool bitaxe_red = theme == UI_THEME_BITAXE_RED;
    const bool woods = theme == UI_THEME_WOODS;

    block_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(block_screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(block_screen, LV_OPA_COVER, 0);
    screen_background_apply(block_screen);
    lv_obj_clear_flag(block_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(block_screen, LV_SCROLLBAR_MODE_OFF);

    block_title_label = lv_label_create(block_screen);
    lv_label_set_text(block_title_label, "CURRENT BLOCK HEIGHT");
    lv_obj_set_style_text_color(block_title_label, (cyberpunk || woods) ? COLOR_NAV_ICON : COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(block_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(block_title_label, LV_ALIGN_TOP_MID, 0, 30);

    block_value_card = lv_obj_create(block_screen);
    lv_obj_set_size(block_value_card, SCREEN_WIDTH - 180, 180);
    lv_obj_align(block_value_card, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(block_value_card, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(block_value_card, (bitaxe_red || woods) ? LV_OPA_TRANSP : LV_OPA_20, 0);
    lv_obj_set_style_border_width(block_value_card, 0, 0);
    lv_obj_set_style_radius(block_value_card, 34, 0);
    lv_obj_set_style_shadow_width(block_value_card, 0, 0);
    lv_obj_set_style_pad_all(block_value_card, 0, 0);
    lv_obj_clear_flag(block_value_card, LV_OBJ_FLAG_SCROLLABLE);

    block_height_shadow_label = lv_label_create(block_screen);
    lv_label_set_text(block_height_shadow_label, current_block_height_text);
    lv_obj_set_style_text_color(block_height_shadow_label, cyberpunk ? lv_color_hex(0x4A1800) : lv_color_black(), 0);
    lv_obj_set_style_text_opa(block_height_shadow_label, cyberpunk ? LV_OPA_80 : LV_OPA_50, 0);
    lv_obj_set_style_text_font(block_height_shadow_label, &montserrat_140, 0);
    lv_obj_set_style_text_letter_space(block_height_shadow_label, 15, 0);
    lv_obj_set_style_text_align(block_height_shadow_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(block_height_shadow_label, SCREEN_WIDTH - 180);
    lv_obj_align(block_height_shadow_label, LV_ALIGN_CENTER, 4, -4);

    block_height_label = lv_label_create(block_screen);
    lv_label_set_text(block_height_label, current_block_height_text);
    lv_obj_set_style_text_color(block_height_label, cyberpunk ? COLOR_NAV_ICON : COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(block_height_label, &montserrat_140, 0);
    lv_obj_set_style_text_letter_space(block_height_label, 15, 0);
    lv_obj_set_style_text_align(block_height_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(block_height_label, SCREEN_WIDTH - 180);
    lv_obj_align(block_height_label, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *bottom_nav = lv_obj_create(block_screen);
    lv_obj_set_size(bottom_nav, SCREEN_WIDTH, 64);
    lv_obj_align(bottom_nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_nav, COLOR_NAV_BG, 0);
    lv_obj_set_style_bg_opa(bottom_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_nav, 0, 0);
    lv_obj_set_style_radius(bottom_nav, 0, 0);
    lv_obj_set_style_pad_all(bottom_nav, 8, 0);
    lv_obj_clear_flag(bottom_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bottom_nav, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(bottom_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, block_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, NULL, true);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, block_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, block_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_BITCOIN, block_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_WEATHER, block_weather_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_CHART, block_night_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, block_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, block_settings_clicked, false);

    apply_cached_block_height();
}

void block_screen_destroy(void)
{
    if (block_screen)
    {
        lv_obj_del(block_screen);
        block_screen = NULL;
        block_value_card = NULL;
        block_height_label = NULL;
        block_height_shadow_label = NULL;
        block_title_label = NULL;
    }
}

lv_obj_t *block_get_screen(void)
{
    return block_screen;
}

void block_update_height(const char *height)
{
    if (!height)
    {
        return;
    }

    long parsed_height = strtol(height, NULL, 10);
    if (parsed_height > 0)
    {
        parsed_height -= 1;
    }
    snprintf(current_block_height_text, sizeof(current_block_height_text), "%ld", parsed_height);

    if (block_height_shadow_label)
    {
        lv_label_set_text(block_height_shadow_label, current_block_height_text);
    }

    if (block_height_label)
    {
        lv_label_set_text(block_height_label, current_block_height_text);
    }
}

static void apply_cached_block_height(void)
{
    if (block_height_shadow_label)
    {
        lv_label_set_text(block_height_shadow_label, current_block_height_text);
    }

    if (block_height_label)
    {
        lv_label_set_text(block_height_label, current_block_height_text);
    }
}

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 46);
    lv_obj_set_style_bg_color(btn, COLOR_NAV_ICON, 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_NAV_ICON, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (!nav_icon_render(btn, symbol, COLOR_NAV_ICON))
    {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, symbol);
        lv_obj_set_style_text_color(label, COLOR_NAV_ICON, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_center(label);
    }

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, ui_navigation_guarded_click_cb, LV_EVENT_CLICKED, ui_navigation_make_user_data(event_cb));
    }

    return btn;
}

static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 46);
    lv_obj_set_style_bg_color(btn, COLOR_NAV_ICON, 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_NAV_ICON, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, img_dsc);
    lv_obj_set_style_img_recolor(img, COLOR_NAV_ICON, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, ui_navigation_guarded_click_cb, LV_EVENT_CLICKED, ui_navigation_make_user_data(event_cb));
    }

    return btn;
}

void block_home_clicked(lv_event_t *e)
{
    home_screen_create();
    lv_scr_load(home_get_screen());
    block_screen_destroy();
}

void block_clock_clicked(lv_event_t *e)
{
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    block_screen_destroy();
}

void block_mempool_clicked(lv_event_t *e)
{
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    block_screen_destroy();
}

void block_price_clicked(lv_event_t *e)
{
    price_screen_create();
    lv_scr_load(price_get_screen());
    block_screen_destroy();
}

void block_weather_clicked(lv_event_t *e)
{
    weather_screen_create();
    lv_scr_load(weather_get_screen());
    block_screen_destroy();
}

void block_wifi_clicked(lv_event_t *e)
{
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    block_screen_destroy();
}

void block_settings_clicked(lv_event_t *e)
{
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    block_screen_destroy();
}

void block_night_clicked(lv_event_t *e)
{
    night_screen_create();
    lv_scr_load(night_get_screen());
    block_screen_destroy();
}
