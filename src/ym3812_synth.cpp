#include "swd2/rix_decoder.hpp"

#include "ymfm_opl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace swd2 {

namespace {

// The AdLib/YM3812 reference crystal used by the DOS hardware and emulators.
constexpr std::uint32_t ym3812_clock = 14'318'180;

class Ym3812Interface final : public ymfm::ymfm_interface {};

std::size_t sample_count(std::size_t milliseconds, std::uint32_t rate) {
    const auto count =
        static_cast<unsigned long long>(milliseconds) * rate / 1'000U;
    if (count > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("YM3812 PCM size overflows size_t");
    }
    return static_cast<std::size_t>(count);
}

std::int16_t clamp_sample(std::int64_t value) {
    return static_cast<std::int16_t>(
        std::clamp<std::int64_t>(value, -32'768, 32'767));
}

}  // namespace

DecodedMusic synthesize_opl(const OplRegisterSequence& sequence,
                            std::uint32_t sample_rate) {
    if (sample_rate < 8'000 || sample_rate > 192'000) {
        throw std::invalid_argument("RIX synthesis sample rate is unsupported");
    }
    if (sequence.total_milliseconds == 0) {
        throw std::invalid_argument("OPL register sequence has no duration");
    }

    std::size_t previous_tick = 0;
    bool first = true;
    for (const auto& write : sequence.writes) {
        if (write.millisecond >= sequence.total_milliseconds ||
            (!first && write.millisecond < previous_tick)) {
            throw std::invalid_argument("OPL register writes are outside timeline order");
        }
        previous_tick = write.millisecond;
        first = false;
    }

    Ym3812Interface interface;
    ymfm::ym3812 chip(interface);
    chip.reset();
    const auto native_rate = chip.sample_rate(ym3812_clock);
    if (native_rate == 0) throw std::runtime_error("YM3812 core returned no sample rate");

    std::vector<std::int16_t> native;
    native.reserve(sample_count(sequence.total_milliseconds, native_rate));
    std::size_t write_index = 0;
    unsigned long long native_remainder = 0;
    for (std::size_t tick = 0; tick < sequence.total_milliseconds; ++tick) {
        while (write_index < sequence.writes.size() &&
               sequence.writes[write_index].millisecond == tick) {
            const auto& write = sequence.writes[write_index++];
            chip.write_address(write.register_index);
            chip.write_data(write.value);
        }

        native_remainder += native_rate;
        const auto tick_samples =
            static_cast<std::size_t>(native_remainder / 1'000U);
        native_remainder %= 1'000U;
        // ymfm evaluates OPL operators at the chip's internal output rate
        // (well above the final 48 kHz device rate), so one millisecond can
        // contain several hundred core samples.
        std::array<ymfm::ym3812::output_data, 1'024> generated{};
        if (tick_samples > generated.size()) {
            throw std::runtime_error("YM3812 millisecond buffer is too small");
        }
        chip.generate(generated.data(), static_cast<std::uint32_t>(tick_samples));
        for (std::size_t index = 0; index < tick_samples; ++index) {
            const auto& sample = generated[index];
            native.push_back(clamp_sample(sample.data[0]));
        }
    }
    if (write_index != sequence.writes.size()) {
        throw std::invalid_argument("OPL register timeline has unconsumed writes");
    }

    if (sample_rate < native_rate) {
        // ymfm exposes the YM3812 engine's 4x-oversampled DAC stream. Picking
        // linearly interpolated points from it aliases ultrasonic operator
        // energy back into the audible band and makes the result much
        // brighter than the reference output. A four-stage, four-sample CIC
        // provides the missing anti-alias filter without another large PCM
        // allocation; the rolling input ring makes every stage safe in-place.
        for (unsigned stage = 0; stage < 4; ++stage) {
            std::array<std::int32_t, 4> history{};
            std::int64_t sum = 0;
            for (std::size_t index = 0; index < native.size(); ++index) {
                const auto slot = index & 3U;
                const auto input = static_cast<std::int32_t>(native[index]);
                sum += input - history[slot];
                history[slot] = input;
                native[index] = clamp_sample(
                    (sum >= 0 ? sum + 2 : sum - 2) / 4);
            }
        }
    }

    DecodedMusic result;
    result.sample_rate = sample_rate;
    const auto output_count =
        sample_count(sequence.total_milliseconds, sample_rate);
    result.mono_samples.resize(output_count);
    if (sample_rate == native_rate) {
        result.mono_samples = std::move(native);
        return result;
    }

    // Deterministic linear clock conversion. Register timing and envelopes are
    // evaluated at the YM3812's native rate; only its final DAC stream is
    // converted to the host device rate.
    for (std::size_t output = 0; output < output_count; ++output) {
        const auto position = static_cast<unsigned long long>(output) * native_rate;
        const auto first_index = static_cast<std::size_t>(position / sample_rate);
        const auto fraction = static_cast<std::uint32_t>(position % sample_rate);
        const auto clamped_first = std::min(first_index, native.size() - 1U);
        const auto second = std::min(clamped_first + 1U, native.size() - 1U);
        const auto interpolated =
            static_cast<std::int64_t>(native[clamped_first]) *
                (sample_rate - fraction) +
            static_cast<std::int64_t>(native[second]) * fraction;
        const auto rounding = static_cast<std::int64_t>(sample_rate / 2U);
        result.mono_samples[output] = clamp_sample(
            (interpolated >= 0 ? interpolated + rounding : interpolated - rounding) /
            sample_rate);
    }
    return result;
}

DecodedMusic synthesize_rix(const RixSequence& sequence,
                            std::uint32_t sample_rate) {
    return synthesize_opl(translate_rix_to_opl(sequence), sample_rate);
}

}  // namespace swd2
