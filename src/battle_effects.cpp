#include "swd2/battle_effects.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace swd2 {

namespace {

enum class EffectKind { damage, status };

struct EffectDescriptor {
    std::uint16_t ability;
    EffectKind kind;
    std::uint8_t resistance;
    bool all_targets;
    std::uint8_t status_slot;
};

std::optional<EffectDescriptor> descriptor(std::uint16_t effect) {
    // Canonical ability IDs and resistance-array addresses are transcribed
    // from the dispatch routines at FIG 1000:48a2..572b.
    switch (effect) {
    case 0x32: return EffectDescriptor{54, EffectKind::damage, 2, true, 0};
    case 0x33: return EffectDescriptor{52, EffectKind::damage, 0xff, false, 0};
    case 0x34: return EffectDescriptor{60, EffectKind::damage, 0xff, false, 0};
    case 0x35: return EffectDescriptor{59, EffectKind::damage, 0xff, true, 0};
    case 0x36: return EffectDescriptor{57, EffectKind::damage, 2, false, 0};
    case 0x37: return EffectDescriptor{73, EffectKind::damage, 1, true, 0};
    case 0x38: return EffectDescriptor{76, EffectKind::damage, 0xff, false, 0};
    case 0x39: return EffectDescriptor{78, EffectKind::damage, 0xff, false, 0};
    case 0x3a: return EffectDescriptor{77, EffectKind::damage, 0xff, false, 0};
    case 0x40: return EffectDescriptor{88, EffectKind::damage, 2, true, 0};
    case 0x41: return EffectDescriptor{70, EffectKind::damage, 1, false, 0};
    case 0x42: return EffectDescriptor{74, EffectKind::damage, 1, true, 0};
    case 0x43: return EffectDescriptor{87, EffectKind::damage, 1, true, 0};
    case 0x44: return EffectDescriptor{89, EffectKind::damage, 0xff, false, 0};
    case 0x45: return EffectDescriptor{91, EffectKind::damage, 0xff, true, 0};
    case 0x46: return EffectDescriptor{95, EffectKind::damage, 0xff, false, 0};
    case 0x48: return EffectDescriptor{84, EffectKind::damage, 0xff, false, 0};
    case 0x49: return EffectDescriptor{5, EffectKind::damage, 0xff, false, 0};
    case 0x4a: return EffectDescriptor{11, EffectKind::damage, 0xff, false, 0};
    case 0x4b: return EffectDescriptor{20, EffectKind::damage, 0xff, false, 0};
    case 0x4c: return EffectDescriptor{10, EffectKind::damage, 0xff, false, 0};
    case 0x4d: return EffectDescriptor{13, EffectKind::damage, 0xff, false, 0};
    case 0x4e: return EffectDescriptor{14, EffectKind::damage, 0xff, false, 0};
    case 0x4f: return EffectDescriptor{15, EffectKind::damage, 0xff, false, 0};
    case 0x50: return EffectDescriptor{17, EffectKind::damage, 0xff, false, 0};
    case 0x51: return EffectDescriptor{18, EffectKind::damage, 0xff, false, 0};
    case 0x52: return EffectDescriptor{19, EffectKind::damage, 0xff, false, 0};
    case 0x53: return EffectDescriptor{21, EffectKind::damage, 0xff, false, 0};
    case 0x54: return EffectDescriptor{22, EffectKind::damage, 0xff, false, 0};
    case 0x55: return EffectDescriptor{25, EffectKind::damage, 0xff, false, 0};
    case 0x56: return EffectDescriptor{26, EffectKind::damage, 0xff, false, 0};
    case 0x57: return EffectDescriptor{27, EffectKind::damage, 0xff, false, 0};
    case 0x58: return EffectDescriptor{28, EffectKind::damage, 0xff, false, 0};
    case 0x59: return EffectDescriptor{29, EffectKind::damage, 0xff, false, 0};
    case 0x5a: return EffectDescriptor{16, EffectKind::damage, 0xff, false, 0};
    case 0x5b: return EffectDescriptor{23, EffectKind::damage, 0xff, true, 0};
    case 0x5c: return EffectDescriptor{24, EffectKind::damage, 0xff, false, 0};
    case 0x5d: return EffectDescriptor{30, EffectKind::damage, 0xff, false, 0};
    case 0x5e: return EffectDescriptor{6, EffectKind::status, 4, false, 0};
    case 0x5f: return EffectDescriptor{72, EffectKind::status, 4, false, 1};
    case 0x60: return EffectDescriptor{48, EffectKind::status, 3, false, 2};
    case 0x64: return EffectDescriptor{56, EffectKind::status, 4, false, 3};
    case 0x65: return EffectDescriptor{58, EffectKind::status, 4, false, 4};
    default: return std::nullopt;
    }
}

std::optional<std::uint16_t> visual_only_ability(std::uint16_t effect) {
    // These six dispatcher entries only animate/reposition combatant sprites.
    // They return normally, so the caller still consumes the selected command
    // resource even though no portable combat statistic changes.
    switch (effect) {
    case 0x31: return 51;
    case 0x3b: return 66;
    case 0x3c: return 82;
    case 0x3d: return 53;
    case 0x3e: return 67;
    case 0x3f: return 83;
    default: return std::nullopt;
    }
}

std::uint16_t checked_draw(const BattleRandom& random, std::uint16_t modulus) {
    if (!random || modulus == 0) {
        throw std::invalid_argument("invalid FIG ability random source/modulus");
    }
    const auto value = random(modulus);
    if (value >= modulus) {
        throw std::runtime_error("ability random source returned an out-of-range value");
    }
    return value;
}

}  // namespace

