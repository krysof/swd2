#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace swd2 {

// MAP0.EXE is a data-only MZ image used by RPG.EXE:e94.  Each area flag's
// low twelve bits address a pointer-directory entry whose list describes one
// or more row-expanded trigger ranges. Endpoint advances use the original
// 16-bit wrapping arithmetic.
struct MapTransitionRecord {
    std::uint8_t row_count{};
    std::uint8_t flag_index{};
    std::uint16_t first_cell{};
    std::uint16_t last_cell{};
    std::uint16_t action{};

    [[nodiscard]] bool is_special() const noexcept {
        return (action & 0x4000U) != 0;
    }
    [[nodiscard]] bool uses_relative_placement() const noexcept {
        return (action & 0x8000U) != 0;
    }
    [[nodiscard]] bool sets_travel_flag() const noexcept {
        return (action & 0x2000U) != 0;
    }
    [[nodiscard]] std::uint16_t destination_directory_offset() const noexcept {
        return static_cast<std::uint16_t>(action & 0x1fffU);
    }
    [[nodiscard]] std::uint16_t special_action() const noexcept {
        return static_cast<std::uint16_t>(action & 0x3fffU);
    }
};

class MapTransitionDatabase {
public:
    static MapTransitionDatabase load(const std::filesystem::path& path);

    [[nodiscard]] std::span<const MapTransitionRecord> records(
        std::uint16_t area_flags) const;
    [[nodiscard]] std::optional<MapTransitionRecord> match(
        std::uint16_t area_flags, std::uint16_t actor_cell,
        std::uint16_t map_width) const;
    [[nodiscard]] std::size_t area_count() const noexcept {
        return area_count_;
    }
    [[nodiscard]] std::size_t record_count() const noexcept {
        return record_count_;
    }

private:
    std::vector<std::vector<MapTransitionRecord>> directory_;
    std::size_t area_count_{};
    std::size_t record_count_{};
};

}  // namespace swd2
