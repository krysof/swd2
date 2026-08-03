#pragma once

#include "swd2/launcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace swd2 {

class SharedState {
public:
    static constexpr std::size_t byte_size = 0x546;
    using Storage = std::array<std::uint8_t, byte_size>;

    static SharedState load(const std::filesystem::path& path);
    static SharedState from_bytes(std::span<const std::uint8_t> bytes);
    void save(const std::filesystem::path& path) const;

    [[nodiscard]] std::uint8_t u8(std::size_t offset) const;
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const;
    [[nodiscard]] std::int16_t i16(std::size_t offset) const;
    void set_u8(std::size_t offset, std::uint8_t value);
    void set_u16(std::size_t offset, std::uint16_t value);

    [[nodiscard]] std::string dos_string(std::size_t offset, std::size_t capacity) const;
    void set_dos_string(std::size_t offset, std::size_t capacity, const std::string& value);

    [[nodiscard]] const Storage& bytes() const noexcept { return bytes_; }

    // Known path buffers from the initial SAVE.DA files. Their gameplay meaning
    // is confirmed by RPG.EXE's embedded resource accesses.
    [[nodiscard]] std::string area_graphics_path() const { return dos_string(0x42d, 22); }
    [[nodiscard]] std::string area_collision_path() const { return dos_string(0x443, 22); }
    [[nodiscard]] std::string music_path() const { return dos_string(0x459, 22); }
    [[nodiscard]] std::string event_executable_path() const { return dos_string(0x46f, 22); }
    [[nodiscard]] std::string event_data_path() const { return dos_string(0x485, 24); }
    [[nodiscard]] std::uint16_t actor_screen_x() const { return u16(0x012); }
    [[nodiscard]] std::uint16_t actor_screen_y() const { return u16(0x02a); }
    [[nodiscard]] std::int16_t actor_x_offset() const { return i16(0x042); }
    [[nodiscard]] std::int16_t actor_y_offset() const { return i16(0x05a); }
    [[nodiscard]] std::uint16_t actor_sprite_base() const { return u16(0x072); }
    [[nodiscard]] std::uint16_t actor_animation() const { return u16(0x08a); }
    [[nodiscard]] std::uint16_t actor_direction() const { return u16(0x0a2); }
    [[nodiscard]] std::uint16_t viewport_columns() const { return u16(0x413); }
    [[nodiscard]] std::uint16_t viewport_rows() const { return u16(0x415); }
    [[nodiscard]] std::uint16_t map_width() const { return u16(0x417); }
    [[nodiscard]] std::uint16_t map_height() const { return u16(0x419); }
    [[nodiscard]] std::uint16_t viewport_x() const { return u16(0x41b); }
    [[nodiscard]] std::uint16_t viewport_y() const { return u16(0x41d); }
    // Byte offset into MAPA/MAPZ's pointer directory (twice the logical slot).
    [[nodiscard]] std::uint16_t map_location_directory_offset() const { return u16(0x424); }
    [[nodiscard]] std::uint16_t map_position() const { return u16(0x408); }
    [[nodiscard]] std::uint16_t battle_encounter_offset() const { return u16(0x4a0); }
    [[nodiscard]] std::uint16_t battle_auxiliary() const { return u16(0x51c); }
    [[nodiscard]] std::uint16_t world_x() const {
        return static_cast<std::uint16_t>(viewport_x() + ((actor_screen_x() + 2U) >> 1U));
    }
    [[nodiscard]] std::uint16_t world_y() const {
        return static_cast<std::uint16_t>(viewport_y() + ((actor_screen_y() + 16U) >> 3U));
    }
    void set_actor_screen_x(std::uint16_t value) { set_u16(0x012, value); }
    void set_actor_screen_y(std::uint16_t value) { set_u16(0x02a, value); }
    void set_actor_animation(std::uint16_t value) { set_u16(0x08a, value); }
    void set_actor_direction(std::uint16_t value) { set_u16(0x0a2, value); }
    void set_viewport_x(std::uint16_t value) { set_u16(0x41b, value); }
    void set_viewport_y(std::uint16_t value) { set_u16(0x41d, value); }

private:
    Storage bytes_{};
};

struct SharedTransfer {
    Marker marker{Marker::none};
    SharedState state;

    static constexpr std::size_t byte_size = SharedState::byte_size + 2;
    static SharedTransfer from_bytes(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::array<std::uint8_t, byte_size> bytes() const;
};

}  // namespace swd2
