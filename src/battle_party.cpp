#include "swd2/battle_party.hpp"

#include <algorithm>
#include <stdexcept>

namespace swd2 {

namespace {

constexpr std::size_t party_record(std::size_t index) {
    return 0x106 + index * 0x9f;
}

void validate_index(std::size_t index) {
    if (index >= 4) throw std::out_of_range("FIG party index is outside 0..3");
}

}  // namespace

BattlePartyMember BattlePartyMember::load(
    const SharedState& state, std::size_t party_index) {
    validate_index(party_index);
    const auto base = party_record(party_index);
    BattlePartyMember result;
    result.party_index = party_index;
    result.identity = state.u16(0x72 + party_index * 6U);
    result.status_bits = state.u16(base + 8);
    result.physical_attack = state.u16(base + 0x0c);
    result.physical_defense = state.u16(base + 0x0e);
    result.base_physical_attack = state.u16(base + 0x41);
    result.base_physical_defense = state.u16(base + 0x43);
    result.right_hand_item = state.u16(base + 0x14);
    result.left_hand_item = state.u16(base + 0x16);
    result.single_weapon_animation = state.u8(base + 0x2c) == 1;
    result.physical_immunity = state.u8(base + 0x26) == 1;
    result.ability_resistances = {
        state.u8(base + 0x27), state.u8(base + 0x28), state.u8(base + 0x2b),
    };
    result.status_immunity = state.u8(base + 0x2a) == 1;
    result.hit_points = state.u16(base + 0x2d);
    result.maximum_hit_points = state.u16(base + 0x2f);
    result.level = state.u16(base + 0x31);
    result.initiative_range = state.u16(base + 0x33);
    result.secondary_points = state.u16(base + 0x35);
    result.maximum_secondary_points = state.u16(base + 0x37);
    result.field_3d = state.u16(base + 0x3d);
    result.field_45 = state.u16(base + 0x45);
    result.field_4f = state.u16(base + 0x4f);
    result.ability_points = state.u16(base + 0x55);
    result.maximum_ability_points = state.u16(base + 0x57);
    result.speed = state.u16(base + 0x5d);
    result.evasion = state.u16(base + 0x65);
    result.base_speed = state.u16(base + 0x5f);
    result.base_evasion = state.u16(base + 0x67);
    for (std::size_t slot = 0; slot < result.abilities.size(); ++slot) {
        result.abilities[slot] = state.u8(base + 0x6d + slot);
    }
    return result;
}

void BattlePartyMember::store(SharedState& state) const {
    validate_index(party_index);
    const auto base = party_record(party_index);
    state.set_u16(base + 8, status_bits);
    state.set_u16(base + 0x0c, physical_attack);
    state.set_u16(base + 0x0e, physical_defense);
    state.set_u16(base + 0x2d, hit_points);
    state.set_u16(base + 0x2f, maximum_hit_points);
    state.set_u16(base + 0x31, level);
    state.set_u16(base + 0x33, initiative_range);
    state.set_u16(base + 0x35, secondary_points);
    state.set_u16(base + 0x37, maximum_secondary_points);
    state.set_u16(base + 0x3d, field_3d);
    state.set_u16(base + 0x45, field_45);
    state.set_u16(base + 0x4f, field_4f);
    state.set_u16(base + 0x55, ability_points);
    state.set_u16(base + 0x57, maximum_ability_points);
    state.set_u16(base + 0x5d, speed);
    state.set_u16(base + 0x65, evasion);
    for (std::size_t slot = 0; slot < abilities.size(); ++slot) {
        state.set_u8(base + 0x6d + slot, abilities[slot]);
    }
}

std::size_t fig_fighter_base_frame(std::uint16_t identity,
                                   std::size_t party_index,
                                   std::size_t frame_count) noexcept {
    const auto exact = static_cast<std::size_t>(identity >> 1U);
    if (exact < frame_count) return exact;
    const auto fallback = party_index * 6U;
    return fallback < frame_count ? fallback : frame_count;
}

FigFighterPlacement fig_fighter_placement(
    std::uint16_t identity, std::size_t party_index, std::size_t pose,
    std::size_t frame_count) noexcept {
    // FIG DS:2fac and DS:2fdc, indexed by (identity >> 1) + pose.
    static constexpr std::array<int, 24> horizontal_offsets = {
         0,  0, -2,  -2, -1, 0,
         0,  0, -3,  -3, -1, 0,
         0,  0, -2,  -4, -2, 0,
         0,  0, -2,  -2,  0, 0,
    };
    static constexpr std::array<int, 24> vertical_offsets = {
         0, -7,  0, -10, -4, 0,
         0,  0,  2,   0, -2, 0,
         0,  0, -3, -10, -5, 0,
         0, -9,  1,  -1, -7, 0,
    };

    auto base = fig_fighter_base_frame(identity, party_index, frame_count);
    if (base >= frame_count) return {frame_count, 0, 0};
    pose = std::min<std::size_t>(pose, 5U);
    auto frame = base + pose;
    if (frame >= frame_count) frame = base;
    const auto table_index = base + pose;
    const auto x_offset = table_index < horizontal_offsets.size()
                              ? horizontal_offsets[table_index]
                              : 0;
    const auto y_offset = table_index < vertical_offsets.size()
                              ? vertical_offsets[table_index]
                              : 0;
    return {
        frame,
        (12 + static_cast<int>(party_index) * 18 + x_offset) * 4,
        155 + y_offset,
    };
}

std::vector<FigWeaponAnimation> fig_weapon_animations(
    const BattlePartyMember& member) {
    if (member.single_weapon_animation) {
        return {{member.right_hand_item, false}};
    }
    if (member.right_hand_item == 0) {
        if (member.left_hand_item != 0) {
            return {{member.left_hand_item, true}};
        }
        return {{0, false}};
    }
    std::vector<FigWeaponAnimation> result = {
        {member.right_hand_item, false},
    };
    if (member.left_hand_item != 0) {
        result.push_back({member.left_hand_item, true});
    }
    return result;
}

FigWeaponPlacement fig_weapon_placement(int target_x, int target_y) noexcept {
    return {target_x - 16, target_y - 50};
}

}  // namespace swd2
