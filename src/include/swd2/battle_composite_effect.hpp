#pragma once

#include "swd2/script_archive.hpp"

#include <cstdint>

namespace swd2 {

// FIG dispatcher entry 6b resolves ITEM logical record (0x8c + ability id)
// and reads two nested effect codes from its unaligned word at +9.
struct BattleCompositeEffect {
    std::uint16_t ability_id{};
    std::uint8_t first_effect{};
    std::uint8_t second_effect{};

    static BattleCompositeEffect parse(std::uint16_t ability_id,
                                       const ScriptArchive& items);
};

}  // namespace swd2
