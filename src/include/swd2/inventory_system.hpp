#pragma once

#include "swd2/item_database.hpp"
#include "swd2/shared_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace swd2 {

constexpr std::size_t inventory_slot_count = 50;
constexpr std::size_t equipment_slot_count = 11;

enum class EquipmentExchangeStatus {
    exchanged,
    invalid_actor,
    invalid_slot,
    invalid_item,
    character_restricted,
    category_mismatch,
    two_handed_conflict,
};

struct EquipmentExchange {
    EquipmentExchangeStatus status{EquipmentExchangeStatus::invalid_slot};
    std::uint16_t equipped_item{};
    std::uint16_t returned_item{};
    std::size_t actor{};
    std::size_t equipment_slot{};
};

class InventorySystem {
public:
    InventorySystem(SharedState& state, const ItemDatabase& items)
        : state_(state), items_(items) {}

    [[nodiscard]] std::uint16_t item(std::size_t slot) const;
    [[nodiscard]] std::optional<std::size_t> first_empty_slot() const;
    void compact();

    // Matches RPG.EXE's special 44h..48h shared counters and 20-copy cap.
    [[nodiscard]] bool insert(std::uint16_t item_id);
    [[nodiscard]] bool purchase(std::uint16_t item_id);
    // RPG.EXE sells for price-floor(price/4), saturates money at 0xffff,
    // clears the selected word, and then performs its stable compaction.
    [[nodiscard]] std::optional<std::uint16_t> sale_value(std::size_t slot) const;
    [[nodiscard]] bool sell(std::size_t slot);

    // Exchanges the selected inventory cell with one of the actor's eleven
    // equipment rows. Empty selected cells perform the DOS unequip operation.
    EquipmentExchange exchange_equipment(std::size_t inventory_slot,
                                         std::size_t actor,
                                         std::size_t equipment_slot);

    [[nodiscard]] bool character_restricted(const ItemDefinition& item,
                                            std::size_t actor) const;
    void recalculate_equipment_traits(std::size_t actor);

private:
    static constexpr std::size_t inventory_offset(std::size_t slot) {
        return 0x382 + slot * 2;
    }
    static constexpr std::size_t actor_offset(std::size_t actor) {
        return 0x106 + actor * 0x9f;
    }

    void exchange_at_offset(std::size_t inventory_slot, std::size_t actor,
                            std::size_t equipment_offset,
                            const ItemDefinition& incoming);

    SharedState& state_;
    const ItemDatabase& items_;
};

}  // namespace swd2
