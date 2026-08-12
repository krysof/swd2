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

class FigTimerClock {
public:
    // Advance the original INT-08h counter without rounding every individual
    // wait independently.  The phase accumulator starts at half a divisor so
    // any sequence of waits has exactly the same nearest-millisecond rounded
    // cumulative wall time as one wait for the sum of its ticks.
    constexpr std::chrono::milliseconds advance(std::uint32_t ticks) {
        const auto numerator = remainder_ +
            static_cast<std::uint64_t>(ticks) * fig_timer_pit_clocks * 1000U;
        const auto milliseconds = numerator / fig_pit_input_hz;
        remainder_ = numerator % fig_pit_input_hz;
        return std::chrono::milliseconds(milliseconds);
    }

    [[nodiscard]] constexpr std::uint64_t remainder() const noexcept {
        return remainder_;
    }

private:
    std::uint64_t remainder_{fig_pit_input_hz / 2U};
};

constexpr std::chrono::milliseconds fig_timer_ticks(std::uint32_t ticks) {
    const auto numerator = static_cast<std::uint64_t>(ticks) *
                           fig_timer_pit_clocks * 1000U;
    return std::chrono::milliseconds(
        (numerator + fig_pit_input_hz / 2U) / fig_pit_input_hz);
}

}  // namespace swd2
