#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace xiaozhi_care::daily {

/**
 * @brief Configuration for the real alarm runtime task.
 */
struct CareAlarmRuntimeConfig {
    bool enabled{true};
    uint32_t startup_delay_ms{20000};
    uint32_t tick_interval_ms{30000};
    uint16_t minimum_valid_year{2024};
    uint32_t task_stack_size{8192};
    UBaseType_t task_priority{4};

    [[nodiscard]] bool IsValid() const {
        return tick_interval_ms >= 5000 &&
               tick_interval_ms <= 3600000 &&
               task_stack_size >= 4096 &&
               minimum_valid_year >= 2024;
    }
};

/**
 * @brief Runtime integration for CareAlarmScheduler.
 *
 * DP-012 validated the deterministic scheduler. DP-013 runs it periodically
 * in production using the routines stored in NVS.
 *
 * This class delegates presentation to CareAlarmNotifier. The notifier exposes
 * weak board hooks so a board-specific file can drive LEDs, screen, or sound
 * without coupling the scheduler to a concrete hardware implementation.
 */
class CareAlarmRuntime {
public:
    static CareAlarmRuntime& GetInstance();

    bool Start(const CareAlarmRuntimeConfig& config = CareAlarmRuntimeConfig{});
    void Stop();

    [[nodiscard]] bool IsRunning() const;

private:
    CareAlarmRuntime() = default;
    ~CareAlarmRuntime() = default;

    CareAlarmRuntime(const CareAlarmRuntime&) = delete;
    CareAlarmRuntime& operator=(const CareAlarmRuntime&) = delete;

    static void TaskEntry(void* arg);
    void TaskLoop();

    CareAlarmRuntimeConfig config_{};
    TaskHandle_t task_handle_{nullptr};
    volatile bool stop_requested_{false};
};

}  // namespace xiaozhi_care::daily
