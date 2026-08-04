#pragma once

#include "swd2/script_archive.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace swd2 {

// Common 36-byte record used by ITEM.EXE's ordinary inventory objects. The
// inventory/equipment fields below are the ones accessed by
// RPG.EXE:2f3d and 3f53; multi-byte fields at odd offsets are intentional.
struct ItemDefinition {
    std::uint16_t id{};
    std::uint16_t type{};                 // +00
    std::uint16_t preview_sprite{};       // +02, CD/AD number used by RPG menus
    std::uint8_t character_restrictions{}; // +04, bits 8/4/2/1 forbid actors 0..3
    // RPG.EXE reads an unaligned word at +05.  Its low byte controls the
    // inventory actions while the high byte contains the equipment category
    // and battle target class.  Keeping both the word and its high-byte view
    // avoids the earlier (incorrect) assumption that +06 bits 20h/40h meant
    // field/battle usability.
    std::uint16_t use_flags{};            // +05, unaligned
    std::uint8_t flags{};                 // +06, high byte of use_flags
    std::uint16_t effect_code{};          // +07, unaligned
    std::uint16_t price{};                // +0b, unaligned
    std::array<std::uint16_t, 4> stat_words{}; // +0d,+0f,+11,+13
    std::array<std::uint8_t, 5> trait_levels{}; // +16,+17,+18,+1a,+1b
    std::int16_t preview_x{};             // +1d, Mode-X byte-column adjustment
    std::int16_t preview_y{};             // +1f, scanline adjustment
    std::uint8_t alchemy_class{};          // +22, even matrix coordinate 0..20h
    std::uint8_t alchemy_rank{};           // +23, product threshold input
    // Alchemy products use ITEM's extended 80-byte record. RPG:4571 compares
    // the word at +34 against the first actor's level plus five.
    std::uint16_t alchemy_required_level{}; // +34, zero on short input records
    // Display order used by DATA:399a/3922: level, wisdom, life, magic,
    // strength, agility, defense, dodge.
    std::array<std::uint16_t, 8> alchemy_stats{};

    [[nodiscard]] std::uint8_t equipment_category() const noexcept {
        return static_cast<std::uint8_t>(flags & 0x0fU);
    }
    [[nodiscard]] bool field_usable() const noexcept { return (use_flags & 0x0001U) != 0; }
    [[nodiscard]] bool battle_usable() const noexcept { return (use_flags & 0x0002U) != 0; }
    [[nodiscard]] bool consumed_on_use() const noexcept { return (use_flags & 0x0004U) != 0; }
    [[nodiscard]] bool sellable() const noexcept { return (use_flags & 0x0008U) != 0; }
    [[nodiscard]] bool discardable() const noexcept { return (use_flags & 0x0020U) != 0; }
    [[nodiscard]] bool shows_effect_panel() const noexcept {
        return (use_flags & 0x0080U) != 0;
    }

    static ItemDefinition parse(std::uint16_t id,
                                std::span<const std::uint8_t> record);
};

class ItemDatabase;

// RPG.EXE DATA:2a42 begins with a 17x17 word matrix. The source records store
// even byte coordinates, so the original address expression is class_a +
// class_b*11h (not a conventional row*17+column word index). Each matrix
// entry points at result/maximum-rank word pairs in the same data slice.
[[nodiscard]] std::optional<std::uint16_t> resolve_item_alchemy_product(
    const ItemDatabase& items,
    std::span<const std::uint8_t> alchemy_data,
    std::uint16_t first_item,
    std::uint16_t second_item);

class ItemDatabase {
public:
    static ItemDatabase load(const std::filesystem::path& item_executable);
    static ItemDatabase from_archive(const ScriptArchive& archive);

    [[nodiscard]] const ItemDefinition& at(std::uint16_t id) const;
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }

private:
    std::vector<ItemDefinition> definitions_;
};

// ITEM2.EXE is the text half of ITEM.EXE.  Its pointer table is parallel to
// the mechanical definition table: entry id+2 contains either
//
//     A1 79 <Big5 name> A1 7A <Big5 description> 24 24
//
// (the original full-width title brackets are U+300E/U+300F), a bare name
// followed by "$$", or just "$$" for an unused id.  Keeping the bytes in
// their original encoding lets the portable renderer use CHAIN.DSK directly
// instead of depending on an operating-system Big5 font.
struct ItemText {
    std::uint16_t id{};
    std::vector<std::uint8_t> name;
    std::vector<std::uint8_t> description;

    [[nodiscard]] bool empty() const noexcept {
        return name.empty() && description.empty();
    }

    static ItemText parse(std::uint16_t id,
                          std::span<const std::uint8_t> record);
};

class ItemTextDatabase {
public:
    static ItemTextDatabase load(const std::filesystem::path& item2_executable);
    static ItemTextDatabase from_archive(const ScriptArchive& archive);

    [[nodiscard]] const ItemText& at(std::uint16_t id) const;
    [[nodiscard]] std::size_t size() const noexcept { return texts_.size(); }

private:
    std::vector<ItemText> texts_;
};

}  // namespace swd2
