#pragma once

#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_ai.hpp"
#include "swd2/battle_composite_effect.hpp"
#include "swd2/battle_database.hpp"
#include "swd2/battle_effects.hpp"
#include "swd2/battle_item_definition.hpp"
#include "swd2/battle_party.hpp"
#include "swd2/monster_definition.hpp"
#include "swd2/script_archive.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <vector>

namespace swd2 {

enum class BattleOutcome { ongoing, victory, defeat, escaped };

// `automatic` is a command-collection mode from FIG 1000:1588 rather than a
// resolvable turn action. BattleCommandMenu expands it into basic attacks for
// every commandable actor before BattleSession sees the command array.
enum class PlayerCommandKind {
    skip,
    basic_attack,
    ability,
    item,
    capture,
    escape,
    automatic,
};

struct PlayerBattleCommand {
    PlayerCommandKind kind{PlayerCommandKind::basic_attack};
    std::uint16_t ability_id{};
    std::size_t target{};
    std::size_t item_slot{};
};

enum class BattleEventKind {
    skipped,
    invalid_command,
    player_attack,
    player_ability,
    monster_attack,
    monster_ability,
    monster_heal,
    status_damage,
    monster_fled,
    player_escaped,
    escape_failed,
    monster_captured,
    capture_failed,
    ally_summoned,
    ally_attack,
    ally_ability,
    ally_fled,
    status_expired,
    death_reaction,
    monster_escape_failed,
    medium_summoned,
    medium_dismissed,
    medium_dismissal_empty,
    missing_medium,
};

struct BattleSessionRewards {
    std::uint16_t experience{};
    std::uint16_t money{};
};

struct SummonedAlly {
    std::uint16_t item_id{};
    std::size_t summon_slot{};
    MonsterDefinition definition;
    MonsterBattleState battle;
    MonsterAiState ai;
};

// Presentation-neutral event emitted by a resolved round. A multi-target
// ability emits one event per affected target so every frontend can animate
// the exact same deterministic state transition in its own way.
struct BattleSessionEvent {
    BattleSessionEvent() = default;
    BattleSessionEvent(BattleEventKind event_kind, bool monster_source,
                       std::size_t source_index, bool monster_target,
                       std::size_t target_index,
                       std::uint16_t event_ability_id = 0,
                       std::uint16_t event_damage = 0,
                       std::uint16_t event_healing = 0,
                       bool event_critical = false,
                       bool event_evaded = false,
                       bool event_resisted = false,
                       bool event_defeated = false,
                       AbilityBlockReason event_block_reason =
                           AbilityBlockReason::none) noexcept
        : kind(event_kind),
          source_is_monster(monster_source),
          source(source_index),
          target_is_monster(monster_target),
          target(target_index),
          ability_id(event_ability_id),
          damage(event_damage),
          healing(event_healing),
          critical(event_critical),
          evaded(event_evaded),
          resisted(event_resisted),
          defeated(event_defeated),
          block_reason(event_block_reason) {}

    BattleEventKind kind{BattleEventKind::skipped};
    bool source_is_monster{};
    // Monster AI's generic branch enters 23b1/2464 and uses the alternating
    // solid-colour 25ee flash. Special-A/B and self-heal actions enter 26af
    // instead; the event kind alone cannot distinguish those call sites.
    bool monster_generic_path{};
    // The 26af selector was not one of its explicit branches and therefore
    // jumped directly to 2938's RET. 20e7/22e0 still expose the normal bare
    // enemy-turn preparation and cleanup pages, but there is no 262f card,
    // voice or effect between them.
    bool monster_special_silent_return{};
    std::size_t source{};
    bool target_is_monster{};
    std::size_t target{};
    std::uint16_t ability_id{};
    std::uint16_t damage{};
    std::uint16_t healing{};
    bool critical{};
    bool evaded{};
    bool resisted{};
    bool defeated{};
    AbilityBlockReason block_reason{AbilityBlockReason::none};
    // Exact dispatcher selector used for presentation. This cannot always be
    // recovered from ability_id because battle items have a separate ITEM
    // record namespace that overlaps FIG's embedded ability table.
    std::uint16_t effect_code{};
    // Exact compact feedback emitted by FIG's effect-61 dispel/cleanse paths.
    // Player buffs use bits +313c..+3164; monster buffs use special/attack/evasion.
    std::uint8_t removed_player_buff_mask{};
    std::uint8_t removed_monster_buff_mask{};
    std::uint8_t expired_player_buff_mask{};
    std::uint8_t recovered_player_status_mask{};
    std::uint8_t expired_monster_buff_mask{};
    // Captured-monster allies share player-side effect handlers but are not
    // party actors. This disambiguates their missing-medium and 1048
    // mediator-summon presentation.
    bool source_is_summoned_ally{};
    // Exact duration returned by status handlers and silent monster-status
    // expiry bits needed by persistent 2deb icon recomposition.
    std::uint16_t status_duration{};
    std::uint8_t expired_monster_status_mask{};
    // Exact post-state for FIG support selectors 01..30. These handlers may
    // clear death/status bits or alter SP/AP/maxima without producing a
    // numeric HP result, so deltas alone cannot drive faithful card redraws.
    std::optional<PlayerSupportState> resulting_player_support_state;
    // 4338 only replaces DS:31e3 with a selected monster/party target when a
    // selector was actually entered. Zero target-mode >30h effects still
    // resolve against monster zero, but keep the caster's own action anchor.
    bool action_anchor_is_target{true};
};

struct BattleRoundResult {
    std::vector<BattleTurn> turn_order;
    std::vector<BattleSessionEvent> events;
    BattleOutcome outcome{BattleOutcome::ongoing};
};

// Portable composition of FIG's already-reversed formation, actor, physical,
// ability, initiative, and normal-AI rules. It owns no rendering/audio/input
// resources and can therefore be driven identically by SDL, tests, or future
// console/mobile frontends.
class BattleSession {
public:
    static BattleSession create(const SharedState& state,
                                const BattleEncounter& encounter,
                                const ScriptArchive& items,
                                bool random_encounter_rules = false);

