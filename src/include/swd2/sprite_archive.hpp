#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

struct SpriteInfo {
    std::uint16_t width{};
    std::uint16_t height{};
    std::size_t pixel_offset{};
};

class SpriteArchive {
public:
    static SpriteArchive parse(std::vector<std::uint8_t> decoded_resource);

    [[nodiscard]] const std::vector<SpriteInfo>& sprites() const noexcept { return sprites_; }
    [[nodiscard]] std::span<const std::uint8_t> pixels(std::size_t index) const;
    [[nodiscard]] bool has_palette() const noexcept { return has_palette_; }
    [[nodiscard]] const std::array<std::uint8_t, 768>& palette() const noexcept { return palette_; }

private:
    std::vector<std::uint8_t> data_;
    std::vector<SpriteInfo> sprites_;
    std::array<std::uint8_t, 768> palette_{};
    bool has_palette_{};
};

}  // namespace swd2
