idf_component_register(
    SRCS "care_mcp_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        care-radio
        care-news
        care-core
        care-daily
        care-family
        cjson
        esp_http_client
        esp-tls
)
