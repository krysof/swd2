#include "swd2/battle_command_menu.hpp"

#include "swd2/battle_item_definition.hpp"
#include "swd2/monster_definition.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace swd2 {

namespace {

constexpr std::uint16_t first_summon_item_id = 0x13a;
constexpr std::array<std::uint8_t, 5> class_five_bits = {
    0x10, 0x08, 0x04, 0x02, 0x01,
};

bool class_five_affordable(const BattleSession& session, std::uint16_t mask) {
    const auto counts = session.special_item_counts();
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if ((static_cast<std::uint8_t>(mask) & class_five_bits[index]) != 0 &&
            counts[index] == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

BattleCommandMenu::BattleCommandMenu(
    const BattleSession& session, const BattleAbilityDatabase& abilities,
    const ScriptArchive& items)
    : session_(session), abilities_(abilities), items_(items) {
    for (auto& command : commands_) command.kind = PlayerCommandKind::skip;
    while (actor_ < session_.party_count() && !commandable(actor_)) ++actor_;
    if (actor_ == session_.party_count()) {
        page_ = BattleCommandMenuPage::complete;
    } else {
        show_commands();
    }
}

bool BattleCommandMenu::commandable(std::size_t actor) const noexcept {
    if (actor >= session_.party_count()) return false;
    const auto& member = session_.party()[actor];
    return member.living() && (member.status_bits & 0x2c7eU) == 0;
}

void BattleCommandMenu::show_commands() {
    page_ = BattleCommandMenuPage::commands;
    // FIG 158b starts on command index two, the upper "attack" tile.
    cursor_ = 2;
    entries_ = {
        {PlayerCommandKind::ability, 0, true},
        {PlayerCommandKind::item, 0, true},
        {PlayerCommandKind::basic_attack, 0, true},
        {PlayerCommandKind::skip, 0, true},
    };
    pending_group_attack_ = false;
    item_choices_.clear();
}

void BattleCommandMenu::show_attack_modes() {
    page_ = BattleCommandMenuPage::attack_modes;
    cursor_ = 0;
    entries_ = {
        {PlayerCommandKind::basic_attack, 0, true},
        {PlayerCommandKind::automatic, 1, true},
    };
}

void BattleCommandMenu::show_tactics() {
    page_ = BattleCommandMenuPage::tactics;
    cursor_ = 0;
    entries_ = {
        {PlayerCommandKind::skip, 0, true},
        {PlayerCommandKind::escape, 1, true},
    };
    // FIG 2003 adds the third (upper) capture tile only for actor zero in a
    // random encounter and while the capture inventory boundary is available.
    if (actor_ == 0 && session_.capture_command_available()) {
        entries_.push_back({PlayerCommandKind::capture, 2, true});
    }
}

void BattleCommandMenu::show_abilities() {
    page_ = BattleCommandMenuPage::abilities;
    cursor_ = 0;
    entries_.clear();
    const auto& member = session_.party()[actor_];
    for (const auto raw_id : member.abilities) {
        const auto id = static_cast<std::size_t>(raw_id);
        if (id == 0 || id >= abilities_.abilities().size()) {
            entries_.push_back({PlayerCommandKind::ability, raw_id, false});
            continue;
        }
        const auto& ability = abilities_.ability(id);
        const auto resource_class =
            static_cast<std::uint8_t>((ability.target_flags >> 8U) & 0x0fU);
        bool enabled = (ability.target_flags & 0x4000U) == 0;
        if (resource_class == 1 || resource_class == 4) {
            enabled = enabled && member.ability_points >= ability.cost;
        } else if (resource_class == 2 || resource_class == 3) {
            enabled = enabled && member.secondary_points >= ability.cost;
        } else if (resource_class == 5) {
            enabled = enabled && class_five_affordable(session_, ability.cost);
        } else {
            enabled = false;
        }
        entries_.push_back({PlayerCommandKind::ability, raw_id, enabled});
    }
}

void BattleCommandMenu::show_items() {
    page_ = BattleCommandMenuPage::items;
    cursor_ = 0;
    entries_.clear();
    item_choices_.clear();
    const auto inventory = session_.inventory();
    for (std::size_t slot = 0; slot < inventory.size(); ++slot) {
        const auto id = inventory[slot];
        if (id == 0) {
            entries_.push_back({PlayerCommandKind::item,
                                static_cast<std::uint16_t>(slot), false});
            item_choices_.push_back({slot, false, false, false});
            continue;
        }
        const auto record_index = static_cast<std::size_t>(id) + 2U;
        if (record_index >= items_.entry_count()) {
            entries_.push_back({PlayerCommandKind::item,
                                static_cast<std::uint16_t>(slot), false});
            item_choices_.push_back({slot, false, false, false});
            continue;
        }
        const auto record = items_.entry(record_index);
        ItemChoice choice;
        choice.slot = slot;
        if (id >= first_summon_item_id) {
            const auto monster = MonsterDefinition::parse(id, record);
            const auto cost = static_cast<std::uint16_t>(monster.level * 2U);
            // FIG 184f first tests ITEM +5 bit 1 for every record, including
            // captured monsters, then requires actor +35 to be strictly
            // greater than level*2 (JBE selects DATA:2e65).
            choice.enabled = record.size() > 5U && (record[5] & 2U) != 0 &&
                             session_.party()[actor_].secondary_points > cost;
            choice.summon = true;
        } else if (record.size() >= 9) {
            const auto item = BattleItemDefinition::parse(id, record);
            // FIG 184f calls 1d0d with SI=5 and tests AL bit 1: this is the
            // low byte at record +5, not the unrelated type word at +0.
            choice.enabled = (item.use_flags & 2U) != 0;
            choice.monster_target = (item.target_flags & 0x20U) != 0;
            choice.party_target = (item.target_flags & 0x10U) != 0;
            if (item.type == 0x10) {
                const auto ability_id = id >= 0x8cU
                                            ? static_cast<std::size_t>(id - 0x8cU)
                                            : abilities_.abilities().size();
                choice.enabled = choice.enabled && id >= 0x8cU &&
                                 ability_id < abilities_.abilities().size() &&
                                 !session_.item_magic_blocked(actor_) &&
                                 session_.party()[actor_].ability_points >=
                                     abilities_.ability(ability_id).cost;
            }
        }
        choice.enabled = choice.enabled && item_reservations_[slot] == 0;
        entries_.push_back({PlayerCommandKind::item,
                            static_cast<std::uint16_t>(slot), choice.enabled});
        item_choices_.push_back(choice);
    }
}

void BattleCommandMenu::show_summon_replace() {
    target_return_page_ = page_;
    target_return_cursor_ = cursor_;
    target_return_entries_ = entries_;
    page_ = BattleCommandMenuPage::summon_replace;
    cursor_ = 0;
    entries_ = {
        {PlayerCommandKind::item, 0, true},
        {PlayerCommandKind::item, 1, true},
    };
}

void BattleCommandMenu::show_targets(bool monsters) {
    target_return_page_ = page_;
    target_return_cursor_ = cursor_;
    target_return_entries_ = entries_;
    page_ = monsters ? BattleCommandMenuPage::monster_target
                     : BattleCommandMenuPage::party_target;
    cursor_ = 0;
    entries_.clear();
    if (monsters) {
        const auto targets = session_.monsters();
        for (std::size_t index = 0; index < targets.size(); ++index) {
            if (targets[index].hit_points != 0) {
                entries_.push_back({pending_.kind,
                                    static_cast<std::uint16_t>(index), true});
            }
        }
    } else {
        // Dead actors remain selectable because several effects revive them.
        for (std::size_t index = 0; index < session_.party_count(); ++index) {
            entries_.push_back({pending_.kind,
                                static_cast<std::uint16_t>(index), true});
        }
    }
    // FIG 178c bypasses its selector entirely when only one living monster
    // remains and writes that physical target immediately.
    if (monsters && entries_.size() == 1) {
        commit_target(entries_.front().value);
    }
}

void BattleCommandMenu::commit_target(std::size_t target) {
    pending_.target = target;
    if (pending_group_attack_) {
        // FIG 1000:1588 writes command type 2 and one selected target for
        // all four party slots. Preserve skip in absent/incapacitated slots.
        for (std::size_t index = actor_; index < commands_.size(); ++index) {
            commands_[index] = commandable(index)
                                   ? PlayerBattleCommand{
                                         PlayerCommandKind::basic_attack,
                                         0, pending_.target, 0}
                                   : PlayerBattleCommand{
                                         PlayerCommandKind::skip, 0, 0, 0};
        }
        automatic_requested_ = pending_.kind == PlayerCommandKind::automatic;
        page_ = BattleCommandMenuPage::complete;
        entries_.clear();
        return;
    }
    finish_command(pending_);
}

void BattleCommandMenu::finish_command(PlayerBattleCommand command) {
    if (command.kind == PlayerCommandKind::item &&
        command.item_slot < item_reservations_.size()) {
        item_reservations_[command.item_slot] =
            static_cast<std::uint8_t>(actor_ + 1U);
    }
    commands_[actor_] = command;
    completed_actors_.push_back(actor_);
    advance_actor();
}

void BattleCommandMenu::advance_actor() {
    ++actor_;
    while (actor_ < session_.party_count() && !commandable(actor_)) ++actor_;
    if (actor_ >= session_.party_count()) {
        page_ = BattleCommandMenuPage::complete;
        entries_.clear();
        return;
    }
    show_commands();
}

void BattleCommandMenu::rewind_actor() {
    if (completed_actors_.empty()) return;
    actor_ = completed_actors_.back();
    completed_actors_.pop_back();
    if (commands_[actor_].kind == PlayerCommandKind::item &&
        commands_[actor_].item_slot < item_reservations_.size() &&
        item_reservations_[commands_[actor_].item_slot] == actor_ + 1U) {
        item_reservations_[commands_[actor_].item_slot] = 0;
    }
    commands_[actor_] = {};
    commands_[actor_].kind = PlayerCommandKind::skip;
    show_commands();
}

void BattleCommandMenu::move_cursor(int delta) {
    if (entries_.empty()) return;
    const auto last = static_cast<int>(entries_.size() - 1U);
    const auto next = std::clamp(static_cast<int>(cursor_) + delta, 0, last);
    cursor_ = static_cast<std::size_t>(next);
}

void BattleCommandMenu::input(InputAction action) {
    if (complete() || quit_requested() || action == InputAction::none) return;
    if (notice_ != BattleCommandNotice::none) {
        // FIG 3e19 consumes the key that closes its modal text panel. It does
        // not reuse that key as movement/confirmation in the underlying list.
        if (action == InputAction::quit) {
            page_ = BattleCommandMenuPage::quit;
            entries_.clear();
        }
        notice_ = BattleCommandNotice::none;
        return;
    }
    if (action == InputAction::quit) {
        page_ = BattleCommandMenuPage::quit;
        entries_.clear();
        return;
    }
    if (page_ == BattleCommandMenuPage::commands) {
        // Original tile indices/geometric coordinates:
        // 0 left=abilities, 1 right=items, 2 up=attack, 3 down=tactics.
        if (action == InputAction::left) cursor_ = 0;
        else if (action == InputAction::right) cursor_ = 1;
        else if (action == InputAction::up) cursor_ = 2;
        else if (action == InputAction::down) cursor_ = 3;
        if (action == InputAction::left || action == InputAction::right ||
            action == InputAction::up || action == InputAction::down) return;
    }
    if (page_ == BattleCommandMenuPage::attack_modes) {
        if (action == InputAction::left || action == InputAction::up) cursor_ = 0;
        else if (action == InputAction::right || action == InputAction::down) cursor_ = 1;
        if (action == InputAction::left || action == InputAction::right ||
            action == InputAction::up || action == InputAction::down) return;
    }
    if (page_ == BattleCommandMenuPage::summon_replace) {
        if (action == InputAction::left || action == InputAction::up) cursor_ = 0;
        else if (action == InputAction::right || action == InputAction::down) {
            cursor_ = 1;
        }
        if (action == InputAction::left || action == InputAction::right ||
            action == InputAction::up || action == InputAction::down) return;
    }
    if (page_ == BattleCommandMenuPage::tactics) {
        if (action == InputAction::left) cursor_ = 0;
        else if (action == InputAction::right) cursor_ = 1;
        else if (action == InputAction::up && entries_.size() == 3) cursor_ = 2;
        if (action == InputAction::left || action == InputAction::right ||
            action == InputAction::up || action == InputAction::down) return;
    }
    if (action == InputAction::up || action == InputAction::left) {
        move_cursor(-1);
        return;
    }
    if (action == InputAction::down || action == InputAction::right) {
        move_cursor(1);
        return;
    }
    if (action == InputAction::cancel) {
        if (page_ == BattleCommandMenuPage::commands) {
            rewind_actor();
        } else if (page_ == BattleCommandMenuPage::monster_target ||
                   page_ == BattleCommandMenuPage::party_target ||
                   page_ == BattleCommandMenuPage::summon_replace) {
            const auto return_cursor = target_return_cursor_;
            if (target_return_page_ == BattleCommandMenuPage::abilities) {
                show_abilities();
            } else if (target_return_page_ == BattleCommandMenuPage::items) {
                show_items();
            } else if (target_return_page_ == BattleCommandMenuPage::attack_modes) {
                show_attack_modes();
            } else if (target_return_page_ == BattleCommandMenuPage::tactics) {
                show_tactics();
            } else {
                show_commands();
            }
            if (!entries_.empty()) {
                cursor_ = std::min(return_cursor, entries_.size() - 1U);
            }
        } else {
            show_commands();
        }
        return;
    }
    if (action != InputAction::confirm || entries_.empty()) return;
    const auto entry = entries_[cursor_];

    if (page_ == BattleCommandMenuPage::commands) {
        switch (entry.kind) {
        case PlayerCommandKind::ability:
            show_abilities();
            return;
        case PlayerCommandKind::item:
            show_items();
            return;
        case PlayerCommandKind::basic_attack:
            show_attack_modes();
            return;
        case PlayerCommandKind::skip:
            show_tactics();
            return;
        default:
            return;
        }
    }

    if (page_ == BattleCommandMenuPage::abilities) {
        const auto ability_id = static_cast<std::size_t>(entry.value);
        if (ability_id == 0 || ability_id >= abilities_.abilities().size()) return;
        const auto& ability = abilities_.ability(ability_id);
        // FIG 4043 simply loops on dispatcher entry zero. Only the three
        // explicit failure paths below invoke 3f2b.
        if (ability.effect_code == 0) return;
        if ((ability.target_flags & 0x4000U) != 0) {
            notice_ = BattleCommandNotice::ability_unavailable;
            return;
        }
        const auto resource_class = static_cast<std::uint8_t>(
            (ability.target_flags >> 8U) & 0x0fU);
        if (resource_class == 1 || resource_class == 4) {
            if (session_.party()[actor_].ability_points < ability.cost) {
                notice_ = BattleCommandNotice::insufficient_resource;
                return;
            }
        } else if (resource_class == 2 || resource_class == 3) {
            if (session_.party()[actor_].secondary_points < ability.cost) {
                notice_ = BattleCommandNotice::insufficient_resource;
                return;
            }
        } else if (!class_five_affordable(session_, ability.cost)) {
            notice_ = BattleCommandNotice::missing_elements;
            return;
        }
    }

    if (page_ == BattleCommandMenuPage::items) {
        const auto slot = static_cast<std::size_t>(entry.value);
        if (slot >= session_.inventory().size() || item_reserved(slot)) return;
        const auto id = session_.inventory()[slot];
        if (id == 0) return;
        const auto record_index = static_cast<std::size_t>(id) + 2U;
        if (record_index >= items_.entry_count()) return;
        const auto record = items_.entry(record_index);
        if (record.size() <= 5U || (record[5] & 2U) == 0) {
            notice_ = BattleCommandNotice::item_unusable;
            return;
        }
        if (id >= first_summon_item_id) {
            const auto monster = MonsterDefinition::parse(id, record);
            const auto cost = static_cast<std::uint16_t>(monster.level * 2U);
            if (session_.party()[actor_].secondary_points <= cost) {
                notice_ = BattleCommandNotice::insufficient_summon_resource;
                return;
            }
        } else if (record.size() >= 9U) {
            const auto item = BattleItemDefinition::parse(id, record);
            if (item.type == 0x10) {
                if (session_.item_magic_blocked(actor_)) {
                    notice_ = BattleCommandNotice::item_magic_blocked;
                    return;
                }
                const auto ability_id = id >= 0x8cU
                                            ? static_cast<std::size_t>(id - 0x8cU)
                                            : abilities_.abilities().size();
                if (ability_id >= abilities_.abilities().size()) return;
                if (session_.party()[actor_].ability_points <
                    abilities_.ability(ability_id).cost) {
                    notice_ = BattleCommandNotice::insufficient_resource;
                    return;
                }
            }
        }
    }

    if (!entry.enabled) return;

    if (page_ == BattleCommandMenuPage::attack_modes) {
        pending_ = {entry.kind, 0, 0, 0};
        // FIG 1621 stores the two-way submenu index in DS:2f18.  Ordinary
        // attack (index zero) records only the current actor and the caller
        // continues command collection at 0332/0367/03a4.  Automatic mode
        // (index one) takes the 0328 shortcut after 166b and therefore keeps
        // the commands prefilled for every remaining commandable actor.
        pending_group_attack_ = entry.kind == PlayerCommandKind::automatic;
        show_targets(true);
        return;
    }

    if (page_ == BattleCommandMenuPage::tactics) {
        if (entry.kind == PlayerCommandKind::skip) {
            finish_command({PlayerCommandKind::skip, 0, actor_, 0});
            return;
        }
        if (entry.kind == PlayerCommandKind::escape) {
            completed_actors_.clear();
            for (std::size_t index = 0; index < commands_.size(); ++index) {
                commands_[index] = commandable(index)
                                       ? PlayerBattleCommand{
                                             PlayerCommandKind::escape, 0, index, 0}
                                       : PlayerBattleCommand{
                                             PlayerCommandKind::skip, 0, 0, 0};
            }
            page_ = BattleCommandMenuPage::complete;
            entries_.clear();
            return;
        }
        if (entry.kind == PlayerCommandKind::capture) {
            pending_ = {PlayerCommandKind::capture, 0, 0, 0};
            pending_group_attack_ = false;
            show_targets(true);
            return;
        }
    }

    if (page_ == BattleCommandMenuPage::abilities) {
        pending_ = {PlayerCommandKind::ability, entry.value, actor_, 0};
        const auto target_flags = abilities_.ability(entry.value).target_flags;
        if ((target_flags & 0x2000U) != 0) {
            show_targets(true);
        } else if ((target_flags & 0x1000U) != 0) {
            show_targets(false);
        } else {
            finish_command(pending_);
        }
        return;
    }

    if (page_ == BattleCommandMenuPage::items) {
        const auto choice = item_choices_.at(cursor_);
        pending_ = {PlayerCommandKind::item, 0, actor_, choice.slot};
        if (choice.summon && session_.summoned_allies().size() >= 2) {
            show_summon_replace();
        } else if (choice.monster_target) {
            show_targets(true);
        } else if (choice.party_target) {
            show_targets(false);
        } else {
            finish_command(pending_);
        }
        return;
    }

    if (page_ == BattleCommandMenuPage::summon_replace) {
        pending_.target = entry.value;
        finish_command(pending_);
        return;
    }

    if (page_ == BattleCommandMenuPage::monster_target ||
        page_ == BattleCommandMenuPage::party_target) {
        commit_target(entry.value);
    }
}

}  // namespace swd2
