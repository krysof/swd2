#include "swd2/battle_random.hpp"

#include "swd2/mz_executable.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace swd2 {

FigBattleRandom FigBattleRandom::load(
    const std::filesystem::path& fig_executable, std::uint16_t cursor) {
    // RPG:4cae adds DOS's one-byte hundredth counter to SAVE+49c after every
    // load. That deliberately permits an odd cursor (a physical reference
    // capture, for example, enters the world at 1077h). 8086 word reads do
    // not require alignment, and FIG:2b41 likewise reads CS:[028a+cursor]
    // byte-for-byte before advancing it by two. Rejecting odd values here
    // therefore let RPG draw the first battle page and then abort before the
    // command panel whenever a loaded game happened to receive an odd
    // hundredth.
    if (cursor < 0x1000 || cursor > 0x2000) {
        throw std::runtime_error("FIG random cursor is outside the exact 1000..2000 domain");
    }
    const auto executable = dos::MzExecutable::load(fig_executable);
    std::ifstream input(fig_executable, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open FIG random-code window");
    input.seekg(static_cast<std::streamoff>(executable.header_size()));
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    constexpr std::size_t code_bias = 0x028a;
    // 2b41 computes CS:[028a+cursor] before incrementing and normalizing the
    // cursor. RPG:202f can hand FIG the boundary value 2000h, so retain the
    // extra word at 228ah rather than rejecting a valid module transition.
    constexpr std::size_t window_finish = code_bias + 0x2002;
    if (image.size() < window_finish) {
        throw std::runtime_error("FIG load image is too short for its random-code window");
    }

    FigBattleRandom result;
    result.code_window_.assign(image.begin() + code_bias,
                               image.begin() + window_finish);
    result.cursor_ = cursor;
    return result;
}

std::uint16_t FigBattleRandom::draw(std::uint16_t modulus) {
    if (modulus == 0) throw std::invalid_argument("FIG random modulus must not be zero");
    const auto value = static_cast<std::uint16_t>(code_window_[cursor_]) |
                       (static_cast<std::uint16_t>(code_window_[cursor_ + 1]) << 8U);
    cursor_ = static_cast<std::uint16_t>(cursor_ + 2U);
    if (cursor_ >= 0x2000) cursor_ = 0x1000;
    return static_cast<std::uint16_t>(value % modulus);
}

}  // namespace swd2
