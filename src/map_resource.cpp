#include "swd2/map_resource.hpp"

#include "swd2/rsk_decoder.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>

namespace swd2 {

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open map resource: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("map resource word is out of bounds");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

std::filesystem::path with_extension(std::filesystem::path base, const std::string& extension) {
    base.replace_extension(extension);
    return base;
}

void draw_tile(std::span<std::uint8_t> destination,
               std::size_t destination_width,
               std::size_t destination_height,
               const std::array<std::vector<std::uint8_t>, 4>& planes,
               std::uint16_t tile_count, std::size_t tile_x,
               std::size_t tile_y, std::uint16_t encoded_tile,
               bool transparent) {
    if (encoded_tile == 0xffffU) return;
    const auto tile = static_cast<std::size_t>(encoded_tile & 0x07ffU);
    if (tile >= tile_count) {
        throw std::runtime_error(
            "reachable RAP/RRO tile is outside its graphics dictionary");
    }
    const auto source = tile * 16U;
    for (std::size_t row = 0; row < 8U; ++row) {
        if (tile_y * 8U + row >= destination_height ||
            tile_x * 8U + 7U >= destination_width) {
            continue;
        }
        for (std::size_t group = 0; group < 2U; ++group) {
            for (std::size_t plane = 0; plane < planes.size(); ++plane) {
                const auto x = tile_x * 8U + group * 4U + plane;
                const auto y = tile_y * 8U + row;
                const auto color = planes[plane][source + row * 2U + group];
                if (!transparent || color != 0xfeU) {
                    destination[y * destination_width + x] = color;
                }
            }
        }
    }
}

}  // namespace

MapResource MapResource::load(const std::filesystem::path& base_path) {
    return load(base_path, base_path);
}

MapResource MapResource::load(const std::filesystem::path& graphics_base_path,
                              const std::filesystem::path& layout_base_path) {
    MapResource result;
    const auto metadata = read_file(with_extension(graphics_base_path, ".RSK"));
    if (metadata.size() != 818) {
        throw std::runtime_error("raw map RSK metadata must contain exactly 818 bytes");
    }
    result.tile_count_ = read_u16(metadata, 0);
    std::copy_n(metadata.begin() + 2, result.palette_.size(), result.palette_.begin());
    for (std::size_t i = 0; i < result.animation_words_.size(); ++i) {
        result.animation_words_[i] = read_u16(metadata, 770 + i * 2);
    }
    for (std::size_t group = 0; group < 6; ++group) {
        const auto base = group * 4U;
        const auto count = result.animation_words_[base];
        if (count == 0) continue;
        const auto first = result.animation_words_[base + 1U];
        const auto last = result.animation_words_[base + 2U];
        if (first % 3U != 0U || last % 3U != 0U ||
            last + 3U > result.palette_.size() ||
            static_cast<std::size_t>(first) +
                    static_cast<std::size_t>(count) * 3U != last) {
            throw std::runtime_error("map RSK palette animation is malformed");
        }
    }

    for (std::size_t plane = 0; plane < result.planes_.size(); ++plane) {
        const auto extension = ".RS" + std::to_string(plane + 1);
        auto decoded = decode_rsk_block(read_file(with_extension(graphics_base_path, extension)));
        const auto expected = static_cast<std::size_t>(result.tile_count_) * 16;
        if (decoded.data.size() != expected) {
            throw std::runtime_error(extension + " does not contain tile_count * 16 bytes");
        }
        result.planes_[plane] = std::move(decoded.data);
    }

    const auto layout_data =
        decode_rsk_block(read_file(with_extension(layout_base_path, ".RAP"))).data;
    if (layout_data.size() < 12U) {
        throw std::runtime_error("decoded RAP map is shorter than its directory");
    }
    // RPG.EXE:01f4 indexes a u16 pointer directory by SAVE+411, then reads
    // height,width and leaves SAVE+40f on the first cell. Most maps have one
    // record (directory bytes=4). ZD has two records (directory bytes=6): a
    // scrolling 93x25 foreground and a fixed 40x25 background.
    const auto directory_bytes = static_cast<std::size_t>(
        read_u16(layout_data, 0));
    if (directory_bytes < 4U || (directory_bytes & 1U) != 0U ||
        directory_bytes + 2U > layout_data.size()) {
        throw std::runtime_error("decoded RAP map has an invalid pointer directory");
    }
    const auto pointer_count = directory_bytes / 2U;
    if (pointer_count < 2U || pointer_count > 3U) {
        throw std::runtime_error("decoded RAP map has an unsupported layer count");
    }
    std::vector<std::uint16_t> pointers;
    pointers.reserve(pointer_count);
    for (std::size_t offset = 0; offset < directory_bytes; offset += 2U) {
        const auto pointer = read_u16(layout_data, offset);
        if (pointer < directory_bytes || pointer + 2U > layout_data.size() ||
            (!pointers.empty() && pointer <= pointers.back())) {
            throw std::runtime_error("decoded RAP map pointer is invalid");
        }
        pointers.push_back(pointer);
    }
    if (read_u16(layout_data, pointers.back()) != 0xffffU) {
        throw std::runtime_error("decoded RAP map has no final frame sentinel");
    }

    struct Record {
        std::uint16_t height{};
        std::uint16_t width{};
        std::uint16_t cell_base{};
        std::vector<std::uint16_t> cells;
    };
    std::vector<Record> records;
    records.reserve(pointer_count - 1U);
    for (std::size_t index = 0; index + 1U < pointers.size(); ++index) {
        const auto offset = static_cast<std::size_t>(pointers[index]);
        Record record;
        record.height = read_u16(layout_data, offset);
        record.width = read_u16(layout_data, offset + 2U);
        if (offset + 4U > 0xffffU) {
            throw std::runtime_error("decoded RAP cell base exceeds a DOS offset");
        }
        record.cell_base = static_cast<std::uint16_t>(offset + 4U);
        const auto count = static_cast<std::size_t>(record.width) * record.height;
        const auto end = offset + 4U + count * 2U;
        if (record.width == 0U || record.height == 0U ||
            end != pointers[index + 1U]) {
            throw std::runtime_error("decoded RAP map record dimensions are invalid");
        }
        record.cells.reserve(count);
        for (std::size_t cell = 0; cell < count; ++cell) {
            record.cells.push_back(read_u16(layout_data, offset + 4U + cell * 2U));
        }
        records.push_back(std::move(record));
    }
    result.layout_ = {
        static_cast<std::uint16_t>(directory_bytes), pointers.back(),
        records[0].width, records[0].height, records[0].cell_base,
        static_cast<std::uint16_t>(records.size()),
    };
    result.cells_ = std::move(records[0].cells);
    if (records.size() == 2U) {
        if (records[1].width != 40U || records[1].height != 25U) {
            throw std::runtime_error(
                "decoded layered RAP background is not the original 40x25 page");
        }
        result.fixed_background_cells_ = std::move(records[1].cells);
    }

    const auto overlay_path = with_extension(layout_base_path, ".RRO");
    if (std::filesystem::is_regular_file(overlay_path)) {
        const auto overlay_data = decode_rsk_block(read_file(overlay_path)).data;
        std::size_t offset = 0;
        bool terminated = false;
        while (offset + 2 <= overlay_data.size()) {
            const auto x = read_u16(overlay_data, offset);
            if (x == 0xffff) {
                if (offset + 2 != overlay_data.size()) {
                    throw std::runtime_error("RRO overlay has data after its sentinel");
                }
                terminated = true;
                break;
            }
            if (offset + 6 > overlay_data.size()) {
                throw std::runtime_error("RRO overlay record is truncated");
            }
            result.overlays_.push_back({x, read_u16(overlay_data, offset + 2),
                                       read_u16(overlay_data, offset + 4)});
            offset += 6;
        }
        if (!terminated) {
            throw std::runtime_error("RRO overlay has no final sentinel");
        }
    }
    return result;
}

