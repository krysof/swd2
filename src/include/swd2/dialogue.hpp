#pragma once

#include "swd2/legacy_font.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

struct DialoguePage {
    std::size_t width{};
    std::size_t height{};
    std::vector<std::uint8_t> pixels;
    std::size_t next_offset{};
    std::size_t cursor_x{};
    std::size_t cursor_y{};
    bool page_break{};
    bool has_more{};
};

// Renders the next page of Big5 dialogue with the original 16x15 DSK glyphs.
// "##" starts a new 16-pixel row and "%%" ends the current page. The returned
// cursor is the exact pixel position used by RPG's MENU continuation marker.
DialoguePage render_dialogue_page(const LegacyFont& font, std::span<const std::uint8_t> text,
                                  std::size_t start_offset = 0, std::size_t width = 320,
                                  std::size_t height = 200, std::uint8_t color = 15,
                                  const LegacyFont* name_font = nullptr);

}  // namespace swd2
