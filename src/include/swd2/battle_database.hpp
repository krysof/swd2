#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace swd2 {

enum class BattlePromptOrder {
    none,
    yes_no,  // trailing literal "YN"
    no_yes,  // trailing literal "NY"
};

// One normal ORC.EXE battle formation.  The four definition ids address
// ITEM.EXE records; each combatant selects one of those ids and supplies its
// horizontal placement.  Vertical placement is part of the ITEM record.
struct BattleEncounter {
    std::uint16_t data_offset{};
    std::string background_path;
    std::array<std::uint16_t, 4> monster_definition_ids{};
    std::vector<std::uint16_t> definition_slots;
    std::vector<std::uint16_t> horizontal_positions;
    std::optional<std::uint16_t> special_value;
    std::vector<std::uint8_t> introduction_text;
    BattlePromptOrder prompt_order{BattlePromptOrder::none};
};

struct BattleGrowthRow {
    std::array<std::uint16_t, 9> fields{};
};

// ORC.EXE is not native code despite its MZ wrapper.  Its first 0x4e4 bytes
// are a byte-offset directory used verbatim by SharedState[0x4a0].  Entries
// 0/2/4/6 point to four 60-row party growth tables; the remaining non-zero
// entries point to normal battle formations.
class BattleDatabase {
public:
    static constexpr std::size_t directory_bytes = 0x4e4;
    static constexpr std::size_t growth_table_count = 4;
    static constexpr std::size_t growth_rows = 60;

    static BattleDatabase load(const std::filesystem::path& path);

    [[nodiscard]] std::size_t directory_entry_count() const noexcept {
        return directory_offsets_.size();
    }
    [[nodiscard]] std::size_t encounter_count() const noexcept { return encounters_.size(); }
    [[nodiscard]] std::size_t occupied_directory_entry_count() const noexcept;
    [[nodiscard]] const std::vector<std::uint16_t>& directory_offsets() const noexcept {
        return directory_offsets_;
    }
    [[nodiscard]] const std::vector<BattleEncounter>& encounters() const noexcept {
        return encounters_;
    }
    [[nodiscard]] const std::array<std::array<BattleGrowthRow, growth_rows>,
                                   growth_table_count>&
    growth_tables() const noexcept {
        return growth_tables_;
    }
    [[nodiscard]] std::optional<std::reference_wrapper<const BattleEncounter>>
    encounter_at_directory_offset(std::uint16_t byte_offset) const noexcept;

private:
    std::vector<std::uint16_t> directory_offsets_;
    std::vector<BattleEncounter> encounters_;
    std::vector<std::optional<std::size_t>> directory_encounters_;
    std::array<std::array<BattleGrowthRow, growth_rows>, growth_table_count> growth_tables_{};
};

// Exact region lookup performed by FIG.EXE when SharedState[0x4a0] is zero.
// `hundredth` is DOS int 21h/2ch DL; its low three bits select one of eight
// adjacent ORC directory entries.
[[nodiscard]] std::uint16_t select_random_encounter(std::uint16_t map_position,
                                                    std::uint16_t viewport_x,
                                                    std::uint16_t viewport_y,
                                                    std::uint8_t hundredth) noexcept;

}  // namespace swd2
