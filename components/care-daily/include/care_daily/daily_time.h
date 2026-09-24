#pragma once

#include <cstdint>

namespace xiaozhi_care::daily {

/**
 * @brief Time within a single day.
 *
 * DailyTime does not represent a date, timezone, timestamp, or duration.
 * It only represents a clock time from 00:00 to 23:59.
 */
struct DailyTime {
    uint8_t hour{0};
    uint8_t minute{0};

    constexpr DailyTime() = default;

    constexpr DailyTime(uint8_t h, uint8_t m)
        : hour(h), minute(m) {}

    [[nodiscard]] constexpr bool IsValid() const {
        return hour < 24 && minute < 60;
    }

    [[nodiscard]] constexpr uint16_t ToMinutes() const {
        return static_cast<uint16_t>(hour) * 60u + minute;
    }

    constexpr bool operator==(const DailyTime& rhs) const {
        return hour == rhs.hour && minute == rhs.minute;
    }

    constexpr bool operator!=(const DailyTime& rhs) const {
        return !(*this == rhs);
    }

    constexpr bool operator<(const DailyTime& rhs) const {
        return ToMinutes() < rhs.ToMinutes();
    }

    constexpr bool operator<=(const DailyTime& rhs) const {
        return ToMinutes() <= rhs.ToMinutes();
    }

    constexpr bool operator>(const DailyTime& rhs) const {
        return ToMinutes() > rhs.ToMinutes();
    }

    constexpr bool operator>=(const DailyTime& rhs) const {
        return ToMinutes() >= rhs.ToMinutes();
    }
};

}  // namespace xiaozhi_care::daily
