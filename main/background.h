#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "lvgl.h"

void screen_background_apply(lv_obj_t *screen);
void translucent_card_apply(lv_obj_t *obj, lv_coord_t radius, lv_opa_t bg_opa);

#endif