MonsterTurnStatusResult advance_monster_turn_status(
    MonsterBattleState& monster, std::uint16_t base_physical_attack,
    std::uint16_t base_evasion, std::uint16_t first_actor_level,
    const BattleRandom& random) {
    MonsterTurnStatusResult result;
    if (monster.status_turns[4] != 0) {
        --monster.status_turns[4];
        if (monster.status_turns[4] == 0) result.expired_status_mask |= 0x10U;
    }
    if (monster.special_status_turns != 0) {
        --monster.special_status_turns;
        if (monster.special_status_turns == 0) result.expired_buff_mask |= 0x01U;
    }
    // 0b00 jumps directly to the shared expiry-card path as soon as one timer
    // reaches zero. Consequently lower-priority timers do not even decrement
    // that turn. Priority is special, evasion, then attack.
    if (result.expired_buff_mask == 0 && monster.evasion_buff_turns != 0) {
        --monster.evasion_buff_turns;
        if (monster.evasion_buff_turns == 0) {
            monster.evasion = base_evasion;
            result.expired_buff_mask |= 0x04U;
        }
    }
    if (result.expired_buff_mask == 0 && monster.attack_buff_turns != 0) {
        --monster.attack_buff_turns;
        if (monster.attack_buff_turns == 0) {
            monster.physical_attack = base_physical_attack;
            result.expired_buff_mask |= 0x02U;
        }
    }

    if (monster.status_turns[2] != 0 && monster.hit_points != 0) {
        const auto modulus = static_cast<std::uint16_t>(first_actor_level << 1U);
        if (modulus != 0) {
            result.periodic_triggered = true;
            result.periodic_damage = checked_draw(random, modulus);
            if (monster.hit_points <= result.periodic_damage) {
                monster.hit_points = 0;
                result.defeated = true;
            } else {
                monster.hit_points = static_cast<std::uint16_t>(
                    monster.hit_points - result.periodic_damage);
            }
        }
    }
    if (monster.hit_points == 0) {
        result.defeated = true;
        result.skipped = true;
        return result;
    }
    for (const auto slot : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
        if (monster.status_turns[slot] != 0) {
            --monster.status_turns[slot];
            if (monster.status_turns[slot] == 0) {
                result.expired_status_mask = static_cast<std::uint8_t>(
                    result.expired_status_mask | (1U << slot));
            }
            result.skipped = true;
            break;
        }
    }
    return result;
}

