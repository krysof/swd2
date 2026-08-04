#include "swd2/planar_sprite_set.hpp"

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
    if (!input) throw std::runtime_error("cannot open planar sprite resource: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("planar sprite word is out of bounds");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::filesystem::path with_extension(std::filesystem::path base, const std::string& extension) {
    base.replace_extension(extension);
    return base;
}

}  // namespace

PlanarSpriteSet PlanarSpriteSet::load(const std::filesystem::path& base_path) {
    return load(base_path, base_path);
}

PlanarSpriteSet PlanarSpriteSet::load(
    const std::filesystem::path& graphics_base_path,
    const std::filesystem::path& layout_base_path) {
    const auto metadata = read_file(with_extension(graphics_base_path, ".RSK"));
    if (metadata.size() != 818) {
        throw std::runtime_error("planar sprite RSK metadata must contain exactly 818 bytes");
    }
    const auto tile_count = static_cast<std::size_t>(u16(metadata, 0));
    PlanarSpriteSet result;
    std::copy_n(metadata.begin() + 2, result.palette_.size(), result.palette_.begin());

    std::array<std::vector<std::uint8_t>, 4> planes;
    for (std::size_t plane = 0; plane < planes.size(); ++plane) {
        const auto extension = ".RS" + std::to_string(plane + 1);
        planes[plane] = decode_rsk_block(
            read_file(with_extension(graphics_base_path, extension))).data;
        if (planes[plane].size() != tile_count * 16) {
            throw std::runtime_error(extension + " planar sprite dictionary has the wrong size");
        }
    }

    const auto layout = decode_rsk_block(
        read_file(with_extension(layout_base_path, ".RAP"))).data;
    // RPG.EXE FUN_1000_647b walks directory pointers until the pointed-to
    // record begins with ffff. Replicate that rather than assuming a count.
    for (std::size_t directory = 0;; directory += 2) {
        const auto record_offset = static_cast<std::size_t>(u16(layout, directory));
        const auto height = u16(layout, record_offset);
        if (height == 0xffff) break;
        const auto width = u16(layout, record_offset + 2);
        const auto tile_cells = static_cast<std::size_t>(width) * height;
        if (record_offset + 4 + tile_cells * 2 > layout.size()) {
            throw std::runtime_error("planar sprite RAP frame is truncated");
        }

        const auto output_width = static_cast<std::size_t>(width) * 8U;
        const auto output_height = static_cast<std::size_t>(height) * 8U;
        if (output_width > 0xffffU || output_height > 0xffffU) {
            throw std::runtime_error("planar sprite frame dimensions overflow u16");
        }
        PlanarSpriteFrame frame{
            static_cast<std::uint16_t>(output_width),
            static_cast<std::uint16_t>(output_height),
            std::vector<std::uint8_t>(output_width * output_height, 0xfe)};
        for (std::size_t index = 0; index < tile_cells; ++index) {
            const auto tile = static_cast<std::size_t>(u16(layout, record_offset + 4 + index * 2));
            if (tile == 0) continue;  // transparent lookup used by the DOS compositor
            if (tile >= tile_count) {
                throw std::runtime_error(
                    with_extension(layout_base_path, ".RAP").string() +
                    " references tile " + std::to_string(tile) +
                    " outside dictionary " +
                    with_extension(graphics_base_path, ".RSK").string() +
                    " (" + std::to_string(tile_count) + " tiles)");
            }
            const auto tile_x = index % width;
            const auto tile_y = index / width;
            for (std::size_t y = 0; y < 8; ++y) {
                for (std::size_t x = 0; x < 8; ++x) {
                    // Four Mode-X planes each store two bytes for one 8-pixel
                    // row: x&3 selects the plane, x>>2 the byte within it.
                    const auto plane = x & 3U;
                    const auto within_tile = y * 2U + (x >> 2U);
                    const auto destination_x = tile_x * 8U + x;
                    const auto destination_y = tile_y * 8U + y;
                    frame.pixels[destination_y * output_width + destination_x] =
                        planes[plane][tile * 16U + within_tile];
                }
            }
        }
        result.frames_.push_back(std::move(frame));
    }
    if (result.frames_.empty()) throw std::runtime_error("planar sprite set has no frames");
    return result;
}

}  // namespace swd2
