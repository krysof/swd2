#pragma once

#include "swd2/battle_rules.hpp"
#include "swd2/shared_state.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace swd2 {

// FIG 1000:2b41 does not use a conventional PRNG. It reads successive words
// from CS:[028a + SharedState[049c]], returns the remainder by the requested
// modulus, advances by two, and wraps 2000 -> 1000. Capturing that code window
// makes replays identical without executing any x86 instructions.
class FigBattleRandom {
public:
    static FigBattleRandom load(const std::filesystem::path& fig_executable,
                                std::uint16_t cursor);
    static FigBattleRandom load(const std::filesystem::path& fig_executable,
                                const SharedState& state) {
        return load(fig_executable, state.u16(0x49c));
    }

    std::uint16_t draw(std::uint16_t modulus);
    [[nodiscard]] BattleRandom function() {
        return [this](std::uint16_t modulus) { return draw(modulus); };
    }
    [[nodiscard]] std::uint16_t cursor() const noexcept { return cursor_; }
    void store(SharedState& state) const { state.set_u16(0x49c, cursor_); }

private:
    std::vector<std::uint8_t> code_window_;
    std::uint16_t cursor_{0x1000};
};

}  // namespace swd2
