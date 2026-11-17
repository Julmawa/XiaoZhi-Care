idf_component_register(
    SRCS
        "care_telegram_repository.cc" "care_telegram_pair.cc"
        "care_telegram_probe.cc"
    INCLUDE_DIRS
        "include"
    REQUIRES care-restore esp_timer esp_hw_support nvs_flash
        cjson
        esp_http_client
        mbedtls
        care-core
)