PlayerTurnStatusResult advance_player_turn_status(
    PlayerBattleState& player) noexcept {
    PlayerTurnStatusResult result;
    for (std::size_t slot = 0; slot < 5; ++slot) {
        auto& timer = player.buff_turns[slot];
        if (timer == 0) continue;
        --timer;
        if (timer != 0) continue;
        result.expired_buff_mask = static_cast<std::uint8_t>(
            result.expired_buff_mask | (1U << slot));
        switch (slot) {
        case 0: player.speed = player.base_speed; break;
        case 1: player.physical_defense = player.base_physical_defense; break;
        case 2: player.physical_attack = player.base_physical_attack; break;
        case 3: player.evasion = player.base_evasion; break;
        default: break;  // +315c has no direct actor-field restoration
        }
    }
    static constexpr std::array<std::uint16_t, 4> status_bits = {
        0x0020, 0x0002, 0x0004, 0x0080,
    };
    for (std::size_t slot = 0; slot < status_bits.size(); ++slot) {
        auto& timer = player.special_status_turns[slot];
        if (timer == 0 || (player.status_bits & status_bits[slot]) == 0) continue;
        --timer;
        if (timer == 0) {
            player.status_bits = static_cast<std::uint16_t>(
                player.status_bits & ~status_bits[slot]);
            result.recovered_status_mask = static_cast<std::uint8_t>(
                result.recovered_status_mask | (1U << slot));
        }
    }
    return result;
}

