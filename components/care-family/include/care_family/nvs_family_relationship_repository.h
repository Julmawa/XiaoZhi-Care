#pragma once

#include <optional>
#include <string>
#include <vector>

#include "care_family/family_relationship.h"

namespace xiaozhi_care::family {

class NvsFamilyRelationshipRepository {
public:
    bool Init();
    bool IsReady() const { return ready_; }

    RelationshipId GenerateId();

    bool Save(const FamilyRelationship& relationship);
    std::optional<FamilyRelationship> FindById(const RelationshipId& id);
    std::vector<FamilyRelationship> List();
    bool Delete(const RelationshipId& id);

private:
    bool ready_ = false;
};

NvsFamilyRelationshipRepository& GetFamilyRelationshipRepository();

}  // namespace xiaozhi_care::family
