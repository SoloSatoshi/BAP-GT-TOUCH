#include "settings.h"
#include "home.h"
#include "wifi.h"
#include "night.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "weather.h"
#include "mempool.h"
#include "background.h"
#include "theme.h"
#include "keyboard_theme.h"
#include "stdio.h"
#include "string.h"
#include "custom_fonts.h"
#include "bap.h"
#include "waveshare_rgb_lcd_port.h"
#include "ota_update.h"
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "settings_screen";

static lv_obj_t *settings_screen = NULL;
static lv_obj_t *performance_low_btn = NULL;
static lv_obj_t *performance_medium_btn = NULL;
static lv_obj_t *performance_high_btn = NULL;
static lv_obj_t *auto_fan_checkbox = NULL;
static lv_obj_t *fan_slider = NULL;
static lv_obj_t *fan_value_label = NULL;
static lv_obj_t *fan_save_btn = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_value_label = NULL;
static lv_obj_t *screen_off_btn = NULL;
static lv_obj_t *currency_dropdown = NULL;
static lv_obj_t *theme_dropdown = NULL;
static lv_obj_t *weather_unit_dropdown = NULL;
static lv_obj_t *weather_country_ta = NULL;
static lv_obj_t *weather_postal_ta = NULL;
static lv_obj_t *weather_save_btn = NULL;
static lv_obj_t *weather_status_label = NULL;
static lv_obj_t *settings_keyboard = NULL;
static lv_obj_t *timezone_dropdown = NULL;
static lv_obj_t *sys_overlay = NULL;
static int diag_counter = 0;

// OTA Update UI elements
static lv_obj_t *ota_update_btn = NULL;
static lv_obj_t *ota_status_label = NULL;
static lv_obj_t *ota_progress_bar = NULL;
static lv_obj_t *ota_version_label = NULL;
static lv_timer_t *ota_timer = NULL;

static settings_info_t current_settings = {
    .performance_mode = PERFORMANCE_MEDIUM,
    .auto_fan_control = true,
    .fan_speed_percent = 50,
    .brightness_percent = 100,
    .price_currency = PRICE_CURRENCY_USD};

static int current_timezone_index = 0;
static bool timezone_applied = false;
static bool settings_nvs_ready = false;
static bool weather_location_loaded = false;
static bool weather_temperature_unit_loaded = false;
static char current_weather_country_code[3] = "US";
static char current_weather_postal_code[20] = "";
static weather_temperature_unit_t current_weather_temperature_unit = WEATHER_TEMPERATURE_UNIT_F;

static const char *timezone_options =
    "UTC\n"
    "US/Pacific\n"
    "US/Mountain\n"
    "US/Central\n"
    "US/Eastern\n"
    "Europe/London\n"
    "Europe/Berlin\n"
    "Asia/Tokyo\n"
    "Australia/Sydney";

static const char *timezone_values[] = {
    "UTC0",
    "PST8PDT,M3.2.0/2,M11.1.0/2",
    "MST7MDT,M3.2.0/2,M11.1.0/2",
    "CST6CDT,M3.2.0/2,M11.1.0/2",
    "EST5EDT,M3.2.0/2,M11.1.0/2",
    "GMT0BST,M3.5.0/1,M10.5.0/2",
    "CET-1CEST,M3.5.0/2,M10.5.0/3",
    "JST-9",
    "AEST-10AEDT,M10.1.0/2,M4.1.0/3",
};

#define SETTINGS_NVS_NAMESPACE "settings"
#define SETTINGS_NVS_TZ_INDEX_KEY "tz_index"
#define SETTINGS_NVS_CURRENCY_KEY "price_currency"
#define SETTINGS_NVS_WEATHER_COUNTRY_KEY "weather_country"
#define SETTINGS_NVS_WEATHER_POSTAL_KEY "weather_postal"
#define SETTINGS_NVS_WEATHER_TEMP_UNIT_KEY "weather_temp_unit"

static const char *currency_options =
    "USD\n"
    "EUR\n"
    "GBP\n"
    "CAD\n"
    "AUD\n"
    "JPY";

static const char *currency_codes[] = {
    "USD",
    "EUR",
    "GBP",
    "CAD",
    "AUD",
    "JPY",
};

static const char *currency_prefixes[] = {
    "$",
    "",
    "",
    "$",
    "$",
    "",
};

static const char *currency_suffixes[] = {
    "",
    " EUR",
    " GBP",
    " CAD",
    " AUD",
    " JPY",
};

static const char *weather_temperature_unit_options =
    "F\n"
    "C";

static bool settings_ensure_nvs_ready(void);
static void settings_load_price_currency(void);
static void settings_save_price_currency(price_currency_t currency);
static void settings_load_weather_location(void);
static void settings_save_weather_location(void);
static void settings_load_weather_temperature_unit(void);
static void settings_save_weather_temperature_unit(weather_temperature_unit_t unit);
static void settings_weather_set_status(const char *text, lv_color_t color);
static lv_obj_t *create_settings_input_field(lv_obj_t *parent, const char *placeholder, const char *accepted_chars, uint32_t max_len);
static void settings_ta_event_handler(lv_event_t *e);
static void settings_keyboard_event_cb(lv_event_t *e);
static void settings_theme_changed(lv_event_t *e);
static void settings_reload_screen_async(void *data);
static void settings_screen_off_clicked(lv_event_t *e);

