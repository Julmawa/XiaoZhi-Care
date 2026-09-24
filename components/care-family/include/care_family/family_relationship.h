#pragma once

#include <string>
#include <utility>

namespace xiaozhi_care::family {

struct RelationshipId {
    std::string value;

    RelationshipId() = default;
    explicit RelationshipId(std::string id) : value(std::move(id)) {}

    bool Empty() const { return value.empty(); }
    bool operator==(const RelationshipId& rhs) const { return value == rhs.value; }
    bool operator!=(const RelationshipId& rhs) const { return !(*this == rhs); }
};

struct FamilyRelationship {
    RelationshipId id;
    std::string from_person_id;
    std::string to_person_id;
    std::string type;
    std::string label;
    std::string notes;
    bool enabled = true;
};

}  // namespace xiaozhi_care::family
