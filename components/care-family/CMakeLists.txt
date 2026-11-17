idf_component_register(
    SRCS
        "src/family_relationship.cc"
        "src/nvs_family_relationship_repository.cc"
    INCLUDE_DIRS
        "include"
    REQUIRES care-restore
        nvs_flash
        cjson
)
