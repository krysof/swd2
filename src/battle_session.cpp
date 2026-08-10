#include "swd2/battle_session.hpp"

#include "swd2/battle_ai.hpp"
#include "swd2/battle_presentation.hpp"

#include <algorithm>
#include <stdexcept>

namespace swd2 {

namespace {

constexpr std::size_t no_target = static_cast<std::size_t>(-1);
constexpr std::uint16_t first_summon_item_id = 0x13a;
constexpr std::uint16_t steadfast_summon_item_id = 0x13c;
constexpr std::array<std::uint8_t, 5> class_five_counter_bits = {
    0x10, 0x08, 0x04, 0x02, 0x01,
};

bool knows_ability(const BattlePartyMember& member, std::uint16_t id) {
    return id != 0 && std::find(member.abilities.begin(), member.abilities.end(), id) !=
                          member.abilities.end();
}

bool has_class_five_resources(
    const std::array<std::uint16_t, 5>& counters, std::uint16_t raw_mask) {
    const auto mask = static_cast<std::uint8_t>(raw_mask);
    for (std::size_t index = 0; index < counters.size(); ++index) {
        if ((mask & class_five_counter_bits[index]) != 0 && counters[index] == 0) {
            return false;
        }
    }
    return true;
}

void consume_class_five_resources(
    std::array<std::uint16_t, 5>& counters, BattlePartyMember& member,
    std::uint16_t ability_id, std::uint16_t raw_mask) {
    const auto mask = static_cast<std::uint8_t>(raw_mask);
    bool exhausted = false;
    for (std::size_t index = 0; index < counters.size(); ++index) {
        if ((mask & class_five_counter_bits[index]) == 0) continue;
        --counters[index];
        exhausted = exhausted || counters[index] == 0;
    }
    if (!exhausted) return;
    const auto learned = std::find(member.abilities.begin(), member.abilities.end(),
                                   static_cast<std::uint8_t>(ability_id));
    if (learned != member.abilities.end()) *learned = 0;
}

void add_ability_events(std::vector<BattleSessionEvent>& events,
                        BattleEventKind kind, bool source_is_monster,
                        std::size_t source, bool target_is_monster,
                        std::uint16_t ability_id, std::uint16_t effect_code,
                        std::span<const AbilityTargetResult> targets,
                        std::uint8_t removed_player_buff_mask = 0,
                        std::uint8_t removed_monster_buff_mask = 0,
                        bool monster_generic_path = false,
                        bool action_anchor_is_target = true) {
    if (targets.empty()) {
        BattleSessionEvent event{
            kind, source_is_monster, source, target_is_monster, 0, ability_id};
        event.effect_code = effect_code;
        event.monster_generic_path = monster_generic_path;
        event.removed_player_buff_mask = removed_player_buff_mask;
        event.removed_monster_buff_mask = removed_monster_buff_mask;
        event.action_anchor_is_target = action_anchor_is_target;
        events.push_back(event);
        return;
    }
    for (const auto& target : targets) {
        BattleSessionEvent event;
        event.kind = kind;
        event.source_is_monster = source_is_monster;
        event.source = source;
        event.target_is_monster = target_is_monster;
        event.target = target.target_index;
        event.ability_id = ability_id;
        event.effect_code = effect_code;
        event.monster_generic_path = monster_generic_path;
        event.removed_player_buff_mask = removed_player_buff_mask;
        event.removed_monster_buff_mask = removed_monster_buff_mask;
        event.action_anchor_is_target = action_anchor_is_target;
        event.damage = target.damage;
        event.healing = target.healing;
        event.resisted = target.resisted;
        event.defeated = target.defeated;
        event.block_reason = target.block_reason;
        event.status_duration = target.status_duration;
        event.resulting_player_support_state =
            target.resulting_player_support_state;
        events.push_back(event);
    }
}

}  // namespace

BattleSession BattleSession::create(const SharedState& state,
                                    const BattleEncounter& encounter,
                                    const ScriptArchive& items,
                                    bool random_encounter_rules) {
    if (encounter.definition_slots.size() != encounter.horizontal_positions.size() ||
        encounter.definition_slots.empty() || encounter.definition_slots.size() > 5) {
        throw std::runtime_error("invalid ORC formation for a FIG battle session");
    }

    BattleSession result;
    result.random_encounter_rules_ = random_encounter_rules;
    // FIG 2003 suppresses the actor-zero capture tile when bit 0100 is set
    // in shared word +3f0, even though the resolver repeats its own rules.
    result.capture_command_available_ = (state.u8(0x3f1) & 0x01U) == 0;
    result.party_count_ = std::min<std::size_t>(state.u16(0x10), result.party_.size());
    // FIG 028d stores party_count*2 in DS:2f20. Failed random-encounter
    // escape attempts decrement it, guaranteeing success when it reaches 0.
    result.escape_attempt_countdown_ =
        static_cast<std::uint16_t>(result.party_count_ * 2U);
    for (std::size_t index = 0; index < result.party_.size(); ++index) {
        result.party_[index] = BattlePartyMember::load(state, index);
    }
    for (std::size_t slot = 0; slot < result.inventory_.size(); ++slot) {
        // The high nibble is FIG's per-actor command-reservation bitmap. All
        // four bits are cleared by 1aa9 before turn execution; the low twelve
        // bits are the actual ITEM logical id used by 1cfc/1138.
        const auto item_id = static_cast<std::uint16_t>(
            state.u16(0x382 + slot * 2U) & 0x0fffU);
        result.inventory_[slot] = item_id;
        if (item_id == 0) continue;
        const auto record_index = static_cast<std::size_t>(item_id) + 2U;
        if (record_index >= items.entry_count()) {
            throw std::runtime_error("battle inventory references a missing ITEM definition");
        }
        const auto record = items.entry(record_index);
        if (item_id >= first_summon_item_id) {
            result.summon_items_[slot] = MonsterDefinition::parse(item_id, record);
        } else if (record.size() >= 9) {
            result.battle_items_[slot] =
                BattleItemDefinition::parse(item_id, record);
        }
    }
    for (std::size_t index = 0; index < result.special_item_counts_.size(); ++index) {
        result.special_item_counts_[index] = state.u16(0x3e6 + index * 2U);
    }
    for (std::size_t ability_id = 0;
         ability_id < result.composite_effects_.size(); ++ability_id) {
        result.composite_effects_[ability_id] = BattleCompositeEffect::parse(
            static_cast<std::uint16_t>(ability_id), items);
    }
    result.critical_countdown_ = state.u16(0x49e);
    if (result.critical_countdown_ == 0) result.critical_countdown_ = 20;

    result.monster_definitions_.reserve(encounter.definition_slots.size());
    result.monsters_.reserve(encounter.definition_slots.size());
    result.monster_ai_.reserve(encounter.definition_slots.size());
    result.monster_fled_.reserve(encounter.definition_slots.size());
    for (const auto definition_slot : encounter.definition_slots) {
        if (definition_slot >= encounter.monster_definition_ids.size()) {
            throw std::runtime_error("ORC formation selects an invalid definition slot");
        }
        const auto definition_id = encounter.monster_definition_ids[definition_slot];
        const auto record_index = static_cast<std::size_t>(definition_id) + 2U;
        if (record_index >= items.entry_count()) {
            throw std::runtime_error("ORC formation references a missing ITEM definition");
        }
        const auto definition =
            MonsterDefinition::parse(definition_id, items.entry(record_index));
        result.monster_definitions_.push_back(definition);
        result.monsters_.push_back({definition.hit_points, definition.hit_points,
                                    definition.resistance_flags, {},
                                    definition.physical_attack, definition.evasion});
        result.monster_ai_.push_back({
            definition.hit_points,
            definition.hit_points,
            definition.level,
            definition.ai_type,
            definition.primary_ability_chance,
            definition.generic_ability,
            definition.healing_ability,
            definition.secondary_ability_chance,
            definition.special_ability_a,
            definition.ability_points,
            definition.special_ability_b,
            false,
        });
        result.monster_fled_.push_back(false);
    }
    return result;
}

BattleOutcome BattleSession::outcome() const noexcept {
    if (escaped_) return BattleOutcome::escaped;
    const auto monster_living = std::any_of(
        monsters_.begin(), monsters_.end(),
        [](const MonsterBattleState& monster) { return monster.hit_points != 0; });
    if (!monster_living) return BattleOutcome::victory;
    for (std::size_t index = 0; index < party_count_; ++index) {
        if (party_[index].living()) return BattleOutcome::ongoing;
    }
    return BattleOutcome::defeat;
}

BattleSessionRewards BattleSession::rewards() const noexcept {
    BattleSessionRewards result;
    for (std::size_t index = 0; index < monster_definitions_.size(); ++index) {
        if (monster_fled_[index]) continue;
        result.experience = static_cast<std::uint16_t>(
            result.experience + monster_definitions_[index].experience_reward);
        result.money = static_cast<std::uint16_t>(
            result.money + monster_definitions_[index].money_reward);
    }
    return result;
}

bool BattleSession::try_grant_encounter_capture(
    std::optional<std::uint16_t> definition_id, const BattleRandom& random) {
    if (outcome() != BattleOutcome::victory || !definition_id ||
        *definition_id == 0 || inventory_.back() != 0) {
        return false;
    }
    if (!random) {
        throw std::invalid_argument("missing FIG encounter-capture random source");
    }
    const auto draw = random(3);
    if (draw >= 3) {
        throw std::runtime_error(
            "encounter-capture random source returned an out-of-range value");
    }
    if (draw != 0) return false;
    inventory_.back() = *definition_id;
    return true;
}

std::size_t BattleSession::first_living_monster(std::size_t preferred) const noexcept {
    if (preferred < monsters_.size() && monsters_[preferred].hit_points != 0) {
        return preferred;
    }
    const auto found = std::find_if(
        monsters_.begin(), monsters_.end(),
        [](const MonsterBattleState& monster) { return monster.hit_points != 0; });
    return found == monsters_.end()
               ? no_target
               : static_cast<std::size_t>(found - monsters_.begin());
}

std::size_t BattleSession::first_living_player(std::size_t preferred) const noexcept {
    if (preferred < party_count_ && party_[preferred].living()) return preferred;
    for (std::size_t index = 0; index < party_count_; ++index) {
        if (party_[index].living()) return index;
    }
    return no_target;
}

void BattleSession::finish_player_turn(
    std::size_t actor, std::vector<BattleSessionEvent>& events) {
    auto state = party_[actor].ability_target();
    state.special_status_turns = player_special_status_turns_[actor];
    state.buff_turns = player_buff_turns_[actor];
    const auto status = advance_player_turn_status(state);
    party_[actor].hit_points = state.hit_points;
    party_[actor].status_bits = state.status_bits;
    party_[actor].physical_attack = state.physical_attack;
    party_[actor].physical_defense = state.physical_defense;
    party_[actor].speed = state.speed;
    party_[actor].evasion = state.evasion;
    player_special_status_turns_[actor] = state.special_status_turns;
    player_buff_turns_[actor] = state.buff_turns;
    if (status.expired_buff_mask != 0 || status.recovered_status_mask != 0) {
        BattleSessionEvent event;
        event.kind = BattleEventKind::status_expired;
        event.source = actor;
        event.target = actor;
        event.expired_player_buff_mask = status.expired_buff_mask;
        event.recovered_player_status_mask = status.recovered_status_mask;
        events.push_back(event);
    }
}

void BattleSession::add_player_death_reaction(
    std::vector<BattleSessionEvent>& events, const BattleRandom& random) {
    // FIG 2293 consumes the global death flag once after the complete enemy
    // action, forces the next player critical countdown to one, then samples
    // exactly one party slot. It does not reroll if that actor is dead or
    // otherwise incapacitated; 0694 simply suppresses the "可惡！" card.
    critical_countdown_ = 1;
    if (party_count_ == 0) return;
    const auto actor = static_cast<std::size_t>(random(
        static_cast<std::uint16_t>(party_count_)));
    if (actor >= party_count_ || (party_[actor].status_bits & 0x2c7eU) != 0) {
        return;
    }
    BattleSessionEvent event;
    event.kind = BattleEventKind::death_reaction;
    event.source = actor;
    event.target = actor;
    events.push_back(event);
}

void BattleSession::compact_inventory() noexcept {
    // FIG's item-removal tail runs the same stable 50-word compactor used by
    // the RPG inventory.  This happens after the complete initiative list has
    // resolved, so the physical item-slot indices reserved by the other party
    // commands remain valid for the current round while the next command page
    // starts at a packed slot zero.  Keep the parsed definition arrays in lock
    // step with their item words.
    std::size_t destination = 0;
    for (std::size_t source = 0; source < inventory_.size(); ++source) {
        if (inventory_[source] == 0) continue;
        if (destination != source) {
            inventory_[destination] = inventory_[source];
            battle_items_[destination] = std::move(battle_items_[source]);
            summon_items_[destination] = std::move(summon_items_[source]);
        }
        ++destination;
    }
    for (; destination < inventory_.size(); ++destination) {
        inventory_[destination] = 0;
        battle_items_[destination].reset();
        summon_items_[destination].reset();
    }
}

BattleRoundResult BattleSession::play_round(
    const std::array<PlayerBattleCommand, 4>& commands,
    const BattleAbilityDatabase& abilities, const BattleRandom& random) {
    BattleRoundResult result;
    result.outcome = outcome();
    if (result.outcome != BattleOutcome::ongoing) return result;
    bool inventory_removed = false;

    // FIG 2bb5/2bd9 redraws every party card before command collection. For
    // each living actor that presentation routine also owns gameplay bit
    // 1000h: clear it, then set it when HP is at or below one quarter. The
    // ordinary flee formula later reads this bit at 09f3, so it cannot be
    // treated as presentation-only state.
    for (std::size_t index = 0; index < party_count_; ++index) {
        auto& member = party_[index];
        if ((member.status_bits & 0x2000U) != 0) continue;
        member.status_bits = static_cast<std::uint16_t>(
            member.status_bits & ~0x1000U);
        if (member.hit_points <= (member.maximum_hit_points >> 2U)) {
            member.status_bits = static_cast<std::uint16_t>(
                member.status_bits | 0x1000U);
        }
    }

    std::array<InitiativeStats, 4> actor_initiative{};
    for (std::size_t index = 0; index < party_.size(); ++index) {
        actor_initiative[index] = party_[index].initiative();
    }
    std::vector<InitiativeStats> monster_initiative;
    monster_initiative.reserve(monsters_.size() + summoned_allies_.size());
    for (std::size_t index = 0; index < monsters_.size(); ++index) {
        monster_initiative.push_back(
            {monster_definitions_[index].initiative_range,
             monster_definitions_[index].speed});
    }
    // Allies summoned during this round are appended only after initiative has
    // already been rolled, matching FIG's pending +31b9 count. Existing allies
    // are packed after the encounter enemies at the start of the next round.
    for (const auto& ally : summoned_allies_) {
        monster_initiative.push_back(
            {ally.definition.initiative_range, ally.definition.speed});
    }
    result.turn_order = roll_turn_order(actor_initiative, monster_initiative, random);

    for (std::size_t turn_position = 0;
         turn_position < result.turn_order.size(); ++turn_position) {
        const auto& turn = result.turn_order[turn_position];
        if (outcome() != BattleOutcome::ongoing) break;
        if (!turn.monster) {
            const auto actor = static_cast<std::size_t>(turn.index);
            if (actor >= party_count_ || !party_[actor].living()) {
                result.events.push_back(
                    {BattleEventKind::skipped, false, actor, true, 0});
                continue;
            }
            // FIG 1000:0694 suppresses the queued action when any of these
            // incapacitating status bits are present, then still performs the
            // normal post-player-turn timer cleanup at 0c41.
            if ((party_[actor].status_bits & 0x2c7eU) != 0) {
                result.events.push_back(
                    {BattleEventKind::skipped, false, actor, true, 0});
                finish_player_turn(actor, result.events);
                continue;
            }
            const auto& command = commands[actor];
            const auto add_missing_medium = [&](std::uint16_t presentation_id,
                                                std::uint16_t effect_code,
                                                bool target_is_monster,
                                                std::size_t target) {
                BattleSessionEvent event;
                event.kind = BattleEventKind::missing_medium;
                event.source = actor;
                event.target_is_monster = target_is_monster;
                event.target = target;
                event.ability_id = presentation_id;
                event.effect_code = effect_code;
                result.events.push_back(event);
            };
            // Both learned abilities and type-10 ITEM wrappers enter the same
            // FIG effect table.  Keep the state-bearing tactical entries in a
            // single adapter so composite selector 6b can dispatch them from
            // either call path while retaining the caller's presentation id.
            const auto apply_tactical = [&](std::uint16_t effect_code,
                                            std::size_t target,
                                            MonsterBattleState* monster,
                                            std::uint16_t base_attack,
                                            std::uint16_t base_evasion,
                                            std::uint16_t presentation_id) {
                std::array<PlayerBattleState, 4> player_states{};
                for (std::size_t index = 0; index < party_count_; ++index) {
                    player_states[index] = party_[index].ability_target();
                    player_states[index].special_status_turns =
                        player_special_status_turns_[index];
                    player_states[index].buff_turns = player_buff_turns_[index];
                }
                const auto applied = apply_player_tactical_effect(
                    effect_code, party_[actor].level, actor, target,
                    std::span<PlayerBattleState>(player_states).first(party_count_),
                    monster, base_attack, base_evasion, abilities, random);
                if (!applied.supported) return false;
                for (std::size_t index = 0; index < party_count_; ++index) {
                    party_[index].hit_points = player_states[index].hit_points;
                    party_[index].status_bits = player_states[index].status_bits;
                    party_[index].physical_attack =
                        player_states[index].physical_attack;
                    party_[index].physical_defense =
                        player_states[index].physical_defense;
                    party_[index].speed = player_states[index].speed;
                    party_[index].evasion = player_states[index].evasion;
                    player_special_status_turns_[index] =
                        player_states[index].special_status_turns;
                    player_buff_turns_[index] = player_states[index].buff_turns;
                }
                add_ability_events(result.events,
                                   BattleEventKind::player_ability,
                                   false, actor, applied.target_is_monster,
                                   presentation_id, effect_code,
                                   applied.targets, 0,
                                   applied.removed_monster_buff_mask);
                return true;
            };
            // FIG 57f2 dispatches ITEM[0x8c + ability_id] bytes +9/+0a and
            // skips the second byte only when the selected monster has died.
            // Learned abilities supply their mapped ITEM record; 1138 stores
            // item_id-8c for every direct item type, which maps straight back
            // to that item's own +9/+0a bytes (including ids below 8ch via
            // 16-bit wraparound).
            const auto apply_composite = [&](
                std::array<std::uint8_t, 2> nested,
                std::uint16_t presentation_id,
                std::size_t preferred_target) {
                const auto target_index = first_living_monster(preferred_target);
                if (target_index == no_target) return false;
                const auto party_target = preferred_target < party_count_
                                              ? preferred_target
                                              : actor;
                for (const auto effect_code : nested) {
                    if (monsters_[target_index].hit_points == 0) break;
                    if (const auto medium = fig_required_medium(effect_code);
                        medium && !battle_media_[*medium]) {
                        // 58fa discards only the selected visual handler's
                        // return address. A 6b wrapper therefore continues to
                        // its second nested selector and remains a successful,
                        // paid command even when one or both handlers fail.
                        add_missing_medium(presentation_id, effect_code, true,
                                           target_index);
                        continue;
                    }
                    if (apply_tactical(
                            effect_code,
                            effect_code == 0x61 ? target_index : party_target,
                            &monsters_[target_index],
                            monster_definitions_[target_index].physical_attack,
                            monster_definitions_[target_index].evasion,
                            presentation_id)) {
                        continue;
                    }
                    if (effect_code <= 0x30U) {
                        std::array<PlayerSupportState, 4> support_states{};
                        for (std::size_t index = 0; index < party_count_; ++index) {
                            support_states[index] = party_[index].support_target();
                        }
                        const auto applied = apply_player_support_effect(
                            effect_code, actor, party_target,
                            std::span<PlayerSupportState>(support_states).first(
                                party_count_),
                            &player_support_runtime_);
                        if (!applied.supported) return false;
                        for (std::size_t index = 0; index < party_count_; ++index) {
                            party_[index].apply_support_target(support_states[index]);
                        }
                        add_ability_events(result.events,
                                           BattleEventKind::player_ability,
                                           false, actor, false, presentation_id,
                                           effect_code, applied.targets);
                        continue;
                    }
                    const auto applied = apply_player_ability_effect(
                        effect_code, party_[actor].level, target_index, monsters_,
                        abilities, random);
                    if (!applied.supported) return false;
                    for (std::size_t index = 0; index < monsters_.size(); ++index) {
                        monster_ai_[index].hit_points = monsters_[index].hit_points;
                    }
                    add_ability_events(result.events,
                                       BattleEventKind::player_ability,
                                       false, actor, true, presentation_id,
                                       effect_code, applied.targets);
                }
                return true;
            };
            if (command.kind == PlayerCommandKind::escape) {
                bool succeeded = false;
                if (random_encounter_rules_) {
                    const auto actor_level = party_[actor].level;
                    const auto monster_level = monster_definitions_.empty()
                                                   ? std::uint16_t{}
                                                   : monster_definitions_.front().level;
                    if (actor_level >= static_cast<std::uint16_t>(monster_level + 4U)) {
                        succeeded = true;
                    } else {
                        // FIG 09f3 uses the sorted-turn cursor + 2 when status
                        // bit 1000 is set, otherwise a fixed modulus of ten.
                        const auto modulus = (party_[actor].status_bits & 0x1000U) != 0
                                                 ? static_cast<std::uint16_t>(
                                                       turn_position + 3U)
                                                 : std::uint16_t{10};
                        succeeded = random(modulus) < 2;
                        if (!succeeded && escape_attempt_countdown_ != 0) {
                            --escape_attempt_countdown_;
                            succeeded = escape_attempt_countdown_ == 0;
                        }
                    }
                }
                if (succeeded) {
                    escaped_ = true;
                    result.events.push_back({
                        BattleEventKind::player_escaped, false, actor, false, actor,
                    });
                } else {
                    result.events.push_back({
                        BattleEventKind::escape_failed, false, actor, false, actor,
                    });
                    finish_player_turn(actor, result.events);
                }
                continue;
            }
            if (command.kind == PlayerCommandKind::skip) {
                result.events.push_back(
                    {BattleEventKind::skipped, false, actor, true, 0});
                finish_player_turn(actor, result.events);
                continue;
            }
            // BattleCommandMenu expands FIG's automatic collection mode into
            // basic attacks. Treat an externally supplied pseudo-command as
            // invalid rather than silently assigning invented semantics.
            if (command.kind == PlayerCommandKind::automatic) {
                result.events.push_back(
                    {BattleEventKind::invalid_command, false, actor, true, 0});
                finish_player_turn(actor, result.events);
                continue;
            }
            if (command.kind == PlayerCommandKind::basic_attack) {
                const auto target_index = first_living_monster(command.target);
                if (target_index == no_target) break;
                auto target = MonsterTargetState{
                    monsters_[target_index].hit_points,
                    monster_definitions_[target_index].physical_defense,
                    monsters_[target_index].evasion,
                    monster_definitions_[target_index].resistance_flags[0] == 1,
                    true,
                };
                const auto attack = player_basic_attack(
                    party_[actor].attack_stats(), target, critical_countdown_, random);
                monsters_[target_index].hit_points = target.hit_points;
                monster_ai_[target_index].hit_points = target.hit_points;
                result.events.push_back({
                    BattleEventKind::player_attack, false, actor, true, target_index,
                    0, attack.damage, 0, attack.critical, attack.evaded, false,
                    attack.defeated,
                });
                finish_player_turn(actor, result.events);
                continue;
            }

            if (command.kind == PlayerCommandKind::capture) {
                const auto target_index = first_living_monster(command.target);
                if (target_index == no_target) break;
                const auto first_level = party_[0].level;
                const auto monster_level = monster_definitions_[target_index].level;
                const auto lower_level = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(first_level + 7U) - 12U);
                const auto level_in_range =
                    static_cast<std::uint16_t>(first_level + 7U) >= monster_level;
                const auto weak_enough =
                    lower_level >= monster_level ||
                    monsters_[target_index].hit_points <=
                        static_cast<std::uint16_t>(
                            monsters_[target_index].maximum_hit_points >> 2U);
                // FIG 0e10 rejects capture while +3e4 contains the first
                // summoned-monster item, even when every level/HP test passes.
                if (random_encounter_rules_ && summoned_allies_.empty() &&
                    inventory_.back() == 0 &&
                    level_in_range && weak_enough) {
                    inventory_.back() = monster_definitions_[target_index].id;
                    monsters_[target_index].hit_points = 0;
                    monster_ai_[target_index].hit_points = 0;
                    BattleSessionEvent event;
                    event.kind = BattleEventKind::monster_captured;
                    event.source = actor;
                    event.target_is_monster = true;
                    event.target = target_index;
                    event.defeated = true;
                    result.events.push_back(event);
                } else {
                    result.events.push_back({
                        BattleEventKind::capture_failed, false, actor, true,
                        target_index,
                    });
                }
                finish_player_turn(actor, result.events);
                continue;
            }

            if (command.kind == PlayerCommandKind::item) {
                const auto slot = command.item_slot;
                if (slot >= inventory_.size() || inventory_[slot] == 0) {
                    result.events.push_back(
                        {BattleEventKind::invalid_command, false, actor, false,
                         command.target});
                    continue;
                }

                if (summon_items_[slot]) {
                    const auto& definition = *summon_items_[slot];
                    const auto cost = static_cast<std::uint16_t>(
                        definition.level * 2U);
                    if (party_[actor].secondary_points <= cost) {
                        result.events.push_back({
                            BattleEventKind::invalid_command, false, actor,
                            false, command.target, definition.id,
                        });
                        continue;
                    }
                    SummonedAlly ally;
                    ally.item_id = definition.id;
                    ally.summon_slot = summoned_allies_.size();
                    ally.definition = definition;
                    ally.battle = {
                        definition.hit_points, definition.hit_points,
                        definition.resistance_flags, {},
                        definition.physical_attack, definition.evasion,
                    };
                    ally.ai = {
                        definition.hit_points,
                        definition.hit_points,
                        definition.level,
                        definition.ai_type,
                        definition.primary_ability_chance,
                        definition.generic_ability,
                        definition.healing_ability,
                        definition.secondary_ability_chance,
                        definition.special_ability_a,
                        definition.ability_points,
                        definition.special_ability_b,
                        false,
                    };
                    party_[actor].secondary_points = static_cast<std::uint16_t>(
                        party_[actor].secondary_points - cost);
                    std::size_t installed_slot{};
                    if (summoned_allies_.size() >= 2) {
                        // 5d24 lets the player select the occupied packed slot.
                        // The displaced captured monster is written back into
                        // the same inventory slot that supplied the newcomer.
                        installed_slot = std::min<std::size_t>(command.target, 1U);
                        auto displaced = std::move(summoned_allies_[installed_slot]);
                        inventory_[slot] = displaced.item_id;
                        summon_items_[slot] = displaced.definition;
                        ally.summon_slot = installed_slot;
                        summoned_allies_[installed_slot] = std::move(ally);
                    } else {
                        installed_slot = summoned_allies_.size();
                        ally.summon_slot = installed_slot;
                        summoned_allies_.push_back(std::move(ally));
                        inventory_[slot] = 0;
                        inventory_removed = true;
                        summon_items_[slot].reset();
                        battle_items_[slot].reset();
                    }
                    result.events.push_back({
                        BattleEventKind::ally_summoned, false, actor, false,
                        installed_slot,
                        summoned_allies_[installed_slot].item_id,
                    });
                    finish_player_turn(actor, result.events);
                    continue;
                }

                if (!battle_items_[slot] ||
                    (battle_items_[slot]->type == 0x10 &&
                     player_special_status_turns_[actor][3] != 0)) {
                    result.events.push_back(
                        {BattleEventKind::invalid_command, false, actor, false,
                         command.target});
                    continue;
                }
                const auto& item = *battle_items_[slot];
                std::uint16_t item_resource_cost = 0;
                auto item_ability_id = abilities.abilities().size();
                if (item.type == 0x10) {
                    item_ability_id = item.id >= 0x8cU
                                          ? static_cast<std::size_t>(
                                                item.id - 0x8cU)
                                          : abilities.abilities().size();
                    if (item.id < 0x8cU ||
                        item_ability_id >= abilities.abilities().size()) {
                        result.events.push_back({
                            BattleEventKind::invalid_command, false, actor,
                            item.targets_monster(), command.target, item.id,
                        });
                        continue;
                    }
                    item_resource_cost = abilities.ability(item_ability_id).cost;
                    if (party_[actor].ability_points < item_resource_cost) {
                        result.events.push_back({
                            BattleEventKind::invalid_command, false, actor,
                            item.targets_monster(), command.target, item.id,
                        });
                        continue;
                    }
                }
                // Item 237 is the type-10 wrapper around effect 47. The
                // original effect discards the caller/resource/consumption
                // return frames, exactly like the learned version: no AP and
                // no item are consumed, fixed battles reject it, random ones
                // end immediately.
                if (item.effect_code == 0x47) {
                    BattleSessionEvent escape_event;
                    escape_event.kind = random_encounter_rules_
                                            ? BattleEventKind::player_escaped
                                            : BattleEventKind::escape_failed;
                    escape_event.source = actor;
                    escape_event.target = actor;
                    escape_event.ability_id = item.id;
                    escape_event.effect_code = item.effect_code;
                    if (random_encounter_rules_) {
                        escaped_ = true;
                        result.events.push_back(escape_event);
                    } else {
                        result.events.push_back(escape_event);
                        finish_player_turn(actor, result.events);
                    }
                    continue;
                }
                if (item.effect_code != 0x6b) {
                    if (const auto medium = fig_required_medium(item.effect_code);
                        medium && !battle_media_[*medium]) {
                        auto target_index = command.target;
                        if (item.targets_monster()) {
                            target_index = first_living_monster(command.target);
                            if (target_index == no_target) break;
                        } else if (target_index >= party_count_) {
                            target_index = actor;
                        }
                        add_missing_medium(item.id, item.effect_code,
                                           item.targets_monster(), target_index);
                        // The dispatcher returns normally to 1138 after 58fa:
                        // embedded AP and the inventory object are still paid.
                        if (item.type == 0x10) {
                            party_[actor].ability_points =
                                static_cast<std::uint16_t>(
                                    party_[actor].ability_points -
                                    item_resource_cost);
                        }
                        if (item.consumed_on_use()) {
                            inventory_[slot] = 0;
                            inventory_removed = true;
                            battle_items_[slot].reset();
                        }
                        finish_player_turn(actor, result.events);
                        continue;
                    }
                }
                bool applied = false;
                if (item.effect_code == 0x6b) {
                    applied = apply_composite(
                        {item.first_composite_effect,
                         item.second_composite_effect},
                        item.id, command.target);
                } else if (item.effect_code == 0) {
                    applied = true;  // dispatch entry zero is a literal RET
                    result.events.push_back({
                        BattleEventKind::player_ability, false, actor,
                        item.targets_monster(), command.target, item.id,
                    });
                } else if (item.targets_monster()) {
                    const auto target_index = first_living_monster(command.target);
                    if (target_index == no_target) break;
                    // 1138 enters the same DS:2bbd dispatcher as a learned
                    // ability. Direct tactical items such as item 211/effect
                    // 61 must therefore clear monster buffs before falling
                    // through to the ordinary damage/status handlers.
                    applied = apply_tactical(
                        item.effect_code, target_index,
                        &monsters_[target_index],
                        monster_definitions_[target_index].physical_attack,
                        monster_definitions_[target_index].evasion,
                        item.id);
                    if (!applied) {
                        const auto effect = apply_player_ability_effect(
                            item.effect_code, party_[actor].level, target_index,
                            monsters_, abilities, random);
                        applied = effect.supported;
                        if (applied) {
                            for (std::size_t index = 0; index < monsters_.size(); ++index) {
                                monster_ai_[index].hit_points = monsters_[index].hit_points;
                            }
                            add_ability_events(result.events,
                                               BattleEventKind::player_ability,
                                               false, actor, true, item.id,
                                               item.effect_code,
                                               effect.targets);
                        }
                    }
                } else {
                    const auto target_index =
                        command.target < party_count_ ? command.target : actor;
                    // Direct item selectors 62/63/66..69 are tactical too;
                    // previously only nested 6b wrappers reached this path,
                    // leaving shipped items 186/204/226 as invalid commands.
                    applied = apply_tactical(
                        item.effect_code, target_index, nullptr, 0, 0,
                        item.id);
                    if (!applied) {
                        if (item.effect_code <= 0x30U) {
                            std::array<PlayerSupportState, 4> support_states{};
                            for (std::size_t index = 0; index < party_count_; ++index) {
                                support_states[index] = party_[index].support_target();
                            }
                            const auto effect = apply_player_support_effect(
                                item.effect_code, actor, target_index,
                                std::span<PlayerSupportState>(support_states).first(
                                    party_count_),
                                &player_support_runtime_);
                            applied = effect.supported;
                            if (applied) {
                                for (std::size_t index = 0; index < party_count_; ++index) {
                                    party_[index].apply_support_target(
                                        support_states[index]);
                                }
                                add_ability_events(result.events,
                                                   BattleEventKind::player_ability,
                                                   false, actor, false, item.id,
                                                   item.effect_code,
                                                   effect.targets);
                            }
                        } else {
                            // Target bits control the selection page, but 1138
                            // still dispatches the effect selector verbatim.
                            // Shipped targetless environment items (31/3b..3f)
                            // and odd records such as wine/5e would otherwise
                            // be misrouted into the 01..30 support table.
                            const auto monster_target =
                                first_living_monster(command.target);
                            if (monster_target == no_target) break;
                            const auto effect = apply_player_ability_effect(
                                item.effect_code, party_[actor].level,
                                monster_target, monsters_, abilities, random);
                            applied = effect.supported;
                            if (applied) {
                                for (std::size_t index = 0;
                                     index < monsters_.size(); ++index) {
                                    monster_ai_[index].hit_points =
                                        monsters_[index].hit_points;
                                }
                                add_ability_events(
                                    result.events,
                                    BattleEventKind::player_ability,
                                    false, actor, !effect.targets.empty(),
                                    item.id, item.effect_code, effect.targets);
                            }
                        }
                    }
                }
                if (!applied) {
                    result.events.push_back(
                        {BattleEventKind::invalid_command, false, actor,
                         item.targets_monster(), command.target, item.id});
                    continue;
                }
                if (item.type == 0x10 && item.effect_code != 0x47) {
                    party_[actor].ability_points = static_cast<std::uint16_t>(
                        party_[actor].ability_points - item_resource_cost);
                }
                if (item.consumed_on_use()) {
                    inventory_[slot] = 0;
                    inventory_removed = true;
                    battle_items_[slot].reset();
                }
                finish_player_turn(actor, result.events);
                continue;
            }

            if (command.kind != PlayerCommandKind::ability ||
                command.ability_id >= abilities.abilities().size() ||
                !knows_ability(party_[actor], command.ability_id)) {
                result.events.push_back(
                    {BattleEventKind::invalid_command, false, actor, true, command.target,
                     command.ability_id});
                continue;
            }
            const auto& ability = abilities.ability(command.ability_id);
            const auto resource_class =
                static_cast<std::uint8_t>((ability.target_flags >> 8U) & 0x0fU);
            std::uint16_t* resource = nullptr;
            if (resource_class == 1 || resource_class == 4) {
                resource = &party_[actor].ability_points;
            } else if (resource_class == 2 || resource_class == 3) {
                resource = &party_[actor].secondary_points;
            }
            const auto class_five = resource_class == 5;
            const auto affordable = class_five
                                        ? has_class_five_resources(
                                              special_item_counts_, ability.cost)
                                        : resource != nullptr &&
                                              *resource >= ability.cost;
            if ((ability.target_flags & 0x4000U) != 0 || !affordable) {
                result.events.push_back(
                    {BattleEventKind::invalid_command, false, actor,
                     (ability.target_flags & 0x2000U) != 0, command.target,
                     command.ability_id});
                continue;
            }

            // Effect 47 deliberately discards the dispatcher/caller return
            // frames before FIG's resource-subtraction routine. It therefore
            // costs nothing: random encounters end immediately, while fixed
            // story encounters consume the turn and reject escape.
            if (ability.effect_code == 0x47) {
                BattleSessionEvent escape_event;
                escape_event.kind = random_encounter_rules_
                                        ? BattleEventKind::player_escaped
                                        : BattleEventKind::escape_failed;
                escape_event.source = actor;
                escape_event.target = actor;
                escape_event.ability_id = command.ability_id;
                escape_event.effect_code = ability.effect_code;
                if (random_encounter_rules_) {
                    escaped_ = true;
                    result.events.push_back(escape_event);
                } else {
                    result.events.push_back(escape_event);
                    finish_player_turn(actor, result.events);
                }
                continue;
            }

            if (!class_five) {
                *resource = static_cast<std::uint16_t>(*resource - ability.cost);
            }
            const auto target_mode =
                static_cast<std::uint16_t>(ability.target_flags & 0x3000U);
            const auto finish_successful_ability = [&] {
                if (class_five) {
                    consume_class_five_resources(special_item_counts_, party_[actor],
                                                 command.ability_id, ability.cost);
                }
                finish_player_turn(actor, result.events);
            };
            if (ability.effect_code == 0x6b) {
                const auto target_index = first_living_monster(command.target);
                if (target_index == no_target ||
                    command.ability_id >= composite_effects_.size() ||
                    !composite_effects_[command.ability_id] ||
                    !apply_composite(
                        {composite_effects_[command.ability_id]->first_effect,
                         composite_effects_[command.ability_id]->second_effect},
                        command.ability_id, command.target)) {
                    if (!class_five) {
                        *resource = static_cast<std::uint16_t>(
                            *resource + ability.cost);
                    }
                    result.events.push_back({
                        BattleEventKind::invalid_command, false, actor,
                        true, command.target, command.ability_id,
                    });
                    continue;
                }
                finish_successful_ability();
                continue;
            }
            if (const auto medium =
                    fig_medium_from_target_flags(ability.target_flags);
                medium && !battle_media_[*medium]) {
                const auto targets_monster = (target_mode & 0x2000U) != 0;
                auto target_index = targets_monster
                                        ? first_living_monster(command.target)
                                        : (command.target < party_count_
                                               ? command.target
                                               : actor);
                if (targets_monster && target_index == no_target) break;
                add_missing_medium(command.ability_id, ability.effect_code,
                                   targets_monster, target_index);
                // Player 4338/58fa does not install a mediator requested by
                // the ability record's low target_flags byte.  It reports the
                // same paid failure as an effect-specific missing mediator;
                // only enemy 23b1 and captured-ally 1048 install this sprite.
                finish_successful_ability();
                continue;
            }
            if (const auto medium = fig_required_medium(ability.effect_code);
                medium && !battle_media_[*medium]) {
                const auto targets_monster = (target_mode & 0x2000U) != 0;
                auto target_index = targets_monster
                                        ? first_living_monster(command.target)
                                        : (command.target < party_count_
                                               ? command.target
                                               : actor);
                if (targets_monster && target_index == no_target) break;
                add_missing_medium(command.ability_id, ability.effect_code,
                                   targets_monster, target_index);
                // Resource subtraction follows the effect dispatcher in FIG;
                // 58fa's stack exit suppresses the effect, not command payment.
                finish_successful_ability();
                continue;
            }
            if (target_mode == 0) {
                if (apply_tactical(ability.effect_code, actor, nullptr, 0, 0,
                                   command.ability_id)) {
                    finish_successful_ability();
                    continue;
                }
                if (ability.effect_code <= 0x30) {
                    std::array<PlayerSupportState, 4> support_states{};
                    for (std::size_t index = 0; index < party_count_; ++index) {
                        support_states[index] = party_[index].support_target();
                    }
                    const auto applied = apply_player_support_effect(
                        ability.effect_code, actor, actor,
                        std::span<PlayerSupportState>(support_states).first(
                            party_count_),
                        &player_support_runtime_);
                    if (!applied.supported) {
                        if (!class_five) {
                            *resource = static_cast<std::uint16_t>(
                                *resource + ability.cost);
                        }
                        result.events.push_back({
                            BattleEventKind::invalid_command, false, actor,
                            false, actor, command.ability_id,
                        });
                        continue;
                    }
                    for (std::size_t index = 0; index < party_count_; ++index) {
                        party_[index].apply_support_target(support_states[index]);
                    }
                    add_ability_events(result.events,
                                       BattleEventKind::player_ability,
                                       false, actor, false, command.ability_id,
                                       ability.effect_code,
                                       applied.targets);
                } else {
                    const auto applied = apply_player_ability_effect(
                        ability.effect_code, party_[actor].level, 0, monsters_,
                        abilities, random);
                    if (!applied.supported) {
                        if (!class_five) {
                            *resource = static_cast<std::uint16_t>(
                                *resource + ability.cost);
                        }
                        result.events.push_back({
                            BattleEventKind::invalid_command, false, actor,
                            false, actor, command.ability_id,
                        });
                        continue;
                    }
                    add_ability_events(result.events,
                                       BattleEventKind::player_ability,
                                       false, actor, true, command.ability_id,
                                       ability.effect_code,
                                       applied.targets, 0, 0, false, false);
                }
                finish_successful_ability();
                continue;
            }
            if ((target_mode & 0x2000U) != 0) {
                const auto target_index = first_living_monster(command.target);
                if (target_index == no_target) break;
                if (apply_tactical(
                        ability.effect_code, target_index, &monsters_[target_index],
                        monster_definitions_[target_index].physical_attack,
                        monster_definitions_[target_index].evasion,
                        command.ability_id)) {
                    finish_successful_ability();
                    continue;
                }
                const auto applied = apply_player_ability_effect(
                    ability.effect_code, party_[actor].level, target_index,
                    monsters_, abilities, random);
                if (!applied.supported) {
                    if (!class_five) {
                        *resource = static_cast<std::uint16_t>(*resource + ability.cost);
                    }
                    result.events.push_back(
                        {BattleEventKind::invalid_command, false, actor, true, target_index,
                         command.ability_id});
                    continue;
                }
                for (std::size_t index = 0; index < monsters_.size(); ++index) {
                    monster_ai_[index].hit_points = monsters_[index].hit_points;
                }
                add_ability_events(result.events, BattleEventKind::player_ability,
                                   false, actor, true, command.ability_id,
                                   ability.effect_code,
                                   applied.targets);
                finish_successful_ability();
                continue;
            }

            const auto target_index = command.target < party_count_ ? command.target : actor;
            if (apply_tactical(ability.effect_code, target_index, nullptr, 0, 0,
                               command.ability_id)) {
                finish_successful_ability();
                continue;
            }
            std::array<PlayerSupportState, 4> support_states{};
            for (std::size_t index = 0; index < party_count_; ++index) {
                support_states[index] = party_[index].support_target();
            }
            const auto applied = apply_player_support_effect(
                ability.effect_code, actor, target_index,
                std::span<PlayerSupportState>(support_states).first(party_count_),
                &player_support_runtime_);
            if (!applied.supported) {
                if (!class_five) {
                    *resource = static_cast<std::uint16_t>(*resource + ability.cost);
                }
                result.events.push_back(
                    {BattleEventKind::invalid_command, false, actor, false, target_index,
                     command.ability_id});
                continue;
            }
            for (std::size_t index = 0; index < party_count_; ++index) {
                party_[index].apply_support_target(support_states[index]);
            }
            add_ability_events(result.events, BattleEventKind::player_ability,
                               false, actor, false, command.ability_id,
                               ability.effect_code,
                               applied.targets);
            finish_successful_ability();
            continue;
        }

        const auto monster_index = static_cast<std::size_t>(turn.index);
        if (monster_index >= monsters_.size()) {
            const auto ally_index = monster_index - monsters_.size();
            if (ally_index >= summoned_allies_.size()) {
                result.events.push_back(
                    {BattleEventKind::skipped, false, ally_index, true, 0});
                continue;
            }

            std::array<bool, 5> living_enemies{};
            for (std::size_t index = 0; index < monsters_.size(); ++index) {
                living_enemies[index] = monsters_[index].hit_points != 0;
            }
            auto& ally = summoned_allies_[ally_index];
            const auto may_leave = ally_index != 0 || summoned_allies_.size() == 1;
            const auto decision = choose_summoned_ally_action(
                ally.ai,
                std::span<const bool>(living_enemies).first(monsters_.size()),
                abilities, random, may_leave,
                ally.item_id == steadfast_summon_item_id);
            if (decision.action == MonsterAiAction::flee) {
                const auto item_id = ally.item_id;
                result.events.push_back({
                    BattleEventKind::ally_fled, false, ally_index, false,
                    ally_index, item_id,
                });
                summoned_allies_.erase(summoned_allies_.begin() +
                                       static_cast<std::ptrdiff_t>(ally_index));
                for (std::size_t index = ally_index;
                     index < summoned_allies_.size(); ++index) {
                    summoned_allies_[index].summon_slot = index;
                }
                continue;
            }
            if (decision.action == MonsterAiAction::skip || !decision.target) {
                result.events.push_back(
                    {BattleEventKind::skipped, false, ally_index, true, 0});
                continue;
            }

            const auto target_index = first_living_monster(*decision.target);
            if (target_index == no_target) break;
            if (decision.action == MonsterAiAction::basic_attack) {
                auto target = MonsterTargetState{
                    monsters_[target_index].hit_points,
                    monster_definitions_[target_index].physical_defense,
                    monsters_[target_index].evasion,
                    false,
                    true,
                };
                const auto attack = summoned_ally_basic_attack(
                    ally.battle.physical_attack, target, random);
                monsters_[target_index].hit_points = target.hit_points;
                monster_ai_[target_index].hit_points = target.hit_points;
                result.events.push_back({
                    BattleEventKind::ally_attack, false, ally_index, true,
                    target_index, 0, attack.damage, 0, false, attack.evaded,
                    false, attack.defeated,
                });
                continue;
            }

            if (decision.ability_id >= abilities.abilities().size()) {
                result.events.push_back({
                    BattleEventKind::skipped, false, ally_index, true,
                    target_index, decision.ability_id,
                });
                continue;
            }
            const auto& selected_ability = abilities.ability(decision.ability_id);
            const auto effect_code = selected_ability.effect_code;
            // Captured allies enter FIG 1048, not the enemy-only 26af path.
            // Before 1048 jumps through the ordinary player effect table it
            // repeats 23b1's 80h/40h/20h mediator test.  An absent requested
            // mediator is installed by SP049/5b41 and ends the action.  Unlike
            // the enemy generic path, the AP already paid by 22f3 is not
            // refunded and the selected ability is not rolled again.
            if (const auto medium =
                    fig_medium_from_target_flags(selected_ability.target_flags);
                medium && !battle_media_[*medium]) {
                battle_media_[*medium] = true;
                BattleSessionEvent event;
                event.kind = BattleEventKind::medium_summoned;
                event.source = ally_index;
                event.target = *medium;
                event.ability_id = decision.ability_id;
                event.effect_code = effect_code;
                event.source_is_summoned_ally = true;
                result.events.push_back(event);
                continue;
            }
            if (const auto medium = fig_required_medium(effect_code);
                medium && !battle_media_[*medium]) {
                BattleSessionEvent event;
                event.kind = BattleEventKind::missing_medium;
                event.source = ally_index;
                event.target_is_monster = true;
                event.target = target_index;
                event.ability_id = decision.ability_id;
                event.effect_code = effect_code;
                event.source_is_summoned_ally = true;
                result.events.push_back(event);
                continue;
            }
            bool applied = false;
            if (effect_code <= 0x30) {
                std::array<PlayerSupportState, 4> support_states{};
                for (std::size_t index = 0; index < party_count_; ++index) {
                    support_states[index] = party_[index].support_target();
                }
                const auto support = apply_player_support_effect(
                    effect_code, 0, 0,
                    std::span<PlayerSupportState>(support_states).first(
                        party_count_),
                    &player_support_runtime_);
                applied = support.supported;
                if (applied) {
                    for (std::size_t index = 0; index < party_count_; ++index) {
                        party_[index].apply_support_target(support_states[index]);
                    }
                    add_ability_events(
                        result.events, BattleEventKind::ally_ability, false,
                        ally_index, false, decision.ability_id, effect_code,
                        support.targets);
                }
            } else {
                std::array<PlayerBattleState, 4> player_states{};
                for (std::size_t index = 0; index < party_count_; ++index) {
                    player_states[index] = party_[index].ability_target();
                    player_states[index].special_status_turns =
                        player_special_status_turns_[index];
                    player_states[index].buff_turns = player_buff_turns_[index];
                }
                const auto tactical = apply_player_tactical_effect(
                    effect_code, party_[0].level, 0, target_index,
                    std::span<PlayerBattleState>(player_states).first(party_count_),
                    &monsters_[target_index],
                    monster_definitions_[target_index].physical_attack,
                    monster_definitions_[target_index].evasion,
                    abilities, random);
                if (tactical.supported) {
                    applied = true;
                    for (std::size_t index = 0; index < party_count_; ++index) {
                        party_[index].hit_points = player_states[index].hit_points;
                        party_[index].status_bits = player_states[index].status_bits;
                        party_[index].physical_attack =
                            player_states[index].physical_attack;
                        party_[index].physical_defense =
                            player_states[index].physical_defense;
                        party_[index].speed = player_states[index].speed;
                        party_[index].evasion = player_states[index].evasion;
                        player_special_status_turns_[index] =
                            player_states[index].special_status_turns;
                        player_buff_turns_[index] = player_states[index].buff_turns;
                    }
                    add_ability_events(
                        result.events, BattleEventKind::ally_ability, false,
                        ally_index, tactical.target_is_monster,
                        decision.ability_id, effect_code, tactical.targets, 0,
                        tactical.removed_monster_buff_mask);
                } else {
                    const auto effect = apply_player_ability_effect(
                        effect_code, party_[0].level, target_index,
                        monsters_, abilities, random);
                    applied = effect.supported;
                    if (applied) {
                        for (std::size_t index = 0; index < monsters_.size(); ++index) {
                            monster_ai_[index].hit_points =
                                monsters_[index].hit_points;
                        }
                        add_ability_events(
                            result.events, BattleEventKind::ally_ability, false,
                            ally_index, true, decision.ability_id,
                            effect_code,
                            effect.targets);
                    }
                }
            }
            if (!applied) {
                result.events.push_back({
                    BattleEventKind::skipped, false, ally_index, true,
                    target_index, decision.ability_id,
                });
            }
            continue;
        }
        if (monsters_[monster_index].hit_points == 0) {
            result.events.push_back(
                {BattleEventKind::skipped, true, monster_index, false, 0});
            continue;
        }
        const auto status = advance_monster_turn_status(
            monsters_[monster_index],
            monster_definitions_[monster_index].physical_attack,
            monster_definitions_[monster_index].evasion,
            party_[0].level, random);
        monster_ai_[monster_index].hit_points =
            monsters_[monster_index].hit_points;
        if (status.expired_buff_mask != 0 || status.expired_status_mask != 0) {
            BattleSessionEvent event;
            event.kind = BattleEventKind::status_expired;
            event.source_is_monster = true;
            event.source = monster_index;
            event.target_is_monster = true;
            event.target = monster_index;
            event.expired_monster_buff_mask = status.expired_buff_mask;
            event.expired_monster_status_mask = status.expired_status_mask;
            result.events.push_back(event);
        }
        if (status.periodic_triggered) {
            result.events.push_back({
                BattleEventKind::status_damage, false, 0, true, monster_index,
                0, status.periodic_damage, 0, false, false, false,
                status.defeated,
            });
        }
        if (status.skipped) {
            result.events.push_back(
                {BattleEventKind::skipped, true, monster_index, false, 0});
            continue;
        }
        auto& ai = monster_ai_[monster_index];
        ai.hit_points = monsters_[monster_index].hit_points;
        ai.magic_blocked = monsters_[monster_index].status_turns[4] != 0;
        std::array<bool, 4> living{};
        for (std::size_t index = 0; index < party_count_; ++index) {
            living[index] = party_[index].living();
        }
        const auto decision = choose_monster_action(
            ai, std::span<const bool>(living).first(party_count_), abilities, random,
            random_encounter_rules_, party_[0].level);
        monsters_[monster_index].hit_points = ai.hit_points;
        if (decision.action == MonsterAiAction::flee) {
            monsters_[monster_index].hit_points = 0;
            ai.hit_points = 0;
            monster_fled_[monster_index] = true;
            result.events.push_back({
                BattleEventKind::monster_fled, true, monster_index, true,
                monster_index,
            });
            continue;
        }
        if (decision.action == MonsterAiAction::flee_failed) {
            result.events.push_back({
                BattleEventKind::monster_escape_failed, true, monster_index,
                true, monster_index,
            });
            continue;
        }
        if (decision.action == MonsterAiAction::skip) {
            result.events.push_back(
                {BattleEventKind::skipped, true, monster_index, false, 0});
            continue;
        }
        if (decision.action == MonsterAiAction::heal_self) {
            result.events.push_back({
                BattleEventKind::monster_heal, true, monster_index, true,
                monster_index, decision.ability_id, 0, decision.power,
            });
            continue;
        }
        const auto target_index = first_living_player(decision.target.value_or(0));
        if (target_index == no_target) break;
        if (decision.action == MonsterAiAction::basic_attack) {
            auto target = party_[target_index].physical_target();
            const auto attack = monster_basic_attack(
                {monster_definitions_[monster_index].level,
                 monsters_[monster_index].physical_attack,
                 monster_definitions_[monster_index].status_strength},
                target, random);
            party_[target_index].hit_points = target.hit_points;
            party_[target_index].status_bits = target.status_bits;
            result.events.push_back({
                BattleEventKind::monster_attack, true, monster_index, false,
                target_index, 0, attack.damage, 0, false, attack.evaded, false,
                attack.defeated,
            });
            if (attack.defeated) {
                add_player_death_reaction(result.events, random);
            }
            continue;
        }

        const auto& selected_ability = abilities.ability(decision.ability_id);
        // Only the generic-ability branch at 227c enters 23b1. Its low record
        // byte can request one of three persistent mediator sprites. When the
        // sprite is absent, FIG refunds the AP already spent by 22f3, presents
        // the summon, and ends the monster's turn without applying the effect.
        if (decision.action == MonsterAiAction::generic_ability) {
            if (const auto medium =
                    fig_medium_from_target_flags(selected_ability.target_flags);
                medium && !battle_media_[*medium]) {
                ai.ability_points = static_cast<std::uint16_t>(
                    ai.ability_points + selected_ability.cost);
                battle_media_[*medium] = true;
                BattleSessionEvent event;
                event.kind = BattleEventKind::medium_summoned;
                event.source_is_monster = true;
                event.source = monster_index;
                event.target = *medium;
                event.ability_id = decision.ability_id;
                event.effect_code = selected_ability.effect_code;
                result.events.push_back(event);
                continue;
            }
        }
        // Special-A/B actions take 26af instead. IDs 35h/43h/53h consume the
        // matching mediator when present and return before normal resolution;
        // if absent they fall through and retain their ordinary effect.
        if (decision.action == MonsterAiAction::special_ability) {
            if (const auto medium = fig_dismissed_medium(decision.ability_id);
                medium && battle_media_[*medium]) {
                battle_media_[*medium] = false;
                BattleSessionEvent event;
                event.kind = BattleEventKind::medium_dismissed;
                event.source_is_monster = true;
                event.source = monster_index;
                event.target = *medium;
                event.ability_id = decision.ability_id;
                event.effect_code = selected_ability.effect_code;
                result.events.push_back(event);
                continue;
            }
        }

        std::array<PlayerBattleState, 4> player_states{};
        for (std::size_t index = 0; index < party_count_; ++index) {
            player_states[index] = party_[index].ability_target();
            player_states[index].special_status_turns =
                player_special_status_turns_[index];
            player_states[index].buff_turns = player_buff_turns_[index];
        }
        auto player_span = std::span<PlayerBattleState>(player_states).first(party_count_);
        if (decision.action == MonsterAiAction::special_ability) {
            const auto special = apply_prepaid_monster_special(
                decision.ability_id, decision.power, ai.ability_points, target_index,
                player_span, monsters_[monster_index], abilities, random);
            if (special.resolution == MonsterSpecialResolution::fallback_basic_attack) {
                auto target = party_[target_index].physical_target();
                const auto attack = monster_basic_attack(
                    {monster_definitions_[monster_index].level,
                     monsters_[monster_index].physical_attack,
                     monster_definitions_[monster_index].status_strength},
                    target, random);
                party_[target_index].hit_points = target.hit_points;
                party_[target_index].status_bits = target.status_bits;
                result.events.push_back({
                    BattleEventKind::monster_attack, true, monster_index, false,
                    target_index, 0, attack.damage, 0, false, attack.evaded, false,
                    attack.defeated,
                });
                if (attack.defeated) {
                    add_player_death_reaction(result.events, random);
                }
                continue;
            }
            for (std::size_t index = 0; index < party_count_; ++index) {
                party_[index].hit_points = player_states[index].hit_points;
                party_[index].status_bits = player_states[index].status_bits;
                player_special_status_turns_[index] =
                    player_states[index].special_status_turns;
                player_buff_turns_[index] = player_states[index].buff_turns;
            }
            if (special.resolution == MonsterSpecialResolution::silent_return) {
                // Preserve the enemy-action boundary so the frontend can
                // reproduce 20e7's bare preparation and 22e0's bare cleanup,
                // but mark that the body jumps directly to 2938's RET.
                BattleSessionEvent event{
                    BattleEventKind::monster_ability, true, monster_index,
                    false, target_index, decision.ability_id};
                event.effect_code = selected_ability.effect_code;
                event.monster_special_silent_return = true;
                result.events.push_back(event);
                continue;
            }
            add_ability_events(result.events, BattleEventKind::monster_ability,
                               true, monster_index, false, decision.ability_id,
                               selected_ability.effect_code,
                               special.targets,
                               special.removed_player_buff_mask);
            continue;
        }

        const auto applied = apply_prepaid_monster_ability(
            decision.ability_id, decision.power, target_index, player_span, abilities);
        for (std::size_t index = 0; index < party_count_; ++index) {
            party_[index].hit_points = player_states[index].hit_points;
            party_[index].status_bits = player_states[index].status_bits;
        }
        add_ability_events(result.events, BattleEventKind::monster_ability,
                           true, monster_index, false, decision.ability_id,
                           selected_ability.effect_code,
                           applied.targets, 0, 0, true);
        if (std::any_of(applied.targets.begin(), applied.targets.end(),
                        [](const AbilityTargetResult& target) {
                            return target.defeated;
                        })) {
            add_player_death_reaction(result.events, random);
        }
    }

    if (inventory_removed) compact_inventory();
    result.outcome = outcome();
    return result;
}

void BattleSession::store(SharedState& state) const {
    for (const auto& member : party_) member.store(state);
    auto inventory = inventory_;
    // FIG 0232 returns both still-present summon slots through +3e4 and runs
    // its stable compactor after each insertion. Filling the first gaps is the
    // same final state and keeps this presentation-neutral store const.
    for (const auto& ally : summoned_allies_) {
        const auto empty = std::find(inventory.begin(), inventory.end(), 0);
        if (empty == inventory.end()) break;
        *empty = ally.item_id;
    }
    for (std::size_t slot = 0; slot < inventory.size(); ++slot) {
        state.set_u16(0x382 + slot * 2U, inventory[slot]);
    }
    for (std::size_t index = 0; index < special_item_counts_.size(); ++index) {
        state.set_u16(0x3e6 + index * 2U, special_item_counts_[index]);
    }
    state.set_u16(0x49e, critical_countdown_);
}

}  // namespace swd2
