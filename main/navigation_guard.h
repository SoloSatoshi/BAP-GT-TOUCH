#ifndef NAVIGATION_GUARD_H
#define NAVIGATION_GUARD_H

#include "lvgl.h"

void *ui_navigation_make_user_data(lv_event_cb_t cb);
void ui_navigation_guarded_click_cb(lv_event_t *e);
void ui_navigation_block_for_ms(uint32_t ms);
void ui_navigation_show_wifi_credentials_required_popup(void);
void ui_navigation_show_wifi_connection_required_popup(void);

#endif
