#include "weather.h"
#include "home.h"
#include "block.h"
#include "mempool.h"
#include "clock.h"
#include "price.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "background.h"
#include "nav_icons.h"
#include "custom_fonts.h"
#include "lvgl_port.h"
#include "ota_update.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WEATHER_HTTP_BUF_SIZE 4096
#define WEATHER_HTTP_TIMEOUT_MS 2500
#define WEATHER_REFRESH_INTERVAL_MS 900000
#define WEATHER_RETRY_INTERVAL_MS 30000
#define WEATHER_POLL_INTERVAL_MS 5000
#define WEATHER_FORECAST_DAYS 3
#define WEATHER_FORECAST_API_DAYS 4

static const char *TAG = "weather";

static lv_obj_t *weather_screen = NULL;
static lv_obj_t *weather_status_label = NULL;
static lv_obj_t *weather_summary_card = NULL;
static lv_obj_t *weather_day_label = NULL;
static lv_obj_t *weather_location_label = NULL;
static lv_obj_t *weather_condition_label = NULL;
static lv_obj_t *weather_temp_label = NULL;
static lv_obj_t *weather_rain_label = NULL;
static lv_obj_t *weather_range_label = NULL;
static lv_obj_t *weather_hint_label = NULL;
static lv_obj_t *weather_main_icon_cont = NULL;
static lv_obj_t *forecast_card_cont[WEATHER_FORECAST_DAYS] = {NULL};
static lv_obj_t *forecast_day_label[WEATHER_FORECAST_DAYS] = {NULL};
static lv_obj_t *forecast_icon_cont[WEATHER_FORECAST_DAYS] = {NULL};
static lv_obj_t *forecast_high_label[WEATHER_FORECAST_DAYS] = {NULL};
static lv_obj_t *forecast_low_label[WEATHER_FORECAST_DAYS] = {NULL};
static lv_obj_t *forecast_pop_label[WEATHER_FORECAST_DAYS] = {NULL};
static TaskHandle_t weather_task_handle = NULL;
static bool weather_netif_ready = false;
static volatile bool weather_refresh_requested = false;

static char current_weather_status[24] = "SET LOCATION";
static char current_weather_day[16] = "TODAY";
static char current_weather_location[64] = "Open Settings to save location";
static char current_weather_condition[32] = "--";
static char current_weather_temp[48] = "--";
static char current_weather_rain[24] = "Rain chance --%";
static char current_weather_heading[32] = "3-DAY FORECAST";
static char current_weather_hint[48] = "Set country code and postal code in Settings";
static char current_country_code[3] = "";
static char current_postal_code[20] = "";
static int current_weather_code = -1;
static int current_forecast_code[WEATHER_FORECAST_DAYS] = {-1, -1, -1};
static char current_forecast_day[WEATHER_FORECAST_DAYS][16] = {"--", "--", "--"};
static char current_forecast_high[WEATHER_FORECAST_DAYS][24] = {
    "H --",
    "H --",
    "H --"
};
static char current_forecast_low[WEATHER_FORECAST_DAYS][24] = {
    "L --",
    "L --",
    "L --"
};
static char current_forecast_pop[WEATHER_FORECAST_DAYS][20] = {
    "Rain --%",
    "Rain --%",
    "Rain --%"
};

static char weather_http_buf[WEATHER_HTTP_BUF_SIZE];
static int weather_http_len = 0;

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void weather_task(void *arg);
static bool weather_fetch_once(void);
static bool weather_ensure_netif(void);
static bool weather_fetch_buffer(const char *url);
static bool weather_geocode_open_meteo(double *latitude, double *longitude, char *location, size_t location_size);
static bool weather_geocode_zippopotam(double *latitude, double *longitude, char *location, size_t location_size);
static bool weather_parse_open_meteo_geocode(const char *json, double *latitude, double *longitude, char *location, size_t location_size);
static bool weather_parse_zippopotam_geocode(const char *json, double *latitude, double *longitude, char *location, size_t location_size);
static bool weather_parse_forecast(const char *json, float *current_c, int *weather_code);
static bool json_get_double_value(const char *json, const char *key, double *out);
static bool json_get_int_value(const char *json, const char *key, int *out);
static bool json_get_string_value(const char *json, const char *key, char *out, size_t out_size);
static int json_get_double_array(const char *json, const char *key, double *out, int max_items);
static int json_get_int_array(const char *json, const char *key, int *out, int max_items);
static int json_get_string_array(const char *json, const char *key, char out[][16], int max_items, size_t item_size);
static void weather_apply_cached(void);
static void weather_set_status(const char *status);
static const char *weather_condition_from_code(int weather_code);
static float weather_c_to_f(float temp_c);
static bool weather_use_celsius(void);
static void weather_format_temperature(float temp_c, char *out, size_t out_size);
static void weather_format_temperature_with_prefix(const char *prefix, float temp_c, char *out, size_t out_size);
static void weather_sync_location(bool *changed);
static void weather_reset_cached_data(void);
static void weather_url_encode_component(const char *src, char *dst, size_t dst_size);
static void weather_format_day_label(const char *iso_date, char *out, size_t out_size, int day_index);
static void weather_render_icon(lv_obj_t *parent, int weather_code, bool compact);
static void weather_clear_children(lv_obj_t *parent);
static void weather_style_surface(lv_obj_t *obj, bool featured);
static void weather_add_glass_highlight(lv_obj_t *parent, bool featured);
static void weather_tint_surface(lv_obj_t *obj, int weather_code, bool featured);
static lv_obj_t *weather_create_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color, lv_opa_t opa, lv_coord_t radius);
static void weather_draw_cloud(lv_obj_t *parent, lv_coord_t size, lv_color_t color, lv_opa_t opa);
static void weather_draw_sun(lv_obj_t *parent, lv_coord_t size, lv_color_t color, lv_opa_t opa, bool rays_only);
static void weather_draw_partly_cloudy(lv_obj_t *parent, lv_coord_t size, lv_color_t sun_color, lv_color_t cloud_color);
static lv_color_t weather_surface_bg_color(int weather_code, bool featured);
static lv_color_t weather_surface_grad_color(int weather_code, bool featured);

void weather_service_start(void)
{
    if (weather_task_handle == NULL)
    {
        if (xTaskCreate(weather_task, "weather_task", 7168, NULL, 5, &weather_task_handle) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to start weather task");
            weather_task_handle = NULL;
        }
    }
}

