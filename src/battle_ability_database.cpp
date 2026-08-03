#include "swd2/battle_ability_database.hpp"

#include "swd2/mz_executable.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("FIG.EXE ability table is truncated");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

}  // namespace

BattleAbilityDatabase BattleAbilityDatabase::load(
    const std::filesystem::path& fig_executable) {
    const auto executable = dos::MzExecutable::load(fig_executable);
    std::ifstream input(fig_executable, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open FIG ability database");
    input.seekg(static_cast<std::streamoff>(executable.header_size()));
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    // Entry starts `mov ax,DATA_SEGMENT; mov ds,ax`; the immediate is a DOS
    // relocation at image 0000:0001. Reading its pre-relocation paragraph value
    // avoids hard-coding the file offset of the initialized data segment.
    const auto has_data_relocation = std::any_of(
        executable.relocations().begin(), executable.relocations().end(),
        [](const dos::Relocation& relocation) {
            return relocation.segment == 0 && relocation.offset == 1;
        });
    if (image.size() < 5 || image[0] != 0xb8 || image[3] != 0x8e || image[4] != 0xd8 ||
        !has_data_relocation) {
        throw std::runtime_error("FIG.EXE has an unexpected data-segment prologue");
    }
    const auto data_paragraph = word(image, 1);
    constexpr std::size_t item_name_data_offset = 0x554;
    constexpr std::size_t item_name_bytes = 12;
    constexpr std::size_t table_data_offset = 0x1dcc;
    constexpr std::size_t record_bytes = 20;
    constexpr std::size_t item_category_bytes = 4;
    constexpr std::size_t item_category_data_offset =
        table_data_offset + ability_count * record_bytes;
    const auto table = static_cast<std::size_t>(data_paragraph) * 16U + table_data_offset;
    if (table + ability_count * record_bytes > image.size()) {
        throw std::runtime_error("FIG.EXE ability table lies outside its load image");
    }

    BattleAbilityDatabase result;
    const auto item_name_table =
        static_cast<std::size_t>(data_paragraph) * 16U + item_name_data_offset;
    if (item_name_table + item_name_count * item_name_bytes > image.size() ||
        item_name_data_offset + item_name_count * item_name_bytes !=
            table_data_offset) {
        throw std::runtime_error("FIG.EXE item-name table is truncated");
    }
    result.item_names_.reserve(item_name_count);
    for (std::size_t id = 0; id < item_name_count; ++id) {
        std::array<std::uint8_t, item_name_bytes> name{};
        std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(
                                     item_name_table + id * item_name_bytes),
                    name.size(), name.begin());
        result.item_names_.push_back(name);
    }
    result.abilities_.reserve(ability_count);
    for (std::size_t id = 0; id < ability_count; ++id) {
        const auto record = std::span<const std::uint8_t>(image).subspan(
            table + id * record_bytes, record_bytes);
        BattleAbility ability;
        std::copy_n(record.begin(), ability.name_big5.size(), ability.name_big5.begin());
        ability.target_flags = word(record, 0x0c);
        ability.effect_code = word(record, 0x0e);
        ability.cost = word(record, 0x10);
        ability.base_power = word(record, 0x12);
        result.abilities_.push_back(ability);
    }
    const auto category_table =
        static_cast<std::size_t>(data_paragraph) * 16U +
        item_category_data_offset;
    if (category_table + item_category_count * item_category_bytes >
        image.size()) {
        throw std::runtime_error("FIG.EXE item-category table is truncated");
    }
    result.item_category_labels_.reserve(item_category_count);
    for (std::size_t category = 0; category < item_category_count; ++category) {
        std::array<std::uint8_t, item_category_bytes> label{};
        std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(
                                     category_table +
                                     category * item_category_bytes),
                    label.size(), label.begin());
        result.item_category_labels_.push_back(label);
    }
    static constexpr std::array<std::size_t, 6> notice_data_offsets = {
        0x2cb7, 0x2cd1, 0x2cf5, 0x2d05, 0x2de7, 0x2e65,
    };
    const auto data_base = static_cast<std::size_t>(data_paragraph) * 16U;
    const auto extract_text = [&](std::size_t data_offset) {
        auto cursor = data_base + data_offset;
        if (cursor >= image.size()) {
            throw std::runtime_error("FIG.EXE battle text lies outside its load image");
        }
        std::vector<std::uint8_t> text;
        while (cursor + 1U < image.size() &&
               !(image[cursor] == '$' && image[cursor + 1U] == '$')) {
            text.push_back(image[cursor++]);
        }
        if (cursor + 1U >= image.size() || (text.size() & 1U) != 0) {
            throw std::runtime_error("FIG.EXE battle text is unterminated or misaligned");
        }
        return text;
    };
    for (std::size_t notice = 0; notice < notice_data_offsets.size(); ++notice) {
        result.notice_texts_[notice] = extract_text(notice_data_offsets[notice]);
    }
    static constexpr std::array<std::size_t, 5> player_status_data_offsets = {
        0x2d8d, 0x2d97, 0x2da1, 0x2dab, 0x2db5,
    };
    for (std::size_t status = 0; status < player_status_data_offsets.size(); ++status) {
        result.player_status_texts_[status] =
            extract_text(player_status_data_offsets[status]);
    }
    // FIG 292c reports each erased party buff in actor-runtime order. The
    // labels share the same four-glyph compact-card representation as 57d6.
    static constexpr std::array<std::size_t, 6>
        player_removed_buff_data_offsets = {
            0x2d37, 0x2d41, 0x2d4b, 0x2d55, 0x2d5f, 0x2d83,
        };
    for (std::size_t slot = 0; slot < player_removed_buff_data_offsets.size();
         ++slot) {
        result.player_removed_buff_texts_[slot] =
            extract_text(player_removed_buff_data_offsets[slot]);
    }
    // FIG 55e4 uses the same initialized strings around the selected monster,
    // in the order special ward, attack enhancement, evasion enhancement.
    static constexpr std::array<std::size_t, 3>
        monster_removed_buff_data_offsets = {0x2d5f, 0x2d4b, 0x2d55};
    for (std::size_t slot = 0; slot < monster_removed_buff_data_offsets.size();
         ++slot) {
        result.monster_removed_buff_texts_[slot] =
            extract_text(monster_removed_buff_data_offsets[slot]);
    }
    result.magic_ward_text_ = extract_text(0x2d7b);
    result.monster_immunity_text_ = extract_text(0x2e9b);
    static constexpr std::array<std::size_t, 4>
        recovered_player_status_data_offsets = {
            0x2dbf, 0x2dc9, 0x2dd3, 0x2ddd,
        };
    for (std::size_t slot = 0;
         slot < recovered_player_status_data_offsets.size(); ++slot) {
        result.recovered_player_status_texts_[slot] =
            extract_text(recovered_player_status_data_offsets[slot]);
    }
    result.death_reaction_text_ = extract_text(0x2d73);
    result.monster_escape_texts_[0] = extract_text(0x2ca5);
    result.monster_escape_texts_[1] = extract_text(0x2d69);
    result.monster_physical_texts_[0] = extract_text(0x2c9d);
    result.monster_physical_texts_[1] = extract_text(0x2c95);
    result.monster_physical_texts_[2] = extract_text(0x2caf);
    // Captured monsters use the fixed MENU-frame action card at FIG 10fc,
    // not the enemy-name panel. Physical attacks reuse DATA:2c9d above;
    // fleeing and ability dispatch have their own initialized labels.
    result.summoned_ally_action_texts_[0] = extract_text(0x2e7b);
    result.summoned_ally_action_texts_[1] = extract_text(0x2e83);
    result.capture_action_text_ = extract_text(0x2e5d);
    // 58fa displays this modal when none of the four persistent mediator
    // slots contains the sprite required by the selected visual handler.
    result.missing_medium_text_ = extract_text(0x2d1f);
    result.summon_replacement_text_ = extract_text(0x2e8b);
    static constexpr std::array<std::size_t, 5> settlement_data_offsets = {
        0x2e0b, 0x2e2f, 0x2e3f, 0x2e4b, 0x2ea3,
    };
    for (std::size_t index = 0; index < settlement_data_offsets.size(); ++index) {
        result.settlement_texts_[index] =
            extract_text(settlement_data_offsets[index]);
    }
    return result;
}

