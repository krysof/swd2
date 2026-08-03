#include "swd2/meo.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace swd2 {

MeoStatus MeoCopyProtection::input(MeoInput input_value, std::uint8_t expected_color) {
    if (input_value == MeoInput::up) {
        choice_ = choice_ == 0 ? 4 : choice_ - 1;
        return MeoStatus::waiting;
    }
    if (input_value == MeoInput::down) {
        choice_ = (choice_ + 1) % 5;
        return MeoStatus::waiting;
    }

    ++confirmations_;
    if (patched_ || choice_ + 1 == expected_color) {
        ++correct_;
    }
    if (confirmations_ < 3) {
        return MeoStatus::waiting;
    }
    return correct_ == 3 ? MeoStatus::accepted : MeoStatus::rejected;
}

namespace {

void blit(IndexedFrame& frame, const SpriteArchive& archive, std::size_t sprite_index,
          int x, int y, std::optional<std::uint8_t> transparent) {
    const auto& info = archive.sprites().at(sprite_index);
    const auto source = archive.pixels(sprite_index);
    for (std::size_t row = 0; row < info.height; ++row) {
        const int target_y = y + static_cast<int>(row);
        if (target_y < 0 || target_y >= static_cast<int>(IndexedFrame::height)) {
            continue;
        }
        for (std::size_t column = 0; column < info.width; ++column) {
            const int target_x = x + static_cast<int>(column);
            if (target_x < 0 || target_x >= static_cast<int>(IndexedFrame::width)) {
                continue;
            }
            const auto color = source[row * info.width + column];
            if (transparent && color == *transparent) {
                continue;
            }
            frame.pixels[static_cast<std::size_t>(target_y) * IndexedFrame::width +
                         static_cast<std::size_t>(target_x)] = color;
        }
    }
}

std::pair<unsigned, unsigned> challenge_coordinates(unsigned minute, unsigned second) {
    second = std::min(second, 59U);
    minute = std::min(minute, 55U);
    return {second * 2U + 15U, minute * 3U + 5U};
}

}  // namespace

IndexedFrame render_meo_frame(const SpriteArchive& archive, std::size_t choice,
                              unsigned minute, unsigned second, bool rejected) {
    if (archive.sprites().size() < 5) {
        throw std::runtime_error("MEO archive does not contain its five expected sprites");
    }
    IndexedFrame frame;
    frame.pixels.fill(0x13);
    if (archive.has_palette()) {
        frame.palette = archive.palette();
    }

    blit(frame, archive, 0, 3, 3, std::nullopt);
    blit(frame, archive, rejected ? 2 : 1, 11, 180, std::nullopt);
    blit(frame, archive, 3, 249, static_cast<int>((choice % 5) * 32 + 14), 0x13);
    const auto [x, y] = challenge_coordinates(minute, second);
    blit(frame, archive, 4, static_cast<int>(x + 1), static_cast<int>(y + 1), 0x13);
    return frame;
}

std::uint8_t meo_expected_color(const IndexedFrame& frame, unsigned minute, unsigned second) {
    const auto [x, y] = challenge_coordinates(minute, second);
    if (x + 1 >= IndexedFrame::width || y + 1 >= IndexedFrame::height) {
        return 0;
    }
    const auto color = frame.pixels[y * IndexedFrame::width + x];
    if (color < 1 || color > 5) {
        return 0;
    }
    if (frame.pixels[y * IndexedFrame::width + x + 1] != color ||
        frame.pixels[(y + 1) * IndexedFrame::width + x] != color ||
        frame.pixels[(y + 1) * IndexedFrame::width + x + 1] != color) {
        return 0;
    }
    return color;
}

}  // namespace swd2
