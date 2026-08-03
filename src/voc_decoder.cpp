#include "swd2/voc_decoder.hpp"

#include <algorithm>
#include <array>
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

}  // namespace swd2
