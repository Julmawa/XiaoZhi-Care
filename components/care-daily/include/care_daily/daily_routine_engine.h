#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "care_daily/care_routine.h"
#include "care_daily/daily_time.h"

namespace xiaozhi_care::daily {

/**
 * @brief Context needed to evaluate routines.
 *
 * The engine does not read the system clock directly. The caller supplies the
 * current day and time, which keeps the engine deterministic and testable.
 *
 * iso_weekday uses ISO convention: Monday = 1, Sunday = 7.
 */
struct RoutineEvaluationContext {
    uint8_t iso_weekday{1};
    DailyTime now{};

    [[nodiscard]] bool IsValid() const {
        return iso_weekday >= 1 && iso_weekday <= 7 && now.IsValid();
    }
};

/**
 * @brief Result category for routine evaluation.
 */
enum class RoutineDueStatus : uint8_t {
    Due,
    NotDue,
    InvalidRoutine,
    InvalidContext,
    Paused,
    Archived,
    AsNeeded,
    UnsupportedRepeat,
};

/**
 * @brief Evaluation result returned by DailyRoutineEngine.
 */
struct RoutineEvaluation {
    RoutineId routine_id;
    RoutineDueStatus status{RoutineDueStatus::NotDue};
    int16_t minutes_from_schedule{0};
    std::string safe_message;

    [[nodiscard]] bool IsDue() const {
        return status == RoutineDueStatus::Due;
    }
};

/**
 * @brief Deterministic engine for Daily Care routine evaluation.
 *
 * This class contains no NVS, MCP, HTTP, BLE, or ESP-IDF dependency.
 */
class DailyRoutineEngine {
public:
    DailyRoutineEngine() = default;

    [[nodiscard]] RoutineEvaluation Evaluate(const CareRoutine& routine,
                                             const RoutineEvaluationContext& context) const;

    [[nodiscard]] std::vector<RoutineEvaluation> EvaluateAll(const std::vector<CareRoutine>& routines,
                                                             const RoutineEvaluationContext& context) const;

    [[nodiscard]] std::vector<CareRoutine> FindDueRoutines(const std::vector<CareRoutine>& routines,
                                                           const RoutineEvaluationContext& context) const;

private:
    [[nodiscard]] bool IsRoutineForWeekday(const CareRoutine& routine,
                                           uint8_t iso_weekday) const;

    [[nodiscard]] std::string BuildSafeMessage(const CareRoutine& routine) const;
};

}  // namespace xiaozhi_care::daily
