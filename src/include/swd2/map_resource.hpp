#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swd2 {

struct MapLayoutHeader {
    std::uint16_t directory_bytes{};
    std::uint16_t image_end{};
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint16_t cell_base{};
    std::uint16_t layer_count{};
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
    [[nodiscard]] std::uint16_t cell_base() const noexcept {
        return layout_.cell_base;
    }
    [[nodiscard]] bool has_fixed_background_layer() const noexcept {
        return !fixed_background_cells_.empty();
    }
    [[nodiscard]] const std::array<std::uint16_t, 24>& animation_words() const noexcept {
        return animation_words_;
    }
    [[nodiscard]] const std::array<std::uint8_t, 768>& palette() const noexcept {
        return palette_;
    }
    [[nodiscard]] IndexedMapImage render(bool include_overlays = true) const;
    // Area flag 1000h selects a two-record RAP. Record 1 is a fixed 40x25
    // background while a viewport of record 0 is overlaid with zero cells
    // transparent, exactly as RPG.EXE:01f4/02af composes it.
    [[nodiscard]] IndexedMapImage render_viewport(
        std::uint16_t viewport_x, std::uint16_t viewport_y,
        bool include_overlays = true) const;
    [[nodiscard]] IndexedMapImage render_viewport_background(
        std::uint16_t viewport_x, std::uint16_t viewport_y,
        bool include_overlays = true) const;
    void composite_viewport_foreground(
        std::span<std::uint8_t> pixels,
        std::uint16_t viewport_x, std::uint16_t viewport_y,
        bool include_overlays = true) const;

private:
    std::uint16_t tile_count_{};
    std::array<std::uint8_t, 768> palette_{};
    std::array<std::uint16_t, 24> animation_words_{};
    // RS1..RS4 are the four interleaved VGA byte planes of every 8x8 tile.
    std::array<std::vector<std::uint8_t>, 4> planes_;
    MapLayoutHeader layout_{};
    std::vector<std::uint16_t> cells_;
    std::vector<std::uint16_t> fixed_background_cells_;
    std::vector<MapOverlay> overlays_;
};

// RPG.EXE:5e16 treats the final 24 RSK words as six four-word palette-cycle
// records: color-shift count, first RGB byte offset, last RGB byte offset and
// an interval/phase word. `runtime_words` is mutable because the high byte of
// every fourth word is the original fixed-point phase accumulator.
bool advance_map_palette(
    std::array<std::uint8_t, 768>& palette,
    std::array<std::uint16_t, 24>& runtime_words);

}  // namespace swd2