PlayerSupportResult apply_player_support_effect(
    std::uint16_t effect_code, std::size_t caster,
    std::size_t selected_target, std::span<PlayerSupportState> players,
    PlayerSupportRuntime* runtime) {
    PlayerSupportResult result;
    if (effect_code == 0 || effect_code > 0x30 ||
        caster >= players.size() || selected_target >= players.size()) {
        return result;
    }
    result.supported = true;
    result.all_targets = effect_code >= 0x0c && effect_code <= 0x0e;
    if (effect_code == 0x12) result.all_targets = true;
    PlayerSupportRuntime local_runtime{};
    auto& operands = runtime != nullptr ? *runtime : local_runtime;

    const auto caster_bonus = static_cast<std::uint16_t>(players[caster].field_45 >> 2U);
    const auto living_normal = [](const PlayerSupportState& player) {
        return (player.status_bits & 0xe000U) == 0;
    };
    const auto heal_percent = [&](std::uint16_t& current, std::uint16_t maximum,
                                  std::uint16_t percent) {
        const auto amount = static_cast<unsigned>(maximum) * percent / 100U + caster_bonus;
        current = static_cast<std::uint16_t>(
            std::min<unsigned>(static_cast<unsigned>(current) + amount, maximum));
    };
    const auto heal_fixed = [](std::uint16_t& current, std::uint16_t maximum,
                               std::uint16_t flat, std::uint16_t quarter_field) {
        const auto amount = static_cast<unsigned>(flat) + (quarter_field >> 2U);
        current = static_cast<std::uint16_t>(
            std::min<unsigned>(static_cast<unsigned>(current) + amount, maximum));
    };
    const auto emit = [&](std::size_t index, std::uint16_t before_hp) {
        AbilityTargetResult target;
        target.target_index = index;
        target.healing = static_cast<std::uint16_t>(players[index].hit_points - before_hp);
        target.resulting_player_support_state = players[index];
        result.targets.push_back(target);
    };

    const auto apply = [&](std::size_t index) {
        auto& player = players[index];
        const auto before_hp = player.hit_points;
        switch (effect_code) {
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04: {
            static constexpr std::array<std::uint16_t, 4> hp = {25, 45, 70, 100};
            static constexpr std::array<std::uint16_t, 4> secondary = {25, 45, 100, 100};
            operands.primary_operand = hp[effect_code - 1];
            operands.secondary_operand = secondary[effect_code - 1];
            if (!living_normal(player)) break;
            heal_percent(player.hit_points, player.maximum_hit_points,
                         operands.primary_operand);
            heal_percent(player.secondary_points, player.maximum_secondary_points,
                         operands.secondary_operand);
            break;
        }
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08: {
            static constexpr std::array<std::uint16_t, 4> percent = {25, 45, 70, 100};
            operands.primary_operand = percent[effect_code - 5];
            if (!living_normal(player)) break;
            heal_percent(player.ability_points, player.maximum_ability_points,
                         operands.primary_operand);
            break;
        }
        case 0x09:
        case 0x0a:
        case 0x0b: {
            static constexpr std::array<std::uint16_t, 3> percent = {25, 45, 70};
            operands.primary_operand = percent[effect_code - 9];
            if (!living_normal(player)) break;
            heal_percent(player.secondary_points, player.maximum_secondary_points,
                         operands.primary_operand);
            break;
        }
        case 0x0c:
        case 0x0d:
        case 0x0e: {
            static constexpr std::array<std::uint16_t, 3> hp = {25, 45, 70};
            static constexpr std::array<std::uint16_t, 3> secondary = {25, 45, 100};
            operands.primary_operand = hp[effect_code - 0x0c];
            operands.secondary_operand = secondary[effect_code - 0x0c];
            if (!living_normal(player)) break;
            heal_percent(player.hit_points, player.maximum_hit_points,
                         operands.primary_operand);
            heal_percent(player.secondary_points, player.maximum_secondary_points,
                         operands.secondary_operand);
            break;
        }
        case 0x0f:
            if (living_normal(player)) player.status_bits = 0;
            break;
        case 0x10:
        case 0x11:
            operands.primary_operand = effect_code == 0x10 ? 30U : 100U;
            operands.secondary_operand = operands.primary_operand;
            if (living_normal(player)) {
                heal_percent(player.hit_points, player.maximum_hit_points,
                             operands.primary_operand);
                heal_percent(player.secondary_points, player.maximum_secondary_points,
                             operands.secondary_operand);
                player.status_bits = 0;
            }
            break;
        case 0x12:
            operands.primary_operand = 100;
            operands.secondary_operand = 100;
            if (living_normal(player)) {
                player.status_bits = 0;
                heal_percent(player.hit_points, player.maximum_hit_points,
                             operands.primary_operand);
                heal_percent(player.secondary_points, player.maximum_secondary_points,
                             operands.secondary_operand);
                heal_percent(player.ability_points, player.maximum_ability_points, 100);
            }
            break;
        case 0x13:
        case 0x14:
        case 0x15: {
            static constexpr std::array<std::uint16_t, 3> flat = {50, 200, 400};
            operands.primary_operand = flat[effect_code - 0x13];
            operands.secondary_operand = operands.primary_operand;
            if (living_normal(player)) {
                heal_fixed(player.hit_points, player.maximum_hit_points,
                           operands.primary_operand,
                           player.secondary_points);
                heal_fixed(player.secondary_points, player.maximum_secondary_points,
                           operands.secondary_operand,
                           player.secondary_points);
            }
            break;
        }
        case 0x16:
        case 0x17:
        case 0x18: {
            static constexpr std::array<std::uint16_t, 3> flat = {50, 200, 400};
            operands.primary_operand = flat[effect_code - 0x16];
            if (living_normal(player)) {
                heal_fixed(player.secondary_points, player.maximum_secondary_points,
                           operands.primary_operand, player.secondary_points);
            }
            break;
        }
        case 0x19:
        case 0x1a:
        case 0x1b: {
            static constexpr std::array<std::uint16_t, 3> flat = {50, 200, 400};
            operands.primary_operand = flat[effect_code - 0x19];
            if (living_normal(player)) {
                heal_fixed(player.ability_points, player.maximum_ability_points,
                           operands.primary_operand, player.field_45);
            }
            break;
        }
        case 0x1c:
            // FIG 46f4 and 46fa both write DS:2bb9. DS:2bbb is
            // intentionally *not* reset and 58bf reuses its prior value.
            operands.primary_operand = 10;
            operands.primary_operand = 10;
            if ((player.status_bits & 0x2000U) != 0) {
                player.status_bits = 0;
                heal_percent(player.hit_points, player.maximum_hit_points,
                             operands.primary_operand);
                heal_percent(player.secondary_points, player.maximum_secondary_points,
                             operands.secondary_operand);
            }
            break;
        case 0x1d: player.status_bits &= 0xfeffU; break;
        case 0x1e: player.status_bits &= 0xfdffU; break;
        case 0x1f: player.status_bits &= 0xfcffU; break;
        case 0x20: player.status_bits &= 0xfbffU; break;
        case 0x21: player.status_bits &= 0xf7ffU; break;
        case 0x22:
            operands.primary_operand = 0x2f;
            player.maximum_hit_points = static_cast<std::uint16_t>(player.maximum_hit_points + 3U);
            break;
        case 0x23:
            operands.primary_operand = 0x37;
            player.maximum_secondary_points = static_cast<std::uint16_t>(player.maximum_secondary_points + 3U);
            break;
        case 0x24:
            operands.primary_operand = 0x3d;
            player.field_3d = static_cast<std::uint16_t>(player.field_3d + 3U);
            player.physical_attack = static_cast<std::uint16_t>(player.physical_attack + 3U);
            break;
        case 0x25:
            operands.primary_operand = 0x45;
            player.field_45 = static_cast<std::uint16_t>(player.field_45 + 3U);
            break;
        case 0x26:
            operands.primary_operand = 0x4f;
            player.field_4f = static_cast<std::uint16_t>(player.field_4f + 3U);
            player.speed = static_cast<std::uint16_t>(player.speed + 3U);
            break;
        case 0x27:
            operands.primary_operand = 0x57;
            player.maximum_ability_points = static_cast<std::uint16_t>(player.maximum_ability_points + 3U);
            break;
        case 0x28:
        case 0x29: break;  // dispatch targets are the preceding RET instructions
        case 0x2a: player.status_bits &= 0xff7fU; break;
        case 0x2b: player.status_bits &= 0xffbfU; break;
        case 0x2c: player.status_bits &= 0xffdfU; break;
        case 0x2d: player.status_bits &= 0xffefU; break;
        case 0x2e: player.status_bits &= 0xfff7U; break;
        case 0x2f: player.status_bits &= 0xfffbU; break;
        case 0x30: player.status_bits &= 0xfffdU; break;
        default: break;
        }
        emit(index, before_hp);
    };

    if (result.all_targets) {
        for (std::size_t index = 0; index < players.size(); ++index) apply(index);
    } else {
        apply(selected_target);
    }
    return result;
}

