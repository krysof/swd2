#include "swd2/inventory_system.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace swd2 {

std::uint16_t InventorySystem::item(std::size_t slot) const {
    if (slot >= inventory_slot_count) throw std::out_of_range("inventory slot is out of range");
    return state_.u16(inventory_offset(slot));
}

std::optional<std::size_t> InventorySystem::first_empty_slot() const {
    for (std::size_t slot = 0; slot < inventory_slot_count; ++slot) {
        if (item(slot) == 0) return slot;
    }
    return std::nullopt;
}

void InventorySystem::compact() {
    std::size_t output = 0;
    for (std::size_t input = 0; input < inventory_slot_count; ++input) {
        const auto value = item(input);
        if (value == 0) continue;
        if (output != input) state_.set_u16(inventory_offset(output), value);
        ++output;
    }
    while (output < inventory_slot_count) {
        state_.set_u16(inventory_offset(output++), 0);
    }
}

bool InventorySystem::insert(std::uint16_t item_id) {
    if (item_id == 0 || item_id >= items_.size()) return false;
    std::optional<std::size_t> quantity_offset;
    if (item_id >= 0x44 && item_id <= 0x48) {
        quantity_offset = 0x3e6 + static_cast<std::size_t>(item_id - 0x44) * 2;
        const auto quantity = state_.u16(*quantity_offset);
        if (quantity >= 20) return false;
        if (quantity != 0) {
            state_.set_u16(*quantity_offset, static_cast<std::uint16_t>(quantity + 1));
            return true;
        }
    }
    const auto slot = first_empty_slot();
    if (!slot) return false;
    state_.set_u16(inventory_offset(*slot), item_id);
    if (quantity_offset) state_.set_u16(*quantity_offset, 1);
    return true;
}

bool InventorySystem::purchase(std::uint16_t item_id) {
    if (item_id == 0 || item_id >= items_.size()) return false;
    const auto price = items_.at(item_id).price;
    const auto money = state_.u16(0x104);
    if (price > money || !insert(item_id)) return false;
    state_.set_u16(0x104, static_cast<std::uint16_t>(money - price));
    return true;
}

std::optional<std::uint16_t> InventorySystem::sale_value(std::size_t slot) const {
    const auto item_id = item(slot);
    if (item_id == 0 || item_id >= items_.size()) return std::nullopt;
    const auto& definition = items_.at(item_id);
    if (!definition.sellable()) return std::nullopt;
    return static_cast<std::uint16_t>(definition.price - definition.price / 4U);
}

bool InventorySystem::sell(std::size_t slot) {
    const auto value = sale_value(slot);
    if (!value) return false;
    const auto sum = static_cast<unsigned>(state_.u16(0x104)) + *value;
    state_.set_u16(0x104, static_cast<std::uint16_t>(
                                  std::min<unsigned>(sum, 0xffffU)));
    state_.set_u16(inventory_offset(slot), 0);
    // This deliberately mirrors RPG.EXE:56b8/568d/3ced, including its choice
    // not to change the 44h..48h auxiliary counters on a shop sale.
    compact();
    return true;
}

bool InventorySystem::character_restricted(const ItemDefinition& definition,
                                           std::size_t actor) const {
    if (actor >= 4) return true;
    const auto identity = state_.u16(0x72 + actor * 6);
    if (identity > 36 || identity % 12 != 0) return false;
    const auto bit = static_cast<std::uint8_t>(8U >> (identity / 12U));
    return (definition.character_restrictions & bit) != 0;
}

void InventorySystem::exchange_at_offset(std::size_t inventory_slot, std::size_t actor,
                                         std::size_t equipment_offset,
                                         const ItemDefinition& incoming) {
    const auto base = actor_offset(actor);
    const auto old_id = state_.u16(base + equipment_offset);
    if (old_id >= items_.size()) throw std::runtime_error("equipped ITEM id is out of range");
    const auto& outgoing = items_.at(old_id);
    state_.set_u16(base + equipment_offset, incoming.id);
    state_.set_u16(inventory_offset(inventory_slot), old_id);

    constexpr std::array<std::size_t, 4> actor_stats{0x0c, 0x0e, 0x5d, 0x67};
    for (std::size_t i = 0; i < actor_stats.size(); ++i) {
        auto value = static_cast<std::uint16_t>(state_.u16(base + actor_stats[i]) -
                                                outgoing.stat_words[i]);
        value = static_cast<std::uint16_t>(value + incoming.stat_words[i]);
        state_.set_u16(base + actor_stats[i], value);
    }
    // RPG.EXE copies the fourth recalculated maximum into the adjacent current
    // value after every exchange.
    state_.set_u16(base + 0x65, state_.u16(base + 0x67));
}

