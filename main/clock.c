#include "clock.h"
#include "home.h"
#include "block.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "price.h"
#include "weather.h"
#include "mempool.h"
#include "background.h"
#include "custom_fonts.h"
#include "esp_timer.h"
#include "lwip/apps/sntp.h"
#include <time.h>

static lv_obj_t *clock_screen = NULL;
static lv_obj_t *clock_value_card = NULL;
static lv_obj_t *clock_time_shadow_cont = NULL;
static lv_obj_t *clock_time_cont = NULL;
static lv_obj_t *clock_time_shadow_label = NULL;
static lv_obj_t *clock_time_label = NULL;
static lv_obj_t *clock_ampm_shadow_label = NULL;
static lv_obj_t *clock_ampm_label = NULL;
static lv_obj_t *clock_date_label = NULL;
static lv_obj_t *clock_tap_area = NULL;
static lv_timer_t *clock_timer = NULL;
static bool clock_content_hidden = false;

static char current_time_text[16] = "--:--";
static char current_ampm_text[4] = "--";
static char current_date_text[32] = "Waiting for time sync";

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void clock_start_sntp(void);
static void clock_update_time_text(void);
static void clock_timer_cb(lv_timer_t *timer);
static void clock_toggle_content_clicked(lv_event_t *e);

void clock_screen_create(void)
{
    if (clock_screen != NULL)
    {
        return;
    }

    const bool cyberpunk = ui_theme_get_current() == UI_THEME_CYBERPUNK;

    clock_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(clock_screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(clock_screen, LV_OPA_COVER, 0);
    screen_background_apply(clock_screen);
    lv_obj_clear_flag(clock_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(clock_screen, LV_SCROLLBAR_MODE_OFF);

    clock_value_card = lv_obj_create(clock_screen);
    lv_obj_set_size(clock_value_card, 560, 180);
    lv_obj_align(clock_value_card, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_bg_color(clock_value_card, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(clock_value_card, LV_OPA_20, 0);
    lv_obj_set_style_border_width(clock_value_card, 0, 0);
    lv_obj_set_style_radius(clock_value_card, 34, 0);
    lv_obj_set_style_shadow_width(clock_value_card, 0, 0);
    lv_obj_set_style_pad_all(clock_value_card, 0, 0);
    lv_obj_clear_flag(clock_value_card, LV_OBJ_FLAG_SCROLLABLE);

    if (cyberpunk)
    {
        clock_time_shadow_cont = lv_obj_create(clock_screen);
        lv_obj_set_size(clock_time_shadow_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(clock_time_shadow_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(clock_time_shadow_cont, 0, 0);
        lv_obj_set_style_pad_all(clock_time_shadow_cont, 0, 0);
        lv_obj_set_style_pad_column(clock_time_shadow_cont, 10, 0);
        lv_obj_set_flex_flow(clock_time_shadow_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(clock_time_shadow_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(clock_time_shadow_cont, LV_ALIGN_CENTER, 4, -24);

        clock_time_shadow_label = lv_label_create(clock_time_shadow_cont);
        lv_label_set_text(clock_time_shadow_label, current_time_text);
        lv_obj_set_style_text_color(clock_time_shadow_label, lv_color_hex(0x4A1800), 0);
        lv_obj_set_style_text_opa(clock_time_shadow_label, LV_OPA_80, 0);
        lv_obj_set_style_text_font(clock_time_shadow_label, &montserrat_160, 0);
        lv_obj_set_style_text_letter_space(clock_time_shadow_label, 1, 0);

        clock_ampm_shadow_label = lv_label_create(clock_time_shadow_cont);
        lv_label_set_text(clock_ampm_shadow_label, current_ampm_text);
        lv_obj_set_style_text_color(clock_ampm_shadow_label, lv_color_hex(0x4A1800), 0);
        lv_obj_set_style_text_opa(clock_ampm_shadow_label, LV_OPA_70, 0);
        lv_obj_set_style_text_font(clock_ampm_shadow_label, &lv_font_montserrat_48, 0);
    }

    clock_time_cont = lv_obj_create(clock_screen);
    lv_obj_set_size(clock_time_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(clock_time_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_time_cont, 0, 0);
    lv_obj_set_style_pad_all(clock_time_cont, 0, 0);
    lv_obj_set_style_pad_column(clock_time_cont, 10, 0);
    lv_obj_set_flex_flow(clock_time_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_time_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(clock_time_cont, LV_ALIGN_CENTER, 0, -28);

    clock_time_label = lv_label_create(clock_time_cont);
    lv_label_set_text(clock_time_label, current_time_text);
    lv_obj_set_style_text_color(clock_time_label, cyberpunk ? COLOR_NAV_ICON : COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(clock_time_label, &montserrat_160, 0);
    lv_obj_set_style_text_letter_space(clock_time_label, 1, 0);

    clock_ampm_label = lv_label_create(clock_time_cont);
    lv_label_set_text(clock_ampm_label, current_ampm_text);
    lv_obj_set_style_text_color(clock_ampm_label, cyberpunk ? COLOR_NAV_ICON : COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_opa(clock_ampm_label, cyberpunk ? LV_OPA_COVER : (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(clock_ampm_label, &lv_font_montserrat_48, 0);

    clock_date_label = lv_label_create(clock_screen);
    lv_label_set_text(clock_date_label, current_date_text);
    lv_obj_set_width(clock_date_label, 760);
    lv_obj_set_style_text_align(clock_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(clock_date_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(clock_date_label, &lv_font_montserrat_24, 0);
    lv_obj_align(clock_date_label, LV_ALIGN_CENTER, 0, 126);

    clock_tap_area = lv_obj_create(clock_screen);
    lv_obj_set_size(clock_tap_area, SCREEN_WIDTH, SCREEN_HEIGHT - 64);
    lv_obj_align(clock_tap_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(clock_tap_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_tap_area, 0, 0);
    lv_obj_set_style_radius(clock_tap_area, 0, 0);
    lv_obj_set_style_pad_all(clock_tap_area, 0, 0);
    lv_obj_add_event_cb(clock_tap_area, clock_toggle_content_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bottom_nav = lv_obj_create(clock_screen);
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
    lv_obj_move_foreground(bottom_nav);

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, clock_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, clock_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, clock_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, NULL, true);
    create_bottom_nav_btn(bottom_nav, "$", clock_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "W", clock_weather_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, clock_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, clock_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, clock_night_clicked, false);

    clock_start_sntp();
    clock_update_time_text();
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
}

void clock_screen_destroy(void)
{
    if (clock_screen)
    {
        if (clock_timer)
        {
            lv_timer_del(clock_timer);
            clock_timer = NULL;
        }
        lv_obj_del(clock_screen);
        clock_screen = NULL;
        clock_value_card = NULL;
        clock_time_shadow_cont = NULL;
        clock_time_cont = NULL;
        clock_time_shadow_label = NULL;
        clock_time_label = NULL;
        clock_ampm_shadow_label = NULL;
        clock_ampm_label = NULL;
        clock_date_label = NULL;
        clock_tap_area = NULL;
        clock_content_hidden = false;
    }
}

lv_obj_t *clock_get_screen(void)
{
    return clock_screen;
}

static void clock_update_time_text(void)
{
    time_t now = time(NULL);
    if (now < 946684800)
    {
        int64_t uptime_us = esp_timer_get_time();
        int32_t uptime_sec = (int32_t)(uptime_us / 1000000);
        int32_t hours = (uptime_sec / 3600) % 24;
        int32_t minutes = (uptime_sec / 60) % 60;
        int32_t hour12 = hours % 12;
        if (hour12 == 0)
        {
            hour12 = 12;
        }
        const char *ampm = (hours < 12) ? "AM" : "PM";
        lv_snprintf(current_time_text, sizeof(current_time_text), "%02d:%02d", (int)hour12, (int)minutes);
        lv_snprintf(current_ampm_text, sizeof(current_ampm_text), "%s", ampm);
        lv_snprintf(current_date_text, sizeof(current_date_text), "Waiting for time sync");
    }
    else
    {
        struct tm time_info;
        localtime_r(&now, &time_info);
        int hour12 = time_info.tm_hour % 12;
        if (hour12 == 0)
        {
            hour12 = 12;
        }
        const char *ampm = (time_info.tm_hour < 12) ? "AM" : "PM";
        lv_snprintf(current_time_text, sizeof(current_time_text), "%02d:%02d",
                    hour12, time_info.tm_min);
        lv_snprintf(current_ampm_text, sizeof(current_ampm_text), "%s", ampm);
        strftime(current_date_text, sizeof(current_date_text), "%A, %B %d, %Y", &time_info);
    }

    if (clock_time_shadow_label)
    {
        lv_label_set_text(clock_time_shadow_label, current_time_text);
    }
    if (clock_time_label)
    {
        lv_label_set_text(clock_time_label, current_time_text);
    }
    if (clock_ampm_shadow_label)
    {
        lv_label_set_text(clock_ampm_shadow_label, current_ampm_text);
    }
    if (clock_ampm_label)
    {
        lv_label_set_text(clock_ampm_label, current_ampm_text);
    }
    if (clock_date_label)
    {
        lv_label_set_text(clock_date_label, current_date_text);
    }
}

static void clock_start_sntp(void)
{
    static bool sntp_started = false;
    if (sntp_started || sntp_enabled())
    {
        sntp_started = true;
        return;
    }

    sntp_started = true;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void clock_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    clock_update_time_text();
}

static void clock_toggle_content_clicked(lv_event_t *e)
{
    LV_UNUSED(e);

    clock_content_hidden = !clock_content_hidden;

    if (clock_value_card)
    {
        if (clock_content_hidden)
        {
            lv_obj_add_flag(clock_value_card, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(clock_value_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (clock_time_shadow_cont)
    {
        if (clock_content_hidden)
        {
            lv_obj_add_flag(clock_time_shadow_cont, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(clock_time_shadow_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (clock_time_cont)
    {
        if (clock_content_hidden)
        {
            lv_obj_add_flag(clock_time_cont, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(clock_time_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (clock_date_label)
    {
        if (clock_content_hidden)
        {
            lv_obj_add_flag(clock_date_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(clock_date_label, LV_OBJ_FLAG_HIDDEN);
        }
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

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, COLOR_NAV_ICON, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
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
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

void clock_home_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    home_screen_create();
    lv_scr_load(home_get_screen());
    clock_screen_destroy();
}

void clock_block_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    block_screen_create();
    lv_scr_load(block_get_screen());
    clock_screen_destroy();
}

void clock_mempool_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    clock_screen_destroy();
}

void clock_price_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    price_screen_create();
    lv_scr_load(price_get_screen());
    clock_screen_destroy();
}

void clock_weather_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    weather_screen_create();
    lv_scr_load(weather_get_screen());
    clock_screen_destroy();
}

void clock_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    clock_screen_destroy();
}

void clock_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    clock_screen_destroy();
}

void clock_night_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    night_screen_create();
    lv_scr_load(night_get_screen());
    clock_screen_destroy();
}