MonsterAbilityCastResult apply_prepaid_monster_ability(
    std::uint16_t ability_id, std::uint16_t power,
    std::size_t selected_target, std::span<PlayerBattleState> players,
    const BattleAbilityDatabase& abilities) {
    MonsterAbilityCastResult result;
    result.cast = true;
    result.ability_id = ability_id;
    result.power = power;
    const auto& ability = abilities.ability(ability_id);
    result.cost = ability.cost;
    result.all_targets = (ability.target_flags & 0x2000U) == 0;
    const auto resistance_selector = static_cast<std::uint8_t>(ability.target_flags & 7U);

    const auto apply = [&](std::size_t index) {
        auto& player = players[index];
        if ((player.status_bits & 0x2000U) != 0 || player.hit_points == 0) return;
        AbilityTargetResult target;
        target.target_index = index;
        if (player.buff_turns[4] != 0) {
            target.resisted = true;
            target.block_reason = AbilityBlockReason::magic_ward;
            result.targets.push_back(target);
            return;
        }
        if (player.buff_turns[5] != 0) {
            --player.buff_turns[5];
            target.resisted = true;
            target.block_reason = AbilityBlockReason::magic_shield;
            result.targets.push_back(target);
            return;
        }
        const auto resistance = resistance_selector >= 1 && resistance_selector <= 3
                                    ? player.ability_resistances[resistance_selector - 1]
                                    : std::uint8_t{};
        if (resistance == 1) {
            target.resisted = true;
            target.block_reason = AbilityBlockReason::resistance;
            result.targets.push_back(target);
            return;
        }
        auto adjusted_power = power;
        if (resistance == 2) {
            adjusted_power = static_cast<std::uint16_t>(adjusted_power << 1U);
        }
        if (resistance == 3) {
            target.absorbed = true;
            const auto sum = static_cast<unsigned>(player.hit_points) + adjusted_power;
            const auto healed_to = static_cast<std::uint16_t>(
                std::min<unsigned>(sum, player.maximum_hit_points));
            target.healing = static_cast<std::uint16_t>(healed_to - player.hit_points);
            player.hit_points = healed_to;
        } else {
            target.damage = adjusted_power;
            if (player.hit_points <= adjusted_power) {
                player.hit_points = 0;
                player.status_bits =
                    static_cast<std::uint16_t>(player.status_bits | 0x2000U);
                target.defeated = true;
            } else {
                player.hit_points =
                    static_cast<std::uint16_t>(player.hit_points - adjusted_power);
            }
        }
        result.targets.push_back(target);
    };

    if (result.all_targets) {
        for (std::size_t index = 0; index < players.size(); ++index) apply(index);
    } else if (selected_target < players.size()) {
        apply(selected_target);
    }
    return result;
}