IndexedMapImage MapResource::render(bool include_overlays) const {
    if (has_fixed_background_layer()) {
        auto image = render_viewport_background(0, 0, include_overlays);
        composite_viewport_foreground(image.pixels, 0, 0, include_overlays);
        return image;
    }
    IndexedMapImage image;
    constexpr std::size_t rendered_tile_size = 8;
    image.width = static_cast<std::size_t>(layout_.width) * rendered_tile_size;
    image.height = static_cast<std::size_t>(layout_.height) * rendered_tile_size;
    image.pixels.assign(image.width * image.height, 0);
    image.palette = palette_;

    for (std::size_t y = 0; y < layout_.height; ++y) {
        for (std::size_t x = 0; x < layout_.width; ++x) {
            draw_tile(image.pixels, image.width, image.height, planes_,
                      tile_count_, x, y, cells_[y * layout_.width + x], false);
        }
    }
    if (include_overlays) {
        for (const auto& overlay : overlays_) {
            if (overlay.x >= layout_.width || overlay.y >= layout_.height) {
                continue;
            }
            draw_tile(image.pixels, image.width, image.height, planes_,
                      tile_count_, overlay.x, overlay.y, overlay.tile, true);
        }
    }
    return image;
}

IndexedMapImage MapResource::render_viewport_background(
    std::uint16_t viewport_x, std::uint16_t viewport_y,
    bool include_overlays) const {
    constexpr std::size_t columns = 40U;
    constexpr std::size_t rows = 25U;
    if (static_cast<std::size_t>(viewport_x) + columns > layout_.width ||
        static_cast<std::size_t>(viewport_y) + rows > layout_.height) {
        throw std::runtime_error("RAP viewport is outside the map layout");
    }
    IndexedMapImage image;
    image.width = columns * 8U;
    image.height = rows * 8U;
    image.pixels.assign(image.width * image.height, 0U);
    image.palette = palette_;
    const auto background_cell = [&](std::size_t x, std::size_t y) {
        auto value = cells_[(static_cast<std::size_t>(viewport_y) + y) *
                                layout_.width +
                            static_cast<std::size_t>(viewport_x) + x];
        if (!fixed_background_cells_.empty()) {
            // RPG.EXE:02af first copies record 1 into a temporary 40x25
            // page, then writes each nonzero record-0 cell after clearing
            // BH's high nibble. In particular, a primary 4000h cell is also
            // present (without 4000h) below actors before 0375 draws the raw
            // primary cell again above them.
            const auto primary = static_cast<std::uint16_t>(value & 0x0fffU);
            value = primary != 0U
                ? primary
                : fixed_background_cells_[y * columns + x];
        }
        return value;
    };
    for (std::size_t y = 0; y < rows; ++y) {
        for (std::size_t x = 0; x < columns; ++x) {
            const auto value = background_cell(x, y);
            if ((value & 0x4000U) == 0U) {
                draw_tile(image.pixels, image.width, image.height, planes_,
                          tile_count_, x, y, value, false);
            }
        }
    }
    if (include_overlays) {
        for (const auto& overlay : overlays_) {
            // RPG.EXE:057f draws 2000h RRO records before actors. RRO uses a
            // different ordering bit from the 4000h bit in RAP cells.
            if ((overlay.tile & 0x2000U) == 0U ||
                overlay.x < viewport_x || overlay.y < viewport_y) {
                continue;
            }
            const auto x = static_cast<std::size_t>(overlay.x - viewport_x);
            const auto y = static_cast<std::size_t>(overlay.y - viewport_y);
            if (x < columns && y < rows) {
                draw_tile(image.pixels, image.width, image.height, planes_,
                          tile_count_, x, y, overlay.tile, true);
            }
        }
    }
    return image;
}

