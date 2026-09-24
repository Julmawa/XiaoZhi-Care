#pragma once

namespace xiaozhi_care::daily {

/**
 * @brief Temporary hardware validation test for DP-003.
 *
 * Call this once from main.cc after CareManager::Init() to validate the Daily
 * Routine Engine. Remove the call after validation.
 */
void RunDailyRoutineEngineTest();

}  // namespace xiaozhi_care::daily
