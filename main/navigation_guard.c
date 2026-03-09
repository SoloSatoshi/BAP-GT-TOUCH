#include "navigation_guard.h"
#include "background.h"
#include "block.h"
#include "clock.h"
#include "home.h"
#include "mempool.h"
#include "night.h"
#include "price.h"
#include "settings.h"
#include "weather.h"
#include "wifi.h"
#include "esp_timer.h"

#define UI_NAVIGATION_GUARD_US (350 * 1000)

typedef union
{
    lv_event_cb_t cb;
    void *ptr;
} ui_navigation_cb_ref_t;

static int64_t s_navigation_block_until_us = 0;
static lv_obj_t *s_wifi_required_popup = NULL;
static lv_obj_t *s_wifi_required_title = NULL;
static lv_obj_t *s_wifi_required_message = NULL;
static lv_obj_t *s_wifi_required_detail = NULL;
static bool s_navigation_async_pending = false;
static lv_event_cb_t s_pending_navigation_cb = NULL;

static void ui_navigation_wifi_required_popup_close(lv_event_t *e);

void *ui_navigation_make_user_data(lv_event_cb_t cb)
{
    ui_navigation_cb_ref_t ref = {
        .cb = cb,
    };
    return ref.ptr;
}

static lv_event_cb_t ui_navigation_cb_from_user_data(void *data)
{
    ui_navigation_cb_ref_t ref = {
        .ptr = data,
    };
    return ref.cb;
}

static void ui_navigation_run_async(void *user_data)
{
    LV_UNUSED(user_data);

    lv_event_cb_t cb = s_pending_navigation_cb;
    s_pending_navigation_cb = NULL;
    s_navigation_async_pending = false;

    ui_navigation_wifi_required_popup_close(NULL);

    if (cb)
    {
        cb(NULL);
    }
}

static bool ui_navigation_cb_allows_missing_wifi_credentials(lv_event_cb_t cb)
{
    return cb == home_wifi_clicked ||
           cb == block_wifi_clicked ||
           cb == mempool_wifi_clicked ||
           cb == clock_wifi_clicked ||
           cb == price_wifi_clicked ||
           cb == weather_wifi_clicked ||
           cb == night_wifi_clicked ||
           cb == settings_wifi_clicked;
}

static void ui_navigation_wifi_required_popup_close(lv_event_t *e)
{
    LV_UNUSED(e);

    if (s_wifi_required_popup)
    {
        lv_obj_del(s_wifi_required_popup);
        s_wifi_required_popup = NULL;
        s_wifi_required_title = NULL;
        s_wifi_required_message = NULL;
        s_wifi_required_detail = NULL;
    }
}

static void ui_navigation_show_wifi_required_popup(const char *title_text,
                                                   const char *message_text,
                                                   const char *detail_text)
{
    if (!title_text || !message_text || !detail_text)
    {
        return;
    }

    if (s_wifi_required_popup)
    {
        lv_label_set_text(s_wifi_required_title, title_text);
        lv_label_set_text(s_wifi_required_message, message_text);
        lv_label_set_text(s_wifi_required_detail, detail_text);
        return;
    }

    s_wifi_required_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_wifi_required_popup, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_wifi_required_popup, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_required_popup, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(s_wifi_required_popup, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_wifi_required_popup, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_required_popup, 0, 0);
    lv_obj_clear_flag(s_wifi_required_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_wifi_required_popup, ui_navigation_wifi_required_popup_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *popup_cont = lv_obj_create(s_wifi_required_popup);
    lv_obj_set_size(popup_cont, 500, 220);
    lv_obj_center(popup_cont);
    translucent_card_apply(popup_cont, 12, LV_OPA_20);
    lv_obj_set_style_bg_color(popup_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(popup_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(popup_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(popup_cont, 2, 0);
    lv_obj_set_style_border_color(popup_cont, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(popup_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(popup_cont, 24, 0);
    lv_obj_add_flag(popup_cont, LV_OBJ_FLAG_CLICKABLE);

    s_wifi_required_title = lv_label_create(popup_cont);
    lv_label_set_text(s_wifi_required_title, title_text);
    lv_obj_set_style_text_color(s_wifi_required_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_wifi_required_title, &lv_font_montserrat_28, 0);
    lv_obj_align(s_wifi_required_title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *close_btn = lv_btn_create(popup_cont);
    lv_obj_set_size(close_btn, 36, 36);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, -4);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, ui_navigation_wifi_required_popup_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_icon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(close_icon);

    s_wifi_required_message = lv_label_create(popup_cont);
    lv_label_set_long_mode(s_wifi_required_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_required_message, 420);
    lv_label_set_text(s_wifi_required_message, message_text);
    lv_obj_set_style_text_color(s_wifi_required_message, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_wifi_required_message, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(s_wifi_required_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_wifi_required_message, LV_ALIGN_TOP_MID, 0, 70);

    s_wifi_required_detail = lv_label_create(popup_cont);
    lv_label_set_long_mode(s_wifi_required_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_required_detail, 420);
    lv_label_set_text(s_wifi_required_detail, detail_text);
    lv_obj_set_style_text_color(s_wifi_required_detail, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_wifi_required_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_wifi_required_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_wifi_required_detail, LV_ALIGN_TOP_MID, 0, 112);
}

void ui_navigation_show_wifi_credentials_required_popup(void)
{
    ui_navigation_show_wifi_required_popup(
        "Wi-Fi Required",
        "Enter Wi-Fi credentials to continue.",
        "You need to enter Wi-Fi credentials before the rest of the device is available.");
}

void ui_navigation_show_wifi_connection_required_popup(void)
{
    ui_navigation_show_wifi_required_popup(
        "Connection Required",
        "Finish connecting before leaving this screen.",
        "The touchscreen and miner both need to be connected before the rest of the device is available.");
}

void ui_navigation_block_for_ms(uint32_t ms)
{
    int64_t until_us = esp_timer_get_time() + ((int64_t)ms * 1000);
    if (until_us > s_navigation_block_until_us)
    {
        s_navigation_block_until_us = until_us;
    }
}

void ui_navigation_guarded_click_cb(lv_event_t *e)
{
    lv_event_cb_t cb = ui_navigation_cb_from_user_data(lv_event_get_user_data(e));
    if (!cb)
    {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (!wifi_has_saved_credentials() && !ui_navigation_cb_allows_missing_wifi_credentials(cb))
    {
        s_navigation_block_until_us = now + UI_NAVIGATION_GUARD_US;
        ui_navigation_show_wifi_credentials_required_popup();
        return;
    }

    if (wifi_navigation_is_locked() && !ui_navigation_cb_allows_missing_wifi_credentials(cb))
    {
        s_navigation_block_until_us = now + UI_NAVIGATION_GUARD_US;
        ui_navigation_show_wifi_connection_required_popup();
        return;
    }

    if (now < s_navigation_block_until_us)
    {
        return;
    }

    if (s_navigation_async_pending)
    {
        return;
    }

    s_navigation_block_until_us = now + UI_NAVIGATION_GUARD_US;
    s_pending_navigation_cb = cb;
    s_navigation_async_pending = true;
    lv_async_call(ui_navigation_run_async, NULL);
}
