#pragma once

#include <string>

namespace xiaozhi_care {

inline bool CanConfirmPhysicalAck(
    const std::string& captured_target,
    const std::string& current_target,
    bool still_pending
) {
    return !captured_target.empty() &&
           captured_target == current_target &&
           still_pending;
}

inline bool BelongsToConfirmedTarget(
    const std::string& state_target,
    const std::string& confirmed_target
) {
    return !confirmed_target.empty() &&
           state_target == confirmed_target;
}

}
