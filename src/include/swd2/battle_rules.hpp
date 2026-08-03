#pragma once

#include <cstdint>
#include <functional>
#include <array>
#include <span>
#include <vector>

namespace swd2 {

// Return a value in [0, modulus), matching FIG.EXE's 1000:2b41 helper.
using BattleRandom = std::function<std::uint16_t(std::uint16_t modulus)>;

struct InitiativeStats {
    std::uint16_t random_range{};
    std::uint16_t base{};
};

struct BattleTurn {
    bool monster{};
    std::uint16_t index{};
    std::uint16_t score{};
};

// FIG.EXE 1000:0863 rolls all four actor slots followed by each packed runtime
// combatant, then repeatedly takes the leftmost maximum. Normal formations
// have at most five enemies; the same runtime may append two captured allies.
// Its selector nevertheless compares only slots 0..9, so in the pathological
// five-enemy/two-ally case runtime slot 6 is ignored and actor slot 0 occupies
// the final zero-score turn. The portable rule preserves that original quirk.
std::vector<BattleTurn> roll_turn_order(
    const std::array<InitiativeStats, 4>& actors,
    std::span<const InitiativeStats> monsters,
    const BattleRandom& random);

struct PhysicalAttackResult {
    bool hit{};
    bool evaded{};
    bool critical{};
    bool defeated{};
    bool inflicted_status{};
    std::uint16_t damage{};
};

struct PlayerAttackStats {
    std::uint16_t level{};
    std::uint16_t physical_attack{};
};

struct MonsterTargetState {
    std::uint16_t hit_points{};
    std::uint16_t physical_defense{};
    std::uint16_t evasion{};
    bool physical_immunity{};  // runtime +34b1 == 1
    bool evasion_enabled{true};
};

// FIG.EXE 1000:1266. critical_countdown is SharedState +49e. On a critical
// roll it is reset to 21 and then decremented, exactly as the original code.
PhysicalAttackResult player_basic_attack(const PlayerAttackStats& attacker,
                                          MonsterTargetState& target,
                                          std::uint16_t& critical_countdown,
                                          const BattleRandom& random);

struct MonsterAttackStats {
    std::uint16_t level{};
    std::uint16_t physical_attack{};
    std::uint16_t status_strength{};  // ITEM +26 / runtime +326d
};

struct PlayerTargetState {
    std::uint16_t hit_points{};
    std::uint16_t physical_defense{};
    std::uint16_t evasion{};
    std::uint16_t status_bits{};
    bool physical_immunity{};         // actor byte +26 == 1
    bool status_immunity{};           // actor byte +2a == 1
    bool evasion_enabled{true};
};

// FIG.EXE 1000:296a, excluding presentation side effects.
PhysicalAttackResult monster_basic_attack(const MonsterAttackStats& attacker,
                                           PlayerTargetState& target,
                                           const BattleRandom& random);

// Captured-monster ally physical branch at FIG 1000:0fc8..102e. Unlike a
// normal player/enemy attack it has no level roll, critical, retry, immunity,
// or status infliction: random(11-evasion)==0 misses, otherwise only the
// positive difference between the ally attack and enemy defense is applied.
PhysicalAttackResult summoned_ally_basic_attack(
    std::uint16_t physical_attack, MonsterTargetState& target,
    const BattleRandom& random);

}  // namespace swd2
