idf_component_register(
    SRCS
        "src/voice_recording_store.cc"
        "src/reminder_voice_runtime.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        care-core
        spiffs
        cjson
        vfs
        freertos
)
