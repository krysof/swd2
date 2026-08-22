#include "swd2/rix_decoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("RIX word is truncated");
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

constexpr std::array<std::uint8_t, 18> opl_operator_offsets{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
};

constexpr std::array<std::uint8_t, 18> opl_operator_channels{
    0, 1, 2, 0, 1, 2, 3, 4, 5, 3, 4, 5, 6, 7, 8, 6, 7, 8,
};

constexpr std::array<bool, 18> opl_carrier_operator{
    false, false, false, true,  true,  true,
    false, false, false, true,  true,  true,
    false, false, false, true,  true,  true,
};

constexpr std::array<std::array<std::uint8_t, 2>, 9> opl_channel_operators{{
    {{0, 3}}, {{1, 4}}, {{2, 5}}, {{6, 9}}, {{7, 10}},
    {{8, 11}}, {{12, 15}}, {{13, 16}}, {{14, 17}},
}};

// RIX rhythm channels 7..10 address one of the shared OPL percussion
// operators rather than a complete two-operator channel.
constexpr std::array<std::uint8_t, 4> opl_rhythm_operators{16, 14, 17, 13};
constexpr std::array<std::uint8_t, 5> opl_rhythm_key_bits{0x10, 0x08, 0x04, 0x02, 0x01};

constexpr std::array<std::uint8_t, 13> default_modulator{
    1, 1, 3, 15, 5, 0, 1, 3, 15, 0, 0, 0, 1,
};
constexpr std::array<std::uint8_t, 13> default_carrier{
    0, 1, 1, 15, 7, 0, 2, 4, 0, 0, 0, 1, 0,
};
constexpr std::array<std::array<std::uint8_t, 13>, 6> default_rhythm_operators{{
    {{0, 0, 0, 10, 4, 0, 8, 12, 11, 0, 0, 0, 1}},
    {{0, 0, 0, 13, 4, 0, 6, 15, 0, 0, 0, 0, 1}},
    {{0, 12, 0, 15, 11, 0, 8, 5, 0, 0, 0, 0, 0}},
    {{0, 4, 0, 15, 11, 0, 7, 5, 0, 0, 0, 0, 0}},
    {{0, 1, 0, 15, 11, 0, 5, 5, 0, 0, 0, 0, 0}},
    {{0, 1, 0, 15, 11, 0, 7, 5, 0, 0, 0, 0, 0}},
}};
constexpr std::array<std::uint8_t, 6> default_rhythm_slots{12, 15, 16, 14, 17, 13};
constexpr std::array<std::uint8_t, 6> default_rhythm_waves{1, 3, 1, 3, 1, 3};

std::uint16_t rotate_right_16(std::uint16_t value, unsigned amount) {
    amount &= 15U;
    return static_cast<std::uint16_t>((value >> amount) | (value << (16U - amount)));
}

std::array<std::array<std::uint16_t, 12>, 25> make_frequency_tables() {
    std::array<std::array<std::uint16_t, 12>, 25> tables{};
    for (std::size_t table = 0; table < tables.size(); ++table) {
        const auto tuning_step = static_cast<std::uint32_t>(table * 4U);
        const auto quotient = static_cast<std::uint32_t>(
            (52'088ULL * (10'000ULL + 6ULL * tuning_step)) / 250'000ULL);

        auto ax = static_cast<std::uint16_t>(quotient);
        auto dx = static_cast<std::uint16_t>(quotient >> 16U);
        const auto bx = static_cast<std::uint16_t>(ax >> 2U);
        ax = static_cast<std::uint16_t>(rotate_right_16(ax, 2U) & 0xc000U);
        dx = static_cast<std::uint16_t>(
            (rotate_right_16(dx, 2U) & 0xc000U) | bx);
        auto value = ((static_cast<std::uint32_t>(dx) << 16U) | ax) * 9ULL;
        value /= 0x1b503ULL;

        tables[table][0] = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) + 4U) >> 3U);
        for (std::size_t semitone = 1; semitone < 12; ++semitone) {
            value = (value * 106ULL) / 100ULL;
            tables[table][semitone] = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(value) + 4U) >> 3U);
        }
    }
    return tables;
}

