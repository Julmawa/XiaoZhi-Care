#pragma once

#include <cstdint>

namespace xiaozhi_care::daily {

/**
 * @brief High-level type of a daily routine.
 *
 * Daily Care treats medication, hydration, exercise, measurements and custom
 * actions as routines. Specific data is stored separately from the schedule.
 */
enum class RoutineType : uint8_t {
    Medication,
    Hydration,
    Exercise,
    HealthMeasurement,
    Reminder,
    Birthday,
    Custom,
};

}  // namespace xiaozhi_care::daily
