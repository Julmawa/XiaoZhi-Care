#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "care_daily/day_activity_memory.h"

namespace xiaozhi_care::daily {

/**
 * @brief Storage boundary for temporary daily accompaniment memory.
 *
 * DayActivityMemory is deliberately independent from CareRoutine and
 * RoutineExecution:
 *
 * - CareRoutine describes what is planned.
 * - RoutineExecution records what was explicitly confirmed.
 * - DayActivityMemory remembers what CARE and the person are currently
 *   thinking, preparing or doing together.
 *
 * Implementations may use /care_data or another future backend without
 * changing the domain model or the companion engine.
 */
class DayActivityMemoryRepository {
public:
    virtual ~DayActivityMemoryRepository() = default;

    /**
     * @brief Initializes the repository backend.
     */
    virtual bool Init() = 0;

    /**
     * @brief Generates an opaque identifier for a new day-memory record.
     */
    virtual std::string GenerateId() = 0;

    /**
     * @brief Saves a record. An existing record with the same id is replaced.
     */
    virtual bool Save(const DayActivityMemory& memory) = 0;

    /**
     * @brief Finds one day-memory record by its opaque identifier.
     */
    virtual std::optional<DayActivityMemory> FindById(
        const std::string& memory_id) = 0;

    /**
     * @brief Lists all currently persisted day-memory records.
     */
    virtual std::vector<DayActivityMemory> List() = 0;

    /**
     * @brief Deletes one day-memory record.
     */
    virtual bool Delete(const std::string& memory_id) = 0;

    /**
     * @brief Removes records older than the configured day-memory window.
     *
     * retention_days=1 means: keep today and yesterday.
     * Returns the number of removed records.
     */
    virtual size_t ClearExpired(
        const std::string& current_iso_date,
        uint16_t retention_days = 1) = 0;
};

}  // namespace xiaozhi_care::daily