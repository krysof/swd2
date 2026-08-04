#pragma once

#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace swd2 {

// The five resistance words are copied from ITEM bytes +16,+17,+18,+1a,+1b.
// Status counters correspond to FIG runtime arrays +33fd,+3411,+3425,+3439,+344d.
struct MonsterBattleState {
    std::uint16_t hit_points{};
    std::uint16_t maximum_hit_points{};
    std::array<std::uint8_t, 5> resistances{};
    std::array<std::uint16_t, 5> status_turns{};
    std::uint16_t physical_attack{};
    std::uint16_t evasion{};
    std::uint16_t special_status_turns{};  // FIG +3461, effect 69
    std::uint16_t attack_buff_turns{};     // FIG +3475, effect 67
    std::uint16_t evasion_buff_turns{};    // FIG +3489, effect 68
};

struct MonsterTurnStatusResult {
    bool skipped{};
    bool defeated{};
    bool periodic_triggered{};
    std::uint16_t periodic_damage{};
    // Bits 0/1/2 are the FIG +3461/+3475/+3489 temporary monster states.
    std::uint8_t expired_buff_mask{};
    // Bits 0..4 are the visible FIG +33fd..+344d status stacks. Expiry is
    // silent, but the next 2db8 recomposition must stop drawing its MENU icon.
    std::uint8_t expired_status_mask{};
};

enum class AbilityBlockReason : std::uint8_t {
    none,
    resistance,
    magic_ward,   // FIG player +315c: blocks without consuming the timer
    magic_shield, // FIG player +3164: consumes one charge and plays effect 62
};

// Start-of-monster-turn state at FIG 1000:0af5..0c3b: decrement magic/special
// and temporary-stat timers, restore base stats on expiry, apply the persistent
// slot-two periodic hit, then consume one of slots 0/1/3 to skip the action.
MonsterTurnStatusResult advance_monster_turn_status(
    MonsterBattleState& monster, std::uint16_t base_physical_attack,
    std::uint16_t base_evasion, std::uint16_t first_actor_level,
    const BattleRandom& random);

// Portable view of the actor fields used by FIG's support-effect handlers
// 447b..478f. RPG's status page identifies +3d/+45 as strength/wisdom. Effect
// 26 increments both persistent reaction +4f and the live battle value +5d.
struct PlayerSupportState {
    std::uint16_t hit_points{};                 // +2d
    std::uint16_t maximum_hit_points{};         // +2f
    std::uint16_t secondary_points{};           // +35
    std::uint16_t maximum_secondary_points{};   // +37
    std::uint16_t ability_points{};             // +55
    std::uint16_t maximum_ability_points{};     // +57
    std::uint16_t status_bits{};                // +08
    std::uint16_t physical_attack{};            // +0c
    std::uint16_t strength{};                    // +3d
    std::uint16_t wisdom{};                      // +45
    std::uint16_t base_reaction{};               // +4f
    std::uint16_t speed{};                      // +5d
};

struct AbilityTargetResult {
    std::size_t target_index{};
    bool resisted{};
    bool absorbed{};
    bool defeated{};
    AbilityBlockReason block_reason{AbilityBlockReason::none};
    std::uint16_t damage{};
    std::uint16_t healing{};
    std::uint16_t status_duration{};
    // Support selectors 01..30 can change far more than HP: the two other
    // gauges, their maxima, status bits, and several permanent actor fields.
    // Retain the exact post-handler state so an event-driven frontend does
    // not keep drawing the pre-action card after 58a9's clean recomposition.
    std::optional<PlayerSupportState> resulting_player_support_state;
};

struct PlayerAbilityResult {
    bool supported{};
    bool all_targets{};
    std::uint16_t canonical_ability_id{};
    std::vector<AbilityTargetResult> targets;
};

struct PlayerSupportResult {
    bool supported{};
    bool all_targets{};
    std::vector<AbilityTargetResult> targets;
};

// FIG keeps the two numeric operands used by support handlers in shared data
// words DS:2bb9/2bbb rather than on the stack.  Most handlers overwrite both,
// but resurrection selector 1c contains a real typo: it writes 2bb9 twice and
// therefore reuses the secondary operand left by the preceding support action.
// A battle session owns this state so that quirk remains deterministic and
// faithful across player, item, and summoned-ally dispatches.
struct PlayerSupportRuntime {
    std::uint16_t primary_operand{};
    std::uint16_t secondary_operand{};
};

// State portion of effect-dispatch entries 01..30. Percentage and fixed
// recovery use FIG's exact max/current and quarter-stat formulas; status masks
// and +3 temporary buffs are transcribed directly from 447b..478f.
PlayerSupportResult apply_player_support_effect(
    std::uint16_t effect_code, std::size_t caster,
    std::size_t selected_target, std::span<PlayerSupportState> players,
    PlayerSupportRuntime* runtime = nullptr);

