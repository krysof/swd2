#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace swd2 {

class SharedState;

// MAPA.EXE is not executable code. Its MZ load image is the immutable world
// database. MAPZ.DA0..5/DAQ use the same image layout without MAPA's final
// 0xffff marker and contain the mutable copy saved by RPG.EXE.
struct MapAreaRecord {
    std::uint16_t flags{};
    std::uint16_t auxiliary{};
    std::array<std::vector<std::uint16_t>, 11> entity_fields;
    // RPG:10fd turns field 0's high byte into a per-entity sprite-segment
    // pointer, then masks the transient field word to its low byte. Keep the
    // loaded resource separate so opcodes 3/39 can replace the frame base
    // without accidentally switching the already-loaded SA archive.
    std::vector<std::uint8_t> entity_sprite_resources;
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

// Converts a raw MAPA/MAPZ area copy into the transient representation built
// by RPG:10fd. Calling it again on an already-prepared runtime area is a no-op.
void prepare_runtime_map_area(MapAreaRecord& area);

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
    // Stable MZ-wrapped byte image used by deterministic replay checkpoints.
    // Opcode 34 patches this exact buffer before typed areas are reparsed, so
    // hashing it observes both aligned fields and unaligned/path-pointer writes.
    [[nodiscard]] std::span<const std::uint8_t> serialized_bytes() const noexcept {
        return file_bytes_;
    }

    // RPG event opcode 34 addresses an unaligned-capable word as
    // area+6 + field*entity_count*2 + signed_byte_offset. Shipped events may
    // deliberately reach the resource-pointer words after the eleven entity
    // arrays. Shared area pointers are represented as copies in this class,
    // so this mutator reparses the payload and propagates the write to every
    // location that aliases the same area.
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

// Installs one MAPA/MAPZ location with RPG.EXE:10fd semantics.  Bit 8000h
// selects the relative-placement early return used by event opcode 37; the
// ordinary form also installs SAVE+429's persisted entity-array image base,
// the area's name, flags and five resource paths.
void install_map_location(SharedState& state, MapDatabase& database,
                          std::uint16_t encoded_directory_offset);

}  // namespace swd2
