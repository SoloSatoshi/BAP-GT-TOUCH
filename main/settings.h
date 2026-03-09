#ifndef SETTINGS_H
#define SETTINGS_H

#include "lvgl.h"

typedef enum {
    PERFORMANCE_LOW = 0,
    PERFORMANCE_MEDIUM,
    PERFORMANCE_HIGH
} performance_mode_t;

typedef enum {
    PRICE_CURRENCY_USD = 0,
    PRICE_CURRENCY_EUR,
    PRICE_CURRENCY_GBP,
    PRICE_CURRENCY_CAD,
    PRICE_CURRENCY_AUD,
    PRICE_CURRENCY_JPY
} price_currency_t;

typedef enum {
    WEATHER_TEMPERATURE_UNIT_F = 0,
    WEATHER_TEMPERATURE_UNIT_C
} weather_temperature_unit_t;

typedef struct {
    performance_mode_t performance_mode;
    bool auto_fan_control;
    int fan_speed_percent;  // 0-100%
    int brightness_percent; // 0-100%
    price_currency_t price_currency;
} settings_info_t;

void settings_screen_create(void);
void settings_screen_destroy(void);
lv_obj_t* settings_get_screen(void);
void settings_update_info(const settings_info_t* info);

void settings_performance_low_clicked(lv_event_t * e);
void settings_performance_medium_clicked(lv_event_t * e);
void settings_performance_high_clicked(lv_event_t * e);
void settings_auto_fan_toggled(lv_event_t * e);
void settings_fan_slider_changed(lv_event_t * e);
void settings_fan_save_clicked(lv_event_t * e);
void settings_brightness_slider_changed(lv_event_t * e);
void settings_timezone_changed(lv_event_t * e);
void settings_price_currency_changed(lv_event_t * e);
void settings_weather_temperature_unit_changed(lv_event_t * e);
void settings_weather_location_save_clicked(lv_event_t * e);
void settings_home_clicked(lv_event_t * e);
void settings_block_clicked(lv_event_t * e);
void settings_clock_clicked(lv_event_t * e);
void settings_price_clicked(lv_event_t * e);
void settings_weather_clicked(lv_event_t * e);
void settings_mempool_clicked(lv_event_t * e);
void settings_wifi_clicked(lv_event_t * e);
void settings_night_clicked(lv_event_t * e);
price_currency_t settings_get_price_currency(void);
const char *settings_get_price_currency_code(void);
const char *settings_get_price_currency_prefix(void);
const char *settings_get_price_currency_suffix(void);
const char *settings_get_weather_country_code(void);
const char *settings_get_weather_postal_code(void);
weather_temperature_unit_t settings_get_weather_temperature_unit(void);
void settings_factory_reset_note_bitaxe_ack(void);
bool settings_factory_reset_is_in_progress(void);

#endif // SETTINGS_H
