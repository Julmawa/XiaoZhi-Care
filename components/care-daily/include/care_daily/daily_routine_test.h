#pragma once

namespace xiaozhi_care::daily {

/**
 * @brief Temporary hardware validation test for DP-002.
 *
 * Call this once from main.cc after CareManager::Init() to validate that a
 * routine can be saved and recovered after reboot. Remove the call after
 * validation. The function is inert unless explicitly called.
 */
void RunDailyRoutinePersistenceTest();

}  // namespace xiaozhi_care::daily
