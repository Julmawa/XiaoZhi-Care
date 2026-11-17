#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

// Submission and completion are distinct. result: 0 pending,
// 1 all PCM accepted by I2S, -1 failed/cancelled. Never a user ACK.
extern "C" bool xiaozhi_care_reminder_audio_notify(
    const char* target_id, const char* message, const uint8_t* audio_data,
    size_t audio_size, std::shared_ptr<std::atomic<int>> result);
