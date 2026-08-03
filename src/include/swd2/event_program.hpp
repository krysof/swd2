#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

struct EventCommand {
    std::uint16_t opcode{};
    std::vector<std::uint16_t> arguments;
    std::vector<std::uint8_t> text;
};

struct EventRecord {
    std::vector<EventCommand> commands;
    std::size_t consumed_bytes{};
};

// Decodes one record from the 62-opcode RPG event language. Branch targets
// remain raw archive offsets; decoding has no game-state side effects.
EventRecord decode_event_record(std::span<const std::uint8_t> bytes);

}  // namespace swd2
