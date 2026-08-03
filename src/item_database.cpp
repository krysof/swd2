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
    return result;
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
