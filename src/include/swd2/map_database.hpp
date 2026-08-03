#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace swd2 {

// MAPA.EXE is not executable code. Its MZ load image is the immutable world
// database. MAPZ.DA0..5/DAQ use the same image layout without MAPA's final
// 0xffff marker and contain the mutable copy saved by RPG.EXE.
struct MapAreaRecord {
    std::uint16_t flags{};
    std::uint16_t auxiliary{};
    std::array<std::vector<std::uint16_t>, 11> entity_fields;
    std::string graphics_path;
    std::string layout_path;
    std::string music_path;
    std::string event_archive_path;
    std::string event_font_path;

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return entity_fields.front().size();
    }
};

struct MapEntityRecord {
    std::uint16_t sprite{};
    std::uint16_t direction{};
    std::uint16_t cell_offset{};
    std::uint16_t behavior{};
    std::uint16_t movement_delay{};
    std::int16_t render_x_offset{};
    std::int16_t render_y_offset{};
    std::uint16_t animation_length{};
    std::uint16_t flags{};
    std::uint16_t event_directory_offset{};
    std::uint16_t animation_frame{};
};

[[nodiscard]] MapEntityRecord map_entity(const MapAreaRecord& area, std::size_t index);

struct MapLocationRecord {
    // Byte position of this location's pointer in the archive directory. The
    // original engine stores this value (not an array index) at SAVE.DA+0x424.
    std::uint16_t directory_offset{};
    std::uint16_t map_position{};
    std::uint16_t viewport_x{};
    std::uint16_t viewport_y{};
    std::uint16_t actor_screen_x{};
    std::uint16_t actor_screen_y{};
    std::uint16_t actor_direction{};
    std::array<std::uint8_t, 14> big5_name{};
    std::uint16_t area_offset{};
    MapAreaRecord area;
};

class MapDatabase {
public:
    static MapDatabase load(const std::filesystem::path& path);
    // Writes the original MZ-wrapped image with only proven mutable MAPZ words
    // patched in place. Unknown directory metadata and string packing are
    // preserved byte-for-byte rather than being regenerated heuristically.
    void save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<MapLocationRecord>& locations() const noexcept {
        return locations_;
    }
    [[nodiscard]] const MapLocationRecord& location_at_directory_offset(
        std::uint16_t directory_offset) const;
    [[nodiscard]] MapLocationRecord& location_at_directory_offset(
        std::uint16_t directory_offset);
    [[nodiscard]] std::size_t unique_area_count() const noexcept { return unique_area_count_; }
    [[nodiscard]] bool has_trailing_sentinel() const noexcept { return has_trailing_sentinel_; }

    // RPG event opcode 34 addresses a word as
    // area+6 + field*entity_count*2 + signed_byte_offset.  Shared area
    // pointers are represented as copies in this class, so this mutator also
    // propagates the write to every location that aliases the same area.
    void mutate_area_word(std::uint16_t location_directory_offset,
                          std::uint16_t field, std::int16_t byte_offset,
                          std::uint16_t value, bool additive);

private:
    std::vector<MapLocationRecord> locations_;
    std::size_t unique_area_count_{};
    bool has_trailing_sentinel_{};
    std::vector<std::uint8_t> file_bytes_;
    std::size_t header_size_{};
    std::size_t image_end_{};
};

}  // namespace swd2
