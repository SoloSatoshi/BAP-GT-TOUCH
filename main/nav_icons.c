#include "nav_icons.h"
#include "assets/nav_bitcoin_icon.h"
#include "assets/nav_chart_up_icon.h"
#include <string.h>

static void nav_icon_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           lv_coord_t radius, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_set_size(shape, w, h);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_style_bg_color(shape, color, 0);
    lv_obj_set_style_bg_opa(shape, opa, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE);
}

static void nav_icon_cloud(lv_obj_t *btn, lv_color_t color)
{
    lv_obj_t *icon = lv_obj_create(btn);
    lv_obj_set_size(icon, 32, 22);
    lv_obj_center(icon);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    nav_icon_shape(icon, 3, 10, 26, 8, 4, color, LV_OPA_COVER);
    nav_icon_shape(icon, 2, 7, 10, 10, LV_RADIUS_CIRCLE, color, LV_OPA_COVER);
    nav_icon_shape(icon, 10, 2, 12, 12, LV_RADIUS_CIRCLE, color, LV_OPA_COVER);
    nav_icon_shape(icon, 19, 7, 10, 10, LV_RADIUS_CIRCLE, color, LV_OPA_COVER);
}

static void nav_icon_bitcoin(lv_obj_t *btn, lv_color_t color)
{
    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, &nav_bitcoin_icon);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
}

static void nav_icon_chart(lv_obj_t *btn, lv_color_t color)
{
    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, &nav_chart_up_icon);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
}

bool nav_icon_render(lv_obj_t *btn, const char *symbol, lv_color_t color)
{
    if (!btn || !symbol)
    {
        return false;
    }

    if (strcmp(symbol, NAV_ICON_BITCOIN) == 0)
    {
        nav_icon_bitcoin(btn, color);
        return true;
    }

    if (strcmp(symbol, NAV_ICON_WEATHER) == 0)
    {
        nav_icon_cloud(btn, color);
        return true;
    }

    if (strcmp(symbol, NAV_ICON_CHART) == 0)
    {
        nav_icon_chart(btn, color);
        return true;
    }

    return false;
}
