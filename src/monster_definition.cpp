#include "swd2/monster_definition.hpp"

#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t word(std::span<const std::uint8_t> record, std::size_t offset) {
    if (offset + 2 > record.size()) {
        throw std::runtime_error("ITEM.EXE monster definition is truncated");
    }
    return static_cast<std::uint16_t>(record[offset]) |
           (static_cast<std::uint16_t>(record[offset + 1]) << 8U);
}

}  // namespace

MonsterDefinition MonsterDefinition::parse(
    std::uint16_t id, std::span<const std::uint8_t> item_record) {
    // The last field ends at +50. Checking the whole known layout once makes
    // every byte access below safe and gives corrupt archives one clear error.
    if (item_record.size() < 0x50) {
        throw std::runtime_error("ITEM.EXE monster definition is shorter than 0x50 bytes");
    }
    MonsterDefinition result;
    result.id = id;
    result.sprite_number = word(item_record, 0x02);
    result.resistance_flags = {
        item_record[0x16], item_record[0x17], item_record[0x18],
        item_record[0x1a], item_record[0x1b],
    };
    result.vertical_position = word(item_record, 0x24);
    result.status_strength = word(item_record, 0x26);
    result.ai_type = word(item_record, 0x28);
    result.hit_points = word(item_record, 0x2c);
    result.primary_ability_chance = word(item_record, 0x2e);
    result.generic_ability = word(item_record, 0x32);
    result.level = word(item_record, 0x34);
    result.initiative_range = word(item_record, 0x36);
    result.physical_attack = word(item_record, 0x38);
    result.healing_ability = word(item_record, 0x3a);
    result.secondary_ability_chance = word(item_record, 0x3e);
    result.speed = word(item_record, 0x40);
    result.special_ability_a = word(item_record, 0x42);
    result.ability_points = word(item_record, 0x44);
    result.special_ability_b = word(item_record, 0x46);
    result.experience_reward = word(item_record, 0x48);
    result.money_reward = word(item_record, 0x4a);
    result.evasion = word(item_record, 0x4c);
    result.physical_defense = word(item_record, 0x4e);
    return result;
}

}  // namespace swd2
