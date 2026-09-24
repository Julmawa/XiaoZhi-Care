#pragma once

#include <cstdint>

namespace xiaozhi_care::daily {

/**
 * @brief Lifecycle state of a routine definition.
 *
 * A routine is not marked completed here. Completion belongs to a future
 * RoutineExecution record, because one routine can execute many times.
 */
enum class RoutineState : uint8_t {
    Active,
    Paused,
    Archived,
};

}  // namespace xiaozhi_care::daily