MonsterSpecialAbilityResult apply_prepaid_monster_special(
    std::uint16_t ability_id, std::uint16_t power, std::uint16_t& ability_points,
    std::size_t selected_target, std::span<PlayerBattleState> players,
    MonsterBattleState& monster, const BattleAbilityDatabase& abilities,
    const BattleRandom& random) {
    MonsterSpecialAbilityResult result;
    result.ability_id = ability_id;
    const auto& ability = abilities.ability(ability_id);
    const auto refund_and_fallback = [&] {
        ability_points = static_cast<std::uint16_t>(ability_points + ability.cost);
        result.resolution = MonsterSpecialResolution::fallback_basic_attack;
    };

    const auto status = [&](std::uint16_t bit, std::size_t timer_slot,
                            bool checks_third_resistance) {
        result.resolution = MonsterSpecialResolution::applied;
        if (selected_target >= players.size()) return;
        auto& player = players[selected_target];
        if (player.hit_points == 0 || (player.status_bits & 0x2000U) != 0) return;
        AbilityTargetResult target;
        target.target_index = selected_target;
        if (checks_third_resistance && player.ability_resistances[2] == 1) {
            target.resisted = true;
        } else {
            target.status_duration = static_cast<std::uint16_t>(
                checked_draw(random, 6) + 2U);
            player.special_status_turns[timer_slot] = target.status_duration;
            player.status_bits = static_cast<std::uint16_t>(player.status_bits | bit);
        }
        result.targets.push_back(target);
    };

    switch (ability.effect_code) {
    case 0x31:
    case 0x3b:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x49:
    case 0x4c:
        // These entries only run presentation/reposition sequences. They are
        // still successful enemy actions and retain their already-paid cost.
        // In particular shipped monsters 343/456/501 use 獅子吼 (4ch), and
        // monster 502 uses 炒飯十八手 (49h); 26af falls through to RET after
        // their visual handlers rather than applying the player-side damage
        // descriptor with the same selector.
        result.resolution = MonsterSpecialResolution::applied;
        break;
    case 0x5e: status(0x0020, 0, true); break;
    case 0x64: status(0x0002, 1, true); break;
    case 0x5f: status(0x0004, 2, false); break;
    case 0x65: status(0x0080, 3, false); break;
    case 0x67:
        if (monster.attack_buff_turns != 0) {
            refund_and_fallback();
        } else {
            result.resolution = MonsterSpecialResolution::applied;
            monster.attack_buff_turns = static_cast<std::uint16_t>(
                checked_draw(random, 5) + 4U);
            monster.physical_attack =
                static_cast<std::uint16_t>(monster.physical_attack + power);
        }
        break;
    case 0x68:
        if (monster.evasion_buff_turns != 0) {
            refund_and_fallback();
        } else {
            result.resolution = MonsterSpecialResolution::applied;
            monster.evasion_buff_turns = static_cast<std::uint16_t>(
                checked_draw(random, 5) + 4U);
            monster.evasion = static_cast<std::uint16_t>(checked_draw(random, 4) + 7U);
        }
        break;
    case 0x69:
        if (monster.special_status_turns != 0) {
            refund_and_fallback();
        } else {
            result.resolution = MonsterSpecialResolution::applied;
            monster.special_status_turns = static_cast<std::uint16_t>(
                checked_draw(random, 6) + 2U);
        }
        break;
    case 0x61:
        if (selected_target >= players.size() ||
            std::none_of(players[selected_target].buff_turns.begin(),
                         players[selected_target].buff_turns.end(),
                         [](std::uint16_t turns) { return turns != 0; })) {
            refund_and_fallback();
        } else {
            result.resolution = MonsterSpecialResolution::applied;
            for (std::size_t slot = 0;
                 slot < players[selected_target].buff_turns.size(); ++slot) {
                if (players[selected_target].buff_turns[slot] != 0) {
                    result.removed_player_buff_mask = static_cast<std::uint8_t>(
                        result.removed_player_buff_mask | (1U << slot));
                }
            }
            std::fill(players[selected_target].buff_turns.begin(),
                      players[selected_target].buff_turns.end(), 0);
            AbilityTargetResult target;
            target.target_index = selected_target;
            result.targets.push_back(target);
        }
        break;
    default:
        break;
    }
    return result;
}

