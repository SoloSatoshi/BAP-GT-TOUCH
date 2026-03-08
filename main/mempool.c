#include "mempool.h"
#include "custom_fonts.h"
#include "home.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "weather.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "background.h"
#include "nav_icons.h"
#include "lvgl_port.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "lwip/apps/sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ota_update.h"

#define MEMPOOL_HTTP_BUF_SIZE 65536
#define MEMPOOL_MAX_BLOCKS 8
#define MEMPOOL_FETCH_INTERVAL_MS 60000

#define CARD_W 210
#define CARD_H 210

static const char *MEMPOOL_API_URL = "https://mempool.space/api/v1/blocks";
static const char *MEMPOOL_NEXT_API_URL = "https://mempool.space/api/v1/fees/mempool-blocks";
static const char *MEMPOOL_RECOMMENDED_API_URL = "https://mempool.space/api/v1/fees/recommended";

typedef struct
{
    long long height;
    long long timestamp;
    int tx_count;
    double median_fee;
    double fee_min;
    double fee_max;
    long long total_fees_sat;
    char pool_name[32];
    int minutes_ago;
} mempool_block_t;

typedef struct
{
    bool valid;
    int tx_count;
    double median_fee;
    double fee_min;
    double fee_max;
    long long total_fees_sat;
    long long block_vsize;
} mempool_next_block_t;

typedef struct
{
    bool valid;
    int fastest_fee;
    int half_hour_fee;
    int hour_fee;
} mempool_recommended_fees_t;

static lv_obj_t *mempool_screen = NULL;
static lv_obj_t *mempool_status_label = NULL;
static lv_obj_t *mempool_row = NULL;
static TaskHandle_t mempool_task_handle = NULL;
static bool mempool_netif_ready = false;
static bool mempool_log_tuned = false;
static bool mempool_sntp_started = false;

static mempool_block_t mempool_blocks[MEMPOOL_MAX_BLOCKS];
static int mempool_block_count = 0;
static mempool_next_block_t mempool_next_block = {0};
static mempool_recommended_fees_t mempool_recommended_fees = {0};

static char mempool_http_buf[MEMPOOL_HTTP_BUF_SIZE];
static int mempool_http_len = 0;

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void mempool_task(void *arg);
static bool mempool_fetch_once(void);
static bool mempool_http_get(const char *url);
static bool mempool_fetch_mined_blocks(void);
static bool mempool_fetch_next_block(void);
static bool mempool_fetch_recommended_fees(void);
static bool mempool_ensure_netif(void);
static bool mempool_wifi_connected(void);
static bool mempool_ip_ready(void);
static void mempool_start_sntp(void);
static bool mempool_time_ready(void);
static void mempool_set_status(const char *status);
static void mempool_rebuild_cards(void);

static bool json_get_ll(const char *obj, const char *key, long long *out);
static bool json_get_double(const char *obj, const char *key, double *out);
static void format_fee_value(double v, char *buf, size_t buf_size);
static void format_btc_from_sats(long long sats, char *buf, size_t buf_size);
static int split_top_level_objects(const char *json, int len, int starts[], int ends[], int max_items);
static bool parse_fee_range(const char *obj, double *out_min, double *out_max);
static void parse_pool_name(const char *obj, char *out, size_t out_size);
static int compute_minutes_ago(long long ts);

static esp_err_t mempool_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0)
    {
        int copy_len = evt->data_len;
        if (mempool_http_len + copy_len >= MEMPOOL_HTTP_BUF_SIZE)
        {
            copy_len = MEMPOOL_HTTP_BUF_SIZE - mempool_http_len - 1;
        }
        if (copy_len > 0)
        {
            memcpy(mempool_http_buf + mempool_http_len, evt->data, copy_len);
            mempool_http_len += copy_len;
            mempool_http_buf[mempool_http_len] = '\0';
        }
    }
    return ESP_OK;
}

