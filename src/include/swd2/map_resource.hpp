#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace swd2 {

struct MapLayoutHeader {
    std::uint16_t tile_size{};
    std::uint16_t flags{};
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint16_t origin_x{};
    std::uint16_t origin_y{};
};

struct MapOverlay {
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t tile{};
};

struct IndexedMapImage {
    std::size_t width{};
    std::size_t height{};
    std::vector<std::uint8_t> pixels;
    std::array<std::uint8_t, 768> palette{};
};

class MapResource {
public:
    // base_path has no extension, e.g. game/T4/AREA2.
    static MapResource load(const std::filesystem::path& base_path);
    // Eight locations use a different RAP/RRO layout over the same tile set.
    // MAPA/MAPZ stores both paths, and the original loader treats them
    // independently.
    static MapResource load(const std::filesystem::path& graphics_base_path,
                            const std::filesystem::path& layout_base_path);

    [[nodiscard]] std::uint16_t tile_count() const noexcept { return tile_count_; }
    [[nodiscard]] const MapLayoutHeader& layout() const noexcept { return layout_; }
    [[nodiscard]] const std::vector<std::uint16_t>& cells() const noexcept { return cells_; }
    [[nodiscard]] const std::vector<MapOverlay>& overlays() const noexcept { return overlays_; }
    [[nodiscard]] const std::array<std::uint16_t, 24>& animation_words() const noexcept {
        return animation_words_;
    }
    [[nodiscard]] IndexedMapImage render(bool include_overlays = true) const;

private:
    std::uint16_t tile_count_{};
    std::array<std::uint8_t, 768> palette_{};
    std::array<std::uint16_t, 24> animation_words_{};
    // RS1..RS4 are the four interleaved VGA byte planes of every 8x8 tile.
    std::array<std::vector<std::uint8_t>, 4> planes_;
    MapLayoutHeader layout_{};
    std::vector<std::uint16_t> cells_;
    std::vector<MapOverlay> overlays_;
};

}  // namespace swd2
