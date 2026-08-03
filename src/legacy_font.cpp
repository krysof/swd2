#include "swd2/legacy_font.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t little_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

}  // namespace

LegacyFont LegacyFont::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open legacy DSK font: " + path.string());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>()};
    if (bytes.size() < 2) throw std::runtime_error("legacy DSK font is truncated");
    const auto count = static_cast<std::size_t>(little_u16(bytes, 0));
    if (bytes.size() != 2 + count * (2 + glyph_bytes)) {
        throw std::runtime_error("legacy DSK font size does not match its glyph count");
    }

    LegacyFont result;
    result.codes_.reserve(count);
    result.glyphs_.reserve(count);
    const auto bitmap_start = 2 + count * 2;
    for (std::size_t index = 0; index < count; ++index) {
        // Big5 byte pairs use display/network order, unlike numeric DOS words.
        result.codes_.push_back(static_cast<std::uint16_t>(bytes[2 + index * 2]) << 8U |
                                bytes[3 + index * 2]);
        std::array<std::uint8_t, glyph_bytes> bitmap{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(bitmap_start + index * glyph_bytes),
                    glyph_bytes, bitmap.begin());
        result.glyphs_.push_back(bitmap);
    }
    return result;
}

bool LegacyFont::contains(std::uint16_t big5_code) const noexcept {
    return std::find(codes_.begin(), codes_.end(), big5_code) != codes_.end();
}

std::span<const std::uint8_t, LegacyFont::glyph_bytes>
LegacyFont::glyph(std::uint16_t big5_code) const {
    const auto found = std::find(codes_.begin(), codes_.end(), big5_code);
    if (found == codes_.end()) throw std::out_of_range("Big5 glyph is absent from DSK font");
    return glyphs_[static_cast<std::size_t>(found - codes_.begin())];
}

std::array<std::uint8_t, LegacyFont::glyph_width * LegacyFont::glyph_height>
LegacyFont::rasterize(std::uint16_t big5_code) const {
    const auto bits = glyph(big5_code);
    std::array<std::uint8_t, glyph_width * glyph_height> pixels{};
    for (std::size_t row = 0; row < glyph_height; ++row) {
        const auto word = static_cast<std::uint16_t>(bits[row * 2]) << 8U | bits[row * 2 + 1];
        for (std::size_t column = 0; column < glyph_width; ++column) {
            pixels[row * glyph_width + column] = (word & (0x8000U >> column)) != 0 ? 1 : 0;
        }
    }
    return pixels;
}

}  // namespace swd2
