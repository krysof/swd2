#include "swd2/map_database.hpp"

#include "swd2/shared_state.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <stdexcept>
#include <system_error>

namespace swd2 {

namespace {

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("MAPA/MAPZ word is out of bounds");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

void set_u16(std::span<std::uint8_t> bytes, std::size_t offset,
             std::uint16_t value) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("MAPA/MAPZ patch word is out of bounds");
    }
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

std::string c_string(std::span<const std::uint8_t> image, std::uint16_t offset) {
    if (offset >= image.size()) {
        throw std::runtime_error("MAPA/MAPZ string pointer is out of bounds");
    }
    const auto begin = image.begin() + offset;
    const auto end = std::find(begin, image.end(), 0);
    if (end == image.end()) {
        throw std::runtime_error("MAPA/MAPZ string is unterminated");
    }
    return {begin, end};
}

// MAPA/MAPZ stores resource names without a drive ("\\SWD2\\..." and
// "CHNA?.EXE"), while RPG:10fd commits drive-qualified names to SAVE.DA.
// The released saves use E:, and downstream DOS file helpers expect that
// two-byte prefix before normalizing the path.  Keeping the prefix is also
// required for a boundary state to resume in the untouched RPG.EXE.
std::string persisted_resource_path(const std::string& path) {
    if (path.empty() || (path.size() >= 2U && path[1] == ':')) return path;
    return "E:" + path;
}

MapAreaRecord parse_area(std::span<const std::uint8_t> image, std::uint16_t offset) {
    MapAreaRecord result;
    result.flags = u16(image, offset);
    result.auxiliary = u16(image, static_cast<std::size_t>(offset) + 2);
    const auto entity_count =
        static_cast<std::size_t>(u16(image, static_cast<std::size_t>(offset) + 4));
    auto cursor = static_cast<std::size_t>(offset) + 6;

    // FUN_1000_10fd invokes the same word-copy loop eleven times. The DOS code
    // keeps these as structure-of-arrays fields, so preserve that exact
    // representation until every field has been semantically named.
    for (auto& field : result.entity_fields) {
        field.reserve(entity_count);
        for (std::size_t index = 0; index < entity_count; ++index) {
            field.push_back(u16(image, cursor));
            cursor += 2;
        }
    }

    std::array<std::string*, 5> paths = {
        &result.graphics_path, &result.layout_path, &result.music_path,
        &result.event_archive_path, &result.event_font_path,
    };
    for (auto* path : paths) {
        *path = c_string(image, u16(image, cursor));
        cursor += 2;
    }
    return result;
}

}  // namespace

MapDatabase MapDatabase::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open MAPA/MAPZ database: " + path.string());
    const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>()};
    if (file.size() < 28 || u16(file, 0) != 0x5a4d) {
        throw std::runtime_error("MAPA/MAPZ database has no MZ wrapper");
    }
    const auto header_size = static_cast<std::size_t>(u16(file, 8)) * 16;
    if (header_size > file.size() || u16(file, 6) != 0 || u16(file, 0x14) != 0 ||
        u16(file, 0x16) != 0) {
        throw std::runtime_error("MAPA/MAPZ wrapper describes native or truncated code");
    }
    const auto image = std::span<const std::uint8_t>(file).subspan(header_size);
    if (image.size() < 8) throw std::runtime_error("MAPA/MAPZ image is truncated");

    const auto sentinel = static_cast<std::size_t>(u16(image, 0));
    const auto directory_bytes = static_cast<std::size_t>(u16(image, 2));
    if ((directory_bytes & 1U) != 0 || directory_bytes < 8 || directory_bytes > sentinel ||
        sentinel > image.size()) {
        throw std::runtime_error("invalid MAPA/MAPZ pointer directory");
    }

    MapDatabase result;
    result.file_bytes_ = file;
    result.header_size_ = header_size;
    result.image_end_ = sentinel;
    if (sentinel == image.size()) {
        result.has_trailing_sentinel_ = false;  // writable MAPZ.DA image
    } else if (sentinel + 2 == image.size() && u16(image, sentinel) == 0xffff) {
        result.has_trailing_sentinel_ = true;   // immutable MAPA.EXE image
    } else {
        throw std::runtime_error("invalid MAPA/MAPZ end marker");
    }

    std::vector<std::uint16_t> offsets;
    offsets.reserve(directory_bytes / 2);
    for (std::size_t cursor = 0; cursor < directory_bytes; cursor += 2) {
        const auto offset = u16(image, cursor);
        if (offset < directory_bytes || offset > sentinel) {
            throw std::runtime_error("MAPA/MAPZ directory pointer is out of bounds");
        }
        offsets.push_back(offset);
    }
    if (offsets.front() != sentinel || offsets.size() < 5) {
        throw std::runtime_error("MAPA/MAPZ directory header is inconsistent");
    }

    std::set<std::uint16_t> area_offsets;
    // Slots 0..3 are the end marker and CHAIN/MAP1 metadata. Slot 4 onward
    // consists of the 466 fixed-size location records used by RPG.EXE.
    result.locations_.reserve(offsets.size() - 4);
    for (std::size_t index = 4; index < offsets.size(); ++index) {
        const auto start = static_cast<std::size_t>(offsets[index]);
        if (start + 28 > sentinel) {
            throw std::runtime_error("MAPA/MAPZ location record is truncated");
        }
        MapLocationRecord location;
        location.directory_offset = static_cast<std::uint16_t>(index * 2);
        location.map_position = u16(image, start);
        location.viewport_x = u16(image, start + 2);
        location.viewport_y = u16(image, start + 4);
        location.actor_screen_x = u16(image, start + 6);
        location.actor_screen_y = u16(image, start + 8);
        location.actor_direction = u16(image, start + 10);
        std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(start + 12),
                    location.big5_name.size(), location.big5_name.begin());
        location.area_offset = u16(image, start + 26);
        location.area = parse_area(image, location.area_offset);
        area_offsets.insert(location.area_offset);
        result.locations_.push_back(std::move(location));
    }
    result.unique_area_count_ = area_offsets.size();
    return result;
}

