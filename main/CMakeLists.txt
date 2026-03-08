idf_component_register(
    SRCS "ota_update.c" "ota_screen.c" "mempool.c" "block.c" "clock.c" "price.c" "weather.c" "Nevan_RUS_96.c" "untyped_96.c" "montserrat_120.c" "montserrat_140.c" "montserrat_160.c" "clock-solid-full.c" "cube-solid-full.c" "cubes-solid-full.c" "bap_client.c" "bap_parser.c" "bap_uart.c" "bap_protocol.c" "settings.c" "home.c" "wifi.c" "night.c" "background.c" "theme.c" "keyboard_theme.c" "nav_icons.c"
        "loading.c"
        "waveshare_rgb_lcd_port.c"
        "main.c"
        "lvgl_port.c"
        "assets/space_wallpaper.c"
        "assets/cyberpunk_wallpaper.c"
        "assets/woods_wallpaper.c"
        "assets/lvgl_logo.c"
        "assets/temperature_icon.c"
        "assets/fan.c"
        "assets/star.c"
        "assets/nav_bitcoin_icon.c"
        "assets/nav_chart_up_icon.c"
        "assets/weather_icons.c"
        "angelwish_96.c"
    INCLUDE_DIRS ".")

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
