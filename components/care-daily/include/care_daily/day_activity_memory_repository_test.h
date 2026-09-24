#pragma once

namespace xiaozhi_care::daily {

/**
 * @brief Manual DP035 persistence validation.
 *
 * Call temporarily after /care_data has been mounted, then remove the call.
 * The test cleans up its own DP035_TEST_* records.
 */
void RunDayActivityMemoryRepositoryTest();

}  // namespace xiaozhi_care::daily