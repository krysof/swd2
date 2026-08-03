#include "swd2/battle_composite_effect.hpp"

#include <stdexcept>

namespace swd2 {

BattleCompositeEffect BattleCompositeEffect::parse(
    std::uint16_t ability_id, const ScriptArchive& items) {
    constexpr std::size_t logical_base = 0x8c;
    constexpr std::size_t archive_metadata_entries = 2;
    const auto record_index = logical_base +
                              static_cast<std::size_t>(ability_id) +
                              archive_metadata_entries;
    if (record_index >= items.entry_count()) {
        throw std::runtime_error(
            "FIG composite ability references a missing ITEM definition");
    }
    const auto record = items.entry(record_index);
    if (record.size() < 11) {
        throw std::runtime_error(
            "FIG composite ability ITEM definition is shorter than +0b");
    }
    return {ability_id, record[9], record[10]};
}

}  // namespace swd2
