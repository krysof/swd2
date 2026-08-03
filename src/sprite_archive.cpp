#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace swd2 {

namespace {

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("sprite archive word is out of bounds");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

}  // namespace

SpriteArchive SpriteArchive::parse(std::vector<std::uint8_t> decoded_resource) {
    if (decoded_resource.size() < 4) {
        throw std::runtime_error("decoded sprite archive is too short");
    }

    SpriteArchive archive;
    archive.data_ = std::move(decoded_resource);
    const std::span<const std::uint8_t> data(archive.data_);
    std::unordered_set<std::size_t> seen_offsets;
    std::size_t directory = 0;
    std::size_t smallest_object = data.size();
    std::size_t sentinel = std::numeric_limits<std::size_t>::max();

    while (directory + 2 <= data.size()) {
        const auto object_offset = static_cast<std::size_t>(read_u16(data, directory));
        directory += 2;
        if (object_offset + 2 > data.size()) {
            throw std::runtime_error("sprite archive directory points outside resource");
        }
        if (!seen_offsets.insert(object_offset).second) {
            throw std::runtime_error("sprite archive contains a repeated directory offset");
        }

        const auto height = read_u16(data, object_offset);
        if (height == 0xffff) {
            sentinel = object_offset;
            break;
        }
        if (object_offset + 4 > data.size()) {
            throw std::runtime_error("sprite archive object header is truncated");
        }
        const auto width = read_u16(data, object_offset + 2);
        if (width == 0 || height == 0) {
            throw std::runtime_error("sprite archive contains a zero-sized image");
        }
        const auto pixel_count = static_cast<std::size_t>(width) * height;
        if (pixel_count > data.size() || object_offset + 4 > data.size() - pixel_count) {
            throw std::runtime_error("sprite archive image pixels are truncated");
        }
        smallest_object = std::min(smallest_object, object_offset);
        archive.sprites_.push_back({width, height, object_offset + 4});

        if (archive.sprites_.size() > 32'768) {
            throw std::runtime_error("sprite archive directory has no sentinel");
        }
    }

    if (sentinel == std::numeric_limits<std::size_t>::max() || archive.sprites_.empty()) {
        throw std::runtime_error("sprite archive sentinel was not found");
    }
    if (smallest_object < directory) {
        throw std::runtime_error("sprite archive directory overlaps image data");
    }

    // The original renderer passes sentinel+3 to the VGA palette loader.
    constexpr std::size_t palette_skip = 3;
    if (sentinel + palette_skip <= data.size() &&
        data.size() - (sentinel + palette_skip) >= archive.palette_.size()) {
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(sentinel + palette_skip),
                    archive.palette_.size(), archive.palette_.begin());
        archive.has_palette_ = true;
    }
    return archive;
}

std::span<const std::uint8_t> SpriteArchive::pixels(std::size_t index) const {
    const auto& sprite = sprites_.at(index);
    return std::span<const std::uint8_t>(data_).subspan(
        sprite.pixel_offset, static_cast<std::size_t>(sprite.width) * sprite.height);
}

}  // namespace swd2
