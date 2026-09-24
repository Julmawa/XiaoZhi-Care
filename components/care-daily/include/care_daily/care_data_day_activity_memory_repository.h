#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "care_daily/day_activity_memory_repository.h"

namespace xiaozhi_care::daily {

/**
 * @brief /care_data implementation of temporary day-memory persistence.
 *
 * All records are stored in one small bounded JSON document:
 *
 *   /care_data/day_activity_memory.json
 *
 * This repository intentionally has no NVS fallback and no permanent-history
 * semantics. Day memory is short-lived conversational context.
 */
class CareDataDayActivityMemoryRepository final
    : public DayActivityMemoryRepository {
public:
    CareDataDayActivityMemoryRepository() = default;

    bool Init() override;
    std::string GenerateId() override;
    bool Save(const DayActivityMemory& memory) override;
    std::optional<DayActivityMemory> FindById(
        const std::string& memory_id) override;
    std::vector<DayActivityMemory> List() override;
    bool Delete(const std::string& memory_id) override;

    size_t ClearExpired(
        const std::string& current_iso_date,
        uint16_t retention_days = 1) override;

private:
    mutable std::mutex mutex_;
    bool initialized_{false};
};

}  // namespace xiaozhi_care::daily