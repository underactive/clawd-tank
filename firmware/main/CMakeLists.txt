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
        "i2c_bus.c"      # BOARD_HAS_I2C_BUS guards the body
        "sound.c"        # BOARD_HAS_AUDIO guards the body
        "es8311.c"       # ES8311 codec driver (only reached when BOARD_HAS_AUDIO)
    INCLUDE_DIRS "."
    REQUIRES esp_lcd driver esp_timer bt nvs_flash freertos json
    EMBED_FILES
        "assets/keyboard_type.pcm"
        "assets/notification_click.pcm"
        "assets/hammering.pcm"
        "assets/thinking_robot.pcm"
        "assets/searching.pcm"
)
