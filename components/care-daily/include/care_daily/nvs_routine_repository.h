#pragma once

#include <string>

#include "care_daily/routine_repository.h"

namespace xiaozhi_care::daily {

/**
 * @brief NVS implementation of RoutineRepository.
 *
 * Keys are intentionally compact because NVS key names are short.
 * Namespace: xiaozhi_care
 * Index key: dr_idx
 * Sequence key: dr_seq
 * Routine key: dr_<routine_id>
 */
class NvsRoutineRepository final : public RoutineRepository {
public:
    NvsRoutineRepository() = default;
    explicit NvsRoutineRepository(std::string nvs_namespace);

    bool Init() override;
    RoutineId GenerateId() override;
    bool Save(const CareRoutine& routine) override;
    std::optional<CareRoutine> FindById(const RoutineId& id) override;
    std::vector<CareRoutine> List() override;
    bool Delete(const RoutineId& id) override;

private:
    std::string nvs_namespace_{"xiaozhi_care"};
};

}  // namespace xiaozhi_care::daily
