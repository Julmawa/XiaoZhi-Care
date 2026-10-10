#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace xiaozhi_care {

enum class PhysicalButtonAction {
    None,
    NormalClick,
    ConfirmAlert
};

struct PhysicalButtonResult {
    PhysicalButtonAction action = PhysicalButtonAction::None;
    std::string target_id;
};

class PhysicalAckButton {
public:
    static constexpr int64_t kHoldTimeUs = 3000000;

    void OnPressDown(int64_t now_us, const std::string& target_id) {
        if (pressed_) {
            return;
        }

        pressed_ = true;
        started_us_ = now_us;
        target_at_press_ = target_id;
    }

    PhysicalButtonResult OnPressUp(
        int64_t now_us,
        const std::string& current_target_id
    ) {
        if (!pressed_) {
            return {};
        }

        pressed_ = false;
        std::string original_target = std::move(target_at_press_);
        target_at_press_.clear();

        if (now_us < started_us_) {
            return {};
        }

        const int64_t elapsed = now_us - started_us_;

        if (elapsed >= kHoldTimeUs) {
            if (!original_target.empty() &&
                original_target == current_target_id) {
                return {
                    PhysicalButtonAction::ConfirmAlert,
                    std::move(original_target)
                };
            }
            return {};
        }

        return {PhysicalButtonAction::NormalClick, ""};
    }

private:
    bool pressed_ = false;
    int64_t started_us_ = 0;
    std::string target_at_press_;
};

}
