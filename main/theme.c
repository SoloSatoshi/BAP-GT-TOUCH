#include "theme.h"
#include "nvs.h"
#include "nvs_flash.h"

#define THEME_NVS_NAMESPACE "settings"
#define THEME_NVS_KEY "ui_theme"

static ui_theme_t current_theme = UI_THEME_BITAXE_RED;
static bool theme_loaded = false;
static bool theme_nvs_ready = false;

static bool ui_theme_ensure_nvs_ready(void)
{
    if (theme_nvs_ready)
    {
        return true;
    }

    esp_err_t init_err = nvs_flash_init();
    if (init_err == ESP_ERR_NVS_NO_FREE_PAGES || init_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        init_err = nvs_flash_init();
    }

    if (init_err != ESP_OK)
    {
        return false;
    }

    theme_nvs_ready = true;
    return true;
}

static void ui_theme_load_if_needed(void)
{
    if (theme_loaded)
    {
        return;
    }

    theme_loaded = true;

    if (!ui_theme_ensure_nvs_ready())
    {
        return;
    }

    nvs_handle_t handle;
    int32_t saved_theme = (int32_t)UI_THEME_BITAXE_RED;
    esp_err_t err = nvs_open(THEME_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    err = nvs_get_i32(handle, THEME_NVS_KEY, &saved_theme);
    nvs_close(handle);
    if (err == ESP_OK && saved_theme >= 0 && saved_theme < UI_THEME_COUNT)
    {
        current_theme = (ui_theme_t)saved_theme;
    }
}

void ui_theme_init(void)
{
    ui_theme_load_if_needed();
}

ui_theme_t ui_theme_get_current(void)
{
    ui_theme_load_if_needed();
    return current_theme;
}

void ui_theme_set_current(ui_theme_t theme)
{
    ui_theme_load_if_needed();
    if (theme >= 0 && theme < UI_THEME_COUNT)
    {
        current_theme = theme;
    }
}

bool ui_theme_save_current(void)
{
    ui_theme_load_if_needed();
    if (!ui_theme_ensure_nvs_ready())
    {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(THEME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    err = nvs_set_i32(handle, THEME_NVS_KEY, (int32_t)current_theme);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

const char *ui_theme_get_options(void)
{
    return "Bitaxe Red\nMonochrome\nSpace\nCyberpunk\nWild Flower";
}

lv_color_t ui_theme_get_background_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
        case UI_THEME_CYBERPUNK:
        case UI_THEME_SPACE:
            return lv_color_hex(0x000000);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0x000000);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0x050506);
    }
}

lv_color_t ui_theme_get_card_bg_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0x140B18);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0x12091D);
        case UI_THEME_SPACE:
            return lv_color_hex(0x0F1218);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0x101010);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0x0F1218);
    }
}

lv_color_t ui_theme_get_accent_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0xB45CFF);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0xFF7A1A);
        case UI_THEME_SPACE:
            return lv_color_hex(0x7EC8FF);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0xFFFFFF);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0xD4021B);
    }
}

lv_color_t ui_theme_get_red_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0xFF7A1A);
        default:
            return ui_theme_get_accent_color();
    }
}

lv_color_t ui_theme_get_text_primary_color(void)
{
    return lv_color_hex(0xFFFFFF);
}

lv_color_t ui_theme_get_text_secondary_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0xB45CFF);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0xC7B7E8);
        case UI_THEME_SPACE:
            return lv_color_hex(0xA3A3A3);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0xB8B8B8);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0xA3A3A3);
    }
}

lv_color_t ui_theme_get_text_on_accent_color(void)
{
    return lv_color_hex(0x000000);
}

lv_color_t ui_theme_get_border_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0x1F1123);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0x1A1027);
        case UI_THEME_SPACE:
            return lv_color_hex(0x1A1D24);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0x3A3A3A);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0x1A1D24);
    }
}

lv_color_t ui_theme_get_nav_bg_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0x120913);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0x09050F);
        case UI_THEME_SPACE:
            return lv_color_hex(0x0C0F14);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0x080808);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0x0C0F14);
    }
}

lv_color_t ui_theme_get_nav_icon_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0xB45CFF);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0xFF7A1A);
        case UI_THEME_SPACE:
            return lv_color_hex(0x7EC8FF);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0xFFFFFF);
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0xD4021B);
    }
}

lv_color_t ui_theme_get_surface_fill_color(void)
{
    return lv_color_hex(0x000000);
}

lv_opa_t ui_theme_get_surface_fill_opa(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
        case UI_THEME_CYBERPUNK:
        case UI_THEME_SPACE:
            return LV_OPA_50;
        case UI_THEME_MONOCHROME:
        case UI_THEME_BITAXE_RED:
        default:
            return LV_OPA_COVER;
    }
}

lv_color_t ui_theme_get_surface_outline_color(void)
{
    switch (ui_theme_get_current())
    {
        case UI_THEME_WILD_FLOWER:
            return lv_color_hex(0xB45CFF);
        case UI_THEME_CYBERPUNK:
            return lv_color_hex(0xFF7A1A);
        case UI_THEME_MONOCHROME:
            return lv_color_hex(0xD9D9D9);
        case UI_THEME_SPACE:
        case UI_THEME_BITAXE_RED:
        default:
            return lv_color_hex(0xFFFFFF);
    }
}

bool ui_theme_uses_wallpaper(void)
{
    ui_theme_t theme = ui_theme_get_current();
    return theme == UI_THEME_SPACE || theme == UI_THEME_CYBERPUNK || theme == UI_THEME_WILD_FLOWER;
}
