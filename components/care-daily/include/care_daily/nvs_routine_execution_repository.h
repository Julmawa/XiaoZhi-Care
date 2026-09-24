#pragma once

#include <string>

#include "care_daily/routine_execution_repository.h"

namespace xiaozhi_care::daily {

/**
 * @brief NVS implementation of RoutineExecutionRepository.
 *
 * Namespace: xiaozhi_care
 * Index key: de_idx
 * Sequence key: de_seq
 * Event key: de_<execution_id>
 */
class NvsRoutineExecutionRepository final : public RoutineExecutionRepository {
public:
    NvsRoutineExecutionRepository() = default;
    explicit NvsRoutineExecutionRepository(std::string nvs_namespace);

    bool Init() override;
    RoutineExecutionId GenerateId() override;
    bool Save(const RoutineExecution& execution) override;
    std::optional<RoutineExecution> FindById(const RoutineExecutionId& id) override;
    std::vector<RoutineExecution> List() override;
    std::vector<RoutineExecution> ListByRoutine(const RoutineId& routine_id) override;
    bool Delete(const RoutineExecutionId& id) override;

private:
    std::string nvs_namespace_{"xiaozhi_care"};
};

}  // namespace xiaozhi_care::daily
