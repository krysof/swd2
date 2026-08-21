#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swd2 {

struct PlanarSpriteFrame {
    std::uint16_t width{};
    std::uint16_t height{};
    std::vector<std::uint8_t> pixels;
};

// DE### is a five-file VGA full-page animation bundle. RAP stores one u16
// lookup per opaque 8x8 output tile; zero selects dictionary tile zero rather
// than transparency. RSK supplies the palette/tile count; RS1..RS4 contain
// the four byte planes of that 8x8 tile dictionary. This is distinct from both
// ordinary map RAP files and the compact MAN1/MEO sprite-archive format.
class PlanarSpriteSet {
public:
    static PlanarSpriteSet load(const std::filesystem::path& base_path);
    // Some DE### resources contain only a RAP frame directory and reuse the
    // most recent full DE dictionary. Keep graphics and layout bases separate
    // just like the corresponding dual-path map loader.
    static PlanarSpriteSet load(const std::filesystem::path& graphics_base_path,
                                const std::filesystem::path& layout_base_path);

    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
    [[nodiscard]] const PlanarSpriteFrame& frame(std::size_t index) const {
        return frames_.at(index);
    }
    [[nodiscard]] const std::array<std::uint8_t, 768>& palette() const noexcept {
        return palette_;
    }

private:
    std::array<std::uint8_t, 768> palette_{};
    std::vector<PlanarSpriteFrame> frames_;
};

}  // namespace swd2
