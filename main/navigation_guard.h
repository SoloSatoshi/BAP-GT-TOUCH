#ifndef NAVIGATION_GUARD_H
#define NAVIGATION_GUARD_H

#include "lvgl.h"

void *ui_navigation_make_user_data(lv_event_cb_t cb);
void ui_navigation_guarded_click_cb(lv_event_t *e);

#endif
