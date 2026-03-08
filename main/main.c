#include "waveshare_rgb_lcd_port.h"
#include "loading.h"
#include "mempool.h"
#include "price.h"
#include "time_service.h"
#include "weather.h"

static const char *TAG = "main";

void app_main()
{
    waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();
    time_service_start();
    weather_service_start();
    price_service_start();
    mempool_service_start();
    
    ESP_LOGI(TAG, "BAP Touch Display -- Build by WantClue with Love");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        // screen init
        loading();
        
        // Release the mutex
        lvgl_port_unlock();
    }
}
