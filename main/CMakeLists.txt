idf_component_register(
    SRCS "cJSON.c" "sd_card.c" "ble_memo.c" "shtc3.c" "pcf85063.c" "battery_monitor.c" "ui_font_date_large.c" "ui_home.c" "ui_menu.c" "http_downloader.c" "wifi_manager.c" "main.cpp"
         "ui_font_custom.c"
         "../ui_assets/ui_icon_note.c"
         "../ui_assets/ui_icon_read.c"
         "../ui_assets/ui_icon_photo.c"
         "../ui_assets/ui_icon_word.c"
         "../ui_assets/ui_icon_music.c"
         "../ui_assets/ui_icon_note_large.c"
         "../ui_assets/ui_icon_read_large.c"
         "../ui_assets/ui_icon_photo_large.c"
         "../ui_assets/ui_icon_word_large.c"
         "../ui_assets/ui_icon_music_large.c"
    
    INCLUDE_DIRS "./"
    REQUIRES bt nvs_flash esp_driver_i2c esp_driver_gpio esp_driver_pcnt app_bsp port_bsp user_app ui_bsp
    PRIV_REQUIRES esp_adc esp_http_client fatfs sdmmc esp_driver_sdmmc)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(ble_memo.c PROPERTIES COMPILE_OPTIONS "-std=gnu17")
endif()
