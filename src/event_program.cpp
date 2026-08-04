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

bool is_terminal_opcode(std::uint16_t opcode) {
    // These handlers tail-jump out of RPG.EXE's 53a8 dispatcher instead of
    // returning to its LODSW loop. Opcode 37 reloads a map but does return.
    return opcode == 28 || opcode == 48 || opcode == 52 || opcode == 58 ||
           opcode == 59 || opcode == 60;
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
        // CHNA6 directory 48 ends in 0000,ffff after a restore command. A
        // literal opcode-zero dialogue needs at least a $$ terminator; this
        // pair is the archive's empty trailing slot rather than text.
        if (opcode == 0 && cursor + 2 <= bytes.size() &&
            word(bytes, cursor) == 0xffffU) {
            result.consumed_bytes = cursor + 2U;
            return result;
        }
        if (opcode == 0x0320U) {
            // CHNA6 directory 90 contains one malformed packed command:
            // bytes 20,03 encode the otherwise standard "opcode 32, 3
            // steps" immediately before an opcode-zero dialogue. The DOS
            // dispatcher would index past its 62-word table and call address
            // zero here; recover the evident intended command so this
            // shipped story interaction remains portable and playable.
            EventCommand command;
            command.opcode = 32;
            command.arguments.push_back(3);
            result.commands.push_back(std::move(command));
            continue;
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
        if (is_terminal_opcode(opcode)) {
            // 59af/5bfc/5c4f/5c57/5c5f never return to the dispatch loop;
            // bytes after the command are therefore deliberately unreachable.
            result.consumed_bytes = cursor;
            return result;
        }
    }
}

}  // namespace swd2
