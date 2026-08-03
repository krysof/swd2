#pragma once

#include "swd2/battle_effects.hpp"
#include "swd2/battle_rules.hpp"
#include "swd2/shared_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace swd2 {

// Typed portable view of one 0x9f-byte party record used by FIG.EXE. The view
// intentionally names only fields whose battle reads/writes have been proven.
struct BattlePartyMember {
    std::size_t party_index{};
    std::uint16_t identity{};
    std::uint16_t status_bits{};
    std::uint16_t physical_attack{};
    std::uint16_t physical_defense{};
    std::uint16_t base_physical_attack{};   // battle snapshot +41
    std::uint16_t base_physical_defense{};  // battle snapshot +43
    std::uint16_t right_hand_item{};        // +14; SW/SW%03u.RSK
    std::uint16_t left_hand_item{};         // +16; SW/SW%03u.RSK
    bool single_weapon_animation{};         // byte +2c == 1
    bool physical_immunity{};  // byte +26
    std::array<std::uint8_t, 3> ability_resistances{};  // +27,+28,+2b
    bool status_immunity{};    // byte +2a
    std::uint16_t hit_points{};
    std::uint16_t maximum_hit_points{};
    std::uint16_t level{};
    std::uint16_t initiative_range{};
    std::uint16_t secondary_points{};  // +35, classes 2/3
    std::uint16_t maximum_secondary_points{};  // +37
    std::uint16_t field_3d{};
    std::uint16_t field_45{};
    std::uint16_t field_4f{};
    std::uint16_t ability_points{};    // +55, classes 1/4
    std::uint16_t maximum_ability_points{};
    std::uint16_t speed{};
    std::uint16_t evasion{};
    std::uint16_t base_speed{};              // battle snapshot +5f
    std::uint16_t base_evasion{};            // battle snapshot +67
    std::array<std::uint8_t, 50> abilities{};

    static BattlePartyMember load(const SharedState& state, std::size_t party_index);
    void store(SharedState& state) const;

    [[nodiscard]] bool living() const noexcept {
        return hit_points != 0 && (status_bits & 0x2000U) == 0;
    }
    [[nodiscard]] InitiativeStats initiative() const noexcept {
        return {initiative_range, speed};
    }
    [[nodiscard]] PlayerAttackStats attack_stats() const noexcept {
        return {level, physical_attack};
    }
    [[nodiscard]] PlayerTargetState physical_target() const noexcept {
        return {hit_points, physical_defense, evasion, status_bits,
                physical_immunity, status_immunity, true};
    }
    [[nodiscard]] PlayerBattleState ability_target() const noexcept {
        PlayerBattleState result{
            hit_points, maximum_hit_points, status_bits, ability_resistances,
        };
        result.physical_attack = physical_attack;
        result.physical_defense = physical_defense;
        result.speed = speed;
        result.evasion = evasion;
        result.base_physical_attack = base_physical_attack;
        result.base_physical_defense = base_physical_defense;
        result.base_speed = base_speed;
        result.base_evasion = base_evasion;
        return result;
    }
    [[nodiscard]] PlayerSupportState support_target() const noexcept {
        return {hit_points, maximum_hit_points,
                secondary_points, maximum_secondary_points,
                ability_points, maximum_ability_points,
                status_bits, physical_attack, field_3d, field_45, field_4f, speed};
    }
    void apply_support_target(const PlayerSupportState& state) noexcept {
        hit_points = state.hit_points;
        maximum_hit_points = state.maximum_hit_points;
        secondary_points = state.secondary_points;
        maximum_secondary_points = state.maximum_secondary_points;
        ability_points = state.ability_points;
        maximum_ability_points = state.maximum_ability_points;
        status_bits = state.status_bits;
        physical_attack = state.physical_attack;
        field_3d = state.field_3d;
        field_45 = state.field_45;
        field_4f = state.field_4f;
        speed = state.speed;
    }
};

// FIG 1000:0909 stores (identity >> 1) in its FMAN frame-base table. The four
// shipped identities are 0,12,24,36 and therefore select the four six-frame
// groups 0,6,12,18. The fallback only protects malformed portable saves.
[[nodiscard]] std::size_t fig_fighter_base_frame(
    std::uint16_t identity, std::size_t party_index,
    std::size_t frame_count) noexcept;

struct FigFighterPlacement {
    std::size_t frame{};
    int left{};
    int top{};
};

struct FigWeaponAnimation {
    std::uint16_t item_id{};
    bool mirrored{};
    bool operator==(const FigWeaponAnimation&) const = default;
};

struct FigWeaponPlacement {
    int left{};
    int top{};
    bool operator==(const FigWeaponPlacement&) const = default;
};

// Exact FMAN pose placement used by FIG 1000:137a. The executable stores the
// horizontal coordinate in Mode-X byte columns, hence the final *4 conversion
// to the portable 320-pixel linear surface.
[[nodiscard]] FigFighterPlacement fig_fighter_placement(
    std::uint16_t identity, std::size_t party_index, std::size_t pose,
    std::size_t frame_count) noexcept;

// FIG 14b1 always has an SW000 unarmed animation. Byte +2c==1 forces only
// the right-hand segment; otherwise a nonzero left hand is played as a second,
// horizontally mirrored 155c pass. Coordinates are target centre-4 Mode-X
// columns and target centre-50 scan lines, independent of sprite dimensions.
[[nodiscard]] std::vector<FigWeaponAnimation> fig_weapon_animations(
    const BattlePartyMember& member);
[[nodiscard]] FigWeaponPlacement fig_weapon_placement(
    int target_x, int target_y) noexcept;

}  // namespace swd2