// State effects behind FIG's effect-dispatch entries 32..60. Presentation is
// deliberately separate; this function reproduces 5974/59a1/5a91, including
// resistance values 1=immune, 2=double damage, and 3=absorb.
PlayerAbilityResult apply_player_ability_effect(
    std::uint16_t effect_code, std::uint16_t actor_level,
    std::size_t selected_target, std::span<MonsterBattleState> monsters,
    const BattleAbilityDatabase& abilities, const BattleRandom& random);

struct PlayerBattleState {
    std::uint16_t hit_points{};
    std::uint16_t maximum_hit_points{};
    std::uint16_t status_bits{};
    std::array<std::uint8_t, 3> ability_resistances{};  // actor +27,+28,+2b
    std::array<std::uint16_t, 4> special_status_turns{}; // effects 5e,64,5f,65
    std::array<std::uint16_t, 6> buff_turns{};           // FIG +313c..+3164
    std::uint16_t physical_attack{};
    std::uint16_t physical_defense{};
    std::uint16_t speed{};
    std::uint16_t evasion{};
    std::uint16_t base_physical_attack{};   // actor +41 snapshot
    std::uint16_t base_physical_defense{};  // actor +43 snapshot
    std::uint16_t base_speed{};             // actor +5f snapshot
    std::uint16_t base_evasion{};           // actor +67 snapshot
};

struct PlayerTacticalResult {
    bool supported{};
    bool target_is_monster{};
    std::uint16_t canonical_ability_id{};
    // FIG 55e4 emits one compact message for every enemy temporary state
    // removed by effect 61. Bits 0..2 are special-status, attack and evasion.
    std::uint8_t removed_monster_buff_mask{};
    std::vector<AbilityTargetResult> targets;
};

// Player-side state handlers at FIG 1000:5557..57f1. These are separate from
// the damage/status dispatcher because they alter temporary actor buffs, magic
// shield charges, or an enemy's temporary buffs rather than HP.
PlayerTacticalResult apply_player_tactical_effect(
    std::uint16_t effect_code, std::uint16_t actor_level,
    std::size_t caster, std::size_t selected_target,
    std::span<PlayerBattleState> players, MonsterBattleState* selected_monster,
    std::uint16_t monster_base_attack, std::uint16_t monster_base_evasion,
    const BattleAbilityDatabase& abilities, const BattleRandom& random);

// Post-player-action counter processing at FIG 1000:0c41..0d98.
struct PlayerTurnStatusResult {
    // Bits 0..4 correspond to FIG +313c..+315c. +3164 is a charge count and
    // is intentionally not decremented at end of turn.
    std::uint8_t expired_buff_mask{};
    // Bits 0..3 correspond to +316c/+3174/+317c/+3184 status timers.
    std::uint8_t recovered_status_mask{};
};

PlayerTurnStatusResult advance_player_turn_status(
    PlayerBattleState& player) noexcept;

struct MonsterAbilityCastResult {
    bool cast{};
    bool all_targets{};
    std::uint16_t ability_id{};
    std::uint16_t cost{};
    std::uint16_t power{};
    std::vector<AbilityTargetResult> targets;
};

// FIG 1000:22f3 plus the state portion of 23b1/24ed. The ability cost is
// removed before power is rolled; low target-flag values 1/2/3 select the
// actor resistance bytes, and flag bit 0x2000 selects a single target.
MonsterAbilityCastResult apply_monster_ability(
    std::uint16_t ability_id, std::uint16_t monster_level,
    std::uint16_t& ability_points, std::size_t selected_target,
    std::span<PlayerBattleState> players,
    const BattleAbilityDatabase& abilities, const BattleRandom& random);

// State half of the same handler when FIG 1000:20e7 has already paid the
// ability cost and rolled its power while choosing the monster's action.
// Keeping this entry point separate prevents a composed battle loop from
// charging and rolling twice.
MonsterAbilityCastResult apply_prepaid_monster_ability(
    std::uint16_t ability_id, std::uint16_t power,
    std::size_t selected_target, std::span<PlayerBattleState> players,
    const BattleAbilityDatabase& abilities);

enum class MonsterSpecialResolution { unsupported, applied, fallback_basic_attack };

struct MonsterSpecialAbilityResult {
    MonsterSpecialResolution resolution{MonsterSpecialResolution::unsupported};
    std::uint16_t ability_id{};
    // FIG 2889..2925 reports every non-zero player buff erased by effect 61
    // in +313c..+3164 order. Preserve that pre-clear state for presentation.
    std::uint8_t removed_player_buff_mask{};
    std::vector<AbilityTargetResult> targets;
};

// State handlers behind FIG 1000:26af for enemy-only status/buff abilities.
// When an already-active buff or an empty cleanse is selected, FIG refunds
// the paid cost and falls back to its physical-attack path; that outcome is
// explicit here so BattleSession can perform the same fallback.
MonsterSpecialAbilityResult apply_prepaid_monster_special(
    std::uint16_t ability_id, std::uint16_t power, std::uint16_t& ability_points,
    std::size_t selected_target, std::span<PlayerBattleState> players,
    MonsterBattleState& monster, const BattleAbilityDatabase& abilities,
    const BattleRandom& random);

}  // namespace swd2
