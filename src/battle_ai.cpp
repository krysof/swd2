#include "swd2/battle_ai.hpp"

#include <algorithm>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t draw(const BattleRandom& random, std::uint16_t modulus) {
    if (!random || modulus == 0) throw std::invalid_argument("invalid FIG AI random draw");
    const auto value = random(modulus);
    if (value >= modulus) throw std::runtime_error("FIG AI random value is out of range");
    return value;
}

std::optional<std::uint16_t> try_ability(
    std::uint16_t id, MonsterAiState& monster,
    const BattleAbilityDatabase& abilities, const BattleRandom& random,
    bool defer_power_roll = false) {
    if (id == 0 || id >= abilities.abilities().size()) return std::nullopt;
    const auto& ability = abilities.ability(id);
    if (monster.ability_points < ability.cost) return std::nullopt;
    monster.ability_points =
        static_cast<std::uint16_t>(monster.ability_points - ability.cost);
    if (defer_power_roll) return ability.base_power;
    return static_cast<std::uint16_t>(
        ability.base_power + draw(
            random, static_cast<std::uint16_t>((monster.level >> 1U) + 2U)));
}

std::optional<std::size_t> choose_living_target(
    std::span<const bool> living, const BattleRandom& random) {
    if (living.empty() ||
        std::none_of(living.begin(), living.end(), [](bool value) { return value; })) {
        return std::nullopt;
    }
    std::size_t target;
    do {
        target = draw(random, static_cast<std::uint16_t>(living.size()));
    } while (!living[target]);
    return target;
}

}  // namespace

MonsterAiDecision choose_monster_action(
    MonsterAiState& monster, std::span<const bool> living_players,
    const BattleAbilityDatabase& abilities, const BattleRandom& random,
    bool random_encounter_rules, std::uint16_t first_actor_level) {
    MonsterAiDecision result;
    if (random_encounter_rules) {
        if (monster.ai_type == 2) {
            result.action = MonsterAiAction::flee;
            return result;
        }
        if (first_actor_level >= monster.level &&
            static_cast<std::uint16_t>(first_actor_level - monster.level) >= 7) {
            const auto intimidation = draw(random, 3);
            if (intimidation == 2) {
                result.action = MonsterAiAction::flee;
                return result;
            }
            if (intimidation == 1) {
                result.action = MonsterAiAction::flee_failed;
                return result;
            }
        }
    }

    const auto low_hit_points =
        monster.hit_points <= static_cast<std::uint16_t>(monster.maximum_hit_points >> 2U);
    if (!monster.magic_blocked && low_hit_points) {
        if (const auto power = try_ability(
                monster.healing_ability, monster, abilities, random);
            power && *power != 0) {
            const auto sum = static_cast<unsigned>(monster.hit_points) + *power;
            monster.hit_points = static_cast<std::uint16_t>(
                std::min<unsigned>(sum, monster.maximum_hit_points));
            result.action = MonsterAiAction::heal_self;
            result.ability_id = monster.healing_ability;
            result.power = *power;
            return result;
        }
    }
    if (random_encounter_rules && low_hit_points && monster.ai_type == 1) {
        const auto desperation = draw(random, 3);
        if (desperation == 0) {
            result.action = MonsterAiAction::flee;
            return result;
        }
        if (desperation == 2) {
            result.action = MonsterAiAction::flee_failed;
            return result;
        }
    }

    result.target = choose_living_target(living_players, random);
    if (monster.magic_blocked) return result;
    if (draw(random, 10) > monster.primary_chance) return result;

    if (draw(random, 10) <= monster.secondary_chance) {
        if ((draw(random, 10) & 1U) != 0) {
            if (const auto power = try_ability(
                    monster.special_ability_a, monster, abilities, random)) {
                result.action = MonsterAiAction::special_ability;
                result.ability_id = monster.special_ability_a;
                result.power = *power;
                return result;
            }
        }
        if (const auto power = try_ability(
                monster.special_ability_b, monster, abilities, random)) {
            result.action = MonsterAiAction::special_ability;
            result.ability_id = monster.special_ability_b;
            result.power = *power;
            return result;
        }
    }
    if (const auto power = try_ability(
            monster.generic_ability, monster, abilities, random)) {
        result.action = MonsterAiAction::generic_ability;
        result.ability_id = monster.generic_ability;
        result.power = *power;
    }
    return result;
}

MonsterAiDecision choose_summoned_ally_action(
    MonsterAiState& ally, std::span<const bool> living_enemies,
    const BattleAbilityDatabase& abilities, const BattleRandom& random,
    bool may_leave, bool never_leaves, bool defer_power_roll) {
    MonsterAiDecision result;

    // FIG rolls this even for the protected definition 316 and for a first
    // ally that cannot be removed while the packed second slot is occupied.
    if (draw(random, 10) == 7 && may_leave && !never_leaves) {
        result.action = MonsterAiAction::flee;
        return result;
    }
    result.target = choose_living_target(living_enemies, random);
    if (!result.target) {
        result.action = MonsterAiAction::skip;
        return result;
    }

    if (draw(random, 10) > ally.primary_chance) return result;
    if (draw(random, 10) <= ally.secondary_chance) {
        if ((draw(random, 10) & 1U) != 0) {
            if (const auto power = try_ability(
                    ally.special_ability_a, ally, abilities, random,
                    defer_power_roll)) {
                result.action = MonsterAiAction::special_ability;
                result.ability_id = ally.special_ability_a;
                result.power = *power;
                return result;
            }
        }
        if (const auto power = try_ability(
                ally.special_ability_b, ally, abilities, random,
                defer_power_roll)) {
            result.action = MonsterAiAction::special_ability;
            result.ability_id = ally.special_ability_b;
            result.power = *power;
            return result;
        }
    }
    if (const auto power = try_ability(
            ally.generic_ability, ally, abilities, random,
            defer_power_roll)) {
        result.action = MonsterAiAction::generic_ability;
        result.ability_id = ally.generic_ability;
        result.power = *power;
    }
    return result;
}

}  // namespace swd2
