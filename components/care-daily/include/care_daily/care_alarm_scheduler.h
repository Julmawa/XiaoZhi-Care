#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "care_daily/care_alert_config.h"
#include "care_daily/daily_routine_engine.h"
#include "care_daily/routine_execution_repository.h"
#include "care_daily/routine_repository.h"

namespace xiaozhi_care::daily {

/**
 * @brief Action requested by the alarm scheduler.
 */
struct CareAlarmAction {
    RoutineId routine_id;
    CareAlertMode mode{CareAlertMode::Disabled};
    std::string safe_message;
    bool visual{false};
    bool sound{false};
    bool voice{false};
    CareVisualPattern visual_pattern{CareVisualPattern::None};
    CareAlertColor visual_color{CareAlertColor::Auto};
    CareSoundPattern sound_pattern{CareSoundPattern::None};
    bool should_notify{false};
    bool execution_saved{false};
};

/**
 * @brief Summary returned after one scheduler pass.
 */
struct CareAlarmSchedulerResult {
    uint16_t due_count{0};
    uint16_t notified_count{0};
    uint16_t suppressed_count{0};
    uint16_t storage_error_count{0};
    std::vector<CareAlarmAction> actions;
};

/**
 * @brief Deterministic care alarm scheduler.
 *
 * This class evaluates routines, decides whether an alert should be emitted,
 * and attempts to record an "Indicated" execution event. It does not directly
 * play sound, draw to a display, or drive LEDs. Presentation remains delegated
 * to XiaoZhi's existing UI/audio/LED infrastructure.
 *
 * Important safety rule: a storage failure must not prevent the alert itself.
 * If NVS is full, the scheduler still emits the action and keeps a volatile
 * in-memory indication so duplicate suppression works during the current boot.
 */
class CareAlarmScheduler {
public:
    CareAlarmScheduler(RoutineRepository& routines,
                       RoutineExecutionRepository& executions);

    [[nodiscard]] CareAlarmSchedulerResult Tick(const RoutineEvaluationContext& context,
                                                const std::string& iso_date);

    /**
     * @brief Test/maintenance helper: evaluate only one routine.
     *
     * Production code should normally use Tick(). This overload exists to keep
     * validation runs deterministic when the ESP32 already has real routines
     * saved in NVS.
     */
    [[nodiscard]] CareAlarmSchedulerResult TickForRoutine(const RoutineEvaluationContext& context,
                                                          const std::string& iso_date,
                                                          const RoutineId& only_routine_id);

    /**
     * @brief NVS-safe helper: evaluate a supplied routine list instead of
     * loading routines from persistent storage.
     *
     * Useful for tests and future temporary/in-memory use cases. Execution
     * events are still attempted through the configured execution repository.
     */
    [[nodiscard]] CareAlarmSchedulerResult TickRoutines(const std::vector<CareRoutine>& routines,
                                                        const RoutineEvaluationContext& context,
                                                        const std::string& iso_date);

private:
    RoutineRepository& routines_;
    RoutineExecutionRepository& executions_;
    DailyRoutineEngine engine_;
    std::vector<RoutineExecution> volatile_indications_;

    [[nodiscard]] bool AlreadyConfirmedToday(const RoutineId& routine_id,
                                             const std::string& iso_date) const;

    [[nodiscard]] uint8_t CountIndicatedToday(const RoutineId& routine_id,
                                             const std::string& iso_date) const;

    [[nodiscard]] DailyTime LastIndicatedTimeToday(const RoutineId& routine_id,
                                                  const std::string& iso_date,
                                                  bool& found) const;

    [[nodiscard]] CareAlarmSchedulerResult TickInternal(const std::vector<CareRoutine>& routines,
                                                        const RoutineEvaluationContext& context,
                                                        const std::string& iso_date,
                                                        const RoutineId* only_routine_id);

    [[nodiscard]] RoutineExecution BuildIndicatedExecution(const CareRoutine& routine,
                                                           const RoutineEvaluationContext& context,
                                                           const std::string& iso_date,
                                                           const std::string& safe_message);

    [[nodiscard]] bool RecordIndicated(const CareRoutine& routine,
                                       const RoutineEvaluationContext& context,
                                       const std::string& iso_date,
                                       const std::string& safe_message,
                                       RoutineExecution& execution);
};

}  // namespace xiaozhi_care::daily
