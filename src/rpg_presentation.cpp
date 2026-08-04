#include "swd2/rpg_presentation.hpp"

#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace swd2 {

namespace {

std::size_t rpg_data_address(std::span<const std::uint8_t> load_image,
                             std::size_t entry_offset,
                             std::uint16_t data_offset) {
    if (entry_offset > load_image.size() ||
        load_image.size() - entry_offset < 5U ||
        load_image[entry_offset] != 0xb8U ||
        load_image[entry_offset + 3U] != 0x8eU ||
        load_image[entry_offset + 4U] != 0xd8U) {
        throw std::runtime_error(
            "RPG executable does not begin with mov ax,DATA; mov ds,ax");
    }
    const auto data_paragraph =
        static_cast<std::size_t>(load_image[entry_offset + 1U]) |
        (static_cast<std::size_t>(load_image[entry_offset + 2U]) << 8U);
    const auto address = data_paragraph * 16U + data_offset;
    if (address > load_image.size()) {
        throw std::runtime_error("RPG embedded data address is outside the load image");
    }
    return address;
}

}  // namespace

std::vector<std::uint8_t> composite_mode_x_address_offset(
    std::span<const std::uint8_t> previous,
    std::span<const std::uint8_t> rendered,
    std::uint16_t address_bytes,
    std::size_t width) {
    if (width == 0 || (width & 3U) != 0 || previous.size() != rendered.size() ||
        previous.size() % width != 0) {
        throw std::runtime_error("mode-X offset composite has invalid surface dimensions");
    }

    auto result = std::vector<std::uint8_t>(previous.begin(), previous.end());
    const auto address_width = width / 4U;
    const auto address_size = rendered.size() / 4U;
    for (std::size_t y = 0; y < rendered.size() / width; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto source_address = y * address_width + x / 4U;
            const auto destination_address = source_address + address_bytes;
            if (destination_address >= address_size) continue;
            const auto destination_y = destination_address / address_width;
            const auto destination_x =
                (destination_address % address_width) * 4U + (x & 3U);
            result[destination_y * width + destination_x] =
                rendered[y * width + x];
        }
    }
    return result;
}

void apply_rpg_event_monochrome_filter(
    std::span<std::uint8_t> pixels,
    std::span<const std::uint8_t, 768> palette) {
    std::array<std::uint8_t, 16> translation{};
    for (std::size_t offset = 0; offset < translation.size(); ++offset) {
        const auto color = 0x10U + offset;
        const auto red = static_cast<unsigned>(palette[color * 3U]);
        const auto green = static_cast<unsigned>(palette[color * 3U + 1U]);
        const auto blue = static_cast<unsigned>(palette[color * 3U + 2U]);
        const auto luminance = (red + green * 2U + blue) >> 4U;
        translation[offset] = static_cast<std::uint8_t>(0x1fU - luminance);
    }
    for (auto& pixel : pixels) {
        if (pixel >= 0x10U && pixel < 0x20U) {
            pixel = translation[pixel - 0x10U];
        }
    }
}

std::vector<std::uint8_t> extract_rpg_embedded_text(
    std::span<const std::uint8_t> load_image,
    std::size_t entry_offset,
    std::uint16_t data_offset) {
    const auto string_offset = rpg_data_address(
        load_image, entry_offset, data_offset);
    if (string_offset >= load_image.size()) {
        throw std::runtime_error("RPG embedded text address is outside the load image");
    }

    std::vector<std::uint8_t> result;
    for (auto cursor = string_offset; cursor + 1U < load_image.size(); ++cursor) {
        if (load_image[cursor] == '$' && load_image[cursor + 1U] == '$') {
            return result;
        }
        result.push_back(load_image[cursor]);
    }
    throw std::runtime_error("RPG embedded text has no $$ terminator");
}

std::vector<std::uint8_t> extract_rpg_embedded_data(
    std::span<const std::uint8_t> load_image,
    std::size_t entry_offset,
    std::uint16_t data_offset,
    std::size_t size) {
    const auto address = rpg_data_address(load_image, entry_offset, data_offset);
    if (size > load_image.size() - address) {
        throw std::runtime_error("RPG embedded data block is truncated");
    }
    return {load_image.begin() + static_cast<std::ptrdiff_t>(address),
            load_image.begin() + static_cast<std::ptrdiff_t>(address + size)};
}

