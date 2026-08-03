#include "swd2/event_program.hpp"

#include <array>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("event record is truncated");
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

// Number of LODSW argument reads in RPG.EXE's 62-entry dispatch table.
// Variable-length opcodes are handled separately below.
constexpr std::array<std::uint8_t, 62> argument_words = {
    0, 0, 1, 2, 2, 0, 0, 0, 1, 1, 2, 2, 0, 1, 0, 2,
    6, 0, 0, 0, 0, 2, 0, 1, 1, 1, 1, 2, 1, 1, 1, 1,
    1, 1, 0, 1, 1, 1, 1, 2, 3, 1, 1, 1, 1, 0, 0, 1,
    2, 1, 0, 1, 0, 2, 2, 0, 0, 1, 1, 2, 2, 0,
};

bool is_text_opcode(std::uint16_t opcode) {
    return opcode == 0 || opcode == 18 || opcode == 20 || opcode == 46;
}

void read_inline_text(std::span<const std::uint8_t> bytes, std::size_t& cursor,
                      EventCommand& command) {
    const auto text_start = cursor;
    while (cursor + 2 <= bytes.size() &&
           !(bytes[cursor] == '$' && bytes[cursor + 1] == '$')) {
        cursor += bytes[cursor] == ' ' ? 1 : 2;
    }
    if (cursor + 2 > bytes.size()) {
        throw std::runtime_error("unterminated RPG event dialogue");
    }
    command.text.assign(bytes.begin() + static_cast<std::ptrdiff_t>(text_start),
                        bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
    cursor += 2;
}

}  // namespace

EventRecord decode_event_record(std::span<const std::uint8_t> bytes) {
    EventRecord result;
    std::size_t cursor = 0;
    while (true) {
        const auto opcode = word(bytes, cursor);
        cursor += 2;
        if (opcode == 0xffff) {
            result.consumed_bytes = cursor;
            return result;
        }
        if (opcode >= argument_words.size()) {
            throw std::runtime_error("unknown RPG event opcode " + std::to_string(opcode));
        }

        EventCommand command;
        command.opcode = opcode;
        if (opcode == 53) {
            // RPG.EXE:5bff reads x/y with LODSW and immediately lets 71c2
            // consume the following inline string through its $$ terminator.
            command.arguments.push_back(word(bytes, cursor));
            command.arguments.push_back(word(bytes, cursor + 2));
            cursor += 4;
            read_inline_text(bytes, cursor, command);
        } else if (is_text_opcode(opcode)) {
            read_inline_text(bytes, cursor, command);
        } else if (opcode == 17 || opcode == 19) {
            const auto count = word(bytes, cursor);
            command.arguments.push_back(count);
            cursor += 2;
            for (std::size_t index = 0; index < count; ++index) {
                command.arguments.push_back(word(bytes, cursor));
                cursor += 2;
            }
        } else if (opcode == 34) {
            while (true) {
                const auto object = word(bytes, cursor);
                command.arguments.push_back(object);
                cursor += 2;
                if (object == 0xf800) break;
                for (int index = 0; index < 3; ++index) {
                    command.arguments.push_back(word(bytes, cursor));
                    cursor += 2;
                }
                if (command.arguments.back() == 0x4144) {
                    command.arguments.push_back(word(bytes, cursor));
                    cursor += 2;
                }
            }
        } else {
            for (std::size_t index = 0; index < argument_words[opcode]; ++index) {
                command.arguments.push_back(word(bytes, cursor));
                cursor += 2;
            }
        }
        result.commands.push_back(std::move(command));
    }
}

}  // namespace swd2
