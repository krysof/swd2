#pragma once

#include <chrono>
#include <cstdint>

namespace swd2 {

// FIG installs its counter ISR behind the music driver's INT 08h handler.
// The music driver programs PIT channel 0 to divisor 4000h and chains FIG
// once every four interrupts, so DATA:3ca5 advances once per 10000h PIT input
// clocks: the original BIOS timer period, not a 70 Hz VGA refresh.
inline constexpr std::uint64_t fig_pit_input_hz = 1'193'182U;
inline constexpr std::uint64_t fig_timer_pit_clocks = 65'536U;

constexpr std::chrono::milliseconds fig_timer_ticks(std::uint32_t ticks) {
    const auto numerator = static_cast<std::uint64_t>(ticks) *
                           fig_timer_pit_clocks * 1000U;
    return std::chrono::milliseconds(
        (numerator + fig_pit_input_hz / 2U) / fig_pit_input_hz);
}

}  // namespace swd2
