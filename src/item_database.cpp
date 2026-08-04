#include "swd2/item_database.hpp"

#include <algorithm>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint8_t byte_or_zero(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return offset < bytes.size() ? bytes[offset] : 0;
}

std::uint16_t word_or_zero(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(byte_or_zero(bytes, offset)) |
           (static_cast<std::uint16_t>(byte_or_zero(bytes, offset + 1)) << 8U);
}

}  // namespace

ItemDefinition ItemDefinition::parse(std::uint16_t id,
                                     std::span<const std::uint8_t> record) {
    ItemDefinition result;
    result.id = id;
    result.type = word_or_zero(record, 0);
    result.preview_sprite = word_or_zero(record, 2);
    result.character_restrictions = byte_or_zero(record, 4);
    result.use_flags = word_or_zero(record, 5);
    result.flags = byte_or_zero(record, 6);
    result.effect_code = word_or_zero(record, 7);
    result.price = word_or_zero(record, 0x0b);
    for (std::size_t i = 0; i < result.stat_words.size(); ++i) {
        result.stat_words[i] = word_or_zero(record, 0x0d + i * 2);
    }
    constexpr std::array<std::size_t, 5> trait_offsets{0x16, 0x17, 0x18, 0x1a, 0x1b};
    for (std::size_t i = 0; i < trait_offsets.size(); ++i) {
        result.trait_levels[i] = byte_or_zero(record, trait_offsets[i]);
    }
    result.preview_x = static_cast<std::int16_t>(word_or_zero(record, 0x1d));
    result.preview_y = static_cast<std::int16_t>(word_or_zero(record, 0x1f));
    result.alchemy_class = byte_or_zero(record, 0x22);
    result.alchemy_rank = byte_or_zero(record, 0x23);
    result.alchemy_required_level = word_or_zero(record, 0x34);
    constexpr std::array<std::size_t, 8> alchemy_stat_offsets{
        0x34, 0x3c, 0x2c, 0x44, 0x38, 0x40, 0x4e, 0x4c};
    for (std::size_t i = 0; i < alchemy_stat_offsets.size(); ++i) {
        result.alchemy_stats[i] = word_or_zero(record, alchemy_stat_offsets[i]);
    }
    return result;
}

std::optional<std::uint16_t> resolve_item_alchemy_product(
    const ItemDatabase& items,
    std::span<const std::uint8_t> alchemy_data,
    std::uint16_t first_item,
    std::uint16_t second_item) {
    if (first_item == 0U || second_item == 0U ||
        first_item >= items.size() || second_item >= items.size()) {
        return std::nullopt;
    }
    const auto& first = items.at(first_item);
    const auto& second = items.at(second_item);
    const auto matrix_offset = static_cast<std::size_t>(first.alchemy_class) +
                               static_cast<std::size_t>(second.alchemy_class) * 0x11U;
    if (matrix_offset + 2U > alchemy_data.size()) return std::nullopt;
    const auto word = [&](std::size_t offset) -> std::optional<std::uint16_t> {
        if (offset + 2U > alchemy_data.size()) return std::nullopt;
        return static_cast<std::uint16_t>(alchemy_data[offset]) |
               (static_cast<std::uint16_t>(alchemy_data[offset + 1U]) << 8U);
    };
    const auto table_address = word(matrix_offset);
    if (!table_address || *table_address < 0x2a42U) return std::nullopt;
    const auto table_offset = static_cast<std::size_t>(*table_address - 0x2a42U);
    const auto average_rank = static_cast<std::uint16_t>(
        (static_cast<unsigned>(first.alchemy_rank) + second.alchemy_rank) / 2U);

    // 4477 restarts at the first result when it encounters the ffff
    // threshold. Shipped tables are ordered, but retaining that fallback is
    // required for the special DATA:2f00 catch-all pair.
    const auto first_result = word(table_offset);
    if (!first_result || *first_result == 0xffffU) return std::nullopt;
    for (auto offset = table_offset; offset + 4U <= alchemy_data.size();
         offset += 4U) {
        const auto result = word(offset);
        const auto threshold = word(offset + 2U);
        if (!result || !threshold) return std::nullopt;
        if (*threshold == 0xffffU) return *first_result;
        if (average_rank <= *threshold) {
            return *result == 0xffffU
                       ? std::optional<std::uint16_t>{}
                       : std::optional<std::uint16_t>{*result};
        }
    }
    return std::nullopt;
}

