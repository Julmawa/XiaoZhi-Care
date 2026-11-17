#pragma once

#include <atomic>
#include <cstdint>

enum class LocalPlaybackResult { Pending, Complete, Failed };

// One producer seals the packet count; I2S updates written from its consumer
// task. Queue resets invalidate the generation even if the driver was busy.
class LocalPlaybackReceipt {
public:
    LocalPlaybackReceipt(uint32_t generation, uint32_t care_epoch)
        : generation(generation), care_epoch(care_epoch) {}
    void AddPacket() { enqueued_.fetch_add(1); }
    void PacketWritten() { written_.fetch_add(1); }
    void Fail() { failed_.store(true); }
    void Seal() { sealed_.store(true); }
    LocalPlaybackResult Result(uint32_t current_generation, uint32_t current_care_epoch) const {
        if (current_generation != generation || current_care_epoch != care_epoch || failed_.load())
            return LocalPlaybackResult::Failed;
        if (!sealed_.load()) return LocalPlaybackResult::Pending;
        const auto count = enqueued_.load();
        if (!count) return LocalPlaybackResult::Failed;
        return written_.load() == count ? LocalPlaybackResult::Complete : LocalPlaybackResult::Pending;
    }

    const uint32_t generation;
    const uint32_t care_epoch;
private:
    std::atomic<unsigned> enqueued_{0};
    std::atomic<unsigned> written_{0};
    std::atomic<bool> sealed_{false};
    std::atomic<bool> failed_{false};
};
