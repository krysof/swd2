#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swd2 {

// DSK files used by the game are compact per-module Big5 glyph subsets.
// Each 16x15 monochrome glyph occupies 30 bytes, stored after the code table.
class LegacyFont {
public:
    static constexpr std::size_t glyph_width = 16;
    static constexpr std::size_t glyph_height = 15;
    static constexpr std::size_t glyph_bytes = 30;

    static LegacyFont load(const std::filesystem::path& path);
    static LegacyFont parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::size_t glyph_count() const noexcept { return codes_.size(); }
    [[nodiscard]] const std::vector<std::uint16_t>& codes() const noexcept { return codes_; }
    [[nodiscard]] bool contains(std::uint16_t big5_code) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t, glyph_bytes>
    glyph(std::uint16_t big5_code) const;
    [[nodiscard]] std::array<std::uint8_t, glyph_width * glyph_height>
    rasterize(std::uint16_t big5_code) const;
    // The DOS 70a6/7284 lookup leaves AX at zero when a code is absent and
    // therefore draws glyph slot zero instead of throwing or skipping it.
    [[nodiscard]] std::array<std::uint8_t, glyph_width * glyph_height>
    rasterize_or_first(std::uint16_t big5_code) const;
    // RPG.EXE's new-game name editor keeps the sixteen Big5 codes fixed and
    // replaces only their 30-byte bitmaps. Expose that literal operation so
    // the active NAME font can remain a portable in-memory save component.
    void replace_glyph(
        std::size_t index,
        std::span<const std::uint8_t, glyph_bytes> bitmap);
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;

private:
    std::vector<std::uint16_t> codes_;
    std::vector<std::array<std::uint8_t, glyph_bytes>> glyphs_;
};

}  // namespace swd2