void MapDatabase::save(const std::filesystem::path& path) const {
    if (file_bytes_.empty()) {
        throw std::runtime_error("MAPA/MAPZ database has no source image to save");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create MAPZ database: " + path.string());
        }
        output.write(reinterpret_cast<const char*>(file_bytes_.data()),
                     static_cast<std::streamsize>(file_bytes_.size()));
        if (!output) {
            throw std::runtime_error("failed to write MAPZ database: " + path.string());
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        // Windows does not replace an existing destination with rename. Keep
        // the portable fallback narrow and never leave the temporary behind.
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot install MAPZ database: " + error.message());
    }
}

const MapLocationRecord& MapDatabase::location_at_directory_offset(
    std::uint16_t directory_offset) const {
    if ((directory_offset & 1U) != 0 || directory_offset < 8) {
        throw std::out_of_range("MAPA/MAPZ location directory offset is invalid");
    }
    const auto index = static_cast<std::size_t>((directory_offset - 8) / 2);
    if (index >= locations_.size() || locations_[index].directory_offset != directory_offset) {
        throw std::out_of_range("MAPA/MAPZ location directory offset is out of range");
    }
    return locations_[index];
}

MapLocationRecord& MapDatabase::location_at_directory_offset(std::uint16_t directory_offset) {
    return const_cast<MapLocationRecord&>(
        static_cast<const MapDatabase&>(*this).location_at_directory_offset(directory_offset));
}

void MapDatabase::mutate_area_word(std::uint16_t location_directory_offset,
                                   std::uint16_t field, std::int16_t byte_offset,
                                   std::uint16_t value, bool additive) {
    const auto area_offset =
        location_at_directory_offset(location_directory_offset).area_offset;
    const auto& area = location_at_directory_offset(location_directory_offset).area;
    const auto relative = static_cast<std::int64_t>(6) +
        static_cast<std::int64_t>(field) *
            static_cast<std::int64_t>(area.entity_count()) * 2 +
        byte_offset;
    if (relative < 0 ||
        static_cast<std::uint64_t>(area_offset) +
                static_cast<std::uint64_t>(relative) + 2U >
            image_end_) {
        throw std::runtime_error("event MAPZ mutation has an invalid image offset");
    }

    // 5a1f writes a 16-bit word to the computed ES:DI address without an
    // alignment or entity-field bounds check.  Shipped, reachable records use
    // both behaviours that the typed-only implementation used to reject:
    // CHNA2 advances the two RAP string pointers just after field 10, while
    // CHNA5 writes across the byte boundary between two field-3 words.
    // Patch the preserved image first, then parse the complete area payload
    // again so entity fields and resource paths stay synchronized.
    const auto image_offset = static_cast<std::size_t>(area_offset) +
                              static_cast<std::size_t>(relative);
    const auto file_offset = header_size_ + image_offset;
    const auto previous = u16(file_bytes_, file_offset);
    const auto patched = additive
        ? static_cast<std::uint16_t>(previous + value)
        : value;
    set_u16(file_bytes_, file_offset, patched);

    MapAreaRecord reparsed;
    try {
        const auto image = std::span<const std::uint8_t>(file_bytes_).subspan(
            header_size_, image_end_);
        reparsed = parse_area(image, area_offset);
        // No released event changes this word.  Letting it resize the eleven
        // arrays would also reinterpret every following byte and path pointer.
        if (reparsed.entity_count() != area.entity_count()) {
            throw std::runtime_error("event attempts to change a MAPZ entity count");
        }
    } catch (...) {
        set_u16(file_bytes_, file_offset, previous);
        throw;
    }

    bool found = false;
    for (auto& location : locations_) {
        if (location.area_offset != area_offset) continue;
        location.area = reparsed;
        found = true;
    }
    if (!found) {
        set_u16(file_bytes_, file_offset, previous);
        throw std::runtime_error("event references an unknown MAPZ area");
    }
}

