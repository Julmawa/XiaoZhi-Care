idf_component_register(
    SRCS
        "care_web_server.cc"
        "care_web_data.cc"
        "care_web_page.cc"
    INCLUDE_DIRS "include"
    REQUIRES care-restore esp_timer
        care-radio
        care-news
        care-core
        care-telegram
        care-daily
        care-listening
        care-family
        care-mcp
        care-voice
        care-vitals
        esp_http_server
        mbedtls
        cjson
        nvs_flash
)
