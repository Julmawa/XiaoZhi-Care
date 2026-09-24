#pragma once

#include <cstdint>

namespace xiaozhi_care::daily {

/**
 * @brief How XiaoZhi Care should alert for a daily care item.
 */
enum class CareAlertMode : uint8_t {
    Disabled,
    VisualOnly,
    SoundOnly,
    VisualAndSound,
};

/**
 * @brief Visual presentation requested for a care alert.
 *
 * This is only a policy. The concrete LED/display implementation is handled
 * by the XiaoZhi UI layer in a later Developer Pack.
 */
enum class CareVisualPattern : uint8_t {
    None,
    Solid,
    Breathing,
    SoftBlink,
    ProgressBar,
    PillboxSlot,
};

/**
 * @brief Preferred color for the visual care alert.
 */
enum class CareAlertColor : uint8_t {
    Auto,
    Green,
    Blue,
    Yellow,
    SoftRed,
    Violet,
    White,
};

/**
 * @brief Sound presentation requested for a care alert.
 */
enum class CareSoundPattern : uint8_t {
    None,
    SoftBeep,
    Chime,
    FriendlyReminder,
    Progressive,
};

/**
 * @brief Alert policy stored with each CareRoutine.
 */
struct CareAlertConfig {
    bool enabled{true};
    CareAlertMode mode{CareAlertMode::VisualOnly};
    CareVisualPattern visual_pattern{CareVisualPattern::Breathing};
    CareAlertColor visual_color{CareAlertColor::Auto};
    CareSoundPattern sound_pattern{CareSoundPattern::SoftBeep};
    uint16_t repeat_minutes{10};
    uint8_t max_repeats{3};
    bool voice_enabled{false};

    [[nodiscard]] bool IsEnabled() const {
        return enabled && mode != CareAlertMode::Disabled;
    }

    [[nodiscard]] bool UsesVisual() const {
        return mode == CareAlertMode::VisualOnly || mode == CareAlertMode::VisualAndSound;
    }

    [[nodiscard]] bool UsesSound() const {
        return mode == CareAlertMode::SoundOnly || mode == CareAlertMode::VisualAndSound;
    }

    [[nodiscard]] bool UsesVoice() const {
        return IsEnabled() && voice_enabled;
    }

    [[nodiscard]] bool IsValid() const {
        if (!enabled || mode == CareAlertMode::Disabled) {
            return true;
        }
        const bool mode_ok = mode == CareAlertMode::VisualOnly ||
                             mode == CareAlertMode::SoundOnly ||
                             mode == CareAlertMode::VisualAndSound;
        const bool visual_ok = visual_pattern == CareVisualPattern::None ||
                               visual_pattern == CareVisualPattern::Solid ||
                               visual_pattern == CareVisualPattern::Breathing ||
                               visual_pattern == CareVisualPattern::SoftBlink ||
                               visual_pattern == CareVisualPattern::ProgressBar ||
                               visual_pattern == CareVisualPattern::PillboxSlot;
        const bool color_ok = visual_color == CareAlertColor::Auto ||
                              visual_color == CareAlertColor::Green ||
                              visual_color == CareAlertColor::Blue ||
                              visual_color == CareAlertColor::Yellow ||
                              visual_color == CareAlertColor::SoftRed ||
                              visual_color == CareAlertColor::Violet ||
                              visual_color == CareAlertColor::White;
        const bool sound_ok = sound_pattern == CareSoundPattern::None ||
                              sound_pattern == CareSoundPattern::SoftBeep ||
                              sound_pattern == CareSoundPattern::Chime ||
                              sound_pattern == CareSoundPattern::FriendlyReminder ||
                              sound_pattern == CareSoundPattern::Progressive;
        return mode_ok && visual_ok && color_ok && sound_ok &&
               repeat_minutes >= 1 && repeat_minutes <= 240 &&
               max_repeats >= 1 && max_repeats <= 10;
    }
};

}  // namespace xiaozhi_care::daily
