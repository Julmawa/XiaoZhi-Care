idf_component_register(
    SRCS "care_storage.cc"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash spiffs vfs
)


