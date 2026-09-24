#pragma once

#include <cstdint>
#include <string>

namespace xiaozhi_care::daily {

/**
 * @brief How a routine is physically organized in the user's life.
 *
 * For medication routines this can describe a pillbox, blister, original box,
 * or another familiar place. It does not represent a geographic location.
 */
enum class PlacementType : uint8_t {
    None,
    Pillbox,
    Blister,
    OriginalBox,
    Table,
    Shelf,
    Custom,
};

/**
 * @brief Physical placement for a routine.
 *
 * CarePillbox fields are intentionally inert in this pack. They only reserve
 * domain concepts that will be used by a future BLE accessory.
 */
struct RoutinePlacement {
    PlacementType type{PlacementType::None};
    std::string description;
    std::string compartment;
    bool monitored{false};
    uint8_t ble_slot{0};
    uint8_t led_number{0};

    [[nodiscard]] bool HasCompartment() const {
        return !compartment.empty();
    }

    [[nodiscard]] bool IsHardwareLinked() const {
        return monitored && ble_slot > 0;
    }
};

}  // namespace xiaozhi_care::daily