ItemDatabase ItemDatabase::load(const std::filesystem::path& item_executable) {
    return from_archive(ScriptArchive::load(item_executable));
}

ItemDatabase ItemDatabase::from_archive(const ScriptArchive& archive) {
    if (archive.entry_count() < 3) {
        throw std::runtime_error("ITEM archive has no definition records");
    }
    ItemDatabase result;
    result.definitions_.reserve(archive.entry_count() - 2);
    for (std::size_t id = 0; id + 2 < archive.entry_count(); ++id) {
        if (id > 0xffffU) throw std::runtime_error("ITEM archive has too many records");
        result.definitions_.push_back(
            ItemDefinition::parse(static_cast<std::uint16_t>(id), archive.entry(id + 2)));
    }
    return result;
}

const ItemDefinition& ItemDatabase::at(std::uint16_t id) const {
    if (id >= definitions_.size()) {
        throw std::out_of_range("ITEM definition id is out of range");
    }
    return definitions_[id];
}

ItemText ItemText::parse(std::uint16_t id,
                         std::span<const std::uint8_t> record) {
    ItemText result;
    result.id = id;

    // The text interpreter stops at the first literal "$$" pair.  Although
    // all shipped records terminate correctly, finding it explicitly keeps a
    // malformed archive from leaking directory/padding bytes into the UI.
    auto logical_end = record.size();
    for (std::size_t i = 0; i + 1 < record.size(); ++i) {
        if (record[i] == '$' && record[i + 1] == '$') {
            logical_end = i;
            break;
        }
    }
    const auto text = record.first(logical_end);

    constexpr std::array<std::uint8_t, 2> open_bracket{0xa1, 0x79};
    constexpr std::array<std::uint8_t, 2> close_bracket{0xa1, 0x7a};
    if (text.size() >= open_bracket.size() &&
        std::equal(open_bracket.begin(), open_bracket.end(), text.begin())) {
        const auto close = std::search(text.begin() + 2, text.end(),
                                       close_bracket.begin(), close_bracket.end());
        if (close != text.end()) {
            result.name.assign(text.begin() + 2, close);
            result.description.assign(close + 2, text.end());
            return result;
        }
    }

    // Id zero is the one shipped bare-name record (Big5 "none").  Empty and
    // any future unbracketed records naturally use the same representation.
    result.name.assign(text.begin(), text.end());
    return result;
}

ItemTextDatabase ItemTextDatabase::load(
    const std::filesystem::path& item2_executable) {
    return from_archive(ScriptArchive::load(item2_executable));
}

ItemTextDatabase ItemTextDatabase::from_archive(const ScriptArchive& archive) {
    if (archive.entry_count() < 3) {
        throw std::runtime_error("ITEM2 archive has no text records");
    }
    ItemTextDatabase result;
    result.texts_.reserve(archive.entry_count() - 2);
    for (std::size_t id = 0; id + 2 < archive.entry_count(); ++id) {
        if (id > 0xffffU) throw std::runtime_error("ITEM2 archive has too many records");
        result.texts_.push_back(
            ItemText::parse(static_cast<std::uint16_t>(id), archive.entry(id + 2)));
    }
    return result;
}

const ItemText& ItemTextDatabase::at(std::uint16_t id) const {
    if (id >= texts_.size()) {
        throw std::out_of_range("ITEM2 text id is out of range");
    }
    return texts_[id];
}

}  // namespace swd2
