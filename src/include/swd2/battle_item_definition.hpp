#pragma once

#include <cstdint>
#include <span>

namespace swd2 {

// The fields read by FIG 1000:1138 when a queued command uses an inventory
// object. Effect is deliberately unaligned at +7 in the original record.
struct BattleItemDefinition {
    std::uint16_t id{};
    std::uint16_t type{};             // +0
    std::uint8_t use_flags{};         // +5; bit 04 consumes the inventory slot
    std::uint8_t target_flags{};      // +6; bit 20 selects a monster target
    std::uint16_t effect_code{};      // unaligned +7
    // Selector 6b reads these two bytes after 1138 stores item_id-8c; adding
    // 8c again in 57f2 resolves the original ITEM record for every item type,
    // not only type-10 wrappers.
    std::uint8_t first_composite_effect{};  // +9
    std::uint8_t second_composite_effect{}; // +a

    [[nodiscard]] bool consumed_on_use() const noexcept {
        return (use_flags & 0x04U) != 0;
    }
    [[nodiscard]] bool targets_monster() const noexcept {
        return (target_flags & 0x20U) != 0;
    }
    [[nodiscard]] bool targets_party() const noexcept {
        return (target_flags & 0x10U) != 0;
    }

    static BattleItemDefinition parse(std::uint16_t id,
                                      std::span<const std::uint8_t> item_record);
};

}  // namespace swd2
