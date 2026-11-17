// DP_TG_V278_DIAG: instrumentacion TEMPORAL, quitar despues del diagnostico.
#pragma once
#if defined(CARE_DP049_ENABLE_RADIO_DIAG) && CARE_DP049_ENABLE_RADIO_DIAG
#include <atomic>
#include <cstdint>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace care_audio_diag_v278 {
// Stages: 10=callback, 20=PCM, 21=resampler-lock, 22=resampler-held,
// 23=resampler-process, 24=resampler-done, 30=output-lock,
// 31=output-held, 32=codec-output, 33=codec-done.
inline std::atomic<int> stage{0};
inline std::atomic<int64_t> since_us{0};
inline std::atomic<unsigned> ordinal{0};
inline void mark(int new_stage) {
    since_us.store(esp_timer_get_time(), std::memory_order_relaxed);
    stage.store(new_stage, std::memory_order_release);
    if (new_stage == 10) ordinal.fetch_add(1, std::memory_order_relaxed);
}
inline void monitor(void*) {
    int previous_stage = -1;
    int64_t last_report_us = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
        const int s = stage.load(std::memory_order_acquire);
        const int64_t now = esp_timer_get_time();
        const int64_t began = since_us.load(std::memory_order_relaxed);
        if (s == 0 || began <= 0 || now < began || now - began < 1500000) {
            previous_stage = -1;
            continue;
        }
        if (s != previous_stage || now - last_report_us > 2000000) {
            ESP_LOGW("CARE_AUDIO_DIAG", "V278 BLOQUEO_POSIBLE etapa=%d duracion_ms=%lld bloque=%u",
                     s, static_cast<long long>((now-began)/1000), ordinal.load());
            previous_stage = s;
            last_report_us = now;
        }
    }
}
inline void start_monitor() {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (started.compare_exchange_strong(expected, true)) {
        BaseType_t ok = xTaskCreate(monitor, "care_audio_diag", 3072, nullptr, 5, nullptr);
        if (ok != pdPASS) {
            started.store(false);
            ESP_LOGE("CARE_AUDIO_DIAG", "No se pudo crear monitor V278");
        } else {
            ESP_LOGI("CARE_AUDIO_DIAG", "V278 monitor activo (no modifica I2S)");
        }
    }
}
} // namespace care_audio_diag_v278

#else
namespace care_audio_diag_v278 {
inline void mark(int) {}
inline void start_monitor() {}
}
#endif
