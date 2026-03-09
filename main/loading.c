/**
 * @file loading.c
 *
 * Simple loading screen UI for boot up phase
 */

/*********************
 *      INCLUDES
 *********************/
#include "loading.h"
#include "home.h"
#include "wifi.h"
#include "background.h"
#include "custom_fonts.h"
#include "assets/logo_background.c"
#include "esp_log.h"
#include "esp_wifi.h"
#include "bap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

/*********************
 *      DEFINES
 *********************/
static const char *TAG = "loading";
#define LOADING_DURATION_MS 1200
#define LOADING_STEPS 100
#define LOADING_STEP_TIME_MS (LOADING_DURATION_MS / LOADING_STEPS)

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *screen;
static lv_obj_t *progress_bar;
static lv_obj_t *loading_label;
static lv_obj_t *logo_img;
static lv_timer_t *loading_timer;
static uint8_t progress_value = 0;
static bool bap_init_started = false;
static bool bap_init_complete = false;
static uint32_t loading_elapsed_ms = 0;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void loading_timer_cb(lv_timer_t *timer);
static void finish_loading(void);
static void bap_init_task(void *pvParameters);
static bool loading_should_boot_to_wifi(void);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void loading(void)
{
    screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    screen_background_apply(screen);

    // Create a title label
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Turbo Touch");
    lv_obj_set_style_text_font(title, &angelwish_96, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Create logo image
    logo_img = lv_img_create(screen);
    lv_img_set_src(logo_img, &logo_background);

    lv_img_set_zoom(logo_img, 200);
    lv_obj_align(logo_img, LV_ALIGN_CENTER, 0, -30);

    // Create loading text
    loading_label = lv_label_create(screen);
    lv_label_set_text(loading_label, "Loading...");
    lv_obj_set_style_text_color(loading_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(loading_label, LV_ALIGN_CENTER, 0, 80);

    // Create progress bar
    progress_bar = lv_bar_create(screen);
    lv_obj_set_size(progress_bar, 200, 20);
    lv_obj_align(progress_bar, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(progress_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(progress_bar, COLOR_BORDER, LV_PART_MAIN);

    if (!bap_init_started)
    {
        ESP_LOGI(TAG, "Creating BAP initialization task...");
        xTaskCreate(bap_init_task, "bap_init", 8192, NULL, 10, NULL);
        bap_init_started = true;
    }

    loading_timer = lv_timer_create(loading_timer_cb, LOADING_STEP_TIME_MS, NULL);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void loading_timer_cb(lv_timer_t *timer)
{
    loading_elapsed_ms += LOADING_STEP_TIME_MS;

    progress_value++;

    lv_bar_set_value(progress_bar, progress_value, LV_ANIM_ON);

    char buffer[32];
    lv_snprintf(buffer, sizeof(buffer), "Hacking the Planet... %d%%", progress_value);
    lv_label_set_text(loading_label, buffer);

    // If loading is complete, transition to dashboard
    if (progress_value >= 100)
    {
        if (!bap_init_complete && loading_elapsed_ms < (LOADING_DURATION_MS + 1500))
        {
            lv_label_set_text(loading_label, "Starting services...");
            return;
        }
        finish_loading();
    }
}

// BAP initialization task - runs in separate task to avoid blocking LVGL
static void bap_init_task(void *pvParameters)
{
    ESP_LOGI(TAG, "BAP initialization task started");

    esp_err_t wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to add BAP init task to watchdog: %s", esp_err_to_name(wdt_ret));
    }

    // Reset watchdog before BAP initialization
    if (wdt_ret == ESP_OK)
    {
        esp_task_wdt_reset();
    }

    ESP_LOGI(TAG, "Initializing BAP...");

    esp_err_t ret = BAP_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BAP initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "BAP initialized successfully");
    }

    bap_init_complete = true;

    // Remove from watchdog before deleting task
    if (wdt_ret == ESP_OK)
    {
        esp_task_wdt_delete(NULL);
    }

    vTaskDelete(NULL);
}

static void finish_loading(void)
{
    lv_timer_del(loading_timer);

    lv_obj_clean(screen);
    if (loading_should_boot_to_wifi())
    {
        wifi_set_post_connect_show_pool(true);
        wifi_screen_create();
        lv_scr_load(wifi_get_screen());
        return;
    }

    home_screen_create();
    lv_scr_load(home_get_screen());
}

static bool loading_should_boot_to_wifi(void)
{
    return !wifi_has_saved_credentials();
}