void MapResource::composite_viewport_foreground(
    std::span<std::uint8_t> pixels, std::uint16_t viewport_x,
    std::uint16_t viewport_y, bool include_overlays) const {
    constexpr std::size_t columns = 40U;
    constexpr std::size_t rows = 25U;
    constexpr std::size_t width = columns * 8U;
    constexpr std::size_t height = rows * 8U;
    if (pixels.size() != width * height ||
        static_cast<std::size_t>(viewport_x) + columns > layout_.width ||
        static_cast<std::size_t>(viewport_y) + rows > layout_.height) {
        throw std::runtime_error("RAP foreground viewport is invalid");
    }
    for (std::size_t y = 0; y < rows; ++y) {
        for (std::size_t x = 0; x < columns; ++x) {
            // RPG.EXE:0375 reads record 0 directly. It never revisits the
            // fixed background record used by the special 1000h path.
            const auto value = cells_[
                (static_cast<std::size_t>(viewport_y) + y) * layout_.width +
                static_cast<std::size_t>(viewport_x) + x];
            if ((value & 0x4000U) != 0U) {
                draw_tile(pixels, width, height, planes_, tile_count_, x, y,
                          value, false);
            }
        }
    }
    if (include_overlays) {
        for (const auto& overlay : overlays_) {
            // RPG.EXE:0611 draws RRO records without 2000h after actors.
            if ((overlay.tile & 0x2000U) != 0U ||
                overlay.x < viewport_x || overlay.y < viewport_y) {
                continue;
            }
            const auto x = static_cast<std::size_t>(overlay.x - viewport_x);
            const auto y = static_cast<std::size_t>(overlay.y - viewport_y);
            if (x < columns && y < rows) {
                draw_tile(pixels, width, height, planes_, tile_count_, x, y,
                          overlay.tile, true);
            }
        }
    }
}

IndexedMapImage MapResource::render_viewport(
    std::uint16_t viewport_x, std::uint16_t viewport_y,
    bool include_overlays) const {
    auto image = render_viewport_background(
        viewport_x, viewport_y, include_overlays);
    composite_viewport_foreground(
        image.pixels, viewport_x, viewport_y, include_overlays);
    return image;
}

bool advance_map_palette(
    std::array<std::uint8_t, 768>& palette,
    std::array<std::uint16_t, 24>& runtime_words) {
    bool changed = false;
    for (std::size_t group = 0; group < 6; ++group) {
        const auto base = group * 4U;
        const auto count = runtime_words[base];
        if (count == 0) continue;

        auto phase = static_cast<std::uint16_t>(
            runtime_words[base + 3U] + 0x0100U);
        const auto interval = static_cast<std::uint8_t>(phase);
        const auto elapsed = static_cast<std::uint8_t>(phase >> 8U);
        if (elapsed < interval) {
            runtime_words[base + 3U] = phase;
            continue;
        }
        // 5e3b clears AH but preserves the interval in AL.
        runtime_words[base + 3U] = interval;

        const auto first = static_cast<std::size_t>(runtime_words[base + 1U]);
        const auto last = static_cast<std::size_t>(runtime_words[base + 2U]);
        const std::array<std::uint8_t, 3> tail{
            palette[last], palette[last + 1U], palette[last + 2U]};
        for (auto offset = last; offset > first; offset -= 3U) {
            std::copy_n(palette.begin() + static_cast<std::ptrdiff_t>(offset - 3U),
                        3, palette.begin() + static_cast<std::ptrdiff_t>(offset));
        }
        std::copy(tail.begin(), tail.end(),
                  palette.begin() + static_cast<std::ptrdiff_t>(first));
        changed = true;
    }
    return changed;
}

}  // namespace swd2