unsigned scaled_total_level(unsigned instrument_level, unsigned channel_volume) {
    const auto available = 63U - (instrument_level & 63U);
    const auto scaled =
        (available * std::min(channel_volume, 127U) * 2U + 127U) / 254U;
    return 63U - std::min(scaled, 63U);
}

class RixOplTranslator {
public:
    explicit RixOplTranslator(const RixSequence& source)
        : source_(source), frequencies_(make_frequency_tables()) {
        output_.rhythm_mode = source.rhythm_mode;
        output_.total_milliseconds = source.total_milliseconds;
        operator_volumes_.fill(127);
        channel_volumes_.fill(127);
    }

    OplRegisterSequence run() {
        initialize();
        std::size_t tick = 0;
        for (const auto& frame : source_.frames) {
            tick_ = tick;
            for (const auto& command : frame.commands) dispatch(command);
            if (tick > std::numeric_limits<std::size_t>::max() -
                           frame.duration_milliseconds) {
                throw std::runtime_error("RIX OPL timeline overflows size_t");
            }
            tick += frame.duration_milliseconds;
        }
        if (tick != source_.total_milliseconds) {
            throw std::runtime_error("RIX total timer tick count is inconsistent");
        }
        return std::move(output_);
    }

private:
    struct OperatorState {
        std::array<std::uint8_t, 13> fields{};
        std::uint8_t waveform{};
    };

    struct ChannelState {
        std::uint8_t note{};
        bool keyed{};
        int note_offset{};
        std::uint8_t frequency_table{};
    };

    const RixSequence& source_;
    OplRegisterSequence output_;
    std::array<OperatorState, 18> operators_{};
    std::array<std::uint8_t, 18> operator_volumes_{};
    std::array<std::uint8_t, 11> channel_volumes_{};
    std::array<ChannelState, 11> channels_{};
    std::array<std::array<std::uint16_t, 12>, 25> frequencies_{};
    std::uint8_t rhythm_keys_{};
    bool rhythm_mode_{};
    std::size_t tick_{};

    void write(std::uint8_t reg, std::uint8_t value) {
        output_.writes.push_back({tick_, reg, value});
    }

    void write_bd() {
        write(0xbd, static_cast<std::uint8_t>(rhythm_keys_ |
                                              (rhythm_mode_ ? 0x20U : 0U)));
    }

    static std::uint8_t effective_volume(std::uint8_t raw) {
        // With both driver-wide volume words at their default 0x0100, the
        // original 16-bit routine reduces to floor(raw * 255 / 256).
        return static_cast<std::uint8_t>((static_cast<unsigned>(raw) * 255U) >> 8U);
    }

    void write_total_level(std::uint8_t slot) {
        const auto& fields = operators_.at(slot).fields;
        const auto attenuation = scaled_total_level(fields[8], operator_volumes_[slot]);
        const auto value = static_cast<std::uint8_t>(
            ((static_cast<unsigned>(fields[0]) << 6U) | attenuation) & 0xffU);
        write(static_cast<std::uint8_t>(0x40U + opl_operator_offsets[slot]), value);
    }

    void load_operator(std::uint8_t slot,
                       std::span<const std::uint8_t, 13> fields,
                       std::uint8_t waveform_value) {
        auto& state = operators_.at(slot);
        std::copy(fields.begin(), fields.end(), state.fields.begin());
        state.waveform = static_cast<std::uint8_t>(waveform_value & 3U);

        // This is the exact write order of FIG's 1000:11ff operator loader.
        write_bd();
        write(0x08, 0x00);
        write_total_level(slot);
        if (!opl_carrier_operator[slot]) {
            const auto feedback = static_cast<std::uint8_t>(state.fields[2] << 1U);
            const auto connection = state.fields[12] == 0 ? 1U : 0U;
            write(static_cast<std::uint8_t>(0xc0U + opl_operator_channels[slot]),
                  static_cast<std::uint8_t>(feedback | connection));
        }
        write(static_cast<std::uint8_t>(0x60U + opl_operator_offsets[slot]),
              static_cast<std::uint8_t>((state.fields[3] << 4U) |
                                        (state.fields[6] & 0x0fU)));
        write(static_cast<std::uint8_t>(0x80U + opl_operator_offsets[slot]),
              static_cast<std::uint8_t>((state.fields[4] << 4U) |
                                        (state.fields[7] & 0x0fU)));

        auto characteristic = static_cast<std::uint8_t>(state.fields[1] & 0x0fU);
        if (state.fields[9] != 0) characteristic |= 0x80U;
        if (state.fields[10] != 0) characteristic |= 0x40U;
        if (state.fields[5] != 0) characteristic |= 0x20U;
        if (state.fields[11] != 0) characteristic |= 0x10U;
        write(static_cast<std::uint8_t>(0x20U + opl_operator_offsets[slot]), characteristic);
        write(static_cast<std::uint8_t>(0xe0U + opl_operator_offsets[slot]), state.waveform);
    }

