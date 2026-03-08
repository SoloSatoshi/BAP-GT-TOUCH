#ifndef WEATHER_H
#define WEATHER_H

#include "lvgl.h"

void weather_service_start(void);
void weather_service_request_refresh(void);
void weather_screen_create(void);
void weather_screen_destroy(void);
lv_obj_t *weather_get_screen(void);

void weather_home_clicked(lv_event_t *e);
void weather_block_clicked(lv_event_t *e);
void weather_mempool_clicked(lv_event_t *e);
void weather_clock_clicked(lv_event_t *e);
void weather_price_clicked(lv_event_t *e);
void weather_wifi_clicked(lv_event_t *e);
void weather_settings_clicked(lv_event_t *e);
void weather_night_clicked(lv_event_t *e);

#endif // WEATHER_H
