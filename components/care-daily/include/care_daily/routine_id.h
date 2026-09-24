#pragma once

#include <string>
#include <utility>

namespace xiaozhi_care::daily {

/**
 * @brief Strong identifier for a daily routine.
 *
 * This type intentionally wraps std::string so routine identifiers are not
 * accidentally mixed with person, reminder, preference, or medication IDs.
 */
struct RoutineId {
    std::string value;

    RoutineId() = default;

    explicit RoutineId(std::string id)
        : value(std::move(id)) {}

    [[nodiscard]] bool Empty() const {
        return value.empty();
    }

    [[nodiscard]] const std::string& Str() const {
        return value;
    }

    bool operator==(const RoutineId& rhs) const {
        return value == rhs.value;
    }

    bool operator!=(const RoutineId& rhs) const {
        return !(*this == rhs);
    }
};

}  // namespace xiaozhi_care::daily
