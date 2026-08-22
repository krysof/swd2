#include "swd2/audio_loop_clock.hpp"

#include <limits>
#include <stdexcept>

namespace swd2 {

void AudioLoopClock::reset(std::size_t milliseconds,
                           std::uint32_t sample_rate) {
    if (milliseconds == 0 || sample_rate == 0) {
        throw std::invalid_argument("audio loop clock has no duration or sample rate");
    }
    constexpr auto timer_rate = std::uint64_t{1'000};
    if (milliseconds >
        std::numeric_limits<std::uint64_t>::max() / sample_rate) {
        throw std::overflow_error("audio loop duration overflows its rational clock");
    }
    const auto numerator =
        static_cast<std::uint64_t>(milliseconds) * sample_rate;
    const auto samples = numerator / timer_rate;
    if (samples == 0 || samples > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("audio loop PCM size is outside size_t");
    }
    samples_per_loop_ = static_cast<std::size_t>(samples);
    sample_remainder_ = static_cast<std::uint32_t>(numerator % timer_rate);
    accumulated_remainder_ = 0;
}

bool AudioLoopClock::advance_loop_boundary() noexcept {
    accumulated_remainder_ += sample_remainder_;
    if (accumulated_remainder_ < 1'000U) return false;
    accumulated_remainder_ -= 1'000U;
    return true;
}

}  // namespace swd2