void InventorySystem::recalculate_equipment_traits(std::size_t actor) {
    if (actor >= 4) throw std::out_of_range("actor is out of range");
    const auto base = actor_offset(actor);
    constexpr std::array<std::size_t, 5> actor_traits{0x26, 0x27, 0x28, 0x2a, 0x2b};
    std::array<std::uint8_t, 5> maxima{};
    for (std::size_t slot = 0; slot < equipment_slot_count; ++slot) {
        const auto id = state_.u16(base + 0x10 + slot * 2);
        if (id >= items_.size()) throw std::runtime_error("equipped ITEM id is out of range");
        const auto& definition = items_.at(id);
        for (std::size_t i = 0; i < maxima.size(); ++i) {
            maxima[i] = std::max(maxima[i], definition.trait_levels[i]);
        }
    }
    for (std::size_t i = 0; i < maxima.size(); ++i) {
        state_.set_u8(base + actor_traits[i], maxima[i]);
    }
}

EquipmentExchange InventorySystem::exchange_equipment(std::size_t inventory_slot,
                                                       std::size_t actor,
                                                       std::size_t equipment_slot) {
    EquipmentExchange result;
    result.actor = actor;
    result.equipment_slot = equipment_slot;
    if (actor >= 4 || actor >= std::min<std::size_t>(state_.u16(0x10), 4)) {
        result.status = EquipmentExchangeStatus::invalid_actor;
        return result;
    }
    if (inventory_slot >= inventory_slot_count || equipment_slot >= equipment_slot_count) {
        result.status = EquipmentExchangeStatus::invalid_slot;
        return result;
    }

    const auto selected_id = item(inventory_slot);
    if (selected_id >= items_.size()) {
        result.status = EquipmentExchangeStatus::invalid_item;
        return result;
    }
    const auto& selected = items_.at(selected_id);
    if (selected_id != 0 && character_restricted(selected, actor)) {
        result.status = EquipmentExchangeStatus::character_restricted;
        return result;
    }

    const auto base = actor_offset(actor);
    std::size_t target_offset = 0x10 + equipment_slot * 2;
    if (selected_id == 0) {
        // A two-handed item is stored in both hand words but contributes its
        // bonuses once. Selecting either hand with an empty inventory cell
        // clears the first copy and exchanges the second.
        if (state_.u8(base + 0x2c) == 1 &&
            (equipment_slot == 2 || equipment_slot == 3)) {
            state_.set_u16(base + 0x14, 0);
            target_offset = 0x16;
            state_.set_u8(base + 0x2c, 0);
        }
    } else {
        const auto category = selected.equipment_category();
        bool matches = false;
        switch (category) {
            case 1: target_offset = 0x1a; matches = equipment_slot == 5; break;
            case 2: target_offset = 0x18; matches = equipment_slot == 4; break;
            case 3: target_offset = 0x12; matches = equipment_slot == 1; break;
            case 4:
                target_offset = equipment_slot == 9 ? 0x22 : 0x24;
                matches = equipment_slot == 9 || equipment_slot == 10;
                break;
            case 5: target_offset = 0x1c; matches = equipment_slot == 6; break;
            case 6:
                target_offset = equipment_slot == 7 ? 0x1e : 0x20;
                matches = equipment_slot == 7 || equipment_slot == 8;
                break;
            case 7: target_offset = 0x10; matches = equipment_slot == 0; break;
            case 8: {
                matches = equipment_slot == 2 || equipment_slot == 3;
                target_offset = equipment_slot == 2 ? 0x14 : 0x16;
                const auto other_offset = equipment_slot == 2 ? 0x16 : 0x14;
                if (matches && state_.u8(base + 0x2c) == 1) {
                    state_.set_u8(base + 0x2c, 0);
                    state_.set_u16(base + other_offset, 0);
                }
                break;
            }
            case 9:
                matches = equipment_slot == 2 || equipment_slot == 3;
                if (matches && state_.u8(base + 0x2c) != 1 &&
                    (state_.u16(base + 0x14) != 0 || state_.u16(base + 0x16) != 0)) {
                    result.status = EquipmentExchangeStatus::two_handed_conflict;
                    return result;
                }
                if (matches) {
                    state_.set_u16(base + 0x14, selected_id);
                    target_offset = 0x16;
                    state_.set_u8(base + 0x2c, 1);
                }
                break;
            default: break;
        }
        if (!matches) {
            result.status = EquipmentExchangeStatus::category_mismatch;
            return result;
        }
    }

    result.equipped_item = selected_id;
    result.returned_item = state_.u16(base + target_offset);
    exchange_at_offset(inventory_slot, actor, target_offset, selected);
    recalculate_equipment_traits(actor);
    result.status = EquipmentExchangeStatus::exchanged;
    return result;
}

}  // namespace swd2
