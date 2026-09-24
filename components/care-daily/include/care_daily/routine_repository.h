#pragma once

#include <optional>
#include <vector>

#include "care_daily/care_routine.h"

namespace xiaozhi_care::daily {

/**
 * @brief Storage boundary for Daily Routine persistence.
 *
 * The domain and future engine depend on this interface, not on NVS directly.
 * Implementations may use NVS, JSON files, SQLite, cloud synchronization, or
 * any future backend without changing CareRoutine.
 */
class RoutineRepository {
public:
    virtual ~RoutineRepository() = default;

    /**
     * @brief Initializes the repository backend.
     */
    virtual bool Init() = 0;

    /**
     * @brief Generates a new routine identifier.
     */
    virtual RoutineId GenerateId() = 0;

    /**
     * @brief Saves a routine. Existing routines with the same id are replaced.
     */
    virtual bool Save(const CareRoutine& routine) = 0;

    /**
     * @brief Finds a routine by id.
     */
    virtual std::optional<CareRoutine> FindById(const RoutineId& id) = 0;

    /**
     * @brief Lists all persisted routines.
     */
    virtual std::vector<CareRoutine> List() = 0;

    /**
     * @brief Deletes a routine by id.
     */
    virtual bool Delete(const RoutineId& id) = 0;
};

}  // namespace xiaozhi_care::daily