void draw_rpg_selector_panel(
    std::span<std::uint8_t> surface,
    std::size_t width,
    std::size_t height,
    const SpriteArchive& menu_sprites,
    int left,
    int top,
    int columns,
    int rows) {
    if (width == 0 || height == 0 || surface.size() != width * height ||
        columns < 0 || rows < 0 || menu_sprites.sprites().size() <= 93U) {
        throw std::runtime_error("invalid RPG selector panel arguments");
    }
    const auto sprite = [&](std::size_t frame, int x_byte, int y,
                            bool transparent) {
        const auto& info = menu_sprites.sprites().at(frame);
        const auto source = menu_sprites.pixels(frame);
        const auto left_pixel = x_byte * 4;
        for (std::size_t row = 0; row < info.height; ++row) {
            for (std::size_t column = 0; column < info.width; ++column) {
                const auto color = source[row * info.width + column];
                if (transparent && color == 0xfeU) continue;
                const auto x = left_pixel + static_cast<int>(column);
                const auto target_y = y + static_cast<int>(row);
                if (x < 0 || target_y < 0 ||
                    x >= static_cast<int>(width) ||
                    target_y >= static_cast<int>(height)) {
                    continue;
                }
                surface[static_cast<std::size_t>(target_y) * width +
                        static_cast<std::size_t>(x)] = color;
            }
        }
    };

    // RPG.EXE:281d. 653a copies the body pieces opaquely; only the tiny
    // 92/93 overhang corners go through 6577's 0xfe transparency key.
    sprite(92, left, top, true);
    sprite(83, left, top + 4, false);
    auto y = top + 12;
    for (auto row = 0; row < rows; ++row, y += 16) {
        sprite(84, left, y, false);
    }
    sprite(85, left, y, false);
    sprite(93, left, y + 8, true);

    auto x = left + 8;
    for (auto column = 0; column < columns; ++column, x += 8) {
        sprite(86, x, top + 4, false);
        y = top + 12;
        for (auto row = 0; row < rows; ++row, y += 16) {
            sprite(87, x, y, false);
        }
        sprite(88, x, y, false);
    }

    sprite(92, x + 6, top, true);
    sprite(89, x, top + 4, false);
    y = top + 12;
    for (auto row = 0; row < rows; ++row, y += 16) {
        sprite(90, x, y, false);
    }
    sprite(91, x, y, false);
    sprite(93, x + 6, y + 8, true);
}

void draw_rpg_compact_panel(
    std::span<std::uint8_t> surface,
    std::size_t width,
    std::size_t height,
    const SpriteArchive& menu_sprites,
    int left,
    int top,
    int columns,
    int rows) {
    if (width == 0 || height == 0 || surface.size() != width * height ||
        columns < 0 || rows < 0 || menu_sprites.sprites().size() <= 173U) {
        throw std::runtime_error("invalid RPG compact-panel arguments");
    }
    const auto sprite = [&](std::size_t frame, int x_byte, int y) {
        const auto& info = menu_sprites.sprites().at(frame);
        const auto source = menu_sprites.pixels(frame);
        const auto left_pixel = x_byte * 4;
        for (std::size_t row = 0; row < info.height; ++row) {
            for (std::size_t column = 0; column < info.width; ++column) {
                const auto x = left_pixel + static_cast<int>(column);
                const auto target_y = y + static_cast<int>(row);
                if (x < 0 || target_y < 0 || x >= static_cast<int>(width) ||
                    target_y >= static_cast<int>(height)) {
                    continue;
                }
                surface[static_cast<std::size_t>(target_y) * width +
                        static_cast<std::size_t>(x)] =
                    source[row * info.width + column];
            }
        }
    };
    const auto row = [&](std::size_t left_frame, std::size_t middle_frame,
                         std::size_t right_frame, int y) {
        sprite(left_frame, left, y);
        auto x = left + 6;
        for (auto column = 0; column < columns; ++column, x += 2) {
            sprite(middle_frame, x, y);
        }
        sprite(right_frame, x, y);
    };

    row(80, 81, 82, top);
    auto y = top + 8;
    for (auto body = 0; body < rows; ++body, y += 16) {
        row(168, 169, 170, y);
    }
    row(171, 172, 173, y);
}

RpgSaveSelectorResult RpgSaveSlotSelector::input(InputAction action) noexcept {
    if (action == InputAction::quit) return RpgSaveSelectorResult::quit;

    if (!confirming_) {
        switch (action) {
        case InputAction::left:
            slot_ = slot_ == 0U ? 4U : static_cast<std::uint8_t>(slot_ - 1U);
            break;
        case InputAction::right:
            if (slot_ < 4U) ++slot_;
            break;
        case InputAction::confirm:
            confirming_ = true;
            break;
        case InputAction::cancel:
            return RpgSaveSelectorResult::cancelled;
        default:
            // 4e4b does not inspect the vertical direction flags.
            break;
        }
        return RpgSaveSelectorResult::waiting;
    }

    // 46cc configures 3948 with mask 03h: up/down are disabled, Left/Right
    // select zero/one and leave status 2 so the prompt redraws. Enter accepts
    // the current selection. 4e4b forgets to inspect 3801 after this nested
    // prompt, so Escape acts on the current choice -- default zero commits.
    switch (action) {
    case InputAction::left:
        confirmation_choice_ = 0;
        return RpgSaveSelectorResult::waiting;
    case InputAction::right:
        confirmation_choice_ = 1;
        return RpgSaveSelectorResult::waiting;
    case InputAction::confirm:
    case InputAction::cancel:
        return confirmation_choice_ == 0U
                   ? RpgSaveSelectorResult::committed
                   : RpgSaveSelectorResult::cancelled;
    default:
        return RpgSaveSelectorResult::waiting;
    }
}

std::optional<std::size_t> rpg_party_target_for_direction(
    InputAction action,
    std::size_t party_count) noexcept {
    std::size_t target{};
    switch (action) {
    case InputAction::left:
        target = 0;
        break;
    case InputAction::right:
        target = 1;
        break;
    case InputAction::up:
        target = 2;
        break;
    case InputAction::down:
        target = 3;
        break;
    default:
        return std::nullopt;
    }
    return target < party_count ? std::optional<std::size_t>(target)
                                : std::nullopt;
}

}  // namespace swd2