    void apply_operator_volume(std::uint8_t slot, std::uint8_t volume) {
        operator_volumes_[slot] = volume;
        write_total_level(slot);
    }

    void apply_channel_volume(std::uint8_t channel, std::uint8_t volume) {
        if (!rhythm_mode_ || channel <= 6) {
            if (channel >= opl_channel_operators.size()) {
                throw std::runtime_error("melodic RIX volume channel is invalid");
            }
            const auto [modulator, carrier] = opl_channel_operators[channel];
            apply_operator_volume(carrier, volume);
            // FIG changes the modulator TL only for additive instruments.
            if (operators_[modulator].fields[12] == 0) {
                apply_operator_volume(modulator, volume);
            }
            return;
        }
        if (channel > 10) throw std::runtime_error("rhythm RIX volume channel is invalid");
        apply_operator_volume(opl_rhythm_operators[channel - 7U], volume);
    }

    void write_note(std::uint8_t channel, int note, bool keyed) {
        if (channel >= 9) throw std::runtime_error("RIX note targets no OPL channel");
        auto& state = channels_[channel];
        state.note = static_cast<std::uint8_t>(std::clamp(note, 0, 255));
        state.keyed = keyed;

        const auto adjusted = std::clamp(note + state.note_offset, 0, 95);
        const auto f_number = frequencies_[state.frequency_table]
                                           [static_cast<std::size_t>(adjusted % 12)];
        write(static_cast<std::uint8_t>(0xa0U + channel),
              static_cast<std::uint8_t>(f_number));
        const auto octave = static_cast<unsigned>(adjusted / 12);
        const auto high = static_cast<std::uint8_t>(
            (keyed ? 0x20U : 0U) | (octave << 2U) | ((f_number >> 8U) & 3U));
        write(static_cast<std::uint8_t>(0xb0U + channel), high);
    }

    void initialize() {
        tick_ = 0;
        // State-equivalent tick-zero prelude. The DOS driver performs some
        // additional inaudible mute/reset writes before arriving here.
        write(0x01, 0x20);
        write(0x08, 0x00);
        write_bd();
        for (std::uint8_t channel = 0; channel < 9; ++channel) {
            write(static_cast<std::uint8_t>(0xa0U + channel), 0);
            write(static_cast<std::uint8_t>(0xb0U + channel), 0);
        }

        if (source_.rhythm_mode) {
            for (std::uint8_t channel = 6; channel < 9; ++channel) {
                write(static_cast<std::uint8_t>(0xa0U + channel), 0);
                write(static_cast<std::uint8_t>(0xb0U + channel), 0);
            }
            write_note(8, 24, false);
            write_note(7, 31, false);
        }
        rhythm_mode_ = source_.rhythm_mode;

        for (int slot = 17; slot >= 0; --slot) {
            if (opl_carrier_operator[static_cast<std::size_t>(slot)]) {
                load_operator(static_cast<std::uint8_t>(slot), default_carrier, 3);
            } else {
                load_operator(static_cast<std::uint8_t>(slot), default_modulator, 1);
            }
        }
        if (rhythm_mode_) {
            for (std::size_t index = 0; index < default_rhythm_slots.size(); ++index) {
                load_operator(default_rhythm_slots[index], default_rhythm_operators[index],
                              default_rhythm_waves[index]);
            }
        }

        const auto last_channel = static_cast<std::uint8_t>(rhythm_mode_ ? 10 : 8);
        for (int channel = last_channel; channel >= 0; --channel) {
            apply_channel_volume(static_cast<std::uint8_t>(channel),
                                 effective_volume(channel_volumes_[channel]));
        }
        write_bd();
    }

