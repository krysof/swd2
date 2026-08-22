#include "swd2/rix_decoder.hpp"

#include "dbopl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace swd2 {

namespace {

// The independently captured reference environment selects its DBOPL provider
// and uses the YM3812 master clock divided by 288 for high-quality output.
constexpr std::uint32_t dbopl_rate = 49'716;

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

    // The reference SB16 configuration constructs an OPL3-capable DBOPL chip.
    // The game only writes the OPL2 register bank, so the chip remains in its
    // mono OPL2 mode just as it does in the independent capture.
    DBOPL::InitTables();
    DBOPL::Chip chip(true);
    chip.Setup(dbopl_rate);

    std::vector<std::int16_t> native;
    native.reserve(sample_count(sequence.total_milliseconds, dbopl_rate));
    std::size_t write_index = 0;
    unsigned long long native_remainder = 0;
    for (std::size_t tick = 0; tick < sequence.total_milliseconds; ++tick) {
        while (write_index < sequence.writes.size() &&
               sequence.writes[write_index].millisecond == tick) {
            const auto& write = sequence.writes[write_index++];
            chip.WriteReg(write.register_index, write.value);
        }

        native_remainder += dbopl_rate;
        const auto tick_samples =
            static_cast<std::size_t>(native_remainder / 1'000U);
        native_remainder %= 1'000U;
        std::array<std::int32_t, 64> generated{};
        if (tick_samples > generated.size()) {
            throw std::runtime_error("YM3812 millisecond buffer is too small");
        }
        chip.GenerateBlock2(static_cast<DBOPL::Bitu>(tick_samples),
                            generated.data());
        for (std::size_t index = 0; index < tick_samples; ++index) {
            // The reference FM mixer applies its documented 1.5 scale before
            // writing device output. Keep that provider-wide scale here; this
            // is not a per-track adjustment.
            native.push_back(clamp_sample(
                static_cast<std::int64_t>(generated[index]) * 3 / 2));
        }
    }
    if (write_index != sequence.writes.size()) {
        throw std::invalid_argument("OPL register timeline has unconsumed writes");
    }

    DecodedMusic result;
    result.sample_rate = sample_rate;
    const auto output_count =
        sample_count(sequence.total_milliseconds, sample_rate);
    result.mono_samples.resize(output_count);
    if (sample_rate == dbopl_rate) {
        result.mono_samples = std::move(native);
        return result;
    }

    // Deterministic linear clock conversion. Register timing and envelopes are
    // evaluated at the YM3812's native rate; only its final DAC stream is
    // converted to the host device rate.
    for (std::size_t output = 0; output < output_count; ++output) {
        const auto position = static_cast<unsigned long long>(output) * dbopl_rate;
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
