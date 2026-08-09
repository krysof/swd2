#include "swd2/voc_decoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace swd2 {

namespace {

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("VOC word is truncated");
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t u24(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 3 > bytes.size()) throw std::runtime_error("VOC length is truncated");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U);
}

}  // namespace

DecodedVoice decode_voc(std::span<const std::uint8_t> bytes) {
    static constexpr std::string_view signature = "Creative Voice File\x1a";
    if (bytes.size() < 26 ||
        !std::equal(signature.begin(), signature.end(), bytes.begin())) {
        throw std::runtime_error("VOC signature is missing");
    }
    const auto version = u16(bytes, 22);
    const auto checksum = u16(bytes, 24);
    if (checksum != static_cast<std::uint16_t>(~version + 0x1234U)) {
        throw std::runtime_error("VOC version checksum is invalid");
    }
    auto cursor = static_cast<std::size_t>(u16(bytes, 20));
    if (cursor < 26 || cursor > bytes.size()) {
        throw std::runtime_error("VOC data offset is out of bounds");
    }

    DecodedVoice result;
    bool found_sound = false;
    while (cursor < bytes.size()) {
        const auto type = bytes[cursor++];
        if (type == 0) break;
        const auto length = static_cast<std::size_t>(u24(bytes, cursor));
        cursor += 3;
        if (length > bytes.size() - cursor) {
            throw std::runtime_error("VOC block exceeds the file");
        }
        const auto payload = bytes.subspan(cursor, length);
        cursor += length;
        if (type != 1) {
            throw std::runtime_error("unsupported VOC block type " +
                                     std::to_string(type));
        }
        if (found_sound || payload.size() < 2) {
            throw std::runtime_error("VOC has an invalid type-1 block sequence");
        }
        if (payload[1] != 0) {
            throw std::runtime_error("VOC uses unsupported compressed audio");
        }
        const auto divisor = static_cast<unsigned>(256U - payload[0]);
        if (divisor == 0) throw std::runtime_error("VOC has an invalid time constant");
        result.sample_rate = 1'000'000U / divisor;
        result.mono_samples.reserve(payload.size() - 2);
        for (const auto sample : payload.subspan(2)) {
            result.mono_samples.push_back(static_cast<std::int16_t>(
                (static_cast<int>(sample) - 128) << 8));
        }
        found_sound = true;
    }
    if (!found_sound || result.mono_samples.empty()) {
        throw std::runtime_error("VOC contains no PCM samples");
    }
    return result;
}

DecodedVoice resample_voice(const DecodedVoice& source,
                            std::uint32_t sample_rate) {
    if (source.sample_rate == 0 || source.mono_samples.empty() ||
        sample_rate == 0) {
        throw std::invalid_argument("VOC resampler has invalid PCM metadata");
    }
    if (source.mono_samples.size() >
        std::numeric_limits<std::uint64_t>::max() / sample_rate) {
        throw std::overflow_error("VOC resampled duration overflows uint64_t");
    }
    const auto duration_numerator =
        static_cast<std::uint64_t>(source.mono_samples.size()) * sample_rate;
    const auto output_count = duration_numerator / source.sample_rate;
    if (output_count == 0 ||
        output_count > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("VOC resampled PCM size is outside size_t");
    }

    DecodedVoice result;
    result.sample_rate = sample_rate;
    result.mono_samples.resize(static_cast<std::size_t>(output_count));
    for (std::size_t output = 0; output < result.mono_samples.size(); ++output) {
        const auto position =
            static_cast<std::uint64_t>(output) * source.sample_rate;
        const auto first = static_cast<std::size_t>(position / sample_rate);
        const auto fraction = static_cast<std::uint32_t>(position % sample_rate);
        const auto clamped_first =
            std::min(first, source.mono_samples.size() - 1U);
        const auto second =
            std::min(clamped_first + 1U, source.mono_samples.size() - 1U);
        const auto interpolated =
            static_cast<std::int64_t>(source.mono_samples[clamped_first]) *
                (sample_rate - fraction) +
            static_cast<std::int64_t>(source.mono_samples[second]) * fraction;
        const auto rounding = static_cast<std::int64_t>(sample_rate / 2U);
        const auto rounded =
            (interpolated >= 0 ? interpolated + rounding
                               : interpolated - rounding) /
            sample_rate;
        result.mono_samples[output] = static_cast<std::int16_t>(
            std::clamp<std::int64_t>(rounded, -32'768, 32'767));
    }
    return result;
}

}  // namespace swd2
