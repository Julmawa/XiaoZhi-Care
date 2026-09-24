#pragma once

#include <cstdint>
#include <map>
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
    bool IsRunning() const { return task_ != nullptr; }

private:
    ReminderVoiceRuntime() = default;

    static void TaskEntry(void* arg);
    void Run();
    void Tick();

    TaskHandle_t task_{nullptr};
    std::map<std::string, std::string> last_fired_occurrence_;
    bool clock_warning_logged_{false};
};

}  // namespace xiaozhi_care::voice
