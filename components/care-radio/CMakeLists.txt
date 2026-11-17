idf_component_register(
    SRCS "radio_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES care-restore esp_timer
        esp_http_client
        esp-tls
        nvs_flash
        espressif__esp_audio_codec
)
