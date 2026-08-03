#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

struct DecodedVoice {
    std::uint32_t sample_rate{};
    std::vector<std::int16_t> mono_samples;
};

// Decodes the Creative Voice type-1 PCM files used by all 67 original voice
// assets. The source encoding is unsigned 8-bit mono with the Sound Blaster
// time-constant rate formula; output is native signed 16-bit PCM.
DecodedVoice decode_voc(std::span<const std::uint8_t> bytes);

}  // namespace swd2
