#pragma once

#include <cstddef>
#include <cstdint>

namespace swd2 {

// Bridges a 70 Hz DOS music duration to an integer-rate PCM device without
// losing the fractional sample at every loop boundary. The decoded PCM keeps
// floor(timer_ticks * sample_rate / 70) samples; advance_loop_boundary()
// schedules one duplicate boundary sample whenever the accumulated remainder
// crosses a whole device sample.
class AudioLoopClock {
public:
    AudioLoopClock() = default;
    AudioLoopClock(std::size_t timer_ticks, std::uint32_t sample_rate) {
        reset(timer_ticks, sample_rate);
    }

    void reset(std::size_t timer_ticks, std::uint32_t sample_rate);

    [[nodiscard]] std::size_t samples_per_loop() const noexcept {
        return samples_per_loop_;
    }
    [[nodiscard]] std::uint32_t sample_remainder() const noexcept {
        return sample_remainder_;
    }

    // Call exactly once after the integer PCM body of a completed loop. True
    // means the backend must emit one extra boundary sample before restarting.
    [[nodiscard]] bool advance_loop_boundary() noexcept;

private:
    std::size_t samples_per_loop_{};
    std::uint32_t sample_remainder_{};
    std::uint32_t accumulated_remainder_{};
};

}  // namespace swd2
