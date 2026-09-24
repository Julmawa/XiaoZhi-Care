#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "care_daily/daily_time.h"
#include "care_daily/routine_id.h"

namespace xiaozhi_care::daily {

/**
 * @brief Strong ID for a routine execution event.
 */
struct RoutineExecutionId {
    std::string value;

    RoutineExecutionId() = default;

    explicit RoutineExecutionId(std::string id) : value(std::move(id)) {}

    [[nodiscard]] bool Empty() const {
        return value.empty();
    }

    [[nodiscard]] const std::string& Str() const {
        return value;
    }

    bool operator==(const RoutineExecutionId& rhs) const {
        return value == rhs.value;
    }

    bool operator!=(const RoutineExecutionId& rhs) const {
        return !(*this == rhs);
    }
};

/**
 * @brief Operational event registered for a routine.
 *
 * These are not medical assertions. They describe what XiaoZhi Care indicated
 * or what a user/caregiver/hardware source reported.
 */
enum class RoutineExecutionEvent : uint8_t {
    Indicated,
    Confirmed,
    Skipped,
    Missed,
    Snoozed,
};

/**
 * @brief Source that generated an execution event.
 */
enum class RoutineExecutionSource : uint8_t {
    Engine,
    Voice,
    Web,
    Hardware,
    Test,
};

/**
 * @brief A persistent event in the daily routine history.
 */
struct RoutineExecution {
    RoutineExecutionId id;
    RoutineId routine_id;

    RoutineExecutionEvent event{RoutineExecutionEvent::Indicated};
    RoutineExecutionSource source{RoutineExecutionSource::Engine};

    /**
     * ISO local date: YYYY-MM-DD.
     */
    std::string iso_date;

    /**
     * Local time for the event.
     */
    DailyTime time{};

    /**
     * Safe non-medical text, suitable for logs/admin review.
     */
    std::string message;

    /**
     * Optional internal note for admin/debug use.
     */
    std::string note;

    [[nodiscard]] bool IsValidDate() const {
        return iso_date.size() == 10 &&
               iso_date[4] == '-' &&
               iso_date[7] == '-';
    }

    [[nodiscard]] bool IsValid() const {
        return !id.Empty() && !routine_id.Empty() && IsValidDate() && time.IsValid();
    }
};

}  // namespace xiaozhi_care::daily
