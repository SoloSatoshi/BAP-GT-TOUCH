#include "time_service.h"
#include "wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"
#include <time.h>

static const char *TAG = "time_service";

static TaskHandle_t time_service_task_handle = NULL;
static bool time_service_sntp_started = false;
static bool time_service_ready = false;

static bool time_service_ip_ready(void);
static bool time_service_clock_valid(void);
static void time_service_task(void *arg);

void time_service_start(void)
{
    if (time_service_task_handle == NULL)
    {
        xTaskCreate(time_service_task, "time_service", 4096, NULL, 5, &time_service_task_handle);
    }
}

bool time_service_is_ready(void)
{
    if (time_service_ready)
    {
        return true;
    }

    time_service_ready = time_service_clock_valid();
    return time_service_ready;
}

static bool time_service_ip_ready(void)
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

    return ip_info.ip.addr != 0;
}

static bool time_service_clock_valid(void)
{
    time_t now = time(NULL);
    struct tm time_info;
    localtime_r(&now, &time_info);
    return time_info.tm_year >= (2023 - 1900);
}

static void time_service_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (!wifi_is_connected() || !time_service_ip_ready())
        {
            time_service_ready = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!time_service_sntp_started && !sntp_enabled())
        {
            ESP_LOGI(TAG, "Starting SNTP");
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, "pool.ntp.org");
            sntp_init();
            time_service_sntp_started = true;
        }
        else if (sntp_enabled())
        {
            time_service_sntp_started = true;
        }

        time_service_ready = time_service_clock_valid();
        vTaskDelay(pdMS_TO_TICKS(time_service_ready ? 10000 : 1000));
    }
}