MapEntityRecord map_entity(const MapAreaRecord& area, std::size_t index) {
    if (index >= area.entity_count()) throw std::out_of_range("MAPZ entity index is out of range");
    return {
        area.entity_fields[0][index],
        area.entity_fields[1][index],
        area.entity_fields[2][index],
        area.entity_fields[3][index],
        area.entity_fields[4][index],
        static_cast<std::int16_t>(area.entity_fields[5][index]),
        static_cast<std::int16_t>(area.entity_fields[6][index]),
        area.entity_fields[7][index],
        area.entity_fields[8][index],
        area.entity_fields[9][index],
        area.entity_fields[10][index],
    };
}

void prepare_runtime_map_area(MapAreaRecord& area) {
    if (!area.entity_sprite_resources.empty()) {
        if (area.entity_sprite_resources.size() != area.entity_count()) {
            throw std::runtime_error(
                "runtime MAPZ sprite-resource table has the wrong size");
        }
        return;
    }
    area.entity_sprite_resources.reserve(area.entity_count());
    for (auto& sprite : area.entity_fields[0]) {
        area.entity_sprite_resources.push_back(
            static_cast<std::uint8_t>(sprite >> 8U));
        sprite &= 0x00ffU;
    }
}

void install_map_location(SharedState& state, MapDatabase& database,
                          std::uint16_t encoded_directory_offset) {
    const auto relative_position =
        (encoded_directory_offset & 0x8000U) != 0;
    const auto directory_offset =
        static_cast<std::uint16_t>(encoded_directory_offset & 0x1fffU);
    const auto& location =
        database.location_at_directory_offset(directory_offset);
    const auto previous_actor_x = state.actor_screen_x();
    const auto previous_actor_y = state.actor_screen_y();
    state.set_u16(0x424, directory_offset);
    state.set_u16(0x40d, location.map_position);
    state.set_viewport_x(location.viewport_x);
    state.set_viewport_y(location.viewport_y);
    for (std::size_t i = 0; i < 12; ++i) {
        if (relative_position) {
            state.set_u16(
                0x12 + i * 2,
                static_cast<std::uint16_t>(
                    state.u16(0x12 + i * 2) + location.actor_screen_x -
                    previous_actor_x));
            state.set_u16(
                0x2a + i * 2,
                static_cast<std::uint16_t>(
                    state.u16(0x2a + i * 2) + location.actor_screen_y -
                    previous_actor_y));
        } else {
            state.set_u16(0x12 + i * 2, location.actor_screen_x);
            state.set_u16(0x2a + i * 2, location.actor_screen_y);
            state.set_u16(0xba + i * 2, 7);
        }
        state.set_u16(0xa2 + i * 2, location.actor_direction);
    }
    if (relative_position) return;

    // RPG:10fd stores SI immediately after reading flags/auxiliary/count.
    // SAVE+429 therefore contains the selected MAPZ area's first entity-field
    // word (area image offset + 6).  Opcode 3 later reopens MAPZ and adds this
    // persisted base to field*count*2+current-entity-offset before rewriting
    // the file.  Omitting this otherwise opaque SAVE word made a resumed
    // original RPG write an event mutation into the previous area's payload.
    state.set_u16(0x429, static_cast<std::uint16_t>(location.area_offset + 6U));
    for (std::size_t i = 0; i < location.big5_name.size(); ++i) {
        state.set_u8(0x3f6 + i, location.big5_name[i]);
    }
    state.set_u16(0x408, location.area.flags);
    state.set_dos_string(
        0x42d, 22, persisted_resource_path(location.area.graphics_path));
    state.set_dos_string(
        0x443, 22, persisted_resource_path(location.area.layout_path));
    state.set_dos_string(
        0x459, 22, persisted_resource_path(location.area.music_path));
    state.set_dos_string(
        0x46f, 22, persisted_resource_path(location.area.event_archive_path));
    state.set_dos_string(
        0x485, 24, persisted_resource_path(location.area.event_font_path));
}

}  // namespace swd2
