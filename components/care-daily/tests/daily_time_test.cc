#include "care_daily/daily_time.h"

namespace xiaozhi_care::daily::tests {

static_assert(DailyTime(7, 30).IsValid(), "07:30 must be valid");
static_assert(!DailyTime(24, 0).IsValid(), "24:00 must be invalid");
static_assert(DailyTime(7, 30) < DailyTime(8, 0), "07:30 must be before 08:00");
static_assert(DailyTime(23, 59).ToMinutes() == 1439, "23:59 must be minute 1439");

}  // namespace xiaozhi_care::daily::tests