PlayerAbilityResult apply_player_ability_effect(
    std::uint16_t effect_code, std::uint16_t actor_level,
    std::size_t selected_target, std::span<MonsterBattleState> monsters,
    const BattleAbilityDatabase& abilities, const BattleRandom& random) {
    PlayerAbilityResult result;
    if (const auto visual = visual_only_ability(effect_code)) {
        result.supported = true;
        result.canonical_ability_id = *visual;
        return result;
    }
    const auto effect = descriptor(effect_code);
    if (!effect) return result;
    result.supported = true;
    result.all_targets = effect->all_targets;
    result.canonical_ability_id = effect->ability;
    const auto& ability = abilities.ability(effect->ability);

    const auto apply = [&](std::size_t index) {
        auto& monster = monsters[index];
        if (monster.hit_points == 0) return;
        AbilityTargetResult target;
        target.target_index = index;
        const auto resistance = effect->resistance == 0xff
                                    ? std::uint8_t{}
                                    : monster.resistances[effect->resistance];
        if (resistance == 1) {
            target.resisted = true;
            target.block_reason = AbilityBlockReason::resistance;
            result.targets.push_back(target);
            return;
        }
        if (effect->kind == EffectKind::status) {
            target.status_duration = static_cast<std::uint16_t>(
                checked_draw(random, ability.base_power) + 2U);
            monster.status_turns[effect->status_slot] = target.status_duration;
            result.targets.push_back(target);
            return;
        }

        auto power = static_cast<std::uint16_t>(
            ability.base_power + checked_draw(
                random, static_cast<std::uint16_t>((actor_level >> 1U) + 2U)));
        if (resistance == 2) power = static_cast<std::uint16_t>(power << 1U);
        if (resistance == 3) {
            target.absorbed = true;
            target.healing = power;
            monster.hit_points = static_cast<std::uint16_t>(monster.hit_points + power);
        } else {
            target.damage = power;
            if (monster.hit_points <= power) {
                monster.hit_points = 0;
                target.defeated = true;
            } else {
                monster.hit_points = static_cast<std::uint16_t>(monster.hit_points - power);
            }
        }
        result.targets.push_back(target);
    };

    if (effect->all_targets) {
        for (std::size_t index = 0; index < monsters.size(); ++index) apply(index);
    } else if (selected_target < monsters.size()) {
        apply(selected_target);
    }
    return result;
}

