#include "navigation_guard.h"
#include "esp_timer.h"

#define UI_NAVIGATION_GUARD_US (350 * 1000)

typedef union
{
    lv_event_cb_t cb;
    void *ptr;
} ui_navigation_cb_ref_t;

static int64_t s_navigation_block_until_us = 0;

void *ui_navigation_make_user_data(lv_event_cb_t cb)
{
    ui_navigation_cb_ref_t ref = {
        .cb = cb,
    };
    return ref.ptr;
}

static lv_event_cb_t ui_navigation_cb_from_user_data(void *data)
{
    ui_navigation_cb_ref_t ref = {
        .ptr = data,
    };
    return ref.cb;
}

void ui_navigation_guarded_click_cb(lv_event_t *e)
{
    lv_event_cb_t cb = ui_navigation_cb_from_user_data(lv_event_get_user_data(e));
    if (!cb)
    {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (now < s_navigation_block_until_us)
    {
        return;
    }

    s_navigation_block_until_us = now + UI_NAVIGATION_GUARD_US;
    cb(e);
}
