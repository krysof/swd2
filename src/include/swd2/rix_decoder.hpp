#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

enum class RixCommandKind { instrument, pitch, volume, note };

struct RixCommand {
    RixCommandKind kind{};
    std::uint8_t channel{};
    std::uint16_t value{};
};

struct RixFrame {
    // RIX delay words are milliseconds. Raw-OPL/audio observations of the
    // original player confirm that a complete OP01 loop's 51,096 delay
    // units take 51.05 seconds; they are not 70 Hz game-loop ticks.
    std::size_t duration_milliseconds{};
    std::vector<RixCommand> commands;
};

struct RixSequence {
    bool rhythm_mode{};
    std::vector<std::array<std::uint16_t, 28>> instruments;
    std::vector<RixFrame> frames;
    std::size_t total_milliseconds{};
};

struct OplRegisterWrite {
    // Commands in a RIX group are issued before that group's delay starts.
    // Several writes can consequently have the same timeline millisecond.
    std::size_t millisecond{};
    std::uint8_t register_index{};
    std::uint8_t value{};
};

struct OplRegisterSequence {
    bool rhythm_mode{};
    std::vector<OplRegisterWrite> writes;
    std::size_t total_milliseconds{};
};

struct DecodedMusic {
    std::uint32_t sample_rate{};
    std::vector<std::int16_t> mono_samples;
};

// Strictly parses Softstar's 55 AA RIX command stream and 64-byte instrument
// records into its platform-independent millisecond timeline.
RixSequence decode_rix(std::span<const std::uint8_t> bytes);

// Recreates the register programming performed by the embedded Softstar RIX
// driver. This includes its 25 micro-tuning tables, channel/operator routing,
// logarithmic pitch-bend selection, rhythm key bits, instrument registers and
// integer total-level scaling. The tick-zero prelude leaves the YM3812 in the
// same ready-to-play state as the original driver's load/start path.
OplRegisterSequence translate_rix_to_opl(const RixSequence& sequence);

// Renders a register sequence with the portable DBOPL OPL2 core and resamples
// the chip's native clock to the requested host rate.
DecodedMusic synthesize_opl(const OplRegisterSequence& sequence,
                            std::uint32_t sample_rate = 44'100);

// Convenience path used by platform backends: exact RIX driver translation,
// followed by YM3812 synthesis. It has no host MIDI dependency.
DecodedMusic synthesize_rix(const RixSequence& sequence,
                            std::uint32_t sample_rate = 44'100);

}  // namespace swd2
