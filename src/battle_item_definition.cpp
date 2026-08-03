#include "swd2/battle_item_definition.hpp"

#include <stdexcept>

namespace swd2 {

BattleItemDefinition BattleItemDefinition::parse(
    std::uint16_t id, std::span<const std::uint8_t> item_record) {
    if (item_record.size() < 9) {
        throw std::runtime_error("ITEM.EXE battle-item record is shorter than nine bytes");
    }
    BattleItemDefinition result;
    result.id = id;
    result.type = static_cast<std::uint16_t>(item_record[0]) |
                  (static_cast<std::uint16_t>(item_record[1]) << 8U);
    result.use_flags = item_record[5];
    result.target_flags = item_record[6];
    result.effect_code = static_cast<std::uint16_t>(item_record[7]) |
                         (static_cast<std::uint16_t>(item_record[8]) << 8U);
    return result;
}

}  // namespace swd2
