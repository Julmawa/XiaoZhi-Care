#pragma once

#include <string>

#include "care_daily/routine_id.h"
#include "care_daily/care_alert_config.h"
#include "care_daily/routine_placement.h"
#include "care_daily/routine_schedule.h"
#include "care_daily/routine_state.h"
#include "care_daily/routine_type.h"

namespace xiaozhi_care::daily {

/**
 * @brief Core domain entity for Daily Care.
 *
 * CareRoutine represents something that belongs to the user's everyday life:
 * a medication, hydration reminder, exercise, measurement, birthday reminder,
 * or custom routine.
 *
 * It does not know about NVS, JSON, MCP, HTTP, BLE, or ESP-IDF.
 */
struct CareRoutine {
    RoutineId id;
    RoutineType type{RoutineType::Custom};
    RoutineState state{RoutineState::Active};

    std::string title;
    std::string description;

    RoutineSchedule schedule;
    RoutinePlacement placement;

    /**
     * Alert policy for this daily care item.
     * This controls whether the scheduler should request visual, sound,
     * or visual+sound notification when the item becomes due.
     */
    CareAlertConfig alert;

    /**
     * Optional payload reserved for type-specific data.
     *
     * In later packs this will hold a compact JSON document or a typed variant.
     * It stays opaque here so the engine can process routines generically.
     */
    std::string data;

    [[nodiscard]] bool IsActive() const {
        return state == RoutineState::Active;
    }

    [[nodiscard]] bool IsValid() const {
        return !id.Empty() && !title.empty() && schedule.IsValid();
    }
};

}  // namespace xiaozhi_care::daily