std::span<const std::uint8_t> BattleAbilityDatabase::notice_text(
    BattleCommandNotice notice) const noexcept {
    const auto value = static_cast<std::size_t>(notice);
    if (value == 0 || value > notice_texts_.size()) return {};
    return notice_texts_[value - 1U];
}

std::span<const std::uint8_t> BattleAbilityDatabase::player_status_text(
    std::uint16_t effect_code) const noexcept {
    switch (effect_code) {
    case 0x63: return player_status_texts_[0];
    case 0x66: return player_status_texts_[1];
    case 0x67: return player_status_texts_[2];
    case 0x68: return player_status_texts_[3];
    case 0x69: return player_status_texts_[4];
    default: return {};
    }
}

std::span<const std::uint8_t> BattleAbilityDatabase::player_removed_buff_text(
    std::size_t slot) const noexcept {
    if (slot >= player_removed_buff_texts_.size()) return {};
    return player_removed_buff_texts_[slot];
}

std::span<const std::uint8_t> BattleAbilityDatabase::monster_removed_buff_text(
    std::size_t slot) const noexcept {
    if (slot >= monster_removed_buff_texts_.size()) return {};
    return monster_removed_buff_texts_[slot];
}

std::span<const std::uint8_t>
BattleAbilityDatabase::recovered_player_status_text(
    std::size_t slot) const noexcept {
    if (slot >= recovered_player_status_texts_.size()) return {};
    return recovered_player_status_texts_[slot];
}

}  // namespace swd2
