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
#include "settings.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BAP_PARSER";

#define BAP_UI_FLUSH_INTERVAL_MS 100

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
    char self_test[16];
    char mode[16];
    char block_height[24];
    char device_model[32];
    char asic_model[32];
    char pool_url[128];
    char pool_port[16];
    char pool_user[128];
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
    bool self_test_dirty;
    bool mode_dirty;
    bool block_height_dirty;
    bool device_model_dirty;
    bool asic_model_dirty;
    bool pool_url_dirty;
    bool pool_port_dirty;
    bool pool_user_dirty;
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
    
    if (parsed_msg.command == BAP_CMD_RES ||
        parsed_msg.command == BAP_CMD_CMD ||
        parsed_msg.command == BAP_CMD_ACK ||
        parsed_msg.command == BAP_CMD_ERR ||
        parsed_msg.command == BAP_CMD_STA) {
        if (!bap_verify_checksum(message)) {
            uint8_t calculated = bap_calculate_checksum(parsed_msg.parameter);
            uint8_t received = (uint8_t)strtol(parsed_msg.checksum_str, NULL, 16);
            ESP_LOGE(TAG, "Checksum mismatch for %s %s: calculated %02X, received %02X",
                    parsed_msg.command_str,
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

    if ((msg->command == BAP_CMD_ACK && strcmp(msg->parameter, "factory_reset") == 0) ||
        (msg->command == BAP_CMD_STA &&
         strcmp(msg->parameter, "status") == 0 &&
         strcmp(msg->value, "factory_resetting") == 0)) {
        settings_factory_reset_note_bitaxe_ack();
        return ESP_OK;
    }

    if (msg->command == BAP_CMD_ERR) {
        ESP_LOGW(TAG, "Ignoring BAP error for %s: %s", msg->parameter, msg->value ? msg->value : "");
        return ESP_OK;
    }
    
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
    } else if (strcmp(msg->parameter, "self_test") == 0) {
        ret = bap_handle_self_test_response(msg->value);
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
                s_ui_cache.self_test_dirty ||
                s_ui_cache.mode_dirty ||
                s_ui_cache.block_height_dirty ||
                s_ui_cache.device_model_dirty ||
                s_ui_cache.asic_model_dirty ||
                s_ui_cache.pool_url_dirty ||
                s_ui_cache.pool_port_dirty ||
                s_ui_cache.pool_user_dirty;

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
    s_ui_cache.self_test_dirty = false;
    s_ui_cache.mode_dirty = false;
    s_ui_cache.block_height_dirty = false;
    s_ui_cache.device_model_dirty = false;
    s_ui_cache.asic_model_dirty = false;
    s_ui_cache.pool_url_dirty = false;
    s_ui_cache.pool_port_dirty = false;
    s_ui_cache.pool_user_dirty = false;
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
    if (snapshot->self_test_dirty)
    {
        s_ui_cache.self_test_dirty = true;
    }
    if (snapshot->mode_dirty)
    {
        s_ui_cache.mode_dirty = true;
    }
    if (snapshot->block_height_dirty)
    {
        s_ui_cache.block_height_dirty = true;
    }
    if (snapshot->device_model_dirty)
    {
        s_ui_cache.device_model_dirty = true;
    }
    if (snapshot->asic_model_dirty)
    {
        s_ui_cache.asic_model_dirty = true;
    }
    if (snapshot->pool_url_dirty)
    {
        s_ui_cache.pool_url_dirty = true;
    }
    if (snapshot->pool_port_dirty)
    {
        s_ui_cache.pool_port_dirty = true;
    }
    if (snapshot->pool_user_dirty)
    {
        s_ui_cache.pool_user_dirty = true;
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
            pool_info_t pool_update = {0};
            bool pool_info_dirty = false;

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
            if (snapshot.self_test_dirty)
            {
                wifi_update_self_test_state(snapshot.self_test);
            }
            if (snapshot.mode_dirty)
            {
                wifi_update_mode(snapshot.mode);
            }
            if (snapshot.block_height_dirty)
            {
                block_update_height(snapshot.block_height);
            }
            if (snapshot.device_model_dirty)
            {
                home_update_device_model(snapshot.device_model);
            }
            if (snapshot.asic_model_dirty)
            {
                home_update_asic_model(snapshot.asic_model);
            }
            if (snapshot.pool_url_dirty)
            {
                strncpy(pool_update.url, snapshot.pool_url, sizeof(pool_update.url) - 1);
                pool_update.url[sizeof(pool_update.url) - 1] = '\0';
                pool_info_dirty = true;
            }
            if (snapshot.pool_port_dirty)
            {
                strncpy(pool_update.port, snapshot.pool_port, sizeof(pool_update.port) - 1);
                pool_update.port[sizeof(pool_update.port) - 1] = '\0';
                pool_info_dirty = true;
            }
            if (snapshot.pool_user_dirty)
            {
                strncpy(pool_update.worker_name, snapshot.pool_user, sizeof(pool_update.worker_name) - 1);
                pool_update.worker_name[sizeof(pool_update.worker_name) - 1] = '\0';
                pool_info_dirty = true;
            }
            if (pool_info_dirty)
            {
                home_update_pool_info(&pool_update);
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
    
    bap_ui_cache_update(s_ui_cache.device_model, sizeof(s_ui_cache.device_model), value, &s_ui_cache.device_model_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_asic_model_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received ASIC model: %s", value);
    
    bap_ui_cache_update(s_ui_cache.asic_model, sizeof(s_ui_cache.asic_model), value, &s_ui_cache.asic_model_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_pool_url_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received pool URL: %s", value);
    
    bap_ui_cache_update(s_ui_cache.pool_url, sizeof(s_ui_cache.pool_url), value, &s_ui_cache.pool_url_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_pool_port_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received pool port: %s", value);
    
    bap_ui_cache_update(s_ui_cache.pool_port, sizeof(s_ui_cache.pool_port), value, &s_ui_cache.pool_port_dirty);
    return ESP_OK;
}

esp_err_t bap_handle_pool_user_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Received pool user: %s", value);
    
    bap_ui_cache_update(s_ui_cache.pool_user, sizeof(s_ui_cache.pool_user), value, &s_ui_cache.pool_user_dirty);
    return ESP_OK;
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

esp_err_t bap_handle_self_test_response(const char *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Received self-test state: %s", value);

    bap_ui_cache_update(s_ui_cache.self_test, sizeof(s_ui_cache.self_test), value, &s_ui_cache.self_test_dirty);
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
    
    bap_ui_cache_update(s_ui_cache.mode, sizeof(s_ui_cache.mode), value, &s_ui_cache.mode_dirty);
    return ESP_OK;
}
