#include "swd2/battle_rules.hpp"

#include <algorithm>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t draw(const BattleRandom& random, std::uint16_t modulus) {
    if (!random) throw std::invalid_argument("battle random source is empty");
    if (modulus == 0) {
        throw std::invalid_argument("FIG random modulus must not be zero");
    }
    const auto value = random(modulus);
    if (value >= modulus) {
        throw std::runtime_error("battle random source returned an out-of-range value");
    }
    return value;
}

std::uint16_t attack_range(std::uint16_t level) noexcept {
    return static_cast<std::uint16_t>((level >> 1U) + 2U);
}

std::uint16_t evasion_range(std::uint16_t evasion) noexcept {
    // Preserve the original 16-bit SUB rather than clamping a malformed stat.
    return static_cast<std::uint16_t>(12U - evasion);
}

}  // namespace

std::vector<BattleTurn> roll_turn_order(
    const std::array<InitiativeStats, 4>& actors,
    std::span<const InitiativeStats> monsters,
    const BattleRandom& random) {
    if (monsters.size() > 7) {
        throw std::invalid_argument(
            "FIG supports at most five enemies and two summoned allies");
    }
    std::vector<std::uint16_t> scores(actors.size() + monsters.size());
    for (std::size_t index = 0; index < actors.size(); ++index) {
        scores[index] = static_cast<std::uint16_t>(
            draw(random, actors[index].random_range) + actors[index].base);
    }
    for (std::size_t index = 0; index < monsters.size(); ++index) {
        scores[index + 4] = static_cast<std::uint16_t>(
            draw(random, monsters[index].random_range) + monsters[index].base);
    }

    std::vector<BattleTurn> result;
    result.reserve(actors.size() + monsters.size());
    constexpr std::size_t original_compared_slots = 10;
    const auto compared_slots = std::min(scores.size(), original_compared_slots);
    for (std::size_t turn = 0; turn < actors.size() + monsters.size(); ++turn) {
        std::size_t selected = 0;
        for (std::size_t candidate = 1; candidate < compared_slots; ++candidate) {
            // JNC keeps the earlier slot on equal scores.
            if (scores[candidate] > scores[selected]) selected = candidate;
        }
        result.push_back({selected >= 4,
                          static_cast<std::uint16_t>(selected >= 4 ? selected - 4 : selected),
                          scores[selected]});
        scores[selected] = 0;
    }
    return result;
}

PhysicalAttackResult player_basic_attack(const PlayerAttackStats& attacker,
                                          MonsterTargetState& target,
                                          std::uint16_t& critical_countdown,
                                          const BattleRandom& random) {
    if (critical_countdown == 0) {
        throw std::invalid_argument("FIG critical countdown must be initialized");
    }
    PhysicalAttackResult result;
    const auto rolled_attack = static_cast<std::uint16_t>(
        draw(random, attack_range(attacker.level)) + attacker.physical_attack);
    result.critical = draw(random, critical_countdown) == 0;
    if (result.critical) critical_countdown = 21;
    --critical_countdown;

    if (target.physical_immunity || rolled_attack <= target.physical_defense) {
        return result;
    }
    if (target.evasion_enabled && draw(random, evasion_range(target.evasion)) == 0) {
        result.evaded = true;
        return result;
    }

    result.hit = true;
    auto damage = static_cast<std::uint16_t>(rolled_attack - target.physical_defense);
    if (result.critical) damage = static_cast<std::uint16_t>(damage << 1U);
    result.damage = damage;
    if (target.hit_points <= damage) {
        target.hit_points = 0;
        result.defeated = true;
    } else {
        target.hit_points = static_cast<std::uint16_t>(target.hit_points - damage);
    }
    return result;
}

PhysicalAttackResult monster_basic_attack(const MonsterAttackStats& attacker,
                                           PlayerTargetState& target,
                                           const BattleRandom& random) {
    PhysicalAttackResult result;
    const auto range = attack_range(attacker.level);
    auto rolled_attack = static_cast<std::uint16_t>(
        draw(random, range) + attacker.physical_attack);
    if (target.physical_immunity) return result;

    if (rolled_attack <= target.physical_defense) {
        const auto retry = draw(random, range);
        if (retry == 0) return result;
        rolled_attack = static_cast<std::uint16_t>(retry + target.physical_defense);
    }
    if (target.evasion_enabled && draw(random, evasion_range(target.evasion)) == 0) {
        result.evaded = true;
        return result;
    }

    result.hit = true;
    result.damage = static_cast<std::uint16_t>(rolled_attack - target.physical_defense);

    // This secondary effect is performed after a successful hit in 296a.
    // The literal value one disables the roll; all other values are compared
    // inclusively with random(10), matching JBE in the executable.
    if (!target.status_immunity && attacker.status_strength != 1 &&
        draw(random, 10) <= attacker.status_strength) {
        target.status_bits = static_cast<std::uint16_t>(target.status_bits | 0x0200U);
        result.inflicted_status = true;
    }

    if (target.hit_points <= result.damage) {
        target.hit_points = 0;
        target.status_bits = static_cast<std::uint16_t>(target.status_bits | 0x2000U);
        result.defeated = true;
    } else {
        target.hit_points = static_cast<std::uint16_t>(target.hit_points - result.damage);
    }
    return result;
}

PhysicalAttackResult summoned_ally_basic_attack(
    std::uint16_t physical_attack, MonsterTargetState& target,
    const BattleRandom& random) {
    PhysicalAttackResult result;
    const auto modulus = static_cast<std::uint16_t>(11U - target.evasion);
    if (draw(random, modulus) == 0) {
        result.evaded = true;
        return result;
    }
    if (physical_attack <= target.physical_defense) return result;

    result.hit = true;
    result.damage = static_cast<std::uint16_t>(
        physical_attack - target.physical_defense);
    if (target.hit_points <= result.damage) {
        target.hit_points = 0;
        result.defeated = true;
    } else {
        target.hit_points = static_cast<std::uint16_t>(
            target.hit_points - result.damage);
    }
    return result;
}

}  // namespace swd2