    void select_instrument(const RixCommand& command) {
        if (command.value >= source_.instruments.size()) {
            throw std::runtime_error("RIX selects an invalid OPL instrument");
        }
        const auto& instrument = source_.instruments[command.value];
        std::array<std::uint8_t, 13> modulator{};
        std::array<std::uint8_t, 13> carrier{};
        for (std::size_t field = 0; field < 13; ++field) {
            modulator[field] = static_cast<std::uint8_t>(instrument[field]);
            carrier[field] = static_cast<std::uint8_t>(instrument[field + 13U]);
        }

        if (!rhythm_mode_ || command.channel < 6) {
            if (command.channel >= opl_channel_operators.size()) {
                throw std::runtime_error("melodic RIX instrument channel is invalid");
            }
            const auto [mod_slot, carrier_slot] = opl_channel_operators[command.channel];
            load_operator(mod_slot, modulator,
                          static_cast<std::uint8_t>(instrument[26]));
            load_operator(carrier_slot, carrier,
                          static_cast<std::uint8_t>(instrument[27]));
            return;
        }
        if (command.channel == 6) {
            const auto [mod_slot, carrier_slot] = opl_channel_operators[6];
            load_operator(mod_slot, modulator,
                          static_cast<std::uint8_t>(instrument[26]));
            load_operator(carrier_slot, carrier,
                          static_cast<std::uint8_t>(instrument[27]));
            return;
        }
        if (command.channel > 10) {
            throw std::runtime_error("rhythm RIX instrument channel is invalid");
        }
        load_operator(opl_rhythm_operators[command.channel - 7U], modulator,
                      static_cast<std::uint8_t>(instrument[26]));
    }

    void set_pitch(const RixCommand& command) {
        if (rhythm_mode_ && command.channel > 6) return;
        if (command.channel >= 9) {
            throw std::runtime_error("melodic RIX pitch channel is invalid");
        }
        const auto clamped = std::min<unsigned>(command.value, 0x3fffU);
        const auto delta = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(clamped + 0xe000U));
        const auto units = (static_cast<std::int32_t>(delta) * 27) / 0x2000;
        auto quotient = units / 25;
        auto remainder = units % 25;
        if (remainder < 0) {
            remainder += 25;
            --quotient;
        }
        auto& state = channels_[command.channel];
        state.note_offset = quotient;
        state.frequency_table = static_cast<std::uint8_t>(remainder);
        write_note(command.channel, state.note, state.keyed);
    }

    void set_volume(const RixCommand& command) {
        const auto raw = static_cast<std::uint8_t>(std::min<unsigned>(command.value, 127U));
        channel_volumes_[command.channel] = raw;
        apply_channel_volume(command.channel, effective_volume(raw));
    }

    void set_note(const RixCommand& command) {
        const auto channel = command.channel;
        if (!rhythm_mode_ || channel < 6) {
            if (channel >= 9) throw std::runtime_error("melodic RIX note channel is invalid");
            const auto& state = channels_[channel];
            write_note(channel, state.note, false);
        } else {
            if (channel > 10) throw std::runtime_error("rhythm RIX note channel is invalid");
            rhythm_keys_ &= static_cast<std::uint8_t>(~opl_rhythm_key_bits[channel - 6U]);
            write_bd();
        }

        if (command.value == 0) return;
        const auto note = std::max<int>(static_cast<int>(command.value) - 12, 0);
        if (!rhythm_mode_ || channel < 6) {
            write_note(channel, note, true);
        } else if (channel == 6) {
            write_note(6, note, false);
        } else if (channel == 8) {
            write_note(8, note, false);
            write_note(7, note + 7, false);
        }
        if (rhythm_mode_ && channel >= 6) {
            rhythm_keys_ |= opl_rhythm_key_bits[channel - 6U];
            write_bd();
        }
    }

    void dispatch(const RixCommand& command) {
        const auto max_channel = source_.rhythm_mode ? 10U : 8U;
        if (command.channel > max_channel) {
            throw std::runtime_error("RIX command channel is incompatible with its mode");
        }
        switch (command.kind) {
        case RixCommandKind::instrument: select_instrument(command); break;
        case RixCommandKind::pitch: set_pitch(command); break;
        case RixCommandKind::volume: set_volume(command); break;
        case RixCommandKind::note: set_note(command); break;
        }
    }
};

}  // namespace

