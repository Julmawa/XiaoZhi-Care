#pragma once

#include <optional>
#include <vector>

#include "care_daily/routine_execution.h"

namespace xiaozhi_care::daily {

/**
 * @brief Repository interface for routine execution history.
 */
class RoutineExecutionRepository {
public:
    virtual ~RoutineExecutionRepository() = default;

    virtual bool Init() = 0;
    virtual RoutineExecutionId GenerateId() = 0;
    virtual bool Save(const RoutineExecution& execution) = 0;
    virtual std::optional<RoutineExecution> FindById(const RoutineExecutionId& id) = 0;
    virtual std::vector<RoutineExecution> List() = 0;
    virtual std::vector<RoutineExecution> ListByRoutine(const RoutineId& routine_id) = 0;
    virtual bool Delete(const RoutineExecutionId& id) = 0;
};

}  // namespace xiaozhi_care::daily
