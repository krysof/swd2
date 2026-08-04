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
    if (layout_data.size() < 12) {
        throw std::runtime_error("decoded RAP map is shorter than its header");
    }
    result.layout_ = {
        read_u16(layout_data, 0), read_u16(layout_data, 2), read_u16(layout_data, 4),
        read_u16(layout_data, 6), read_u16(layout_data, 8), read_u16(layout_data, 10),
    };
    if (result.layout_.tile_size != 4) {
        throw std::runtime_error("unsupported RAP tile size");
    }
    const auto cells = static_cast<std::size_t>(result.layout_.width) * result.layout_.height;
    if (layout_data.size() != 12 + cells * 2) {
        throw std::runtime_error("decoded RAP size does not match map dimensions");
    }
    result.cells_.reserve(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        result.cells_.push_back(read_u16(layout_data, 12 + i * 2));
    }

    const auto overlay_path = with_extension(layout_base_path, ".RRO");
    if (std::filesystem::is_regular_file(overlay_path)) {
        const auto overlay_data = decode_rsk_block(read_file(overlay_path)).data;
        std::size_t offset = 0;
        while (offset + 2 <= overlay_data.size()) {
            const auto x = read_u16(overlay_data, offset);
            if (x == 0xffff) {
                if (offset + 2 != overlay_data.size()) {
                    throw std::runtime_error("RRO overlay has data after its sentinel");
                }
                break;
            }
            if (offset + 6 > overlay_data.size()) {
                throw std::runtime_error("RRO overlay record is truncated");
            }
            result.overlays_.push_back({x, read_u16(overlay_data, offset + 2),
                                       read_u16(overlay_data, offset + 4)});
            offset += 6;
        }
    }
    return result;
}

IndexedMapImage MapResource::render(bool include_overlays) const {
    IndexedMapImage image;
    // RAP calls this value 4, matching the four packed byte planes.  The
    // original VGA renderer interleaves those planes into an 8x8 pixel tile.
    constexpr std::size_t rendered_tile_size = 8;
    image.width = static_cast<std::size_t>(layout_.width) * rendered_tile_size;
    image.height = static_cast<std::size_t>(layout_.height) * rendered_tile_size;
    image.pixels.assign(image.width * image.height, 0);
    image.palette = palette_;

    const auto draw_tile = [&](std::size_t x, std::size_t y, std::uint16_t encoded_tile,
                               bool transparent) {
        if (encoded_tile == 0xffff) {
            return;
        }
        // The upper five bits are map attributes.  The original 16-bit
        // tile*16 calculation also discards them, leaving an 11-bit index.
        const auto tile = static_cast<std::size_t>(encoded_tile & 0x07ffU);
        if (tile >= tile_count_) {
            throw std::runtime_error("map references a tile outside RS planes");
        }
        const auto source = tile * 16;
        for (std::size_t row = 0; row < 8; ++row) {
            if (y * rendered_tile_size + row >= image.height ||
                x * rendered_tile_size + 7 >= image.width) {
                continue;
            }
            for (std::size_t group = 0; group < 2; ++group) {
                for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
                    const auto destination_x = x * rendered_tile_size + group * 4 + plane;
                    const auto color = planes_[plane][source + row * 2 + group];
                    // RPG.EXE's RRO compositor treats -2 (0xfe) as transparent;
                    // ordinary RAP map cells are copied without a color key.
                    if (!transparent || color != 0xfe) {
                        image.pixels[(y * rendered_tile_size + row) * image.width +
                                     destination_x] = color;
                    }
                }
            }
        }
    };

    for (std::size_t y = 0; y < layout_.height; ++y) {
        for (std::size_t x = 0; x < layout_.width; ++x) {
            draw_tile(x, y, cells_[y * layout_.width + x], false);
        }
    }
    if (include_overlays) {
        for (const auto& overlay : overlays_) {
            draw_tile(overlay.x, overlay.y, overlay.tile, true);
        }
    }
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
