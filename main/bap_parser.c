/**
 * @file bap_parser.c
 * @brief BAP message parsing and UI integration
 * 
 * Handles parsing of incoming BAP messages and updating the UI
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "bap_parser.h"
#include "bap_protocol.h"
#include "home.h"
#include "wifi.h"
#include "block.h"
#include "night.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BAP_PARSER";

#define BAP_UI_FLUSH_INTERVAL_MS 250

typedef struct
{
    char hashrate[16];
    char temperature[16];
    char power[16];
    char fan_rpm[16];
    char shares[32];
    char best_difficulty[32];
    char wifi_ssid[33];
    char wifi_rssi[16];
    char wifi_ip[16];
    char wifi_password[65];
    char block_height[24];
    bool hashrate_dirty;
    bool temperature_dirty;
    bool power_dirty;
    bool fan_rpm_dirty;
    bool shares_dirty;
    bool best_difficulty_dirty;
    bool wifi_ssid_dirty;
    bool wifi_rssi_dirty;
    bool wifi_ip_dirty;
    bool wifi_password_dirty;
    bool block_height_dirty;
} bap_ui_cache_t;

static bap_ui_cache_t s_ui_cache = {0};
static portMUX_TYPE s_ui_cache_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_ui_flush_task_handle = NULL;

static void bap_ui_flush_task(void *arg);
static void bap_ui_ensure_flush_task(void);
static void bap_ui_cache_update(char *dst, size_t dst_size, const char *src, bool *dirty_flag);
static bool bap_ui_cache_take_snapshot(bap_ui_cache_t *snapshot);
static void bap_ui_cache_restore_dirty(const bap_ui_cache_t *snapshot);

esp_err_t bap_parse_and_handle_message(const char *message) {
    if (message == NULL) {
        ESP_LOGE(TAG, "Received NULL message");
        return ESP_ERR_INVALID_ARG;
    }

    bap_ui_ensure_flush_task();
    
    ESP_LOGI(TAG, "Received: %s", message);
    
    bap_message_t parsed_msg;
    esp_err_t ret = bap_parse_message_header(message, &parsed_msg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse message header");
        return ret;
    }
    
    if (parsed_msg.command == BAP_CMD_RES) {
        // Verify checksum for RES messages
        if (!bap_verify_checksum(message)) {
            uint8_t calculated = bap_calculate_checksum(parsed_msg.parameter);
            uint8_t received = (uint8_t)strtol(parsed_msg.checksum_str, NULL, 16);
            ESP_LOGE(TAG, "Checksum mismatch for %s: calculated %02X, received %02X",
                    parsed_msg.parameter, calculated, received);
            return ESP_ERR_INVALID_CRC;
        }
        return bap_handle_response(&parsed_msg);
    } else if (parsed_msg.command == BAP_CMD_CMD) {
        // Handle CMD messages (checksum already verified by protocol parser)
        if (!bap_verify_checksum(message)) {
            uint8_t calculated = bap_calculate_checksum(parsed_msg.parameter);
            uint8_t received = (uint8_t)strtol(parsed_msg.checksum_str, NULL, 16);
            ESP_LOGE(TAG, "Checksum mismatch for CMD %s: calculated %02X, received %02X",
                    parsed_msg.parameter, calculated, received);
            return ESP_ERR_INVALID_CRC;
        }
        return bap_handle_response(&parsed_msg);
    } else {
        ESP_LOGD(TAG, "Ignoring message type: %s", parsed_msg.command_str);
        return ESP_OK;
    }
}

esp_err_t bap_handle_response(const bap_message_t *msg) {
    if (!msg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (strcmp(msg->parameter, "hashrate") == 0) {
        ret = bap_handle_hashrate_response(msg->value);
    } else if (strcmp(msg->parameter, "chipTemp") == 0) {
        ret = bap_handle_temperature_response(msg->value);
    } else if (strcmp(msg->parameter, "power") == 0) {
        ret = bap_handle_power_response(msg->value);
    } else if (strcmp(msg->parameter, "shares") == 0) {
        ret = bap_handle_shares_response(msg->value);
    } else if (strcmp(msg->parameter, "deviceModel") == 0) {
        ret = bap_handle_device_model_response(msg->value);
    } else if (strcmp(msg->parameter, "asicModel") == 0) {
        ret = bap_handle_asic_model_response(msg->value);
    } else if (strcmp(msg->parameter, "pool") == 0) {
        ret = bap_handle_pool_url_response(msg->value);
    } else if (strcmp(msg->parameter, "poolPort") == 0) {
        ret = bap_handle_pool_port_response(msg->value);
    } else if (strcmp(msg->parameter, "poolUser") == 0) {
        ret = bap_handle_pool_user_response(msg->value);
    } else if (strcmp(msg->parameter, "fan_speed") == 0) {
        ret = bap_handle_fan_rpm_response(msg->value);
    } else if (strcmp(msg->parameter, "best_difficulty") == 0) {
        ret = bap_handle_best_difficulty_response(msg->value);
    } else if (strcmp(msg->parameter, "voltage") == 0) {
        ESP_LOGI(TAG, "Received voltage: %s", msg->value);
    } else if (strcmp(msg->parameter, "wifi_ssid") == 0) {
        ret = bap_handle_wifi_ssid_response(msg->value);
    } else if (strcmp(msg->parameter, "wifi_rssi") == 0) {
        ret = bap_handle_wifi_rssi_response(msg->value);
    } else if (strcmp(msg->parameter, "wifi_ip") == 0) {
        ret = bap_handle_wifi_ip_response(msg->value);
    } else if (strcmp(msg->parameter, "wifi_password") == 0) {
        ret = bap_handle_wifi_password_response(msg->value);
    } else if (strcmp(msg->parameter, "block_height") == 0) {
        ret = bap_handle_block_height_response(msg->value);
    } else if (strcmp(msg->parameter, "mode") == 0) {
        ret = bap_handle_mode(msg->value);
    } else {
        ESP_LOGI(TAG, "Received RES for %s: %s", msg->parameter, msg->value);
        // Unknown parameter, but not an error
    }
    
    return ret;
}

static void bap_ui_ensure_flush_task(void)
{
    if (s_ui_flush_task_handle != NULL)
    {
        return;
    }

    if (xTaskCreate(bap_ui_flush_task, "bap_ui_flush", 4096, NULL, 4, &s_ui_flush_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to start UI flush task");
        s_ui_flush_task_handle = NULL;
    }
}

static void bap_ui_cache_update(char *dst, size_t dst_size, const char *src, bool *dirty_flag)
{
    if (!dst || !src || !dirty_flag || dst_size == 0)
    {
        return;
    }

    portENTER_CRITICAL(&s_ui_cache_mux);
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
    *dirty_flag = true;
    portEXIT_CRITICAL(&s_ui_cache_mux);
}

static bool bap_ui_cache_take_snapshot(bap_ui_cache_t *snapshot)
{
    bool any_dirty = false;

    if (!snapshot)
    {
        return false;
    }

    portENTER_CRITICAL(&s_ui_cache_mux);
    *snapshot = s_ui_cache;
    any_dirty = s_ui_cache.hashrate_dirty ||
                s_ui_cache.temperature_dirty ||
                s_ui_cache.power_dirty ||
                s_ui_cache.fan_rpm_dirty ||
                s_ui_cache.shares_dirty ||
                s_ui_cache.best_difficulty_dirty ||
                s_ui_cache.wifi_ssid_dirty ||
                s_ui_cache.wifi_rssi_dirty ||
                s_ui_cache.wifi_ip_dirty ||
                s_ui_cache.wifi_password_dirty ||
                s_ui_cache.block_height_dirty;

    s_ui_cache.hashrate_dirty = false;
    s_ui_cache.temperature_dirty = false;
    s_ui_cache.power_dirty = false;
    s_ui_cache.fan_rpm_dirty = false;
    s_ui_cache.shares_dirty = false;
    s_ui_cache.best_difficulty_dirty = false;
    s_ui_cache.wifi_ssid_dirty = false;
    s_ui_cache.wifi_rssi_dirty = false;
    s_ui_cache.wifi_ip_dirty = false;
    s_ui_cache.wifi_password_dirty = false;
    s_ui_cache.block_height_dirty = false;
    portEXIT_CRITICAL(&s_ui_cache_mux);

    return any_dirty;
}

static void bap_ui_cache_restore_dirty(const bap_ui_cache_t *snapshot)
{
    if (!snapshot)
    {
        return;
    }

    portENTER_CRITICAL(&s_ui_cache_mux);
    if (snapshot->hashrate_dirty)
    {
        s_ui_cache.hashrate_dirty = true;
    }
    if (snapshot->temperature_dirty)
    {
        s_ui_cache.temperature_dirty = true;
    }
    if (snapshot->power_dirty)
    {
        s_ui_cache.power_dirty = true;
    }
    if (snapshot->fan_rpm_dirty)
    {
        s_ui_cache.fan_rpm_dirty = true;
    }
    if (snapshot->shares_dirty)
    {
        s_ui_cache.shares_dirty = true;
    }
    if (snapshot->best_difficulty_dirty)
    {
        s_ui_cache.best_difficulty_dirty = true;
    }
    if (snapshot->wifi_ssid_dirty)
    {
        s_ui_cache.wifi_ssid_dirty = true;
    }
    if (snapshot->wifi_rssi_dirty)
    {
        s_ui_cache.wifi_rssi_dirty = true;
    }
    if (snapshot->wifi_ip_dirty)
    {
        s_ui_cache.wifi_ip_dirty = true;
    }
    if (snapshot->wifi_password_dirty)
    {
        s_ui_cache.wifi_password_dirty = true;
    }
    if (snapshot->block_height_dirty)
    {
        s_ui_cache.block_height_dirty = true;
    }
    portEXIT_CRITICAL(&s_ui_cache_mux);
}

static void bap_ui_flush_task(void *arg)
{
    (void)arg;

    bap_ui_cache_t snapshot;

    while (1)
    {
        if (!bap_ui_cache_take_snapshot(&snapshot))
        {
            vTaskDelay(pdMS_TO_TICKS(BAP_UI_FLUSH_INTERVAL_MS));
            continue;
        }

        if (lvgl_port_lock(20))
        {
            if (snapshot.hashrate_dirty)
            {
                home_update_hashrate(snapshot.hashrate);
                night_update_hashrate(snapshot.hashrate);
            }
            if (snapshot.temperature_dirty)
            {
                home_update_temperature(snapshot.temperature);
            }
            if (snapshot.power_dirty)
            {
                home_update_power(snapshot.power);
            }
            if (snapshot.fan_rpm_dirty)
            {
                home_update_fan_speed(snapshot.fan_rpm);
            }
            if (snapshot.shares_dirty)
            {
                home_update_shares(snapshot.shares);
            }
            if (snapshot.best_difficulty_dirty)
            {
                home_update_best_difficulty(snapshot.best_difficulty);
            }
            if (snapshot.wifi_ssid_dirty)
            {
                wifi_update_ssid(snapshot.wifi_ssid);
            }
            if (snapshot.wifi_rssi_dirty)
            {
                wifi_update_rssi(snapshot.wifi_rssi);
            }
            if (snapshot.wifi_ip_dirty)
            {
                wifi_update_ip(snapshot.wifi_ip);
            }
            if (snapshot.wifi_password_dirty)
            {
                wifi_update_password(snapshot.wifi_password);
            }
            if (snapshot.block_height_dirty)
            {
                block_update_height(snapshot.block_height);
            }

            lvgl_port_unlock();
        }
        else
        {
            bap_ui_cache_restore_dirty(&snapshot);
        }

        vTaskDelay(pdMS_TO_TICKS(BAP_UI_FLUSH_INTERVAL_MS));
    }
}

esp_err_t bap_handle_hashrate_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Valid hashrate: %s", value);
    
    bap_ui_cache_update(s_ui_cache.hashrate, sizeof(s_ui_cache.hashrate), value, &s_ui_cache.hashrate_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_temperature_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float temp_value = atof(value);
    char formatted_temp[16];
    snprintf(formatted_temp, sizeof(formatted_temp), "%.2f", temp_value);
    
    ESP_LOGI(TAG, "Valid chipTemp: %s", formatted_temp);
    
    bap_ui_cache_update(s_ui_cache.temperature, sizeof(s_ui_cache.temperature), formatted_temp, &s_ui_cache.temperature_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_power_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received power: %s", value);
    
    bap_ui_cache_update(s_ui_cache.power, sizeof(s_ui_cache.power), value, &s_ui_cache.power_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_fan_rpm_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Received fan RPM: %s", value);

    bap_ui_cache_update(s_ui_cache.fan_rpm, sizeof(s_ui_cache.fan_rpm), value, &s_ui_cache.fan_rpm_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_shares_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received shares: %s", value);
    
    bap_ui_cache_update(s_ui_cache.shares, sizeof(s_ui_cache.shares), value, &s_ui_cache.shares_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_best_difficulty_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received best difficulty: %s", value);
    
    bap_ui_cache_update(s_ui_cache.best_difficulty, sizeof(s_ui_cache.best_difficulty), value, &s_ui_cache.best_difficulty_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_device_model_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received device model: %s", value);
    
    if (lvgl_port_lock(100)) {
        home_update_device_model(value);
        lvgl_port_unlock();
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex for device model update");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t bap_handle_asic_model_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received ASIC model: %s", value);
    
    if (lvgl_port_lock(100)) {
        home_update_asic_model(value);
        lvgl_port_unlock();
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex for ASIC model update");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t bap_handle_pool_url_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received pool URL: %s", value);
    
    pool_info_t pool_update = {0};
    strncpy(pool_update.url, value, sizeof(pool_update.url) - 1);
    
    if (lvgl_port_lock(100)) {
        home_update_pool_info(&pool_update);
        lvgl_port_unlock();
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex for pool info update");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t bap_handle_pool_port_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received pool port: %s", value);
    
    pool_info_t pool_update = {0};
    strncpy(pool_update.port, value, sizeof(pool_update.port) - 1);
    
    if (lvgl_port_lock(100)) {
        home_update_pool_info(&pool_update);
        lvgl_port_unlock();
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex for pool port update");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t bap_handle_pool_user_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received pool user: %s", value);
    
    pool_info_t pool_update = {0};
    strncpy(pool_update.worker_name, value, sizeof(pool_update.worker_name) - 1);
    
    if (lvgl_port_lock(100)) {
        home_update_pool_info(&pool_update);
        lvgl_port_unlock();
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex for pool user update");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t bap_handle_wifi_ssid_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received WiFi SSID: %s", value);
    
    bap_ui_cache_update(s_ui_cache.wifi_ssid, sizeof(s_ui_cache.wifi_ssid), value, &s_ui_cache.wifi_ssid_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_wifi_rssi_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received WiFi RSSI: %s", value);
    
    bap_ui_cache_update(s_ui_cache.wifi_rssi, sizeof(s_ui_cache.wifi_rssi), value, &s_ui_cache.wifi_rssi_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_wifi_ip_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received WiFi IP: %s", value);
    
    bap_ui_cache_update(s_ui_cache.wifi_ip, sizeof(s_ui_cache.wifi_ip), value, &s_ui_cache.wifi_ip_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_wifi_password_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Received WiFi password");

    bap_ui_cache_update(s_ui_cache.wifi_password, sizeof(s_ui_cache.wifi_password), value, &s_ui_cache.wifi_password_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_block_height_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Received block height: %s", value);

    bap_ui_cache_update(s_ui_cache.block_height, sizeof(s_ui_cache.block_height), value, &s_ui_cache.block_height_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_mode(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received mode: %s", value);
    

    if (lvgl_port_lock(100)) {
        if(strcmp(value, "ap_mode") == 0) {
            home_wifi_clicked(NULL);
        }
        lvgl_port_unlock();
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex for mode update");
        return ESP_ERR_TIMEOUT;
    }
}
