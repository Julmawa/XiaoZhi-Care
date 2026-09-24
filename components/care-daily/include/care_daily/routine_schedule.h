#pragma once

#include <array>
#include <cstdint>

#include "care_daily/daily_time.h"

namespace xiaozhi_care::daily {

/**
 * @brief Recurrence strategy for a daily routine.
 */
enum class RoutineRepeatType : uint8_t {
    Daily,
    SpecificWeekdays,
    EveryNDays,
    AsNeeded,
};

/**
 * @brief Conversational moment associated with a routine.
 *
 * This helps XiaoZhi Care say "en ayunas", "con el desayuno" or
 * "antes de dormir" instead of only reading a clock time.
 */
enum class RoutineMoment : uint8_t {
    Anytime,
    Fasting,
    BeforeBreakfast,
    WithBreakfast,
    AfterBreakfast,
    BeforeLunch,
    WithLunch,
    AfterLunch,
    BeforeDinner,
    WithDinner,
    AfterDinner,
    Bedtime,
};

/**
 * @brief Schedule definition for a routine.
 *
 * Weekdays use ISO order: Monday = index 0, Sunday = index 6.
 */
struct RoutineSchedule {
    DailyTime time{};
    RoutineRepeatType repeat{RoutineRepeatType::Daily};
    RoutineMoment moment{RoutineMoment::Anytime};
    std::array<bool, 7> weekdays{{true, true, true, true, true, true, true}};
    uint16_t interval_days{1};
    uint16_t reminder_window_minutes{30};

    [[nodiscard]] bool IsValid() const {
        if (!time.IsValid()) {
            return false;
        }

        if (repeat == RoutineRepeatType::EveryNDays && interval_days == 0) {
            return false;
        }

        if (repeat == RoutineRepeatType::SpecificWeekdays) {
            bool any_day = false;
            for (bool enabled : weekdays) {
                any_day = any_day || enabled;
            }
            if (!any_day) {
                return false;
            }
        }

        return true;
    }
};

}  // namespace xiaozhi_care::daily