static lv_obj_t *create_settings_button(lv_obj_t *parent, const char *text, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 170, 48);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_set_style_bg_color(btn, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, active ? COLOR_TEXT_ON_ACCENT : COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
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

static void update_performance_buttons(void)
{
    if (!performance_low_btn || !performance_medium_btn || !performance_high_btn)
        return;

    lv_obj_set_style_bg_color(performance_low_btn, COLOR_CARD_BG, 0);
    lv_obj_t *low_label = lv_obj_get_child(performance_low_btn, 0);
    if (low_label)
        lv_obj_set_style_text_color(low_label, COLOR_ACCENT, 0);

    lv_obj_set_style_bg_color(performance_medium_btn, COLOR_CARD_BG, 0);
    lv_obj_t *medium_label = lv_obj_get_child(performance_medium_btn, 0);
    if (medium_label)
        lv_obj_set_style_text_color(medium_label, COLOR_ACCENT, 0);

    lv_obj_set_style_bg_color(performance_high_btn, COLOR_CARD_BG, 0);
    lv_obj_t *high_label = lv_obj_get_child(performance_high_btn, 0);
    if (high_label)
        lv_obj_set_style_text_color(high_label, COLOR_ACCENT, 0);

    lv_obj_t *active_btn = NULL;
    lv_obj_t *active_label = NULL;

    switch (current_settings.performance_mode)
    {
    case PERFORMANCE_LOW:
        active_btn = performance_low_btn;
        active_label = low_label;
        break;
    case PERFORMANCE_MEDIUM:
        active_btn = performance_medium_btn;
        active_label = medium_label;
        break;
    case PERFORMANCE_HIGH:
        active_btn = performance_high_btn;
        active_label = high_label;
        break;
    }

    if (active_btn && active_label)
    {
        lv_obj_set_style_bg_color(active_btn, COLOR_ACCENT, 0);
        lv_obj_set_style_border_opa(active_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(active_label, COLOR_TEXT_ON_ACCENT, 0);
    }
}

static void apply_timezone_by_index(int index)
{
    size_t tz_count = sizeof(timezone_values) / sizeof(timezone_values[0]);
    if (index < 0 || (size_t)index >= tz_count)
    {
        return;
    }

    setenv("TZ", timezone_values[index], 1);
    tzset();
    timezone_applied = true;
}

static void settings_load_timezone(void)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    int32_t saved_index = 0;
    err = nvs_get_i32(handle, SETTINGS_NVS_TZ_INDEX_KEY, &saved_index);
    nvs_close(handle);
    if (err == ESP_OK)
    {
        current_timezone_index = (int)saved_index;
    }
}

static void settings_save_timezone(int index)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    nvs_set_i32(handle, SETTINGS_NVS_TZ_INDEX_KEY, index);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool settings_ensure_nvs_ready(void)
{
    if (settings_nvs_ready)
    {
        return true;
    }

    esp_err_t init_err = nvs_flash_init();
    if (init_err == ESP_ERR_NVS_NO_FREE_PAGES || init_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        init_err = nvs_flash_init();
    }
    if (init_err != ESP_OK)
    {
        return false;
    }

    settings_nvs_ready = true;
    return true;
}

static void settings_load_price_currency(void)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    int32_t saved_currency = PRICE_CURRENCY_USD;
    err = nvs_get_i32(handle, SETTINGS_NVS_CURRENCY_KEY, &saved_currency);
    nvs_close(handle);
    if (err == ESP_OK &&
        saved_currency >= PRICE_CURRENCY_USD &&
        saved_currency <= PRICE_CURRENCY_JPY)
    {
        current_settings.price_currency = (price_currency_t)saved_currency;
    }
}

static void settings_save_price_currency(price_currency_t currency)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    nvs_set_i32(handle, SETTINGS_NVS_CURRENCY_KEY, (int32_t)currency);
    nvs_commit(handle);
    nvs_close(handle);
}

static void settings_load_weather_location(void)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    size_t country_len = sizeof(current_weather_country_code);
    err = nvs_get_str(handle, SETTINGS_NVS_WEATHER_COUNTRY_KEY, current_weather_country_code, &country_len);
    if (err != ESP_OK || current_weather_country_code[0] == '\0')
    {
        strncpy(current_weather_country_code, "US", sizeof(current_weather_country_code) - 1);
        current_weather_country_code[sizeof(current_weather_country_code) - 1] = '\0';
    }

    size_t postal_len = sizeof(current_weather_postal_code);
    err = nvs_get_str(handle, SETTINGS_NVS_WEATHER_POSTAL_KEY, current_weather_postal_code, &postal_len);
    if (err != ESP_OK)
    {
        current_weather_postal_code[0] = '\0';
    }

    nvs_close(handle);
    weather_location_loaded = true;
}

static void settings_load_weather_temperature_unit(void)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    int32_t saved_unit = WEATHER_TEMPERATURE_UNIT_F;
    err = nvs_get_i32(handle, SETTINGS_NVS_WEATHER_TEMP_UNIT_KEY, &saved_unit);
    nvs_close(handle);

    if (err == ESP_OK &&
        saved_unit >= WEATHER_TEMPERATURE_UNIT_F &&
        saved_unit <= WEATHER_TEMPERATURE_UNIT_C)
    {
        current_weather_temperature_unit = (weather_temperature_unit_t)saved_unit;
    }

    weather_temperature_unit_loaded = true;
}

static void settings_save_weather_temperature_unit(weather_temperature_unit_t unit)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    nvs_set_i32(handle, SETTINGS_NVS_WEATHER_TEMP_UNIT_KEY, (int32_t)unit);
    nvs_commit(handle);
    nvs_close(handle);
    weather_temperature_unit_loaded = true;
}

