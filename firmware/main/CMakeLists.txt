# firmware/main/CMakeLists.txt
idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "ble_service.c"
        "notification.c"
        "ui_manager.c"
        "scene.c"
        "notification_ui.c"
        "config_store.c"
        "rgb_led.c"
        "pixel_font.c"
        "button.c"       # BOARD_HAS_BOOT_BUTTON guards the body
        "touch.c"        # BOARD_HAS_TOUCH guards the body
        "battery.c"      # BOARD_HAS_BATTERY guards the body
    INCLUDE_DIRS "."
    REQUIRES esp_lcd driver esp_timer bt nvs_flash freertos json esp_adc
)
