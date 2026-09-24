idf_component_register(
    SRCS
        "care_web_server.cc"
        "care_web_data.cc"
        "care_web_page.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        care-core
        care-daily
        care-family
        care-mcp
        care-voice
        esp_http_server
        mbedtls
        cjson
        nvs_flash
)
