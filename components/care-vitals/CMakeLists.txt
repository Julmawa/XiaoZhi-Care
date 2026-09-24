idf_component_register(
    SRCS "care_vitals_store.cc"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash cjson
)