void weather_service_request_refresh(void)
{
    weather_refresh_requested = true;
}

static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0)
    {
        int copy_len = evt->data_len;
        if (weather_http_len + copy_len >= WEATHER_HTTP_BUF_SIZE)
        {
            copy_len = WEATHER_HTTP_BUF_SIZE - weather_http_len - 1;
        }

        if (copy_len > 0)
        {
            memcpy(weather_http_buf + weather_http_len, evt->data, copy_len);
            weather_http_len += copy_len;
            weather_http_buf[weather_http_len] = '\0';
        }
    }

    return ESP_OK;
}

void weather_screen_create(void)
{
    bool location_changed = false;
    const bool woods = ui_theme_get_current() == UI_THEME_WOODS;

    if (weather_screen != NULL)
    {
        return;
    }

    weather_sync_location(&location_changed);
    if (location_changed)
    {
        weather_reset_cached_data();
    }

    weather_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(weather_screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(weather_screen, LV_OPA_COVER, 0);
    screen_background_apply(weather_screen);
    lv_obj_clear_flag(weather_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(weather_screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(weather_screen);
    lv_label_set_text(title, "LOCAL WEATHER");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 34, 18);

    weather_status_label = lv_label_create(weather_screen);
    lv_label_set_text(weather_status_label, current_weather_status);
    lv_obj_add_flag(weather_status_label, LV_OBJ_FLAG_HIDDEN);

    weather_day_label = lv_label_create(weather_screen);
    lv_label_set_text(weather_day_label, current_weather_day);
    lv_obj_set_style_text_color(weather_day_label, lv_color_hex(0xCABEB2), 0);
    lv_obj_set_style_text_font(weather_day_label, &lv_font_montserrat_18, 0);

    weather_location_label = lv_label_create(weather_screen);
    lv_label_set_text(weather_location_label, current_weather_location);
    lv_obj_set_width(weather_location_label, 520);
    lv_obj_set_style_text_align(weather_location_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(weather_location_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(weather_location_label, &lv_font_montserrat_24, 0);

    weather_summary_card = lv_obj_create(weather_screen);
    lv_obj_set_size(weather_summary_card, 760, 172);
    lv_obj_align(weather_summary_card, LV_ALIGN_TOP_MID, 0, 56);
    weather_style_surface(weather_summary_card, true);
    weather_add_glass_highlight(weather_summary_card, true);

    weather_main_icon_cont = lv_obj_create(weather_summary_card);
    lv_obj_set_size(weather_main_icon_cont, 142, 128);
    lv_obj_align(weather_main_icon_cont, LV_ALIGN_LEFT_MID, 20, 0);
    translucent_card_apply(weather_main_icon_cont, 30, woods ? LV_OPA_40 : LV_OPA_20);
    if (woods)
    {
        lv_obj_set_style_bg_color(weather_main_icon_cont, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(weather_main_icon_cont, LV_OPA_40, 0);
        lv_obj_set_style_border_width(weather_main_icon_cont, 0, 0);
        lv_obj_set_style_shadow_width(weather_main_icon_cont, 0, 0);
    }
    lv_obj_set_style_pad_all(weather_main_icon_cont, 0, 0);
    lv_obj_clear_flag(weather_main_icon_cont, LV_OBJ_FLAG_SCROLLABLE);
    weather_add_glass_highlight(weather_main_icon_cont, true);

    lv_obj_set_parent(weather_day_label, weather_summary_card);
    lv_obj_align(weather_day_label, LV_ALIGN_TOP_RIGHT, -22, 16);

    lv_obj_set_parent(weather_location_label, weather_summary_card);
    lv_obj_align(weather_location_label, LV_ALIGN_TOP_LEFT, 188, 14);
    lv_obj_set_style_text_color(weather_location_label, lv_color_hex(0xBEB2A6), 0);
    lv_obj_set_style_text_font(weather_location_label, &lv_font_montserrat_22, 0);

    weather_condition_label = lv_label_create(weather_summary_card);
    lv_label_set_text(weather_condition_label, current_weather_condition);
    lv_obj_set_width(weather_condition_label, 520);
    lv_obj_set_style_text_align(weather_condition_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(weather_condition_label, lv_color_hex(0xD9CDC1), 0);
    lv_obj_set_style_text_font(weather_condition_label, &lv_font_montserrat_26, 0);
    lv_obj_align(weather_condition_label, LV_ALIGN_TOP_LEFT, 188, 48);

    weather_temp_label = lv_label_create(weather_summary_card);
    lv_label_set_text(weather_temp_label, current_weather_temp);
    lv_obj_set_width(weather_temp_label, 520);
    lv_obj_set_style_text_align(weather_temp_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(weather_temp_label, lv_color_hex(0xECE4DA), 0);
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_48, 0);
    lv_obj_align(weather_temp_label, LV_ALIGN_TOP_LEFT, 188, 78);

    weather_rain_label = lv_label_create(weather_summary_card);
    lv_label_set_text(weather_rain_label, current_weather_rain);
    lv_obj_set_width(weather_rain_label, 520);
    lv_obj_set_style_text_align(weather_rain_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(weather_rain_label, lv_color_hex(0xA5937E), 0);
    lv_obj_set_style_text_font(weather_rain_label, &lv_font_montserrat_18, 0);
    lv_obj_align(weather_rain_label, LV_ALIGN_TOP_LEFT, 188, 138);

    weather_hint_label = lv_label_create(weather_summary_card);
    lv_label_set_text(weather_hint_label, current_weather_hint);
    lv_obj_set_width(weather_hint_label, 520);
    lv_obj_set_style_text_align(weather_hint_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(weather_hint_label, lv_color_hex(0x71675D), 0);
    lv_obj_set_style_text_font(weather_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_hint_label, LV_ALIGN_TOP_LEFT, 188, 154);

    weather_range_label = lv_label_create(weather_screen);
    lv_label_set_text(weather_range_label, current_weather_heading);
    lv_obj_set_style_text_color(weather_range_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(weather_range_label, &lv_font_montserrat_18, 0);
    lv_obj_align(weather_range_label, LV_ALIGN_TOP_LEFT, 42, 246);

    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++)
    {
        forecast_card_cont[i] = lv_obj_create(weather_screen);
        lv_obj_set_size(forecast_card_cont[i], 232, 134);
        lv_obj_align(forecast_card_cont[i], LV_ALIGN_TOP_LEFT, 34 + (i * 250), 274);
        weather_style_surface(forecast_card_cont[i], false);
        weather_add_glass_highlight(forecast_card_cont[i], false);

        forecast_day_label[i] = lv_label_create(forecast_card_cont[i]);
        lv_label_set_text(forecast_day_label[i], current_forecast_day[i]);
        lv_obj_set_width(forecast_day_label[i], 196);
        lv_obj_set_style_text_color(forecast_day_label[i], lv_color_hex(0xCABEB2), 0);
        lv_obj_set_style_text_font(forecast_day_label[i], &lv_font_montserrat_18, 0);
        lv_obj_align(forecast_day_label[i], LV_ALIGN_TOP_LEFT, 18, 14);

        forecast_icon_cont[i] = lv_obj_create(forecast_card_cont[i]);
        lv_obj_set_size(forecast_icon_cont[i], 70, 70);
        lv_obj_align(forecast_icon_cont[i], LV_ALIGN_TOP_LEFT, 14, 40);
        translucent_card_apply(forecast_icon_cont[i], 22, woods ? LV_OPA_30 : LV_OPA_20);
        if (woods)
        {
            lv_obj_set_style_bg_color(forecast_icon_cont[i], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(forecast_icon_cont[i], LV_OPA_30, 0);
            lv_obj_set_style_border_width(forecast_icon_cont[i], 0, 0);
            lv_obj_set_style_shadow_width(forecast_icon_cont[i], 0, 0);
        }
        lv_obj_set_style_pad_all(forecast_icon_cont[i], 0, 0);
        lv_obj_clear_flag(forecast_icon_cont[i], LV_OBJ_FLAG_SCROLLABLE);
        weather_add_glass_highlight(forecast_icon_cont[i], false);

        forecast_high_label[i] = lv_label_create(forecast_card_cont[i]);
        lv_label_set_text(forecast_high_label[i], current_forecast_high[i]);
        lv_obj_set_width(forecast_high_label[i], 112);
        lv_obj_set_style_text_color(forecast_high_label[i], lv_color_hex(0xD8CCC0), 0);
        lv_obj_set_style_text_font(forecast_high_label[i], &lv_font_montserrat_18, 0);
        lv_obj_align(forecast_high_label[i], LV_ALIGN_TOP_LEFT, 108, 42);

        forecast_low_label[i] = lv_label_create(forecast_card_cont[i]);
        lv_label_set_text(forecast_low_label[i], current_forecast_low[i]);
        lv_obj_set_width(forecast_low_label[i], 112);
        lv_obj_set_style_text_color(forecast_low_label[i], lv_color_hex(0x97897D), 0);
        lv_obj_set_style_text_font(forecast_low_label[i], &lv_font_montserrat_18, 0);
        lv_obj_align(forecast_low_label[i], LV_ALIGN_TOP_LEFT, 108, 68);

        forecast_pop_label[i] = lv_label_create(forecast_card_cont[i]);
        lv_label_set_text(forecast_pop_label[i], current_forecast_pop[i]);
        lv_obj_set_width(forecast_pop_label[i], 112);
        lv_obj_set_style_text_color(forecast_pop_label[i], lv_color_hex(0xA08D78), 0);
        lv_obj_set_style_text_font(forecast_pop_label[i], &lv_font_montserrat_16, 0);
        lv_obj_align(forecast_pop_label[i], LV_ALIGN_TOP_LEFT, 108, 95);
    }

    lv_obj_t *bottom_nav = lv_obj_create(weather_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, weather_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, weather_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, weather_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, weather_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_BITCOIN, weather_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_WEATHER, NULL, true);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_CHART, weather_night_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, weather_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, weather_settings_clicked, false);

    weather_apply_cached();
    weather_service_start();
}

void weather_screen_destroy(void)
{
    if (weather_screen)
    {
        lv_obj_del(weather_screen);
        weather_screen = NULL;
        weather_status_label = NULL;
        weather_summary_card = NULL;
        weather_day_label = NULL;
        weather_location_label = NULL;
        weather_condition_label = NULL;
        weather_temp_label = NULL;
        weather_rain_label = NULL;
        weather_range_label = NULL;
        weather_hint_label = NULL;
        weather_main_icon_cont = NULL;
        for (int i = 0; i < WEATHER_FORECAST_DAYS; i++)
        {
            forecast_card_cont[i] = NULL;
            forecast_day_label[i] = NULL;
            forecast_icon_cont[i] = NULL;
            forecast_high_label[i] = NULL;
            forecast_low_label[i] = NULL;
            forecast_pop_label[i] = NULL;
        }
    }
}

lv_obj_t *weather_get_screen(void)
{
    return weather_screen;
}

static void weather_task(void *arg)
{
    int64_t next_fetch_at_ms = 0;

    (void)arg;

    while (1)
    {
        bool location_changed = false;
        weather_sync_location(&location_changed);

        if (location_changed)
        {
            next_fetch_at_ms = 0;
            weather_refresh_requested = false;
            weather_reset_cached_data();
            if (lvgl_port_lock(50))
            {
                weather_apply_cached();
                lvgl_port_unlock();
            }
        }

        if (ota_update_is_running())
        {
            vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
            continue;
        }

        if (current_postal_code[0] == '\0')
        {
            weather_set_status("SET LOCATION");
            if (lvgl_port_lock(50))
            {
                weather_apply_cached();
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
            continue;
        }

        if (!wifi_is_connected())
        {
            weather_set_status("WAITING FOR WIFI");
            if (lvgl_port_lock(50))
            {
                weather_apply_cached();
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
            continue;
        }

        if (!weather_ensure_netif())
        {
            weather_set_status("NETIF ERROR");
            if (lvgl_port_lock(50))
            {
                weather_apply_cached();
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
            continue;
        }

        if (!weather_refresh_requested)
        {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms < next_fetch_at_ms)
            {
                vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
                continue;
            }
        }

        weather_refresh_requested = false;
        weather_set_status("LOOKING UP LOCATION");
        if (lvgl_port_lock(50))
        {
            weather_apply_cached();
            lvgl_port_unlock();
        }

        bool updated = weather_fetch_once();
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (updated)
        {
            weather_set_status("LIVE");
        }
        else if (strcmp(current_weather_status, "LOOKUP FAILED") != 0)
        {
            weather_set_status("RETRYING...");
        }
        if (lvgl_port_lock(50))
        {
            weather_apply_cached();
            lvgl_port_unlock();
        }

        next_fetch_at_ms = now_ms + (updated ? WEATHER_REFRESH_INTERVAL_MS : WEATHER_RETRY_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
    }
}

static bool weather_fetch_once(void)
{
    char location_text[64];
    double latitude = 0.0;
    double longitude = 0.0;
    float current_c = 0.0f;
    int forecast_codes[WEATHER_FORECAST_API_DAYS] = {-1, -1, -1, -1};
    double forecast_highs[WEATHER_FORECAST_API_DAYS] = {0.0, 0.0, 0.0, 0.0};
    double forecast_lows[WEATHER_FORECAST_API_DAYS] = {0.0, 0.0, 0.0, 0.0};
    double forecast_pop[WEATHER_FORECAST_API_DAYS] = {0.0, 0.0, 0.0, 0.0};
    char forecast_dates[WEATHER_FORECAST_API_DAYS][16] = {{0}};
    char forecast_url[320];

    if (!weather_geocode_open_meteo(&latitude, &longitude, location_text, sizeof(location_text)) &&
        !weather_geocode_zippopotam(&latitude, &longitude, location_text, sizeof(location_text)))
    {
        weather_reset_cached_data();
        weather_set_status("LOOKUP FAILED");
        snprintf(current_weather_location, sizeof(current_weather_location), "%s %s not found",
                 current_country_code, current_postal_code);
        strncpy(current_weather_hint, "Try another postal code or country code", sizeof(current_weather_hint) - 1);
        current_weather_hint[sizeof(current_weather_hint) - 1] = '\0';
        return false;
    }

    snprintf(forecast_url, sizeof(forecast_url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=%d",
             latitude, longitude, WEATHER_FORECAST_API_DAYS);
    weather_set_status("FETCHING FORECAST");

    if (!weather_fetch_buffer(forecast_url))
    {
        return false;
    }

    if (!weather_parse_forecast(weather_http_buf, &current_c, &current_weather_code))
    {
        return false;
    }

    if (json_get_int_array(weather_http_buf, "\"weather_code\":[", forecast_codes, WEATHER_FORECAST_API_DAYS) < WEATHER_FORECAST_API_DAYS ||
        json_get_double_array(weather_http_buf, "\"temperature_2m_max\":[", forecast_highs, WEATHER_FORECAST_API_DAYS) < WEATHER_FORECAST_API_DAYS ||
        json_get_double_array(weather_http_buf, "\"temperature_2m_min\":[", forecast_lows, WEATHER_FORECAST_API_DAYS) < WEATHER_FORECAST_API_DAYS ||
        json_get_double_array(weather_http_buf, "\"precipitation_probability_max\":[", forecast_pop, WEATHER_FORECAST_API_DAYS) < WEATHER_FORECAST_API_DAYS ||
        json_get_string_array(weather_http_buf, "\"time\":[", forecast_dates, WEATHER_FORECAST_API_DAYS, sizeof(forecast_dates[0])) < WEATHER_FORECAST_API_DAYS)
    {
        return false;
    }

    strncpy(current_weather_location, location_text, sizeof(current_weather_location) - 1);
    current_weather_location[sizeof(current_weather_location) - 1] = '\0';
    weather_format_day_label(forecast_dates[0], current_weather_day, sizeof(current_weather_day), 0);
    strncpy(current_weather_condition, weather_condition_from_code(current_weather_code), sizeof(current_weather_condition) - 1);
    current_weather_condition[sizeof(current_weather_condition) - 1] = '\0';
    weather_format_temperature(current_c, current_weather_temp, sizeof(current_weather_temp));
    snprintf(current_weather_rain, sizeof(current_weather_rain), "Rain chance %d%%", (int)(forecast_pop[0] + 0.5));
    strncpy(current_weather_heading, "3-DAY FORECAST", sizeof(current_weather_heading) - 1);
    current_weather_heading[sizeof(current_weather_heading) - 1] = '\0';
    current_weather_hint[0] = '\0';
    current_weather_hint[sizeof(current_weather_hint) - 1] = '\0';

    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++)
    {
        int source_index = i + 1;
        current_forecast_code[i] = forecast_codes[source_index];
        weather_format_day_label(forecast_dates[source_index], current_forecast_day[i], sizeof(current_forecast_day[i]), source_index);
        weather_format_temperature_with_prefix("H", (float)forecast_highs[source_index], current_forecast_high[i], sizeof(current_forecast_high[i]));
        weather_format_temperature_with_prefix("L", (float)forecast_lows[source_index], current_forecast_low[i], sizeof(current_forecast_low[i]));
        snprintf(current_forecast_pop[i], sizeof(current_forecast_pop[i]), "Rain %d%%", (int)(forecast_pop[source_index] + 0.5));
    }

    return true;
}

static bool weather_ensure_netif(void)
{
    if (weather_netif_ready)
    {
        return true;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return false;
    }

    weather_netif_ready = true;
    return true;
}

static bool weather_fetch_buffer(const char *url)
{
    if (!url)
    {
        return false;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
    };

    weather_http_len = 0;
    weather_http_buf[0] = '\0';

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300 || weather_http_len == 0)
    {
        return false;
    }

    return true;
}

static bool weather_geocode_open_meteo(double *latitude, double *longitude, char *location, size_t location_size)
{
    char encoded_postal[64];
    char geocode_url[192];

    weather_url_encode_component(current_postal_code, encoded_postal, sizeof(encoded_postal));
    snprintf(geocode_url, sizeof(geocode_url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json&countryCode=%s",
             encoded_postal, current_country_code);

    if (!weather_fetch_buffer(geocode_url))
    {
        ESP_LOGW(TAG, "Open-Meteo geocode failed for %s %s", current_country_code, current_postal_code);
        return false;
    }

    return weather_parse_open_meteo_geocode(weather_http_buf, latitude, longitude, location, location_size);
}

static bool weather_geocode_zippopotam(double *latitude, double *longitude, char *location, size_t location_size)
{
    char encoded_postal[64];
    char geocode_url[160];

    weather_url_encode_component(current_postal_code, encoded_postal, sizeof(encoded_postal));
    snprintf(geocode_url, sizeof(geocode_url), "https://api.zippopotam.us/%s/%s", current_country_code, encoded_postal);
    weather_set_status("TRYING BACKUP LOOKUP");

    if (!weather_fetch_buffer(geocode_url))
    {
        ESP_LOGW(TAG, "Zippopotam geocode failed for %s %s", current_country_code, current_postal_code);
        return false;
    }

    return weather_parse_zippopotam_geocode(weather_http_buf, latitude, longitude, location, location_size);
}

static bool weather_parse_open_meteo_geocode(const char *json, double *latitude, double *longitude, char *location, size_t location_size)
{
    char name[32] = "";

    if (!json || !latitude || !longitude || !location || location_size == 0)
    {
        return false;
    }

    if (!strstr(json, "\"results\":["))
    {
        return false;
    }

    if (!json_get_double_value(json, "\"latitude\":", latitude) ||
        !json_get_double_value(json, "\"longitude\":", longitude) ||
        !json_get_string_value(json, "\"name\":\"", name, sizeof(name)))
    {
        return false;
    }

    snprintf(location, location_size, "%s", name);

    return true;
}

static bool weather_parse_zippopotam_geocode(const char *json, double *latitude, double *longitude, char *location, size_t location_size)
{
    char place_name[32] = "";
    char lat_text[20] = "";
    char lon_text[20] = "";

    if (!json || !latitude || !longitude || !location || location_size == 0)
    {
        return false;
    }

    if (!strstr(json, "\"places\":["))
    {
        return false;
    }

    if (!json_get_string_value(json, "\"place name\":\"", place_name, sizeof(place_name)) ||
        !json_get_string_value(json, "\"latitude\": \"", lat_text, sizeof(lat_text)) ||
        !json_get_string_value(json, "\"longitude\": \"", lon_text, sizeof(lon_text)))
    {
        return false;
    }

    *latitude = strtod(lat_text, NULL);
    *longitude = strtod(lon_text, NULL);
    if (*latitude == 0.0 && *longitude == 0.0)
    {
        return false;
    }

    snprintf(location, location_size, "%s", place_name);

    return true;
}

static bool weather_parse_forecast(const char *json, float *current_c, int *weather_code)
{
    const char *current_obj = strstr(json, "\"current\":{");
    double current_temp = 0.0;
    int code = -1;

    if (!json || !current_c || !weather_code || !current_obj)
    {
        return false;
    }

    if (!json_get_double_value(current_obj, "\"temperature_2m\":", &current_temp) ||
        !json_get_int_value(current_obj, "\"weather_code\":", &code))
    {
        return false;
    }

    *current_c = (float)current_temp;
    *weather_code = code;
    return true;
}

static bool json_get_double_value(const char *json, const char *key, double *out)
{
    const char *value_ptr = NULL;

    if (!json || !key || !out)
    {
        return false;
    }

    value_ptr = strstr(json, key);
    if (!value_ptr)
    {
        return false;
    }

    value_ptr += strlen(key);
    *out = strtod(value_ptr, NULL);
    return true;
}

static bool json_get_int_value(const char *json, const char *key, int *out)
{
    const char *value_ptr = NULL;

    if (!json || !key || !out)
    {
        return false;
    }

    value_ptr = strstr(json, key);
    if (!value_ptr)
    {
        return false;
    }

    value_ptr += strlen(key);
    *out = (int)strtol(value_ptr, NULL, 10);
    return true;
}

static bool json_get_string_value(const char *json, const char *key, char *out, size_t out_size)
{
    const char *start = NULL;
    size_t i = 0;

    if (!json || !key || !out || out_size == 0)
    {
        return false;
    }

    start = strstr(json, key);
    if (!start)
    {
        return false;
    }

    start += strlen(key);
    while (*start != '\0' && *start != '"' && i < out_size - 1)
    {
        if (*start == '\\' && start[1] != '\0')
        {
            start++;
        }
        out[i++] = *start++;
    }
    out[i] = '\0';

    return i > 0;
}

static int json_get_double_array(const char *json, const char *key, double *out, int max_items)
{
    const char *p = NULL;
    int count = 0;

    if (!json || !key || !out || max_items <= 0)
    {
        return 0;
    }

    p = strstr(json, key);
    if (!p)
    {
        return 0;
    }

    p += strlen(key);
    while (*p != '\0' && *p != ']' && count < max_items)
    {
        while (*p == ' ' || *p == '\n')
        {
            p++;
        }
        out[count++] = strtod(p, (char **)&p);
        while (*p == ' ' || *p == ',')
        {
            p++;
        }
    }

    return count;
}

static int json_get_int_array(const char *json, const char *key, int *out, int max_items)
{
    const char *p = NULL;
    int count = 0;

    if (!json || !key || !out || max_items <= 0)
    {
        return 0;
    }

    p = strstr(json, key);
    if (!p)
    {
        return 0;
    }

    p += strlen(key);
    while (*p != '\0' && *p != ']' && count < max_items)
    {
        while (*p == ' ' || *p == '\n')
        {
            p++;
        }
        out[count++] = (int)strtol(p, (char **)&p, 10);
        while (*p == ' ' || *p == ',')
        {
            p++;
        }
    }

    return count;
}

static int json_get_string_array(const char *json, const char *key, char out[][16], int max_items, size_t item_size)
{
    const char *p = NULL;
    int count = 0;

    if (!json || !key || !out || max_items <= 0 || item_size == 0)
    {
        return 0;
    }

    p = strstr(json, key);
    if (!p)
    {
        return 0;
    }

    p += strlen(key);
    while (*p != '\0' && *p != ']' && count < max_items)
    {
        size_t i = 0;
        while (*p != '\0' && *p != '"')
        {
            p++;
        }
        if (*p != '"')
        {
            break;
        }
        p++;
        while (*p != '\0' && *p != '"' && i < item_size - 1)
        {
            out[count][i++] = *p++;
        }
        out[count][i] = '\0';
        while (*p != '\0' && *p != '"')
        {
            p++;
        }
        if (*p == '"')
        {
            p++;
        }
        count++;
        while (*p == ' ' || *p == ',')
        {
            p++;
        }
    }

    return count;
}

static void weather_apply_cached(void)
{
    if (weather_status_label)
    {
        lv_label_set_text(weather_status_label, current_weather_status);
        lv_color_t status_bg = COLOR_CARD_BG;
        lv_color_t status_fg = COLOR_TEXT_PRIMARY;
        if (strcmp(current_weather_status, "LIVE") == 0)
        {
            status_bg = lv_color_hex(0x1E8E3E);
        }
        else if (strcmp(current_weather_status, "LOOKUP FAILED") == 0)
        {
            status_bg = COLOR_RED;
        }
        lv_obj_set_style_bg_color(weather_status_label, status_bg, 0);
        lv_obj_set_style_text_color(weather_status_label, status_fg, 0);
    }
    if (weather_location_label)
    {
        lv_label_set_text(weather_location_label, current_weather_location);
    }
    if (weather_day_label)
    {
        lv_label_set_text(weather_day_label, current_weather_day);
    }
    if (weather_condition_label)
    {
        lv_label_set_text(weather_condition_label, current_weather_condition);
    }
    if (weather_temp_label)
    {
        lv_label_set_text(weather_temp_label, current_weather_temp);
    }
    if (weather_rain_label)
    {
        lv_label_set_text(weather_rain_label, current_weather_rain);
    }
    if (weather_range_label)
    {
        lv_label_set_text(weather_range_label, current_weather_heading);
    }
    if (weather_hint_label)
    {
        lv_label_set_text(weather_hint_label, current_weather_hint);
    }
    if (weather_main_icon_cont)
    {
        weather_render_icon(weather_main_icon_cont, current_weather_code, false);
    }
    if (weather_summary_card)
    {
        weather_tint_surface(weather_summary_card, current_weather_code, true);
    }

    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++)
    {
        if (forecast_day_label[i])
        {
            lv_label_set_text(forecast_day_label[i], current_forecast_day[i]);
        }
        if (forecast_high_label[i])
        {
            lv_label_set_text(forecast_high_label[i], current_forecast_high[i]);
        }
        if (forecast_low_label[i])
        {
            lv_label_set_text(forecast_low_label[i], current_forecast_low[i]);
        }
        if (forecast_pop_label[i])
        {
            lv_label_set_text(forecast_pop_label[i], current_forecast_pop[i]);
        }
        if (forecast_icon_cont[i])
        {
            weather_render_icon(forecast_icon_cont[i], current_forecast_code[i], true);
        }
        if (forecast_card_cont[i])
        {
            weather_tint_surface(forecast_card_cont[i], current_forecast_code[i], false);
        }
    }
}

static void weather_set_status(const char *status)
{
    if (!status)
    {
        return;
    }

    strncpy(current_weather_status, status, sizeof(current_weather_status) - 1);
    current_weather_status[sizeof(current_weather_status) - 1] = '\0';

    ESP_LOGI(TAG, "Status: %s", current_weather_status);
}

static const char *weather_condition_from_code(int weather_code)
{
    switch (weather_code)
    {
        case 0:
            return "Clear";
        case 1:
            return "Mainly Clear";
        case 2:
            return "Partly Cloudy";
        case 3:
            return "Overcast";
        case 45:
        case 48:
            return "Fog";
        case 51:
        case 53:
        case 55:
            return "Drizzle";
        case 56:
        case 57:
            return "Freezing Drizzle";
        case 61:
        case 63:
        case 65:
            return "Rain";
        case 66:
        case 67:
            return "Freezing Rain";
        case 71:
        case 73:
        case 75:
            return "Snow";
        case 77:
            return "Snow Grains";
        case 80:
        case 81:
        case 82:
            return "Rain Showers";
        case 85:
        case 86:
            return "Snow Showers";
        case 95:
            return "Thunderstorm";
        case 96:
        case 99:
            return "Storm With Hail";
        default:
            return "Unknown";
    }
}

static float weather_c_to_f(float temp_c)
{
    return (temp_c * 9.0f / 5.0f) + 32.0f;
}

static bool weather_use_celsius(void)
{
    return settings_get_weather_temperature_unit() == WEATHER_TEMPERATURE_UNIT_C;
}

static void weather_format_temperature(float temp_c, char *out, size_t out_size)
{
    float display_temp = weather_use_celsius() ? temp_c : weather_c_to_f(temp_c);
    char unit = weather_use_celsius() ? 'C' : 'F';
    lv_snprintf(out, out_size, "%d%c", (int)(display_temp + (display_temp >= 0.0f ? 0.5f : -0.5f)), unit);
}

static void weather_format_temperature_with_prefix(const char *prefix, float temp_c, char *out, size_t out_size)
{
    char value[12];
    weather_format_temperature(temp_c, value, sizeof(value));
    lv_snprintf(out, out_size, "%s %s", prefix, value);
}

static void weather_sync_location(bool *changed)
{
    char new_country[3];
    char new_postal[20];

    if (changed)
    {
        *changed = false;
    }

    strncpy(new_country, settings_get_weather_country_code(), sizeof(new_country) - 1);
    new_country[sizeof(new_country) - 1] = '\0';
    strncpy(new_postal, settings_get_weather_postal_code(), sizeof(new_postal) - 1);
    new_postal[sizeof(new_postal) - 1] = '\0';

    if (strcmp(current_country_code, new_country) != 0 || strcmp(current_postal_code, new_postal) != 0)
    {
        strncpy(current_country_code, new_country, sizeof(current_country_code) - 1);
        current_country_code[sizeof(current_country_code) - 1] = '\0';
        strncpy(current_postal_code, new_postal, sizeof(current_postal_code) - 1);
        current_postal_code[sizeof(current_postal_code) - 1] = '\0';

        if (changed)
        {
            *changed = true;
        }
    }
}

static void weather_reset_cached_data(void)
{
    current_weather_code = -1;
    if (current_postal_code[0] == '\0')
    {
        strncpy(current_weather_status, "SET LOCATION", sizeof(current_weather_status) - 1);
        strncpy(current_weather_day, "TODAY", sizeof(current_weather_day) - 1);
        strncpy(current_weather_location, "Open Settings to save location", sizeof(current_weather_location) - 1);
        strncpy(current_weather_hint, "Set country code and postal code in Settings", sizeof(current_weather_hint) - 1);
    }
    else
    {
        strncpy(current_weather_status, "LOADING...", sizeof(current_weather_status) - 1);
        strncpy(current_weather_day, "TODAY", sizeof(current_weather_day) - 1);
        snprintf(current_weather_location, sizeof(current_weather_location), "%s %s",
                 current_country_code, current_postal_code);
        strncpy(current_weather_hint, "Looking up postal code and forecast", sizeof(current_weather_hint) - 1);
    }

    current_weather_status[sizeof(current_weather_status) - 1] = '\0';
    current_weather_day[sizeof(current_weather_day) - 1] = '\0';
    current_weather_location[sizeof(current_weather_location) - 1] = '\0';
    current_weather_hint[sizeof(current_weather_hint) - 1] = '\0';
    strncpy(current_weather_condition, "--", sizeof(current_weather_condition) - 1);
    current_weather_condition[sizeof(current_weather_condition) - 1] = '\0';
    strncpy(current_weather_temp, "--", sizeof(current_weather_temp) - 1);
    current_weather_temp[sizeof(current_weather_temp) - 1] = '\0';
    strncpy(current_weather_rain, "Rain chance --%", sizeof(current_weather_rain) - 1);
    current_weather_rain[sizeof(current_weather_rain) - 1] = '\0';
    strncpy(current_weather_heading, "3-DAY FORECAST", sizeof(current_weather_heading) - 1);
    current_weather_heading[sizeof(current_weather_heading) - 1] = '\0';

    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++)
    {
        current_forecast_code[i] = -1;
        strncpy(current_forecast_day[i], "--", sizeof(current_forecast_day[i]) - 1);
        current_forecast_day[i][sizeof(current_forecast_day[i]) - 1] = '\0';
        strncpy(current_forecast_high[i], "H --", sizeof(current_forecast_high[i]) - 1);
        current_forecast_high[i][sizeof(current_forecast_high[i]) - 1] = '\0';
        strncpy(current_forecast_low[i], "L --", sizeof(current_forecast_low[i]) - 1);
        current_forecast_low[i][sizeof(current_forecast_low[i]) - 1] = '\0';
        strncpy(current_forecast_pop[i], "Rain --%", sizeof(current_forecast_pop[i]) - 1);
        current_forecast_pop[i][sizeof(current_forecast_pop[i]) - 1] = '\0';
    }
}

static void weather_url_encode_component(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i = 0;

    if (!dst || dst_size == 0)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    while (*src != '\0' && i + 1 < dst_size)
    {
        unsigned char c = (unsigned char)*src++;
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            dst[i++] = (char)c;
        }
        else
        {
            if (i + 3 >= dst_size)
            {
                break;
            }
            dst[i++] = '%';
            dst[i++] = hex[(c >> 4) & 0x0F];
            dst[i++] = hex[c & 0x0F];
        }
    }

    dst[i] = '\0';
}

static void weather_format_day_label(const char *iso_date, char *out, size_t out_size, int day_index)
{
    struct tm tm_info = {0};
    int year = 0;
    int month = 0;
    int day = 0;

    if (!out || out_size == 0)
    {
        return;
    }

    if (!iso_date || sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3)
    {
        lv_snprintf(out, out_size, "DAY %d", day_index + 1);
        return;
    }

    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_isdst = -1;
    mktime(&tm_info);
    strftime(out, out_size, "%A", &tm_info);
}

static void weather_render_icon(lv_obj_t *parent, int weather_code, bool compact)
{
    lv_coord_t size = compact ? 70 : 128;
    lv_color_t warm = lv_color_hex(0xF5C75C);
    lv_color_t cloud = lv_color_hex(0xD6DEE8);
    lv_color_t cool = lv_color_hex(0x7BC9FF);
    lv_color_t snow = lv_color_hex(0xF3F8FD);
    lv_color_t storm = lv_color_hex(0xFFD25C);
    lv_color_t fog = lv_color_hex(0xBDC6D2);

    if (!parent)
    {
        return;
    }

    weather_clear_children(parent);
    lv_obj_set_style_bg_color(parent, ui_theme_get_surface_fill_color(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 1, 0);
    lv_obj_set_style_border_color(parent, ui_theme_get_surface_outline_color(), 0);
    lv_obj_set_style_border_opa(parent, LV_OPA_COVER, 0);

    if (weather_code < 0)
    {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, "--");
        lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(label, compact ? &lv_font_montserrat_18 : &lv_font_montserrat_28, 0);
        lv_obj_center(label);
        return;
    }

    if (weather_code == 0 || weather_code == 1)
    {
        weather_draw_sun(parent, size, warm, LV_OPA_COVER, false);
        return;
    }

    if (weather_code == 2)
    {
        weather_draw_partly_cloudy(parent, size, warm, cloud);
        return;
    }

    weather_draw_cloud(parent, size, cloud, LV_OPA_COVER);

    if (weather_code == 3)
    {
        return;
    }

    if (weather_code == 45 || weather_code == 48)
    {
        weather_create_shape(parent, size / 5, size * 3 / 4 - 4, size * 3 / 5, 4, fog, LV_OPA_80, LV_RADIUS_CIRCLE);
        weather_create_shape(parent, size / 4, size * 3 / 4 + 8, size / 2, 4, fog, LV_OPA_70, LV_RADIUS_CIRCLE);
        return;
    }

    if ((weather_code >= 51 && weather_code <= 67) || (weather_code >= 80 && weather_code <= 82))
    {
        weather_create_shape(parent, size / 3 - 2, size * 3 / 4 - 2, 4, size / 7 + 2, cool, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        weather_create_shape(parent, size / 2 - 2, size * 3 / 4 + 4, 4, size / 7 + 2, cool, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        weather_create_shape(parent, size * 2 / 3 - 2, size * 3 / 4 - 1, 4, size / 7 + 2, cool, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        return;
    }

    if ((weather_code >= 71 && weather_code <= 77) || weather_code == 85 || weather_code == 86)
    {
        weather_create_shape(parent, size / 3 - 4, size * 3 / 4, 8, 8, snow, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        weather_create_shape(parent, size / 2 - 4, size * 3 / 4 + 6, 8, 8, snow, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        weather_create_shape(parent, size * 2 / 3 - 4, size * 3 / 4, 8, 8, snow, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        return;
    }

    if (weather_code >= 95)
    {
        weather_create_shape(parent, size / 2 - 3, size * 2 / 3 - 4, 6, size / 5, storm, LV_OPA_COVER, 3);
        weather_create_shape(parent, size / 2 - 10, size * 2 / 3 + 6, 20, 6, storm, LV_OPA_COVER, 3);
    }
}

static void weather_style_surface(lv_obj_t *obj, bool featured)
{
    if (!obj)
    {
        return;
    }

    if (ui_theme_get_current() == UI_THEME_WOODS)
    {
        translucent_card_apply(obj, featured ? 26 : 22, featured ? (lv_opa_t)115 : LV_OPA_40);
        lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(obj, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(obj, featured ? (lv_opa_t)115 : LV_OPA_40, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
    }
    else
    {
        translucent_card_apply(obj, featured ? 26 : 22, LV_OPA_20);
    }
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void weather_add_glass_highlight(lv_obj_t *parent, bool featured)
{
    LV_UNUSED(parent);
    LV_UNUSED(featured);
    return;

    lv_coord_t width = featured ? 620 : 162;
    lv_coord_t height = featured ? 16 : 10;
    lv_coord_t y = featured ? 8 : 7;

    if (!parent)
    {
        return;
    }

    lv_obj_t *sheen = lv_obj_create(parent);
    lv_obj_set_size(sheen, width, height);
    lv_obj_align(sheen, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(sheen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(sheen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(sheen, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(sheen, featured ? LV_OPA_10 : LV_OPA_10, 0);
    lv_obj_set_style_border_width(sheen, 0, 0);
    lv_obj_set_style_radius(sheen, height / 2, 0);
    lv_obj_set_style_pad_all(sheen, 0, 0);
    lv_obj_clear_flag(sheen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spark = lv_obj_create(parent);
    lv_obj_set_size(spark, featured ? 72 : 38, featured ? 72 : 38);
    lv_obj_align(spark, LV_ALIGN_TOP_RIGHT, featured ? -14 : -8, featured ? -8 : -4);
    lv_obj_set_style_bg_color(spark, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(spark, featured ? LV_OPA_10 : LV_OPA_10, 0);
    lv_obj_set_style_border_width(spark, 0, 0);
    lv_obj_set_style_radius(spark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(spark, 0, 0);
    lv_obj_clear_flag(spark, LV_OBJ_FLAG_SCROLLABLE);
}

static void weather_tint_surface(lv_obj_t *obj, int weather_code, bool featured)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_style_bg_color(obj, weather_surface_bg_color(weather_code, featured), 0);
    lv_obj_set_style_bg_grad_color(obj, weather_surface_grad_color(weather_code, featured), 0);
}

static lv_color_t weather_surface_bg_color(int weather_code, bool featured)
{
    LV_UNUSED(weather_code);
    LV_UNUSED(featured);
    return ui_theme_get_surface_fill_color();
}

static lv_color_t weather_surface_grad_color(int weather_code, bool featured)
{
    LV_UNUSED(weather_code);
    LV_UNUSED(featured);
    return ui_theme_get_surface_fill_color();
}

static lv_obj_t *weather_create_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color, lv_opa_t opa, lv_coord_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void weather_draw_sun(lv_obj_t *parent, lv_coord_t size, lv_color_t color, lv_opa_t opa, bool rays_only)
{
    if (!rays_only)
    {
        weather_create_shape(parent, size / 2 - size / 6, size / 2 - size / 6, size / 3, size / 3, color, opa, LV_RADIUS_CIRCLE);
    }

    weather_create_shape(parent, size / 2 - 2, size / 7, 4, size / 7, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 2 - 2, size - size / 7 - 8, 4, size / 7, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 7, size / 2 - 2, size / 7, 4, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size - size / 7 - 8, size / 2 - 2, size / 7, 4, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 4, size / 4, 4, size / 8, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size - size / 4 - 8, size - size / 4 - 10, 4, size / 8, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 4, size - size / 4 - 10, 4, size / 8, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size - size / 4 - 8, size / 4, 4, size / 8, color, opa, LV_RADIUS_CIRCLE);
}

static void weather_draw_cloud(lv_obj_t *parent, lv_coord_t size, lv_color_t color, lv_opa_t opa)
{
    weather_create_shape(parent, size / 4, size / 2 - size / 12, size / 3, size / 4, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 2 - size / 6, size / 3, size / 3, size / 3, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 2, size / 2 - size / 10, size / 4, size / 4, color, opa, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, size / 4, size / 2, size / 2, size / 6, color, opa, 10);
}

static void weather_draw_partly_cloudy(lv_obj_t *parent, lv_coord_t size, lv_color_t sun_color, lv_color_t cloud_color)
{
    lv_coord_t sun_size = size / 3;
    lv_coord_t sun_x = size / 6;
    lv_coord_t sun_y = size / 7;
    lv_coord_t ray = size / 9;
    lv_coord_t center_x = sun_x + (sun_size / 2);
    lv_coord_t center_y = sun_y + (sun_size / 2);

    weather_create_shape(parent, sun_x, sun_y, sun_size, sun_size, sun_color, LV_OPA_90, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, center_x - 2, sun_y - ray + 2, 4, ray, sun_color, LV_OPA_80, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, sun_x - ray + 2, center_y - 2, ray, 4, sun_color, LV_OPA_80, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, center_x + (sun_size / 2) - 2, center_y - 2, ray, 4, sun_color, LV_OPA_80, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, sun_x + 4, sun_y + 4, 4, ray, sun_color, LV_OPA_70, LV_RADIUS_CIRCLE);
    weather_create_shape(parent, sun_x + sun_size - 8, sun_y + 6, 4, ray, sun_color, LV_OPA_70, LV_RADIUS_CIRCLE);

    weather_draw_cloud(parent, size, cloud_color, LV_OPA_COVER);
}

static void weather_clear_children(lv_obj_t *parent)
{
    if (!parent)
    {
        return;
    }

    while (lv_obj_get_child_cnt(parent) > 0)
    {
        lv_obj_del(lv_obj_get_child(parent, 0));
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

void weather_home_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    home_screen_create();
    lv_scr_load(home_get_screen());
    weather_screen_destroy();
}

void weather_block_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    block_screen_create();
    lv_scr_load(block_get_screen());
    weather_screen_destroy();
}

void weather_mempool_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    weather_screen_destroy();
}

void weather_clock_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    weather_screen_destroy();
}

void weather_price_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    price_screen_create();
    lv_scr_load(price_get_screen());
    weather_screen_destroy();
}

void weather_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    weather_screen_destroy();
}

void weather_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    weather_screen_destroy();
}

void weather_night_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    night_screen_create();
    lv_scr_load(night_get_screen());
    weather_screen_destroy();
}
