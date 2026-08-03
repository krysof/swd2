#pragma once

#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_session.hpp"
#include "swd2/platform.hpp"
#include "swd2/script_archive.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

// FIG collected all living actors' choices before rolling initiative.  This
// presentation-neutral state machine preserves that separation: a frontend
// feeds directional/confirm/cancel actions and receives an explicit command
// array which can be resolved by BattleSession on every platform.
enum class BattleCommandMenuPage {
    commands,
    attack_modes,
    tactics,
    abilities,
    items,
    monster_target,
    party_target,
    summon_replace,
    complete,
    quit,
};

struct BattleCommandMenuEntry {
    PlayerCommandKind kind{PlayerCommandKind::skip};
    // Ability id on the abilities page, inventory slot on the items page,
    // and the actual party/monster index on target pages.
    std::uint16_t value{};
    bool enabled{true};
};

class BattleCommandMenu {
public:
    BattleCommandMenu(const BattleSession& session,
                      const BattleAbilityDatabase& abilities,
                      const ScriptArchive& items);

    [[nodiscard]] BattleCommandMenuPage page() const noexcept { return page_; }
    [[nodiscard]] std::size_t actor() const noexcept { return actor_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    [[nodiscard]] std::span<const BattleCommandMenuEntry> entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] BattleCommandMenuPage target_return_page() const noexcept {
        return target_return_page_;
    }
    [[nodiscard]] std::size_t target_return_cursor() const noexcept {
        return target_return_cursor_;
    }
    [[nodiscard]] std::span<const BattleCommandMenuEntry>
    target_return_entries() const noexcept {
        return target_return_entries_;
    }
    [[nodiscard]] const std::array<PlayerBattleCommand, 4>& commands() const noexcept {
        return commands_;
    }
    [[nodiscard]] bool complete() const noexcept {
        return page_ == BattleCommandMenuPage::complete;
    }
    [[nodiscard]] bool quit_requested() const noexcept {
        return page_ == BattleCommandMenuPage::quit;
    }
    [[nodiscard]] bool automatic_requested() const noexcept {
        return automatic_requested_;
    }
    [[nodiscard]] bool item_reserved(std::size_t slot) const noexcept {
        return slot < item_reservations_.size() && item_reservations_[slot] != 0;
    }
    [[nodiscard]] BattleCommandNotice notice() const noexcept { return notice_; }

    void input(InputAction action);

private:
    struct ItemChoice {
        std::size_t slot{};
        bool monster_target{};
        bool party_target{};
        bool enabled{};
        bool summon{};
    };

    [[nodiscard]] bool commandable(std::size_t actor) const noexcept;
    void show_commands();
    void show_attack_modes();
    void show_tactics();
    void show_abilities();
    void show_items();
    void show_targets(bool monsters);
    void show_summon_replace();
    void commit_target(std::size_t target);
    void finish_command(PlayerBattleCommand command);
    void advance_actor();
    void rewind_actor();
    void move_cursor(int delta);

    const BattleSession& session_;
    const BattleAbilityDatabase& abilities_;
    const ScriptArchive& items_;
    std::array<PlayerBattleCommand, 4> commands_{};
    std::array<std::uint8_t, 50> item_reservations_{};  // actor index + 1
    std::vector<std::size_t> completed_actors_;
    std::vector<BattleCommandMenuEntry> entries_;
    std::vector<BattleCommandMenuEntry> target_return_entries_;
    std::vector<ItemChoice> item_choices_;
    BattleCommandMenuPage page_{BattleCommandMenuPage::commands};
    BattleCommandMenuPage target_return_page_{BattleCommandMenuPage::commands};
    PlayerBattleCommand pending_{};
    bool pending_group_attack_{};
    bool automatic_requested_{};
    BattleCommandNotice notice_{BattleCommandNotice::none};
    std::size_t actor_{};
    std::size_t cursor_{};
    std::size_t target_return_cursor_{};
};

}  // namespace swd2
