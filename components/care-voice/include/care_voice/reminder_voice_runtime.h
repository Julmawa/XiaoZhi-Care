#pragma once

#include <cstdint>
#include <map>
#include <atomic>
#include <memory>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace xiaozhi_care::voice {

/**
 * DP-018-r2 — runtime de audio para CareReminder.
 *
 * Evalúa únicamente recordatorios que tienen un audio asignado en care-voice.
 * No modifica el modelo CareReminder ni su esquema NVS.
 */
class ReminderVoiceRuntime {
public:
    static ReminderVoiceRuntime& GetInstance();

    // Idempotente. Puede llamarse más de una vez sin crear tareas duplicadas.
    bool Start();
    bool IsRunning() const { return started_.load(); }

private:
    ReminderVoiceRuntime() = default;

    static void TaskEntry(void* arg);
    void Run();
    void Tick();

    std::atomic<bool> started_{false};
    std::map<std::string, std::string> last_fired_occurrence_;
    std::string fired_date_,pending_id_,pending_occurrence_;
    std::shared_ptr<std::atomic<int>> pending_result_;
    bool clock_warning_logged_{false};
};

}  // namespace xiaozhi_care::voice
