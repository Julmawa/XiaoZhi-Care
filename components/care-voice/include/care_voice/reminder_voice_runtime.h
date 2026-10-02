#pragma once

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace xiaozhi_care::voice {

// DP044B_UNIVERSAL_ACK
// Estado efímero de acuse de recibo. No representa que una actividad haya
// sido realizada ni que una medicación haya sido tomada.
struct ReminderAckState {
    bool found{false};
    bool acknowledged{false};
    uint8_t attempts{0};
    std::time_t last_presented_at{0};
};

struct ReminderAckResult {
    bool acknowledged{false};
    std::string target_id;
    std::string occurrence;
    std::string message;
    uint8_t attempts{0};
};

/**
 * DP-018-r2 — runtime de audio para CareReminder.
 *
 * Evalúa únicamente recordatorios que tienen un audio asignado en care-voice.
 * No modifica el modelo CareReminder ni su esquema NVS.
 *
 * DP044B agrega un coordinador de acuse de recibo EN RAM compartido por
 * CareReminder y CareRoutine. "Acknowledged" significa solamente que la
 * persona recibió/escuchó el aviso; NO equivale a RoutineExecution::Confirmed.
 */
class ReminderVoiceRuntime {
public:
    static ReminderVoiceRuntime& GetInstance();

    // Idempotente. Puede llamarse más de una vez sin crear tareas duplicadas.
    bool Start();
    bool IsRunning() const { return task_ != nullptr; }

    // DP044B_UNIVERSAL_ACK
    void RegisterPresentation(const std::vector<std::string>& target_ids,
                              const std::string& occurrence,
                              const std::string& message,
                              std::time_t presented_at = 0);
    ReminderAckState GetAcknowledgementState(const std::string& target_id,
                                             const std::string& occurrence) const;
    bool IsRetryDue(const std::string& target_id,
                    const std::string& occurrence,
                    std::time_t now = 0) const;
    bool HasPendingTarget(const std::string& target_id) const;
    ReminderAckResult AcknowledgeTarget(const std::string& target_id);
    ReminderAckResult AcknowledgeLatest();

    static constexpr uint32_t kAckRetrySeconds = 180;
    static constexpr uint8_t kAckMaxAttempts = 3;

private:
    ReminderVoiceRuntime() = default;

    struct AckEntry {
        std::vector<std::string> target_ids;
        std::string occurrence;
        std::string message;
        uint8_t attempts{0};
        std::time_t last_presented_at{0};
        bool acknowledged{false};
        uint32_t sequence{0};
    };

    static void TaskEntry(void* arg);
    void Run();
    void Tick();

    TaskHandle_t task_{nullptr};
    bool clock_warning_logged_{false};

    // Acuses deliberadamente efímeros: no cambian CareReminder, RoutineExecution
    // ni el esquema persistente de DP-036.
    mutable std::mutex ack_mutex_;
    std::vector<AckEntry> ack_entries_;
    uint32_t ack_sequence_{0};
};

}  // namespace xiaozhi_care::voice