RixSequence decode_rix(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 14 || u16(bytes, 0) != 0x55aaU) {
        throw std::runtime_error("RIX signature/header is missing");
    }
    const auto instrument_offset = static_cast<std::size_t>(u16(bytes, 8));
    const auto music_offset = static_cast<std::size_t>(u16(bytes, 12));
    if (instrument_offset < 14 || instrument_offset > music_offset ||
        music_offset + 2 > bytes.size() ||
        ((music_offset - instrument_offset) % 64U) != 0) {
        throw std::runtime_error("RIX instrument/music offsets are invalid");
    }

    RixSequence result;
    result.rhythm_mode = bytes[2] != 0;
    const auto instrument_count = (music_offset - instrument_offset) / 64U;
    result.instruments.reserve(instrument_count);
    for (std::size_t index = 0; index < instrument_count; ++index) {
        std::array<std::uint16_t, 28> instrument{};
        const auto start = instrument_offset + index * 64U;
        for (std::size_t field = 0; field < instrument.size(); ++field) {
            instrument[field] = u16(bytes, start + field * 2U);
        }
        result.instruments.push_back(instrument);
    }

    std::vector<RixCommand> pending;
    auto cursor = music_offset;
    bool terminated = false;
    while (cursor + 1 < bytes.size()) {
        const auto value = bytes[cursor];
        const auto control = bytes[cursor + 1];
        cursor += 2;
        if (control == 0x80U) {
            terminated = true;
            break;
        }
        const auto channel = static_cast<std::uint8_t>(control & 0x0fU);
        const auto max_channel = result.rhythm_mode ? 10U : 8U;
        switch (control & 0xf0U) {
        case 0x90:
            if (channel > max_channel || value >= result.instruments.size()) {
                throw std::runtime_error("RIX selects an invalid instrument/channel");
            }
            pending.push_back({RixCommandKind::instrument, channel, value});
            break;
        case 0xa0:
            if (channel > max_channel) throw std::runtime_error("RIX pitch channel is invalid");
            pending.push_back({RixCommandKind::pitch, channel,
                               static_cast<std::uint16_t>(
                                   static_cast<std::uint16_t>(value) << 6U)});
            break;
        case 0xb0:
            if (channel > max_channel) throw std::runtime_error("RIX volume channel is invalid");
            pending.push_back({RixCommandKind::volume, channel,
                               static_cast<std::uint16_t>(std::min<unsigned>(value, 127))});
            break;
        case 0xc0:
            if (channel > max_channel) throw std::runtime_error("RIX note channel is invalid");
            pending.push_back({RixCommandKind::note, channel, value});
            break;
        default: {
            const auto delay = static_cast<std::uint16_t>(value) |
                               (static_cast<std::uint16_t>(control) << 8U);
            if (delay == 0) break;
            const auto milliseconds = static_cast<std::size_t>(delay);
            result.frames.push_back({milliseconds, std::move(pending)});
            pending.clear();
            if (result.total_milliseconds >
                std::numeric_limits<std::size_t>::max() - milliseconds) {
                throw std::runtime_error("RIX duration overflows size_t");
            }
            result.total_milliseconds += milliseconds;
            break;
        }
        }
    }
    if (!terminated) throw std::runtime_error("RIX stream has no 80 terminator");
    if (!pending.empty()) {
        result.frames.push_back({1, std::move(pending)});
        ++result.total_milliseconds;
    }
    if (result.frames.empty() || result.total_milliseconds == 0) {
        throw std::runtime_error("RIX stream has no timed command groups");
    }
    return result;
}

OplRegisterSequence translate_rix_to_opl(const RixSequence& sequence) {
    return RixOplTranslator(sequence).run();
}

}  // namespace swd2
