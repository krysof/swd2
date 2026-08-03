#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swd2 {

// Modal bottom-panel messages reached from FIG's ability/item selectors.
// Their text remains in the original FIG.EXE Big5 data and is exposed by
// BattleAbilityDatabase rather than replaced with frontend-local strings.
enum class BattleCommandNotice : std::uint8_t {
    none,
    insufficient_resource,       // DATA:2cb7
    item_unusable,               // DATA:2cd1
    ability_unavailable,         // DATA:2cf5
    missing_elements,            // DATA:2d05
    item_magic_blocked,          // DATA:2de7
    insufficient_summon_resource // DATA:2e65
};

// FIG.EXE embeds 151 fixed 20-byte effect descriptors in its initialized data
// segment. Entries 115..150 are enemy-only abilities, which is why stopping at
// the highest player ability silently dropped valid monster actions. They are
// game data, not machine instructions, and are addressed by item/spell/monster
// ability IDs throughout the battle dispatcher.
struct BattleAbility {
    std::array<std::uint8_t, 12> name_big5{};
    std::uint16_t target_flags{};  // record +0c; low/high bytes tested separately
    std::uint16_t effect_code{};   // record +0e; indexes FIG's effect dispatcher
    std::uint16_t cost{};          // record +10
    std::uint16_t base_power{};    // record +12
};

class BattleAbilityDatabase {
public:
    static constexpr std::size_t ability_count = 151;
    static constexpr std::size_t item_name_count = 522;
    static constexpr std::size_t item_category_count = 42;
    static BattleAbilityDatabase load(const std::filesystem::path& fig_executable);

    [[nodiscard]] std::span<const BattleAbility> abilities() const noexcept {
        return abilities_;
    }
    [[nodiscard]] const BattleAbility& ability(std::size_t id) const {
        return abilities_.at(id);
    }
    [[nodiscard]] const std::array<std::uint8_t, 12>& item_name(
        std::size_t id) const {
        return item_names_.at(id);
    }
    [[nodiscard]] const std::array<std::uint8_t, 4>& item_category_label(
        std::size_t category) const {
        return item_category_labels_.at(category);
    }
    [[nodiscard]] std::span<const std::uint8_t> notice_text(
        BattleCommandNotice notice) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> player_status_text(
        std::uint16_t effect_code) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> player_removed_buff_text(
        std::size_t slot) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> monster_removed_buff_text(
        std::size_t slot) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> magic_ward_text() const noexcept {
        return magic_ward_text_;
    }
    [[nodiscard]] std::span<const std::uint8_t> monster_immunity_text() const noexcept {
        return monster_immunity_text_;
    }
    [[nodiscard]] std::span<const std::uint8_t> recovered_player_status_text(
        std::size_t slot) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> death_reaction_text() const noexcept {
        return death_reaction_text_;
    }
    [[nodiscard]] std::span<const std::uint8_t> monster_escape_text(
        bool succeeded) const noexcept {
        return monster_escape_texts_[succeeded ? 1U : 0U];
    }
    [[nodiscard]] std::span<const std::uint8_t> monster_attack_text() const noexcept {
        return monster_physical_texts_[0];
    }
    [[nodiscard]] std::span<const std::uint8_t> physical_failure_text() const noexcept {
        return monster_physical_texts_[1];
    }
    [[nodiscard]] std::span<const std::uint8_t> evasion_text() const noexcept {
        return monster_physical_texts_[2];
    }
    [[nodiscard]] std::span<const std::uint8_t>
    summoned_ally_flee_text() const noexcept {
        return summoned_ally_action_texts_[0];
    }
    [[nodiscard]] std::span<const std::uint8_t>
    summoned_ally_ability_text() const noexcept {
        return summoned_ally_action_texts_[1];
    }
    [[nodiscard]] std::span<const std::uint8_t>
    capture_action_text() const noexcept {
        return capture_action_text_;
    }
    [[nodiscard]] std::span<const std::uint8_t>
    missing_medium_text() const noexcept {
        return missing_medium_text_;
    }
    [[nodiscard]] std::span<const std::uint8_t>
    summon_replacement_text() const noexcept {
        return summon_replacement_text_;
    }
    [[nodiscard]] std::span<const std::uint8_t> victory_text() const noexcept {
        return settlement_texts_[0];
    }
    [[nodiscard]] std::span<const std::uint8_t> level_up_title_text() const noexcept {
        return settlement_texts_[1];
    }
    [[nodiscard]] std::span<const std::uint8_t> defeat_text() const noexcept {
        return settlement_texts_[2];
    }
    [[nodiscard]] std::span<const std::uint8_t> encounter_capture_text() const noexcept {
        return settlement_texts_[3];
    }
    [[nodiscard]] std::span<const std::uint8_t> level_up_stats_text() const noexcept {
        return settlement_texts_[4];
    }

private:
    std::vector<BattleAbility> abilities_;
    std::vector<std::array<std::uint8_t, 12>> item_names_;
    std::vector<std::array<std::uint8_t, 4>> item_category_labels_;
    std::array<std::vector<std::uint8_t>, 6> notice_texts_;
    std::array<std::vector<std::uint8_t>, 5> player_status_texts_;
    std::array<std::vector<std::uint8_t>, 6> player_removed_buff_texts_;
    std::array<std::vector<std::uint8_t>, 3> monster_removed_buff_texts_;
    std::vector<std::uint8_t> magic_ward_text_;
    std::vector<std::uint8_t> monster_immunity_text_;
    std::array<std::vector<std::uint8_t>, 4> recovered_player_status_texts_;
    std::vector<std::uint8_t> death_reaction_text_;
    std::array<std::vector<std::uint8_t>, 2> monster_escape_texts_;
    std::array<std::vector<std::uint8_t>, 3> monster_physical_texts_;
    std::array<std::vector<std::uint8_t>, 2> summoned_ally_action_texts_;
    std::vector<std::uint8_t> capture_action_text_;
    std::vector<std::uint8_t> missing_medium_text_;
    std::vector<std::uint8_t> summon_replacement_text_;
    std::array<std::vector<std::uint8_t>, 5> settlement_texts_;
};

}  // namespace swd2