PlayerTacticalResult apply_player_tactical_effect(
    std::uint16_t effect_code, std::uint16_t actor_level,
    std::size_t caster, std::size_t selected_target,
    std::span<PlayerBattleState> players, MonsterBattleState* selected_monster,
    std::uint16_t monster_base_attack, std::uint16_t monster_base_evasion,
    const BattleAbilityDatabase& abilities, const BattleRandom& random) {
    PlayerTacticalResult result;
    if (caster >= players.size()) return result;

    const auto roll_power = [&](std::uint16_t ability_id) {
        const auto& ability = abilities.ability(ability_id);
        return static_cast<std::uint16_t>(
            ability.base_power +
            checked_draw(random, static_cast<std::uint16_t>(
                                     (actor_level >> 1U) + 2U)));
    };
    const auto emit_player = [&](std::size_t target, std::uint16_t duration = 0) {
        AbilityTargetResult event;
        event.target_index = target;
        event.status_duration = duration;
        result.targets.push_back(event);
    };

    switch (effect_code) {
    case 0x61: {
        if (selected_monster == nullptr) return result;
        result.supported = true;
        result.target_is_monster = true;
        result.canonical_ability_id = 71;
        if (selected_monster->special_status_turns != 0) {
            result.removed_monster_buff_mask |= 0x01U;
        }
        selected_monster->special_status_turns = 0;
        if (selected_monster->attack_buff_turns != 0) {
            result.removed_monster_buff_mask |= 0x02U;
            selected_monster->attack_buff_turns = 0;
            selected_monster->physical_attack = monster_base_attack;
        }
        if (selected_monster->evasion_buff_turns != 0) {
            result.removed_monster_buff_mask |= 0x04U;
            selected_monster->evasion_buff_turns = 0;
            selected_monster->evasion = monster_base_evasion;
        }
        AbilityTargetResult target;
        target.target_index = selected_target;
        result.targets.push_back(target);
        break;
    }
    case 0x62:
        if (selected_target >= players.size()) return result;
        result.supported = true;
        result.canonical_ability_id = 64;
        ++players[selected_target].buff_turns[5];
        emit_player(selected_target, players[selected_target].buff_turns[5]);
        break;
    case 0x63: {
        result.supported = true;
        result.canonical_ability_id = 86;
        auto& player = players[caster];
        player.speed = static_cast<std::uint16_t>(
            player.base_speed + roll_power(result.canonical_ability_id));
        player.buff_turns[0] = static_cast<std::uint16_t>(checked_draw(random, 5) + 4U);
        emit_player(caster, player.buff_turns[0]);
        break;
    }
    case 0x66: {
        result.supported = true;
        result.canonical_ability_id = 35;
        auto& player = players[caster];
        player.physical_defense = static_cast<std::uint16_t>(
            player.base_physical_defense + roll_power(result.canonical_ability_id));
        player.buff_turns[1] = static_cast<std::uint16_t>(checked_draw(random, 5) + 4U);
        emit_player(caster, player.buff_turns[1]);
        break;
    }
    case 0x67: {
        result.supported = true;
        result.canonical_ability_id = 38;
        auto& player = players[caster];
        player.physical_attack = static_cast<std::uint16_t>(
            player.base_physical_attack + roll_power(result.canonical_ability_id));
        player.buff_turns[2] = static_cast<std::uint16_t>(checked_draw(random, 5) + 4U);
        emit_player(caster, player.buff_turns[2]);
        break;
    }
    case 0x68: {
        result.supported = true;
        result.canonical_ability_id = 33;
        auto& player = players[caster];
        player.evasion = static_cast<std::uint16_t>(checked_draw(random, 4) + 7U);
        player.buff_turns[3] = static_cast<std::uint16_t>(checked_draw(random, 5) + 4U);
        emit_player(caster, player.buff_turns[3]);
        break;
    }
    case 0x69: {
        result.supported = true;
        result.canonical_ability_id = 37;
        auto& player = players[caster];
        player.buff_turns[4] = static_cast<std::uint16_t>(checked_draw(random, 6) + 2U);
        emit_player(caster, player.buff_turns[4]);
        break;
    }
    default:
        break;
    }
    return result;
}

MonsterAbilityCastResult apply_monster_ability(
    std::uint16_t ability_id, std::uint16_t monster_level,
    std::uint16_t& ability_points, std::size_t selected_target,
    std::span<PlayerBattleState> players,
    const BattleAbilityDatabase& abilities, const BattleRandom& random) {
    MonsterAbilityCastResult result;
    result.ability_id = ability_id;
    const auto& ability = abilities.ability(ability_id);
    result.cost = ability.cost;
    if (ability_points < ability.cost) return result;

    ability_points = static_cast<std::uint16_t>(ability_points - ability.cost);
    result.cast = true;
    result.power = static_cast<std::uint16_t>(
        ability.base_power + checked_draw(
            random, static_cast<std::uint16_t>((monster_level >> 1U) + 2U)));
    auto applied = apply_prepaid_monster_ability(
        ability_id, result.power, selected_target, players, abilities);
    applied.cost = result.cost;
    result = std::move(applied);
    return result;
}

}  // namespace swd2
