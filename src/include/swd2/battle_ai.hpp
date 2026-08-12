#pragma once

#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace swd2 {

// Directly corresponds to the ITEM fields consumed by FIG 1000:20e7.
struct MonsterAiState {
    std::uint16_t hit_points{};
    std::uint16_t maximum_hit_points{};
    std::uint16_t level{};
    std::uint16_t ai_type{};              // ITEM +28
    std::uint16_t primary_chance{};       // ITEM +2e
    std::uint16_t generic_ability{};      // ITEM +32
    std::uint16_t healing_ability{};      // ITEM +3a
    std::uint16_t secondary_chance{};     // ITEM +3e
    std::uint16_t special_ability_a{};    // ITEM +42
    std::uint16_t ability_points{};       // ITEM +44, mutable runtime pool
    std::uint16_t special_ability_b{};    // ITEM +46
    bool magic_blocked{};                 // runtime +344d != 0
};

enum class MonsterAiAction {
    basic_attack,
    heal_self,
    generic_ability,
    special_ability,
    flee,
    flee_failed,
    skip,
};

struct MonsterAiDecision {
    MonsterAiAction action{MonsterAiAction::basic_attack};
    std::optional<std::size_t> target;
    std::uint16_t ability_id{};
    std::uint16_t power{};
};

// Normal (non-story capture/flee) branch of FIG 1000:20e7. Ability attempts
// consume their cost immediately, even when a later state-specific handler may
// reject and refund them just as the original did.
MonsterAiDecision choose_monster_action(
    MonsterAiState& monster, std::span<const bool> living_players,
    const BattleAbilityDatabase& abilities, const BattleRandom& random,
    bool random_encounter_rules = false,
    std::uint16_t first_actor_level = 0);

// Captured monsters summoned from inventory use the shorter FIG 1000:0ead
// decision tree. They can leave on the literal random(10)==7 branch, choose a
// living encounter enemy, spend their own ability pool through 22f3, and fall
// back to their special-B/generic ability before making a physical attack.
// `may_leave` encodes FIG's packed-runtime constraint: the first ally may only
// leave when no second ally exists, while the second ally may always leave.
// BattleSession sets `defer_power_roll` because 1048 first checks the
// unflagged required-medium guard; a 58fa rejection spends AP but does not
// consume the otherwise unused monster-power random word.
MonsterAiDecision choose_summoned_ally_action(
    MonsterAiState& ally, std::span<const bool> living_enemies,
    const BattleAbilityDatabase& abilities, const BattleRandom& random,
    bool may_leave, bool never_leaves = false,
    bool defer_power_roll = false);

}  // namespace swd2
