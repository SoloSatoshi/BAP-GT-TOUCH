#include "background.h"
#include "assets/cyberpunk_wallpaper.h"
#include "assets/space_wallpaper.h"
#include "assets/woods_wallpaper.h"
#include "theme.h"

void screen_background_apply(lv_obj_t *screen)
{
    if (!screen)
    {
        return;
    }

    lv_obj_set_style_bg_color(screen, ui_theme_get_background_color(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    if (ui_theme_uses_wallpaper())
    {
        lv_obj_t *wallpaper = lv_img_create(screen);
        const lv_img_dsc_t *wallpaper_src = &space_wallpaper;
        if (ui_theme_get_current() == UI_THEME_CYBERPUNK)
        {
            wallpaper_src = &cyberpunk_wallpaper;
        }
        else if (ui_theme_get_current() == UI_THEME_WOODS)
        {
            wallpaper_src = (const lv_img_dsc_t *)&woods_wallpaper;
        }
        lv_img_set_src(wallpaper, wallpaper_src);
        lv_obj_align(wallpaper, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(wallpaper);
    }
}

void translucent_card_apply(lv_obj_t *obj, lv_coord_t radius, lv_opa_t bg_opa)
{
    if (!obj)
    {
        return;
    }

    const bool woods = ui_theme_get_current() == UI_THEME_WOODS;
    const lv_opa_t resolved_bg_opa = ui_theme_uses_wallpaper() ? ui_theme_get_surface_fill_opa() : bg_opa;

    lv_obj_set_style_bg_color(obj, ui_theme_get_surface_fill_color(), 0);
    lv_obj_set_style_bg_grad_color(obj, ui_theme_get_surface_fill_color(), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(obj, woods ? LV_OPA_TRANSP : resolved_bg_opa, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, ui_theme_get_surface_outline_color(), 0);
    lv_obj_set_style_border_opa(obj, woods ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(obj, woods ? LV_OPA_TRANSP : LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(obj, woods ? 0 : 10, 0);
    lv_obj_set_style_shadow_spread(obj, 0, 0);
    lv_obj_set_style_shadow_ofs_x(obj, 0, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 3, 0);
}