static void settings_save_weather_location(void)
{
    if (!settings_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    nvs_set_str(handle, SETTINGS_NVS_WEATHER_COUNTRY_KEY, current_weather_country_code);
    nvs_set_str(handle, SETTINGS_NVS_WEATHER_POSTAL_KEY, current_weather_postal_code);
    nvs_commit(handle);
    nvs_close(handle);
}

static void settings_weather_set_status(const char *text, lv_color_t color)
{
    if (!weather_status_label || !text)
    {
        return;
    }

    lv_label_set_text(weather_status_label, text);
    lv_obj_set_style_text_color(weather_status_label, color, 0);
}

static lv_obj_t *create_settings_input_field(lv_obj_t *parent, const char *placeholder, const char *accepted_chars, uint32_t max_len)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, 180, 40);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    if (accepted_chars)
    {
        lv_textarea_set_accepted_chars(ta, accepted_chars);
    }
    translucent_card_apply(ta, 8, LV_OPA_20);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_border_color(ta, COLOR_RED, 0);
    lv_obj_set_style_border_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(ta, settings_ta_event_handler, LV_EVENT_ALL, NULL);
    return ta;
}

static void settings_ta_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED)
    {
        if (settings_keyboard && lv_obj_has_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN))
        {
            lv_keyboard_set_textarea(settings_keyboard, ta);
            lv_obj_clear_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(settings_keyboard);
        }
    }
    else if (code == LV_EVENT_DEFOCUSED)
    {
        if (settings_keyboard && !lv_obj_has_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void settings_keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
    {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *ta = lv_keyboard_get_textarea(kb);
        if (ta)
        {
            lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        }
    }
}

static void settings_reload_screen_async(void *data)
{
    LV_UNUSED(data);

    if (!settings_screen)
    {
        return;
    }

    settings_screen_destroy();
    settings_screen_create();
    lv_scr_load(settings_get_screen());
}

static void update_fan_controls(void)
{
    if (!fan_slider || !fan_value_label)
        return;

    if (current_settings.auto_fan_control)
    {
        lv_obj_add_flag(fan_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fan_value_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(fan_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(fan_value_label, LV_OBJ_FLAG_HIDDEN);

        lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
        char fan_text[16];
        snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
        lv_label_set_text(fan_value_label, fan_text);
    }
}

static void decode_sys_info(char *output, size_t output_size)
{
    static const uint8_t encoded_data[] = {
        38, 39, 52, 39, 46, 45, 50, 39, 38, 98,
        32, 59, 98, 21, 35, 44, 54, 1, 46, 55, 39
    };
    const uint8_t key = 0x42;
    size_t len = sizeof(encoded_data);

    for (size_t i = 0; i < len && i < output_size - 1; i++) {
        output[i] = encoded_data[i] ^ key;
    }
    output[len < output_size ? len : output_size - 1] = '\0';
}

static void cleanup_system_overlay(lv_event_t *e)
{
    if (sys_overlay) {
        lv_obj_del(sys_overlay);
        sys_overlay = NULL;
    }
    diag_counter = 0;
}

static void create_system_overlay(void)
{
    if (sys_overlay) {
        return;
    }

    sys_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sys_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(sys_overlay, 0, 0);
    lv_obj_set_style_bg_color(sys_overlay, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(sys_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(sys_overlay, 0, 0);
    lv_obj_clear_flag(sys_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(sys_overlay, cleanup_system_overlay, LV_EVENT_CLICKED, NULL);

    lv_obj_t *dialog = lv_obj_create(sys_overlay);
    lv_obj_set_size(dialog, 400, 200);
    lv_obj_center(dialog);
    translucent_card_apply(dialog, 14, LV_OPA_20);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_border_opa(dialog, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    char display_buffer[64];
    decode_sys_info(display_buffer, sizeof(display_buffer));

    lv_obj_t *label = lv_label_create(dialog);
    lv_label_set_text(label, display_buffer);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_center(label);
}

static void settings_diagnostics_handler(lv_event_t *e)
{
    diag_counter++;

    if (diag_counter >= 3) {
        create_system_overlay();
        diag_counter = 0;
    }
}

// OTA Update timer callback - updates progress UI
static void ota_update_timer_cb(lv_timer_t *timer)
{
    if (!ota_status_label || !ota_progress_bar || !ota_update_btn) {
        return;
    }

    ota_info_t info;
    ota_update_get_info(&info);

    char status_text[128];

    lv_obj_t *btn_label = lv_obj_get_child(ota_update_btn, 0);

    switch (info.status) {
        case OTA_STATUS_IDLE:
            lv_label_set_text(ota_status_label, "Ready for update");
            if (btn_label) lv_label_set_text(btn_label, "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_CHECKING:
            lv_label_set_text(ota_status_label, "Checking for updates...");
            lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_UPDATE_AVAILABLE:
            snprintf(status_text, sizeof(status_text), "Update available: %s", info.latest_version);
            lv_label_set_text(ota_status_label, status_text);
            if (ota_version_label) {
                snprintf(status_text, sizeof(status_text), "Current: %s | Latest: %s", 
                    info.current_version, info.latest_version);
                lv_label_set_text(ota_version_label, status_text);
            }
            if (btn_label) lv_label_set_text(btn_label, "INSTALL UPDATE");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_NO_UPDATE:
            lv_label_set_text(ota_status_label, "Already up to date");
            if (btn_label) lv_label_set_text(btn_label, "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 100, LV_ANIM_OFF);
            break;

        case OTA_STATUS_DOWNLOADING:
            lv_label_set_text_fmt(ota_status_label, "Downloading... %d%%", info.progress_percent);
            lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, info.progress_percent, LV_ANIM_ON);
            break;

        case OTA_STATUS_FLASHING:
            lv_label_set_text_fmt(ota_status_label, "Installing... %d%%", info.progress_percent);
            lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, info.progress_percent, LV_ANIM_ON);
            break;

        case OTA_STATUS_SUCCESS:
            lv_label_set_text(ota_status_label, "Update successful! Rebooting...");
            lv_bar_set_value(ota_progress_bar, 100, LV_ANIM_ON);
            break;

        case OTA_STATUS_ERROR:
            lv_label_set_text_fmt(ota_status_label, "Error: %s", 
                info.error_msg[0] ? info.error_msg : "Unknown error");
            if (btn_label) lv_label_set_text(btn_label, "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;
    }
}

// OTA Update button click handler
static void settings_ota_update_clicked(lv_event_t *e)
{
    ota_info_t info;
    ota_update_get_info(&info);

    if (info.status == OTA_STATUS_UPDATE_AVAILABLE) {
        esp_err_t ret = ota_update_start_latest();
        if (ret != ESP_OK && ota_status_label) {
            lv_label_set_text(ota_status_label, "Failed to start update");
        }
    } else {
        ota_check_for_updates();
    }
}

void settings_screen_create(void)
{
    if (settings_screen != NULL)
    {
        return;
    }

    settings_load_timezone();
    settings_load_price_currency();
    settings_load_weather_location();
    settings_load_weather_temperature_unit();
    ui_theme_init();

    settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settings_screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(settings_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(settings_screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *main_cont = lv_obj_create(settings_screen);
    lv_obj_set_size(main_cont, SCREEN_WIDTH - 60, SCREEN_HEIGHT - 100);
    lv_obj_align(main_cont, LV_ALIGN_TOP_MID, 0, 16);
    translucent_card_apply(main_cont, 14, LV_OPA_20);
    lv_obj_set_style_bg_opa(main_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(main_cont, 0, 0);
    lv_obj_set_style_border_opa(main_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(main_cont, 16, 0);
    lv_obj_add_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(main_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(main_cont, LV_DIR_VER);
    lv_obj_set_style_pad_bottom(main_cont, 80, 0);

    lv_obj_t *title_label = lv_label_create(main_cont);
    lv_label_set_text(title_label, "SETTINGS");
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *perf_section = lv_obj_create(main_cont);
    lv_obj_set_size(perf_section, 680, 110);
    lv_obj_align(perf_section, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_opa(perf_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(perf_section, 0, 0);
    lv_obj_set_style_pad_all(perf_section, 10, 0);
    lv_obj_clear_flag(perf_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *perf_title = lv_label_create(perf_section);
    lv_label_set_text(perf_title, "Performance Mode:");
    lv_obj_set_style_text_color(perf_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(perf_title, &lv_font_montserrat_18, 0);
    lv_obj_align(perf_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *perf_btn_cont = lv_obj_create(perf_section);
    lv_obj_set_size(perf_btn_cont, 560, 56);
    lv_obj_align(perf_btn_cont, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_opa(perf_btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(perf_btn_cont, 0, 0);
    lv_obj_set_style_pad_all(perf_btn_cont, 0, 0);
    lv_obj_set_flex_flow(perf_btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(perf_btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    performance_low_btn = create_settings_button(perf_btn_cont, "LOW", settings_performance_low_clicked,
                                                 current_settings.performance_mode == PERFORMANCE_LOW);
    performance_medium_btn = create_settings_button(perf_btn_cont, "MEDIUM", settings_performance_medium_clicked,
                                                    current_settings.performance_mode == PERFORMANCE_MEDIUM);
    performance_high_btn = create_settings_button(perf_btn_cont, "HIGH", settings_performance_high_clicked,
                                                  current_settings.performance_mode == PERFORMANCE_HIGH);

    lv_obj_t *brightness_section = lv_obj_create(main_cont);
    lv_obj_set_size(brightness_section, 680, 70);
    lv_obj_align(brightness_section, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_opa(brightness_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brightness_section, 0, 0);
    lv_obj_set_style_pad_all(brightness_section, 10, 0);
    lv_obj_clear_flag(brightness_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brightness_title = lv_label_create(brightness_section);
    lv_label_set_text(brightness_title, "Screen Brightness:");
    lv_obj_set_style_text_color(brightness_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(brightness_title, &lv_font_montserrat_18, 0);
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

    brightness_slider = lv_slider_create(brightness_section);
    lv_obj_set_size(brightness_slider, 400, 20);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_slider_set_range(brightness_slider, 5, 100);
    lv_slider_set_value(brightness_slider, current_settings.brightness_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, settings_brightness_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    brightness_value_label = lv_label_create(brightness_section);
    char brightness_text[16];
    snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_settings.brightness_percent);
    lv_label_set_text(brightness_value_label, brightness_text);
    lv_obj_set_style_text_color(brightness_value_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(brightness_value_label, &lv_font_montserrat_22, 0);
    lv_obj_align(brightness_value_label, LV_ALIGN_TOP_LEFT, 445, 26);

    screen_off_btn = create_settings_button(brightness_section, "SCREEN OFF", settings_screen_off_clicked, false);
    lv_obj_set_size(screen_off_btn, 150, 40);
    lv_obj_align(screen_off_btn, LV_ALIGN_TOP_LEFT, 520, 18);

    lv_obj_t *fan_section = lv_obj_create(main_cont);
    lv_obj_set_size(fan_section, 680, 200);
    lv_obj_align(fan_section, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_set_style_bg_opa(fan_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fan_section, 0, 0);
    lv_obj_set_style_pad_all(fan_section, 10, 0);
    lv_obj_clear_flag(fan_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fan_title = lv_label_create(fan_section);
    lv_label_set_text(fan_title, "Fan Control:");
    lv_obj_set_style_text_color(fan_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(fan_title, &lv_font_montserrat_18, 0);
    lv_obj_align(fan_title, LV_ALIGN_TOP_LEFT, 0, 0);

    auto_fan_checkbox = lv_checkbox_create(fan_section);
    lv_checkbox_set_text(auto_fan_checkbox, "Automatic Fan Control");
    lv_obj_set_style_text_color(auto_fan_checkbox, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(auto_fan_checkbox, &lv_font_montserrat_16, 0);
    lv_obj_align(auto_fan_checkbox, LV_ALIGN_TOP_LEFT, 0, 35);
    lv_obj_add_event_cb(auto_fan_checkbox, settings_auto_fan_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    if (current_settings.auto_fan_control)
    {
        lv_obj_add_state(auto_fan_checkbox, LV_STATE_CHECKED);
    }

    fan_slider = lv_slider_create(fan_section);
    lv_obj_set_size(fan_slider, 420, 20);
    lv_obj_align(fan_slider, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_slider_set_range(fan_slider, 0, 100);
    lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan_slider, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan_slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan_slider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(fan_slider, settings_fan_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    fan_value_label = lv_label_create(fan_section);
    char fan_text[16];
    snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
    lv_label_set_text(fan_value_label, fan_text);
    lv_obj_set_style_text_color(fan_value_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(fan_value_label, &lv_font_montserrat_16, 0);
    lv_obj_align(fan_value_label, LV_ALIGN_TOP_LEFT, 450, 65);

    fan_save_btn = create_settings_button(fan_section, "SAVE FAN SETTINGS", settings_fan_save_clicked, false);
    lv_obj_set_size(fan_save_btn, 220, 36);
    lv_obj_align(fan_save_btn, LV_ALIGN_TOP_LEFT, 0, 115);

    update_fan_controls();

    lv_obj_t *currency_section = lv_obj_create(main_cont);
    lv_obj_set_size(currency_section, 680, 50);
    lv_obj_align(currency_section, LV_ALIGN_TOP_MID, 0, 510);
    lv_obj_set_style_bg_opa(currency_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(currency_section, 0, 0);
    lv_obj_set_style_pad_all(currency_section, 10, 0);
    lv_obj_clear_flag(currency_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *currency_title = lv_label_create(currency_section);
    lv_label_set_text(currency_title, "Price Currency:");
    lv_obj_set_style_text_color(currency_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(currency_title, &lv_font_montserrat_18, 0);
    lv_obj_align(currency_title, LV_ALIGN_TOP_LEFT, 0, 0);

    currency_dropdown = lv_dropdown_create(currency_section);
    lv_obj_set_size(currency_dropdown, 300, 34);
    lv_obj_align(currency_dropdown, LV_ALIGN_TOP_LEFT, 170, -4);
    lv_dropdown_set_options(currency_dropdown, currency_options);
    lv_dropdown_set_selected(currency_dropdown, current_settings.price_currency);
    translucent_card_apply(currency_dropdown, 8, LV_OPA_20);
    lv_obj_set_style_border_width(currency_dropdown, 2, 0);
    lv_obj_set_style_border_color(currency_dropdown, COLOR_RED, 0);
    lv_obj_set_style_border_opa(currency_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(currency_dropdown, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(currency_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(currency_dropdown, settings_price_currency_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *theme_section = lv_obj_create(main_cont);
    lv_obj_set_size(theme_section, 680, 50);
    lv_obj_align(theme_section, LV_ALIGN_TOP_MID, 0, 570);
    lv_obj_set_style_bg_opa(theme_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(theme_section, 0, 0);
    lv_obj_set_style_pad_all(theme_section, 10, 0);
    lv_obj_clear_flag(theme_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *theme_title = lv_label_create(theme_section);
    lv_label_set_text(theme_title, "Theme:");
    lv_obj_set_style_text_color(theme_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(theme_title, &lv_font_montserrat_18, 0);
    lv_obj_align(theme_title, LV_ALIGN_TOP_LEFT, 0, 0);

    theme_dropdown = lv_dropdown_create(theme_section);
    lv_obj_set_size(theme_dropdown, 300, 34);
    lv_obj_align(theme_dropdown, LV_ALIGN_TOP_LEFT, 170, -4);
    lv_dropdown_set_options(theme_dropdown, ui_theme_get_options());
    lv_dropdown_set_selected(theme_dropdown, (uint16_t)ui_theme_get_current());
    translucent_card_apply(theme_dropdown, 8, LV_OPA_20);
    lv_obj_set_style_border_width(theme_dropdown, 2, 0);
    lv_obj_set_style_border_color(theme_dropdown, COLOR_RED, 0);
    lv_obj_set_style_border_opa(theme_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(theme_dropdown, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(theme_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(theme_dropdown, settings_theme_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *weather_section = lv_obj_create(main_cont);
    lv_obj_set_size(weather_section, 680, 182);
    lv_obj_align(weather_section, LV_ALIGN_TOP_MID, 0, 630);
    lv_obj_set_style_bg_opa(weather_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_section, 0, 0);
    lv_obj_set_style_pad_all(weather_section, 10, 0);
    lv_obj_clear_flag(weather_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *weather_title = lv_label_create(weather_section);
    lv_label_set_text(weather_title, "Weather Location:");
    lv_obj_set_style_text_color(weather_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(weather_title, &lv_font_montserrat_18, 0);
    lv_obj_align(weather_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *weather_hint = lv_label_create(weather_section);
    lv_label_set_text(weather_hint, "Use 2-letter country code and postal code");
    lv_obj_set_style_text_color(weather_hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(weather_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_hint, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_t *country_title = lv_label_create(weather_section);
    lv_label_set_text(country_title, "Country");
    lv_obj_set_style_text_color(country_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(country_title, &lv_font_montserrat_14, 0);
    lv_obj_align(country_title, LV_ALIGN_TOP_LEFT, 0, 52);

    weather_country_ta = create_settings_input_field(weather_section, "US", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 2);
    lv_obj_align(weather_country_ta, LV_ALIGN_TOP_LEFT, 0, 74);
    lv_textarea_set_text(weather_country_ta, current_weather_country_code);

    lv_obj_t *postal_title = lv_label_create(weather_section);
    lv_label_set_text(postal_title, "Postal Code");
    lv_obj_set_style_text_color(postal_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(postal_title, &lv_font_montserrat_14, 0);
    lv_obj_align(postal_title, LV_ALIGN_TOP_LEFT, 220, 52);

    weather_postal_ta = create_settings_input_field(weather_section, "10001 or SW1A 1AA", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789- ", 16);
    lv_obj_set_size(weather_postal_ta, 220, 40);
    lv_obj_align(weather_postal_ta, LV_ALIGN_TOP_LEFT, 220, 74);
    lv_textarea_set_text(weather_postal_ta, current_weather_postal_code);

    weather_save_btn = create_settings_button(weather_section, "SAVE LOCATION", settings_weather_location_save_clicked, false);
    lv_obj_set_size(weather_save_btn, 180, 40);
    lv_obj_align(weather_save_btn, LV_ALIGN_TOP_LEFT, 470, 74);

    lv_obj_t *weather_unit_title = lv_label_create(weather_section);
    lv_label_set_text(weather_unit_title, "Temp Unit:");
    lv_obj_set_style_text_color(weather_unit_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(weather_unit_title, &lv_font_montserrat_16, 0);
    lv_obj_align(weather_unit_title, LV_ALIGN_TOP_LEFT, 0, 140);

    weather_unit_dropdown = lv_dropdown_create(weather_section);
    lv_obj_set_size(weather_unit_dropdown, 100, 34);
    lv_obj_align(weather_unit_dropdown, LV_ALIGN_TOP_LEFT, 120, 134);
    lv_dropdown_set_options(weather_unit_dropdown, weather_temperature_unit_options);
    lv_dropdown_set_selected(weather_unit_dropdown, current_weather_temperature_unit);
    translucent_card_apply(weather_unit_dropdown, 8, LV_OPA_20);
    lv_obj_set_style_border_width(weather_unit_dropdown, 2, 0);
    lv_obj_set_style_border_color(weather_unit_dropdown, COLOR_RED, 0);
    lv_obj_set_style_border_opa(weather_unit_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(weather_unit_dropdown, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(weather_unit_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(weather_unit_dropdown, settings_weather_temperature_unit_changed, LV_EVENT_VALUE_CHANGED, NULL);

    weather_status_label = lv_label_create(weather_section);
    lv_obj_set_style_text_color(weather_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(weather_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_status_label, LV_ALIGN_TOP_LEFT, 250, 142);

    if (current_weather_postal_code[0] != '\0')
    {
        settings_weather_set_status("Saved weather location ready", COLOR_TEXT_SECONDARY);
    }
    else
    {
        settings_weather_set_status("Enter country + postal code, then save", COLOR_TEXT_SECONDARY);
    }

    lv_obj_t *timezone_section = lv_obj_create(main_cont);
    lv_obj_set_size(timezone_section, 680, 50);
    lv_obj_align(timezone_section, LV_ALIGN_TOP_MID, 0, 830);
    lv_obj_set_style_bg_opa(timezone_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timezone_section, 0, 0);
    lv_obj_set_style_pad_all(timezone_section, 10, 0);
    lv_obj_clear_flag(timezone_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *timezone_title = lv_label_create(timezone_section);
    lv_label_set_text(timezone_title, "Time Zone:");
    lv_obj_set_style_text_color(timezone_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(timezone_title, &lv_font_montserrat_18, 0);
    lv_obj_align(timezone_title, LV_ALIGN_TOP_LEFT, 0, 0);

    timezone_dropdown = lv_dropdown_create(timezone_section);
    lv_obj_set_size(timezone_dropdown, 300, 34);
    lv_obj_align(timezone_dropdown, LV_ALIGN_TOP_LEFT, 140, -4);
    lv_dropdown_set_options(timezone_dropdown, timezone_options);
    lv_dropdown_set_selected(timezone_dropdown, current_timezone_index);
    translucent_card_apply(timezone_dropdown, 8, LV_OPA_20);
    lv_obj_set_style_border_width(timezone_dropdown, 2, 0);
    lv_obj_set_style_border_color(timezone_dropdown, COLOR_RED, 0);
    lv_obj_set_style_border_opa(timezone_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(timezone_dropdown, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(timezone_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(timezone_dropdown, settings_timezone_changed, LV_EVENT_VALUE_CHANGED, NULL);

    if (!timezone_applied)
    {
        apply_timezone_by_index(current_timezone_index);
    }

    // OTA Update Section
    lv_obj_t *ota_section = lv_obj_create(main_cont);
    lv_obj_set_size(ota_section, 680, 160);
    lv_obj_align(ota_section, LV_ALIGN_TOP_MID, 0, 890);
    lv_obj_set_style_bg_opa(ota_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ota_section, 0, 0);
    lv_obj_set_style_pad_all(ota_section, 10, 0);
    lv_obj_clear_flag(ota_section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ota_title = lv_label_create(ota_section);
    lv_label_set_text(ota_title, "Firmware Update:");
    lv_obj_set_style_text_color(ota_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_title, &lv_font_montserrat_18, 0);
    lv_obj_align(ota_title, LV_ALIGN_TOP_LEFT, 0, 0);
    ota_version_label = lv_label_create(ota_section);
    char version_text[64];
    const char *version = ota_get_current_version();
    snprintf(version_text, sizeof(version_text), "Current: %s", version ? version : "Unknown");
    lv_label_set_text(ota_version_label, version_text);
    lv_obj_set_style_text_color(ota_version_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_version_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ota_version_label, LV_ALIGN_TOP_LEFT, 0, 30);

    ota_status_label = lv_label_create(ota_section);
    lv_label_set_text(ota_status_label, "Ready for update");
    lv_obj_set_style_text_color(ota_status_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ota_status_label, LV_ALIGN_TOP_LEFT, 0, 55);

    ota_progress_bar = lv_bar_create(ota_section);
    lv_obj_set_size(ota_progress_bar, 420, 16);
    lv_obj_align(ota_progress_bar, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_bar_set_range(ota_progress_bar, 0, 100);
    lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ota_progress_bar, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_progress_bar, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_progress_bar, 8, 0);

    ota_update_btn = create_settings_button(ota_section, "CHECK FOR UPDATES", settings_ota_update_clicked, false);
    lv_obj_set_size(ota_update_btn, 240, 36);
    lv_obj_align(ota_update_btn, LV_ALIGN_TOP_LEFT, 0, 110);

    // Create OTA update timer (500ms interval)
    ota_timer = lv_timer_create(ota_update_timer_cb, 500, NULL);

    lv_obj_t *bottom_nav = lv_obj_create(settings_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, settings_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, settings_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, settings_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, settings_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", settings_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "W", settings_weather_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, settings_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, settings_diagnostics_handler, true);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, settings_night_clicked, false);

    settings_keyboard = lv_keyboard_create(settings_screen);
    keyboard_theme_apply(settings_keyboard);
    lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(settings_keyboard, settings_keyboard_event_cb, LV_EVENT_ALL, NULL);
}

void settings_screen_destroy(void)
{
    // Clean up Easter egg overlay if showing
    if (sys_overlay) {
        lv_obj_del(sys_overlay);
        sys_overlay = NULL;
    }
    diag_counter = 0;

    // Clean up OTA timer
    if (ota_timer) {
        lv_timer_del(ota_timer);
        ota_timer = NULL;
    }

    if (settings_screen)
    {
        lv_obj_del(settings_screen);
        settings_screen = NULL;
        performance_low_btn = NULL;
        performance_medium_btn = NULL;
        performance_high_btn = NULL;
        auto_fan_checkbox = NULL;
        fan_slider = NULL;
        fan_value_label = NULL;
        fan_save_btn = NULL;
        brightness_slider = NULL;
        brightness_value_label = NULL;
        screen_off_btn = NULL;
        currency_dropdown = NULL;
        theme_dropdown = NULL;
        weather_unit_dropdown = NULL;
        weather_country_ta = NULL;
        weather_postal_ta = NULL;
        weather_save_btn = NULL;
        weather_status_label = NULL;
        settings_keyboard = NULL;
        timezone_dropdown = NULL;
        ota_update_btn = NULL;
        ota_status_label = NULL;
        ota_progress_bar = NULL;
        ota_version_label = NULL;
    }
}

lv_obj_t *settings_get_screen(void)
{
    return settings_screen;
}

void settings_update_info(const settings_info_t *info)
{
    if (info)
    {
        current_settings = *info;
        update_performance_buttons();
        update_fan_controls();

        if (auto_fan_checkbox)
        {
            if (current_settings.auto_fan_control)
            {
                lv_obj_add_state(auto_fan_checkbox, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_clear_state(auto_fan_checkbox, LV_STATE_CHECKED);
            }
        }

        if (brightness_slider)
        {
            lv_slider_set_value(brightness_slider, current_settings.brightness_percent, LV_ANIM_OFF);
        }
        if (brightness_value_label)
        {
            char brightness_text[16];
            snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_settings.brightness_percent);
            lv_label_set_text(brightness_value_label, brightness_text);
        }
    }
}

void settings_performance_low_clicked(lv_event_t *e)
{
    current_settings.performance_mode = PERFORMANCE_LOW;
    update_performance_buttons();
    printf("Performance mode set to LOW\n");

    BAP_send_frequency_setting(575.0f);
    BAP_send_asic_voltage(1160.0f);
}

void settings_performance_medium_clicked(lv_event_t *e)
{
    current_settings.performance_mode = PERFORMANCE_MEDIUM;
    update_performance_buttons();
    printf("Performance mode set to MEDIUM\n");

    BAP_send_frequency_setting(600.0f);
    BAP_send_asic_voltage(1200.0f);
}

void settings_performance_high_clicked(lv_event_t *e)
{
    current_settings.performance_mode = PERFORMANCE_HIGH;
    update_performance_buttons();
    printf("Performance mode set to HIGH\n");

    BAP_send_frequency_setting(655.0f);
    BAP_send_asic_voltage(1200.0f);
}

void settings_auto_fan_toggled(lv_event_t *e)
{
    lv_obj_t *checkbox = lv_event_get_target(e);
    current_settings.auto_fan_control = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    update_fan_controls();
    printf("Auto fan control: %s\n", current_settings.auto_fan_control ? "ON" : "OFF");
}

void settings_fan_slider_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    current_settings.fan_speed_percent = lv_slider_get_value(slider);

    if (fan_value_label)
    {
        char fan_text[16];
        snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
        lv_label_set_text(fan_value_label, fan_text);
    }

    printf("Fan speed set to: %d%%\n", current_settings.fan_speed_percent);
}

void settings_fan_save_clicked(lv_event_t *e)
{
    printf("Saving fan settings - Auto: %s, Speed: %d%%\n",
           current_settings.auto_fan_control ? "ON" : "OFF",
           current_settings.fan_speed_percent);

    if (current_settings.auto_fan_control)
    {
        BAP_send_automatic_fan_control(true);
        printf("Sending auto fan control command\n");
    }
    else
    {
        BAP_send_fan_speed(current_settings.fan_speed_percent);
        printf("Sending manual fan speed: %d%%\n", current_settings.fan_speed_percent);
    }
}

void settings_home_clicked(lv_event_t *e)
{
    home_screen_create();
    lv_scr_load(home_get_screen());
    settings_screen_destroy();
}

void settings_wifi_clicked(lv_event_t *e)
{
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    settings_screen_destroy();
}

void settings_clock_clicked(lv_event_t *e)
{
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    settings_screen_destroy();
}

void settings_price_clicked(lv_event_t *e)
{
    price_screen_create();
    lv_scr_load(price_get_screen());
    settings_screen_destroy();
}

void settings_weather_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    weather_screen_create();
    lv_scr_load(weather_get_screen());
    settings_screen_destroy();
}

void settings_block_clicked(lv_event_t *e)
{
    block_screen_create();
    lv_scr_load(block_get_screen());
    settings_screen_destroy();
}

void settings_mempool_clicked(lv_event_t *e)
{
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    settings_screen_destroy();
}

void settings_brightness_slider_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    current_settings.brightness_percent = lv_slider_get_value(slider);

    if (brightness_value_label)
    {
        char brightness_text[16];
        snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_settings.brightness_percent);
        lv_label_set_text(brightness_value_label, brightness_text);
    }

    // Apply brightness change immediately
    lcd_backlight_set_brightness(current_settings.brightness_percent);

    printf("Screen brightness set to: %d%%\n", current_settings.brightness_percent);
}

static void settings_screen_off_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    lcd_screen_turn_off();
}

void settings_timezone_changed(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    current_timezone_index = (int)lv_dropdown_get_selected(dropdown);
    apply_timezone_by_index(current_timezone_index);
    settings_save_timezone(current_timezone_index);
}

void settings_price_currency_changed(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    current_settings.price_currency = (price_currency_t)lv_dropdown_get_selected(dropdown);
    settings_save_price_currency(current_settings.price_currency);
    printf("Price currency set to: %s\n", settings_get_price_currency_code());
}

void settings_weather_temperature_unit_changed(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    current_weather_temperature_unit = (weather_temperature_unit_t)lv_dropdown_get_selected(dropdown);
    settings_save_weather_temperature_unit(current_weather_temperature_unit);
    weather_service_start();
    weather_service_request_refresh();
}

static void settings_theme_changed(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    ui_theme_t selected_theme = (ui_theme_t)lv_dropdown_get_selected(dropdown);
    ui_theme_t current_theme = ui_theme_get_current();

    if (selected_theme == current_theme)
    {
        return;
    }

    ui_theme_set_current(selected_theme);
    ui_theme_save_current();
    lv_async_call(settings_reload_screen_async, NULL);
}

void settings_weather_location_save_clicked(lv_event_t *e)
{
    LV_UNUSED(e);

    if (!weather_country_ta || !weather_postal_ta)
    {
        return;
    }

    const char *country_text = lv_textarea_get_text(weather_country_ta);
    const char *postal_text = lv_textarea_get_text(weather_postal_ta);

    if (!country_text || strlen(country_text) != 2 || !postal_text || postal_text[0] == '\0')
    {
        settings_weather_set_status("Use 2-letter country code and postal code", COLOR_RED);
        return;
    }

    memset(current_weather_country_code, 0, sizeof(current_weather_country_code));
    for (size_t i = 0; i < sizeof(current_weather_country_code) - 1 && country_text[i] != '\0'; i++)
    {
        current_weather_country_code[i] = (char)toupper((unsigned char)country_text[i]);
    }

    strncpy(current_weather_postal_code, postal_text, sizeof(current_weather_postal_code) - 1);
    current_weather_postal_code[sizeof(current_weather_postal_code) - 1] = '\0';

    settings_save_weather_location();
    weather_location_loaded = true;
    lv_textarea_set_text(weather_country_ta, current_weather_country_code);
    lv_textarea_set_text(weather_postal_ta, current_weather_postal_code);
    weather_service_start();
    weather_service_request_refresh();
    settings_weather_set_status("Weather location saved, fetching now", COLOR_ACCENT);
}

price_currency_t settings_get_price_currency(void)
{
    return current_settings.price_currency;
}

const char *settings_get_price_currency_code(void)
{
    return currency_codes[current_settings.price_currency];
}

const char *settings_get_price_currency_prefix(void)
{
    return currency_prefixes[current_settings.price_currency];
}

const char *settings_get_price_currency_suffix(void)
{
    return currency_suffixes[current_settings.price_currency];
}

const char *settings_get_weather_country_code(void)
{
    if (!weather_location_loaded)
    {
        settings_load_weather_location();
    }
    return current_weather_country_code;
}

const char *settings_get_weather_postal_code(void)
{
    if (!weather_location_loaded)
    {
        settings_load_weather_location();
    }
    return current_weather_postal_code;
}

weather_temperature_unit_t settings_get_weather_temperature_unit(void)
{
    if (!weather_temperature_unit_loaded)
    {
        settings_load_weather_temperature_unit();
    }
    return current_weather_temperature_unit;
}

void settings_night_clicked(lv_event_t *e)
{
    // Navigate to night mode screen
    night_screen_create();
    lv_scr_load(night_get_screen());
    settings_screen_destroy();
}