    [[nodiscard]] BattleOutcome outcome() const noexcept;
    [[nodiscard]] std::size_t party_count() const noexcept { return party_count_; }
    [[nodiscard]] std::span<const BattlePartyMember> party() const noexcept {
        return party_;
    }
    [[nodiscard]] std::span<const MonsterDefinition> monster_definitions() const noexcept {
        return monster_definitions_;
    }
    [[nodiscard]] std::span<const MonsterBattleState> monsters() const noexcept {
        return monsters_;
    }
    [[nodiscard]] std::uint16_t critical_countdown() const noexcept {
        return critical_countdown_;
    }
    [[nodiscard]] const std::array<std::uint16_t, 50>& inventory() const noexcept {
        return inventory_;
    }
    [[nodiscard]] BattleSessionRewards rewards() const noexcept;
    [[nodiscard]] const std::array<std::uint16_t, 5>& special_item_counts() const noexcept {
        return special_item_counts_;
    }
    [[nodiscard]] std::span<const SummonedAlly> summoned_allies() const noexcept {
        return summoned_allies_;
    }
    [[nodiscard]] bool random_encounter_rules() const noexcept {
        return random_encounter_rules_;
    }
    [[nodiscard]] bool capture_command_available() const noexcept {
        return random_encounter_rules_ && capture_command_available_;
    }
    [[nodiscard]] bool item_magic_blocked(std::size_t actor) const noexcept {
        return actor >= party_count_ || player_special_status_turns_[actor][3] != 0;
    }
    [[nodiscard]] const std::array<bool, 3>& battle_media() const noexcept {
        return battle_media_;
    }

    // After a victory, ORC's optional ## value has an exact one-in-three
    // chance to occupy inventory slot 49, provided that slot is empty. The
    // FIG module's 058c caller compacts it into the first gap before waiting
    // for acknowledgement of the capture-reward card.
    bool try_grant_encounter_capture(std::optional<std::uint16_t> definition_id,
                                     const BattleRandom& random);

    BattleRoundResult play_round(
        const std::array<PlayerBattleCommand, 4>& commands,
        const BattleAbilityDatabase& abilities, const BattleRandom& random);

    // Commits the battle-persistent actor fields and critical countdown into
    // the 1350-byte inter-module state. FIG's separate end-of-battle snapshot
    // restoration remains the BattleModule's responsibility.
    void store(SharedState& state) const;

private:
    std::size_t first_living_monster(std::size_t preferred) const noexcept;
    std::size_t first_living_player(std::size_t preferred) const noexcept;
    void finish_player_turn(std::size_t actor,
                            std::vector<BattleSessionEvent>& events);
    void add_player_death_reaction(
        std::vector<BattleSessionEvent>& events, const BattleRandom& random);
    void compact_inventory() noexcept;

    std::array<BattlePartyMember, 4> party_{};
    std::size_t party_count_{};
    std::vector<MonsterDefinition> monster_definitions_;
    std::vector<MonsterBattleState> monsters_;
    std::vector<MonsterAiState> monster_ai_;
    std::vector<bool> monster_fled_;
    std::array<std::uint16_t, 50> inventory_{};
    std::array<std::optional<BattleItemDefinition>, 50> battle_items_{};
    std::array<std::optional<MonsterDefinition>, 50> summon_items_{};
    std::vector<SummonedAlly> summoned_allies_;
    std::array<std::optional<BattleCompositeEffect>,
               BattleAbilityDatabase::ability_count> composite_effects_{};
    std::array<std::uint16_t, 5> special_item_counts_{};
    std::array<std::array<std::uint16_t, 4>, 4> player_special_status_turns_{};
    std::array<std::array<std::uint16_t, 6>, 4> player_buff_turns_{};
    PlayerSupportRuntime player_support_runtime_{};  // FIG DS:2bb9/2bbb
    std::array<bool, 3> battle_media_{};  // FIG DS:319d/31a5/31ad
    std::uint16_t critical_countdown_{20};
    std::uint16_t escape_attempt_countdown_{};
    bool random_encounter_rules_{};
    bool capture_command_available_{};
    bool escaped_{};
};

}  // namespace swd2
