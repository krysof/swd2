#include "swd2/dialogue.hpp"

#include <stdexcept>

namespace swd2 {

DialoguePage render_dialogue_page(const LegacyFont& font, std::span<const std::uint8_t> text,
                                  std::size_t start_offset, std::size_t width,
                                  std::size_t height, std::uint8_t color,
                                  const LegacyFont* name_font) {
    if (start_offset > text.size()) throw std::out_of_range("dialogue offset is out of bounds");
    DialoguePage result;
    result.width = width;
    result.height = height;
    result.pixels.assign(width * height, 0);
    result.next_offset = start_offset;
    std::size_t x = 0;
    std::size_t y = 0;
    auto cursor = start_offset;
    while (cursor < text.size()) {
        if (cursor + 1 < text.size() && text[cursor] == '%' && text[cursor + 1] == '%') {
            result.next_offset = cursor + 2;
            result.cursor_x = x;
            result.cursor_y = y;
            result.page_break = true;
            result.has_more = result.next_offset < text.size();
            return result;
        }
        if (cursor + 1 < text.size() && text[cursor] == '#' && text[cursor + 1] == '#') {
            x = 0;
            y += 16;
            cursor += 2;
            continue;
        }
        if (text[cursor] == ' ') {
            x += 4;
            ++cursor;
            continue;
        }
        if (cursor + 1 >= text.size()) throw std::runtime_error("truncated Big5 dialogue code");
        const auto code = static_cast<std::uint16_t>(text[cursor]) << 8U | text[cursor + 1];
        const auto glyph = !font.contains(code) && name_font &&
                                   name_font->contains(code)
                               ? name_font->rasterize(code)
                               : font.rasterize_or_first(code);
        for (std::size_t row = 0; row < LegacyFont::glyph_height; ++row) {
            for (std::size_t column = 0; column < LegacyFont::glyph_width; ++column) {
                if (glyph[row * LegacyFont::glyph_width + column] != 0 &&
                    x + column < width && y + row < height) {
                    result.pixels[(y + row) * width + x + column] = color;
                }
            }
        }
        x += LegacyFont::glyph_width;
        cursor += 2;
        result.glyph_end_offsets.push_back(cursor);
    }
    result.next_offset = cursor;
    result.cursor_x = x;
    result.cursor_y = y;
    return result;
}

}  // namespace swd2