void mempool_screen_create(void)
{
    if (mempool_screen != NULL)
    {
        return;
    }

    if (!mempool_log_tuned)
    {
        // Timeouts are expected on unreliable networks; keep logs readable.
        esp_log_level_set("esp-tls", ESP_LOG_WARN);
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
        esp_log_level_set("transport_base", ESP_LOG_WARN);
        mempool_log_tuned = true;
    }

    mempool_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(mempool_screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(mempool_screen, LV_OPA_COVER, 0);
    screen_background_apply(mempool_screen);
    lv_obj_clear_flag(mempool_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(mempool_screen, LV_SCROLLBAR_MODE_OFF);

    const ui_theme_t current_theme = ui_theme_get_current();
    const bool cyberpunk = current_theme == UI_THEME_CYBERPUNK;
    const bool woods = current_theme == UI_THEME_WOODS;

    lv_obj_t *title = lv_label_create(mempool_screen);
    lv_label_set_text(title, "LATEST BLOCKS");
    lv_obj_set_style_text_color(title, (cyberpunk || woods) ? COLOR_NAV_ICON : COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    mempool_status_label = lv_label_create(mempool_screen);
    lv_label_set_text(mempool_status_label, "LOADING...");
    lv_obj_set_style_text_color(mempool_status_label, (cyberpunk || woods) ? COLOR_NAV_ICON : COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_opa(mempool_status_label, (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(mempool_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(mempool_status_label, LV_ALIGN_TOP_MID, 0, 48);

    mempool_row = lv_obj_create(mempool_screen);
    const int row_y = 76;
    const int nav_h = 64;
    const int row_bottom_gap = 16;
    int row_h = SCREEN_HEIGHT - row_y - nav_h - row_bottom_gap;
    if (row_h < 300)
    {
        row_h = 300;
    }
    lv_obj_set_size(mempool_row, SCREEN_WIDTH, row_h);
    lv_obj_align(mempool_row, LV_ALIGN_TOP_MID, 0, row_y);
    lv_obj_set_style_bg_opa(mempool_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mempool_row, 0, 0);
    lv_obj_set_style_pad_left(mempool_row, 18, 0);
    lv_obj_set_style_pad_right(mempool_row, 18, 0);
    lv_obj_set_style_pad_top(mempool_row, 0, 0);
    lv_obj_set_style_pad_bottom(mempool_row, 0, 0);
    lv_obj_set_style_pad_column(mempool_row, 24, 0);
    lv_obj_set_scroll_dir(mempool_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(mempool_row, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(mempool_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mempool_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bottom_nav = lv_obj_create(mempool_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, mempool_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, mempool_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, NULL, true);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, mempool_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_BITCOIN, mempool_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_WEATHER, mempool_weather_clicked, false);
    create_bottom_nav_btn(bottom_nav, NAV_ICON_CHART, mempool_night_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, mempool_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, mempool_settings_clicked, false);

    mempool_rebuild_cards();

    if (mempool_task_handle == NULL)
    {
        xTaskCreate(mempool_task, "mempool_task", 6144, NULL, 5, &mempool_task_handle);
    }
}

void mempool_screen_destroy(void)
{
    if (mempool_screen)
    {
        lv_obj_del(mempool_screen);
        mempool_screen = NULL;
        mempool_status_label = NULL;
        mempool_row = NULL;
    }
}

lv_obj_t *mempool_get_screen(void)
{
    return mempool_screen;
}

static void mempool_task(void *arg)
{
    (void)arg;
    while (1)
    {
        if (ota_update_is_running())
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!mempool_wifi_connected())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("WAITING FOR WIFI");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!mempool_ensure_netif())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("NETIF ERROR");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!mempool_ip_ready())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("WAITING FOR IP");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        mempool_start_sntp();
        if (!mempool_time_ready())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("SYNCING TIME");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (lvgl_port_lock(50))
        {
            mempool_set_status("LOADING...");
            lvgl_port_unlock();
        }

        bool updated = mempool_fetch_once();
        if (lvgl_port_lock(50))
        {
            if (updated)
            {
                mempool_set_status("LIVE");
                mempool_rebuild_cards();
                lvgl_port_unlock();
                vTaskDelay(pdMS_TO_TICKS(MEMPOOL_FETCH_INTERVAL_MS));
                continue;
            }

            mempool_set_status("RETRYING...");
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static bool mempool_fetch_once(void)
{
    mempool_next_block.valid = false;
    mempool_recommended_fees.valid = false;
    bool next_ok = mempool_fetch_next_block();
    bool fees_ok = mempool_fetch_recommended_fees();
    bool mined_ok = mempool_fetch_mined_blocks();
    return mined_ok || next_ok || fees_ok;
}

static bool mempool_http_get(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = mempool_http_event_handler,
        .timeout_ms = 12000,
    };

#if defined(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY) && CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
    // Optional insecure mode to reduce TLS overhead on constrained links.
    config.skip_cert_common_name_check = true;
#else
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    mempool_http_len = 0;
    mempool_http_buf[0] = '\0';

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300 || mempool_http_len == 0)
    {
        return false;
    }

    return true;
}

static bool mempool_fetch_mined_blocks(void)
{
    if (!mempool_http_get(MEMPOOL_API_URL))
    {
        return false;
    }

    int starts[MEMPOOL_MAX_BLOCKS];
    int ends[MEMPOOL_MAX_BLOCKS];
    int obj_count = split_top_level_objects(mempool_http_buf, mempool_http_len, starts, ends, MEMPOOL_MAX_BLOCKS);
    if (obj_count <= 0)
    {
        return false;
    }

    mempool_block_count = 0;

    for (int i = 0; i < obj_count && i < MEMPOOL_MAX_BLOCKS; i++)
    {
        int len = ends[i] - starts[i] + 1;
        if (len <= 0 || len > MEMPOOL_HTTP_BUF_SIZE - 1)
        {
            continue;
        }

        char *obj = malloc((size_t)len + 1);
        if (!obj)
        {
            continue;
        }
        memcpy(obj, mempool_http_buf + starts[i], (size_t)len);
        obj[len] = '\0';

        mempool_block_t block = {0};
        long long ts = 0;
        long long height = 0;
        long long txc = 0;
        double median = 0.0;
        double fee_min = 0.0;
        double fee_max = 0.0;
        long long total_fees = 0;

        if (!json_get_ll(obj, "\"height\":", &height))
        {
            free(obj);
            continue;
        }
        json_get_ll(obj, "\"timestamp\":", &ts);
        json_get_ll(obj, "\"tx_count\":", &txc);
        json_get_double(obj, "\"medianFee\":", &median);
        json_get_ll(obj, "\"totalFees\":", &total_fees);
        parse_fee_range(obj, &fee_min, &fee_max);
        parse_pool_name(obj, block.pool_name, sizeof(block.pool_name));

        block.height = height;
        block.timestamp = ts;
        block.tx_count = (int)txc;
        block.median_fee = median;
        block.fee_min = fee_min;
        block.fee_max = fee_max;
        block.total_fees_sat = total_fees;
        block.minutes_ago = compute_minutes_ago(ts);

        mempool_blocks[mempool_block_count++] = block;
        free(obj);
    }

    return mempool_block_count > 0;
}

static bool mempool_fetch_next_block(void)
{
    if (!mempool_http_get(MEMPOOL_NEXT_API_URL))
    {
        return false;
    }

    int starts[1];
    int ends[1];
    int obj_count = split_top_level_objects(mempool_http_buf, mempool_http_len, starts, ends, 1);
    if (obj_count <= 0)
    {
        return false;
    }

    int len = ends[0] - starts[0] + 1;
    if (len <= 0 || len > MEMPOOL_HTTP_BUF_SIZE - 1)
    {
        return false;
    }

    char *obj = malloc((size_t)len + 1);
    if (!obj)
    {
        return false;
    }
    memcpy(obj, mempool_http_buf + starts[0], (size_t)len);
    obj[len] = '\0';

    long long txc = 0;
    long long total_fees = 0;
    long long block_vsize = 0;
    double median = 0.0;
    double fee_min = 0.0;
    double fee_max = 0.0;

    json_get_ll(obj, "\"nTx\":", &txc);
    json_get_ll(obj, "\"totalFees\":", &total_fees);
    json_get_ll(obj, "\"blockVSize\":", &block_vsize);
    json_get_double(obj, "\"medianFee\":", &median);
    parse_fee_range(obj, &fee_min, &fee_max);

    mempool_next_block.valid = true;
    mempool_next_block.tx_count = (int)txc;
    mempool_next_block.median_fee = median;
    mempool_next_block.fee_min = fee_min;
    mempool_next_block.fee_max = fee_max;
    mempool_next_block.total_fees_sat = total_fees;
    mempool_next_block.block_vsize = block_vsize;

    free(obj);
    return true;
}

static bool mempool_fetch_recommended_fees(void)
{
    if (!mempool_http_get(MEMPOOL_RECOMMENDED_API_URL))
    {
        return false;
    }

    long long fastest = 0;
    long long half_hour = 0;
    long long hour = 0;

    if (!json_get_ll(mempool_http_buf, "\"fastestFee\":", &fastest))
    {
        return false;
    }
    json_get_ll(mempool_http_buf, "\"halfHourFee\":", &half_hour);
    json_get_ll(mempool_http_buf, "\"hourFee\":", &hour);

    mempool_recommended_fees.valid = true;
    mempool_recommended_fees.fastest_fee = (int)fastest;
    mempool_recommended_fees.half_hour_fee = (int)half_hour;
    mempool_recommended_fees.hour_fee = (int)hour;
    return true;
}

static bool mempool_ensure_netif(void)
{
    if (mempool_netif_ready)
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

    mempool_netif_ready = true;
    return true;
}

static bool mempool_wifi_connected(void)
{
    return wifi_is_connected();
}

static bool mempool_ip_ready(void)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta)
    {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta, &ip_info) != ESP_OK)
    {
        return false;
    }

    // 0.0.0.0 means DHCP hasn't completed yet.
    return ip_info.ip.addr != 0;
}

static void mempool_start_sntp(void)
{
    if (mempool_sntp_started || sntp_enabled())
    {
        mempool_sntp_started = true;
        return;
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    mempool_sntp_started = true;
}

static bool mempool_time_ready(void)
{
    time_t now = time(NULL);
    struct tm time_info;
    localtime_r(&now, &time_info);
    // Pre-2023 typically means SNTP hasn't synced yet.
    return time_info.tm_year >= (2023 - 1900);
}

static void mempool_set_status(const char *status)
{
    if (!status || !mempool_status_label)
    {
        return;
    }

    lv_label_set_text(mempool_status_label, status);
}

static void mempool_rebuild_cards(void)
{
    if (!mempool_row)
    {
        return;
    }

    lv_obj_clean(mempool_row);

    if (mempool_block_count <= 0 && !mempool_next_block.valid)
    {
        return;
    }

    const ui_theme_t theme = ui_theme_get_current();
    const bool bitaxe_red = theme == UI_THEME_BITAXE_RED;
    const bool monochrome = theme == UI_THEME_MONOCHROME;
    const bool space = theme == UI_THEME_SPACE;
    const bool cyberpunk = theme == UI_THEME_CYBERPUNK;
    const bool woods = theme == UI_THEME_WOODS;
    const lv_color_t color_height = monochrome ? COLOR_TEXT_PRIMARY : ((cyberpunk || woods) ? COLOR_NAV_ICON : (bitaxe_red ? lv_color_hex(0x00F0FF) : lv_color_hex(0x00E5FF)));
    const lv_color_t color_card = (cyberpunk || woods) ? lv_color_black() : ((monochrome || space) ? ui_theme_get_surface_fill_color() : (bitaxe_red ? lv_color_hex(0x19152E) : lv_color_hex(0x0B1E3A)));
    const lv_color_t color_card_grad = (cyberpunk || woods) ? lv_color_black() : ((monochrome || space) ? ui_theme_get_surface_fill_color() : (bitaxe_red ? lv_color_hex(0x111E46) : lv_color_hex(0x0B1E3A)));
    const lv_color_t color_mid = (cyberpunk || woods) ? lv_color_black() : ((monochrome || space) ? ui_theme_get_surface_fill_color() : (bitaxe_red ? lv_color_hex(0xB022FF) : lv_color_hex(0x1E5BFF)));
    const lv_color_t color_mid_grad = (cyberpunk || woods) ? lv_color_black() : ((monochrome || space) ? ui_theme_get_surface_fill_color() : (bitaxe_red ? lv_color_hex(0x2A78FF) : lv_color_hex(0x143FBA)));
    const lv_color_t color_bottom = (cyberpunk || woods) ? lv_color_black() : ((monochrome || space) ? ui_theme_get_surface_fill_color() : (bitaxe_red ? lv_color_hex(0x1E63FF) : lv_color_hex(0x7C3BFF)));
    const lv_color_t color_bottom_grad = (cyberpunk || woods) ? lv_color_black() : ((monochrome || space) ? ui_theme_get_surface_fill_color() : (bitaxe_red ? lv_color_hex(0x1546D8) : lv_color_hex(0x6126D8)));
    const lv_color_t color_fee = monochrome ? lv_color_hex(0xD9D9D9) : ((cyberpunk || woods) ? COLOR_NAV_ICON : lv_color_hex(0xFFE600));
    const int row_h = lv_obj_get_height(mempool_row);
    const int wrap_top_offset = 38;
    const int wrap_pool_h = 24;
    const int wrap_bottom_pad = 8;
    const int wrap_extra = wrap_top_offset + wrap_pool_h + wrap_bottom_pad;
    const int card_x = 0;
    const int card_w = CARD_W;
    int card_h = row_h - wrap_extra;
    if (card_h > CARD_H)
    {
        card_h = CARD_H;
    }
    if (card_h < 200)
    {
        card_h = 200;
    }
    if (mempool_next_block.valid)
    {
        lv_obj_t *card_wrap = lv_obj_create(mempool_row);
        lv_obj_set_size(card_wrap, CARD_W, card_h + wrap_extra);
        lv_obj_set_style_bg_opa(card_wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(card_wrap, 0, 0);
        lv_obj_set_style_pad_all(card_wrap, 0, 0);
        lv_obj_clear_flag(card_wrap, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *height_chip = NULL;
        if (cyberpunk)
        {
            height_chip = lv_obj_create(card_wrap);
            lv_obj_set_size(height_chip, 150, 32);
            lv_obj_align(height_chip, LV_ALIGN_TOP_MID, 0, -2);
            lv_obj_set_style_bg_color(height_chip, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(height_chip, LV_OPA_50, 0);
            lv_obj_set_style_border_width(height_chip, 0, 0);
            lv_obj_set_style_radius(height_chip, 16, 0);
            lv_obj_set_style_pad_all(height_chip, 0, 0);
            lv_obj_set_style_shadow_width(height_chip, 0, 0);
            lv_obj_clear_flag(height_chip, LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_t *height_label = lv_label_create(card_wrap);
        lv_label_set_text(height_label, "NEXT BLOCK");
        lv_obj_set_style_text_color(height_label, cyberpunk ? COLOR_NAV_ICON : color_height, 0);
        lv_obj_set_style_text_font(height_label, &lv_font_montserrat_20, 0);
        lv_obj_align(height_label, LV_ALIGN_TOP_MID, 0, 2);

        lv_obj_t *card = lv_obj_create(card_wrap);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, card_x, wrap_top_offset);
        lv_obj_set_style_radius(card, cyberpunk ? 20 : 8, 0);
        lv_obj_set_style_bg_color(card, color_card, 0);
        lv_obj_set_style_bg_grad_color(card, color_card_grad, 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(card, cyberpunk ? LV_OPA_50 : (space ? LV_OPA_70 : (woods ? LV_OPA_70 : LV_OPA_COVER)), 0);
        lv_obj_set_style_border_width(card, (monochrome || space || cyberpunk || woods) ? 2 : 0, 0);
        lv_obj_set_style_border_color(card, (space || cyberpunk || woods) ? COLOR_NAV_ICON : ui_theme_get_surface_outline_color(), 0);
        lv_obj_set_style_border_opa(card, (monochrome || space || cyberpunk || woods) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(card, (cyberpunk || woods) ? 0 : 12, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x02060D), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
        lv_obj_set_style_shadow_ofs_y(card, 4, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        const int bottom_bar_h = 52;
        int content_h = card_h - bottom_bar_h;
        if (content_h < 120)
        {
            content_h = 120;
        }

        lv_obj_t *mid = lv_obj_create(card);
        lv_obj_set_size(mid, card_w, content_h);
        lv_obj_set_pos(mid, 0, 0);
        lv_obj_set_style_radius(mid, cyberpunk ? 20 : 8, 0);
        lv_obj_set_style_bg_color(mid, color_mid, 0);
        lv_obj_set_style_bg_grad_color(mid, color_mid_grad, 0);
        lv_obj_set_style_bg_grad_dir(mid, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(mid, (monochrome || space || cyberpunk || woods) ? LV_OPA_TRANSP : (bitaxe_red ? LV_OPA_COVER : LV_OPA_60), 0);
        lv_obj_set_style_border_width(mid, 0, 0);
        lv_obj_set_style_pad_all(mid, 0, 0);
        lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);

        const int median_y = 10;
        const int range_y = median_y + 28;
        const int btc_y = range_y + 34;
        int priority_y = btc_y + 36;
        if (priority_y > content_h - 56)
        {
            priority_y = content_h - 56;
        }

        char median_txt[32];
        int median_fee_i = (int)(mempool_next_block.median_fee + 0.5);
        lv_snprintf(median_txt, sizeof(median_txt), "~%d sat/vB", median_fee_i);
        lv_obj_t *median_label = lv_label_create(card);
        lv_label_set_text(median_label, median_txt);
        lv_obj_set_style_text_color(median_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(median_label, &lv_font_montserrat_20, 0);
        lv_obj_align(median_label, LV_ALIGN_TOP_MID, 0, median_y);

        char fee_min_txt[16];
        char fee_max_txt[16];
        format_fee_value(mempool_next_block.fee_min, fee_min_txt, sizeof(fee_min_txt));
        format_fee_value(mempool_next_block.fee_max, fee_max_txt, sizeof(fee_max_txt));
        char range_txt[48];
        lv_snprintf(range_txt, sizeof(range_txt), "%s - %s sat/vB", fee_min_txt, fee_max_txt);
        lv_obj_t *range_label = lv_label_create(card);
        lv_label_set_text(range_label, range_txt);
        lv_obj_set_style_text_color(range_label, color_fee, 0);
        lv_obj_set_style_text_font(range_label, &lv_font_montserrat_14, 0);
        lv_obj_align(range_label, LV_ALIGN_TOP_MID, 0, range_y);

        char btc_txt[32];
        format_btc_from_sats(mempool_next_block.total_fees_sat, btc_txt, sizeof(btc_txt));
        lv_obj_t *btc_label = lv_label_create(card);
        lv_label_set_text(btc_label, btc_txt);
        lv_obj_set_style_text_color(btc_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(btc_label, &lv_font_montserrat_28, 0);
        lv_obj_align(btc_label, LV_ALIGN_TOP_MID, 0, btc_y);

        char next_fee_txt[32];
        char half_hour_txt[32];
        char hour_txt[32];
        if (mempool_recommended_fees.valid)
        {
            lv_snprintf(next_fee_txt, sizeof(next_fee_txt), "Next: %d sat/vB", mempool_recommended_fees.fastest_fee);
            lv_snprintf(half_hour_txt, sizeof(half_hour_txt), "30m: %d sat/vB", mempool_recommended_fees.half_hour_fee);
            lv_snprintf(hour_txt, sizeof(hour_txt), "1h: %d sat/vB", mempool_recommended_fees.hour_fee);
        }
        else
        {
            lv_snprintf(next_fee_txt, sizeof(next_fee_txt), "Next: --");
            lv_snprintf(half_hour_txt, sizeof(half_hour_txt), "30m: --");
            lv_snprintf(hour_txt, sizeof(hour_txt), "1h: --");
        }

        lv_obj_t *next_fee_label = lv_label_create(card);
        lv_label_set_text(next_fee_label, next_fee_txt);
        lv_obj_set_width(next_fee_label, card_w - 24);
        lv_obj_set_style_text_align(next_fee_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(next_fee_label, color_fee, 0);
        lv_obj_set_style_text_font(next_fee_label, &lv_font_montserrat_14, 0);
        lv_obj_align(next_fee_label, LV_ALIGN_TOP_LEFT, 12, priority_y);

        lv_obj_t *half_hour_label = lv_label_create(card);
        lv_label_set_text(half_hour_label, half_hour_txt);
        lv_obj_set_width(half_hour_label, card_w - 24);
        lv_obj_set_style_text_align(half_hour_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(half_hour_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(half_hour_label, &lv_font_montserrat_14, 0);
        lv_obj_align(half_hour_label, LV_ALIGN_TOP_LEFT, 12, priority_y + 20);

        lv_obj_t *hour_label = lv_label_create(card);
        lv_label_set_text(hour_label, hour_txt);
        lv_obj_set_width(hour_label, card_w - 24);
        lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(hour_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(hour_label, &lv_font_montserrat_14, 0);
        lv_obj_align(hour_label, LV_ALIGN_TOP_LEFT, 12, priority_y + 40);

        lv_obj_t *bottom_bar = lv_obj_create(card);
        lv_obj_set_size(bottom_bar, card_w, bottom_bar_h);
        lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_radius(bottom_bar, cyberpunk ? 20 : 0, 0);
        lv_obj_set_style_bg_color(bottom_bar, color_bottom, 0);
        lv_obj_set_style_bg_grad_color(bottom_bar, color_bottom_grad, 0);
        lv_obj_set_style_bg_grad_dir(bottom_bar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(bottom_bar, cyberpunk ? LV_OPA_50 : (space ? LV_OPA_70 : (woods ? LV_OPA_70 : LV_OPA_COVER)), 0);
        lv_obj_set_style_border_width(bottom_bar, 0, 0);
        lv_obj_set_style_pad_all(bottom_bar, 0, 0);
        lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ago_label = lv_label_create(bottom_bar);
        lv_label_set_text(ago_label, "ESTIMATED");
        lv_obj_set_style_text_color(ago_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(ago_label, &lv_font_montserrat_20, 0);
        lv_obj_center(ago_label);
    }

    for (int i = 0; i < mempool_block_count; i++)
    {
        mempool_block_t *b = &mempool_blocks[i];

        lv_obj_t *card_wrap = lv_obj_create(mempool_row);
        lv_obj_set_size(card_wrap, CARD_W, card_h + wrap_extra);
        lv_obj_set_style_bg_opa(card_wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(card_wrap, 0, 0);
        lv_obj_set_style_pad_all(card_wrap, 0, 0);
        lv_obj_clear_flag(card_wrap, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *height_chip = NULL;
        if (cyberpunk)
        {
            height_chip = lv_obj_create(card_wrap);
            lv_obj_set_size(height_chip, 130, 32);
            lv_obj_align(height_chip, LV_ALIGN_TOP_MID, 0, -2);
            lv_obj_set_style_bg_color(height_chip, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(height_chip, LV_OPA_50, 0);
            lv_obj_set_style_border_width(height_chip, 0, 0);
            lv_obj_set_style_radius(height_chip, 16, 0);
            lv_obj_set_style_pad_all(height_chip, 0, 0);
            lv_obj_set_style_shadow_width(height_chip, 0, 0);
            lv_obj_clear_flag(height_chip, LV_OBJ_FLAG_SCROLLABLE);
        }

        char height_txt[16];
        lv_snprintf(height_txt, sizeof(height_txt), "%lld", b->height);
        lv_obj_t *height_label = lv_label_create(card_wrap);
        lv_label_set_text(height_label, height_txt);
        lv_obj_set_style_text_color(height_label, cyberpunk ? COLOR_NAV_ICON : color_height, 0);
        lv_obj_set_style_text_font(height_label, &lv_font_montserrat_24, 0);
        lv_obj_align(height_label, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *card = lv_obj_create(card_wrap);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, card_x, wrap_top_offset);
        lv_obj_set_style_radius(card, cyberpunk ? 20 : 8, 0);
        lv_obj_set_style_bg_color(card, color_card, 0);
        lv_obj_set_style_bg_grad_color(card, color_card_grad, 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(card, cyberpunk ? LV_OPA_50 : (space ? LV_OPA_70 : (woods ? LV_OPA_70 : LV_OPA_COVER)), 0);
        lv_obj_set_style_border_width(card, (monochrome || space || cyberpunk || woods) ? 2 : 0, 0);
        lv_obj_set_style_border_color(card, (space || cyberpunk || woods) ? COLOR_NAV_ICON : ui_theme_get_surface_outline_color(), 0);
        lv_obj_set_style_border_opa(card, (monochrome || space || cyberpunk || woods) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(card, (cyberpunk || woods) ? 0 : 12, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x02060D), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
        lv_obj_set_style_shadow_ofs_y(card, 4, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        const int bottom_bar_h = 52;
        int content_h = card_h - bottom_bar_h;
        if (content_h < 120)
        {
            content_h = 120;
        }

        lv_obj_t *mid = lv_obj_create(card);
        lv_obj_set_size(mid, card_w, content_h);
        lv_obj_set_pos(mid, 0, 0);
        lv_obj_set_style_radius(mid, cyberpunk ? 20 : 8, 0);
        lv_obj_set_style_bg_color(mid, color_mid, 0);
        lv_obj_set_style_bg_grad_color(mid, color_mid_grad, 0);
        lv_obj_set_style_bg_grad_dir(mid, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(mid, (monochrome || space || cyberpunk || woods) ? LV_OPA_TRANSP : (bitaxe_red ? LV_OPA_COVER : LV_OPA_60), 0);
        lv_obj_set_style_border_width(mid, 0, 0);
        lv_obj_set_style_pad_all(mid, 0, 0);
        lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);

        const int median_y = 10;
        const int range_y = median_y + 28;
        const int btc_y = range_y + 34;
        int tx_y = btc_y + 44;
        if (tx_y > content_h - 28)
        {
            tx_y = content_h - 28;
        }

        char median_txt[32];
        int median_fee_i = (int)(b->median_fee + 0.5);
        lv_snprintf(median_txt, sizeof(median_txt), "~%d sat/vB", median_fee_i);
        lv_obj_t *median_label = lv_label_create(card);
        lv_label_set_text(median_label, median_txt);
        lv_obj_set_style_text_color(median_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(median_label, &lv_font_montserrat_20, 0);
        lv_obj_align(median_label, LV_ALIGN_TOP_MID, 0, median_y);

        char fee_min_txt[16];
        char fee_max_txt[16];
        format_fee_value(b->fee_min, fee_min_txt, sizeof(fee_min_txt));
        format_fee_value(b->fee_max, fee_max_txt, sizeof(fee_max_txt));
        char range_txt[48];
        lv_snprintf(range_txt, sizeof(range_txt), "%s - %s sat/vB", fee_min_txt, fee_max_txt);
        lv_obj_t *range_label = lv_label_create(card);
        lv_label_set_text(range_label, range_txt);
        lv_obj_set_style_text_color(range_label, color_fee, 0);
        lv_obj_set_style_text_font(range_label, &lv_font_montserrat_14, 0);
        lv_obj_align(range_label, LV_ALIGN_TOP_MID, 0, range_y);

        char btc_txt[32];
        format_btc_from_sats(b->total_fees_sat, btc_txt, sizeof(btc_txt));
        lv_obj_t *btc_label = lv_label_create(card);
        lv_label_set_text(btc_label, btc_txt);
        lv_obj_set_style_text_color(btc_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(btc_label, &lv_font_montserrat_28, 0);
        lv_obj_align(btc_label, LV_ALIGN_TOP_MID, 0, btc_y);

        char tx_txt[40];
        lv_snprintf(tx_txt, sizeof(tx_txt), "%d transactions", b->tx_count);
        lv_obj_t *tx_label = lv_label_create(card);
        lv_label_set_text(tx_label, tx_txt);
        lv_obj_set_style_text_color(tx_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(tx_label, &lv_font_montserrat_14, 0);
        lv_obj_align(tx_label, LV_ALIGN_TOP_MID, 0, tx_y);

        lv_obj_t *bottom_bar = lv_obj_create(card);
        lv_obj_set_size(bottom_bar, card_w, bottom_bar_h);
        lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_radius(bottom_bar, cyberpunk ? 20 : 0, 0);
        lv_obj_set_style_bg_color(bottom_bar, color_bottom, 0);
        lv_obj_set_style_bg_grad_color(bottom_bar, color_bottom_grad, 0);
        lv_obj_set_style_bg_grad_dir(bottom_bar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(bottom_bar, cyberpunk ? LV_OPA_50 : (space ? LV_OPA_70 : (woods ? LV_OPA_70 : LV_OPA_COVER)), 0);
        lv_obj_set_style_border_width(bottom_bar, 0, 0);
        lv_obj_set_style_pad_all(bottom_bar, 0, 0);
        lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

        char ago_txt[32];
        if (b->minutes_ago >= 0)
        {
            lv_snprintf(ago_txt, sizeof(ago_txt), "%d minutes ago", b->minutes_ago);
        }
        else
        {
            lv_snprintf(ago_txt, sizeof(ago_txt), "-- minutes ago");
        }
        lv_obj_t *ago_label = lv_label_create(bottom_bar);
        lv_label_set_text(ago_label, ago_txt);
        lv_obj_set_style_text_color(ago_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(ago_label, &lv_font_montserrat_20, 0);
        lv_obj_center(ago_label);

        if (b->pool_name[0] != '\0')
        {
            lv_obj_t *pool_chip = NULL;
            if (cyberpunk)
            {
                pool_chip = lv_obj_create(card_wrap);
                lv_obj_set_size(pool_chip, CARD_W, 24);
                lv_obj_align(pool_chip, LV_ALIGN_BOTTOM_MID, 0, -wrap_bottom_pad);
                lv_obj_set_style_bg_color(pool_chip, lv_color_black(), 0);
                lv_obj_set_style_bg_opa(pool_chip, LV_OPA_50, 0);
                lv_obj_set_style_border_width(pool_chip, 0, 0);
                lv_obj_set_style_radius(pool_chip, 12, 0);
                lv_obj_set_style_pad_all(pool_chip, 0, 0);
                lv_obj_set_style_shadow_width(pool_chip, 0, 0);
                lv_obj_clear_flag(pool_chip, LV_OBJ_FLAG_SCROLLABLE);
            }

            lv_obj_t *pool_label = lv_label_create(card_wrap);
            lv_label_set_text(pool_label, b->pool_name);
            lv_label_set_long_mode(pool_label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(pool_label, CARD_W);
            lv_obj_set_style_text_color(pool_label, cyberpunk ? COLOR_TEXT_PRIMARY : COLOR_TEXT_PRIMARY, 0);
            lv_obj_set_style_text_font(pool_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(pool_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(pool_label, LV_ALIGN_BOTTOM_MID, 0, -wrap_bottom_pad);
        }
    }
}

static bool json_get_ll(const char *obj, const char *key, long long *out)
{
    if (!obj || !key || !out)
    {
        return false;
    }

    const char *p = strstr(obj, key);
    if (!p)
    {
        return false;
    }
    p += strlen(key);
    *out = strtoll(p, NULL, 10);
    return true;
}

static bool json_get_double(const char *obj, const char *key, double *out)
{
    if (!obj || !key || !out)
    {
        return false;
    }

    const char *p = strstr(obj, key);
    if (!p)
    {
        return false;
    }
    p += strlen(key);
    *out = strtod(p, NULL);
    return true;
}

static void format_fee_value(double v, char *buf, size_t buf_size)
{
    int v_i = (int)(v + 0.5);
    lv_snprintf(buf, buf_size, "%d", v_i);
}

static void format_btc_from_sats(long long sats, char *buf, size_t buf_size)
{
    if (sats < 0)
    {
        sats = 0;
    }

    long long whole = sats / 100000000LL;
    long long frac = sats % 100000000LL;
    long long frac3 = (frac + 50000LL) / 100000LL; // round to 3 decimals
    if (frac3 >= 1000LL)
    {
        whole += 1;
        frac3 = 0;
    }

    lv_snprintf(buf, buf_size, "%lld.%03lld BTC", whole, frac3);
}

static int split_top_level_objects(const char *json, int len, int starts[], int ends[], int max_items)
{
    int count = 0;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    int start_idx = -1;

    for (int i = 0; i < len; i++)
    {
        char c = json[i];
        if (in_str)
        {
            if (esc)
            {
                esc = false;
            }
            else if (c == '\\')
            {
                esc = true;
            }
            else if (c == '"')
            {
                in_str = false;
            }
            continue;
        }

        if (c == '"')
        {
            in_str = true;
            continue;
        }

        if (c == '{')
        {
            if (depth == 0)
            {
                start_idx = i;
            }
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0 && start_idx >= 0)
            {
                if (count < max_items)
                {
                    starts[count] = start_idx;
                    ends[count] = i;
                    count++;
                }
                else
                {
                    break;
                }
                start_idx = -1;
            }
        }
    }

    return count;
}

static bool parse_fee_range(const char *obj, double *out_min, double *out_max)
{
    if (!obj || !out_min || !out_max)
    {
        return false;
    }

    const char *p = strstr(obj, "\"feeRange\":[");
    if (!p)
    {
        return false;
    }
    p += strlen("\"feeRange\":[");

    double min_v = 0.0;
    double max_v = 0.0;
    bool have = false;

    while (*p && *p != ']')
    {
        char *endptr = NULL;
        double v = strtod(p, &endptr);
        if (endptr == p)
        {
            p++;
            continue;
        }
        if (!have)
        {
            min_v = max_v = v;
            have = true;
        }
        else
        {
            if (v < min_v)
                min_v = v;
            if (v > max_v)
                max_v = v;
        }
        p = endptr;
        if (*p == ',')
        {
            p++;
        }
    }

    if (!have)
    {
        return false;
    }

    *out_min = min_v;
    *out_max = max_v;
    return true;
}

static void parse_pool_name(const char *obj, char *out, size_t out_size)
{
    if (!obj || !out || out_size == 0)
    {
        return;
    }

    out[0] = '\0';
    const char *pool = strstr(obj, "\"pool\"");
    if (!pool)
    {
        return;
    }
    const char *name = strstr(pool, "\"name\":\"");
    if (!name)
    {
        return;
    }
    name += strlen("\"name\":\"");
    const char *end = strchr(name, '"');
    if (!end)
    {
        return;
    }

    size_t len = (size_t)(end - name);
    if (len >= out_size)
    {
        len = out_size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

static int compute_minutes_ago(long long ts)
{
    time_t now = time(NULL);
    if (now < 946684800 || ts <= 0)
    {
        return -1;
    }

    long long diff = (long long)now - ts;
    if (diff < 0)
    {
        diff = 0;
    }
    return (int)(diff / 60);
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

void mempool_home_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    home_screen_create();
    lv_scr_load(home_get_screen());
    mempool_screen_destroy();
}

void mempool_block_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    block_screen_create();
    lv_scr_load(block_get_screen());
    mempool_screen_destroy();
}

void mempool_clock_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    mempool_screen_destroy();
}

void mempool_price_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    price_screen_create();
    lv_scr_load(price_get_screen());
    mempool_screen_destroy();
}

void mempool_weather_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    weather_screen_create();
    lv_scr_load(weather_get_screen());
    mempool_screen_destroy();
}

void mempool_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    mempool_screen_destroy();
}

void mempool_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    mempool_screen_destroy();
}

void mempool_night_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    night_screen_create();
    lv_scr_load(night_get_screen());
    mempool_screen_destroy();
}
