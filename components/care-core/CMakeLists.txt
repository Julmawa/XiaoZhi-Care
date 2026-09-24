idf_component_register(
    SRCS "care_manager.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        care-storage
        mbedtls
)
