#include "swd2/battle_database.hpp"

#include "swd2/mz_executable.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("ORC.EXE word is outside the data image");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::vector<std::uint8_t> mz_image(const std::filesystem::path& path) {
    const auto executable = dos::MzExecutable::load(path);
    const auto& header = executable.header();
    if (header.relocation_count != 0 || header.initial_ip != 0 || header.initial_cs != 0) {
        throw std::runtime_error("ORC.EXE contains native code instead of battle data");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open battle database: " + path.string());
    input.seekg(static_cast<std::streamoff>(executable.header_size()));
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string fixed_dos_path(std::span<const std::uint8_t> bytes) {
    const auto terminator = std::find(bytes.begin(), bytes.end(), 0);
    if (terminator == bytes.end()) {
        throw std::runtime_error("ORC.EXE encounter background path has no terminator");
    }
    return {bytes.begin(), terminator};
}

}  // namespace

BattleDatabase BattleDatabase::load(const std::filesystem::path& path) {
    const auto image = mz_image(path);
    if (image.size() < directory_bytes) {
        throw std::runtime_error("ORC.EXE battle directory is truncated");
    }

    BattleDatabase result;
    result.directory_offsets_.reserve(directory_bytes / 2);
    for (std::size_t offset = 0; offset < directory_bytes; offset += 2) {
        const auto value = word(image, offset);
        if (value != 0 && (value < directory_bytes || value >= image.size())) {
            throw std::runtime_error("ORC.EXE directory points outside the record area");
        }
        result.directory_offsets_.push_back(value);
    }

    std::array<std::uint16_t, growth_table_count> growth_offsets{};
    for (std::size_t table = 0; table < growth_table_count; ++table) {
        growth_offsets[table] = result.directory_offsets_.at(table);
    }
    if (!std::is_sorted(growth_offsets.begin(), growth_offsets.end())) {
        throw std::runtime_error("ORC.EXE growth tables are not ordered");
    }
    constexpr std::size_t growth_bytes = growth_rows * 9 * 2;
    for (std::size_t table = 0; table < growth_table_count; ++table) {
        const auto start = static_cast<std::size_t>(growth_offsets[table]);
        const auto finish = table + 1 < growth_table_count
                                ? static_cast<std::size_t>(growth_offsets[table + 1])
                                : image.size();
        if (finish - start != growth_bytes) {
            throw std::runtime_error("ORC.EXE party growth table has an unexpected size");
        }
        auto cursor = start;
        for (auto& row : result.growth_tables_[table]) {
            for (auto& field : row.fields) {
                field = word(image, cursor);
                cursor += 2;
            }
        }
    }

    const auto first_growth = static_cast<std::size_t>(growth_offsets.front());
    std::vector<std::uint16_t> starts;
    for (const auto offset : result.directory_offsets_) {
        if (offset >= directory_bytes && offset < first_growth) starts.push_back(offset);
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    if (starts.empty() || starts.front() != directory_bytes) {
        throw std::runtime_error("ORC.EXE first encounter does not follow its directory");
    }

    std::map<std::uint16_t, std::size_t> encounter_indices;
    result.encounters_.reserve(starts.size());
    for (std::size_t record_index = 0; record_index < starts.size(); ++record_index) {
        const auto start = static_cast<std::size_t>(starts[record_index]);
        const auto finish = record_index + 1 < starts.size()
                                ? static_cast<std::size_t>(starts[record_index + 1])
                                : first_growth;
        if (start + 30 > finish) {
            throw std::runtime_error("ORC.EXE encounter header is truncated");
        }

        BattleEncounter encounter;
        encounter.data_offset = starts[record_index];
        encounter.background_path = fixed_dos_path(
            std::span<const std::uint8_t>(image).subspan(start, 20));
        auto cursor = start + 20;
        for (auto& definition : encounter.monster_definition_ids) {
            definition = word(image, cursor);
            cursor += 2;
        }
        const auto count = static_cast<std::size_t>(word(image, cursor));
        cursor += 2;
        if (count == 0 || count > 5 || cursor + count * 4 > finish) {
            throw std::runtime_error("ORC.EXE encounter has an invalid combatant count");
        }
        encounter.definition_slots.reserve(count);
        encounter.horizontal_positions.reserve(count);
        for (std::size_t index = 0; index < count; ++index, cursor += 2) {
            const auto slot = word(image, cursor);
            if (slot >= encounter.monster_definition_ids.size() ||
                encounter.monster_definition_ids[slot] == 0) {
                throw std::runtime_error("ORC.EXE combatant selects an empty definition slot");
            }
            encounter.definition_slots.push_back(slot);
        }
        for (std::size_t index = 0; index < count; ++index, cursor += 2) {
            encounter.horizontal_positions.push_back(word(image, cursor));
        }
        if (word(image, cursor) == 0x2323) {  // literal "##"
            encounter.special_value = word(image, cursor + 2);
            cursor += 4;
        }
        const auto text_start = cursor;
        while (cursor + 2 <= finish && word(image, cursor) != 0x2121) cursor += 2;
        if (cursor + 2 > finish) {
            throw std::runtime_error("ORC.EXE encounter introduction has no !! terminator");
        }
        encounter.introduction_text.assign(
            image.begin() + static_cast<std::ptrdiff_t>(text_start),
            image.begin() + static_cast<std::ptrdiff_t>(cursor));
        if (encounter.introduction_text.size() >= 4) {
            const auto prompt_offset = encounter.introduction_text.size() - 2U;
            const auto prompt = word(encounter.introduction_text, prompt_offset);
            if (prompt == 0x4e59 || prompt == 0x594e) {
                if (word(encounter.introduction_text, prompt_offset - 2U) != 0x2424) {
                    throw std::runtime_error(
                        "ORC.EXE encounter prompt does not follow its $$ text terminator");
                }
                encounter.prompt_order = prompt == 0x4e59
                                             ? BattlePromptOrder::yes_no
                                             : BattlePromptOrder::no_yes;
                encounter.introduction_text.resize(prompt_offset);
            }
        }
        if (!encounter.introduction_text.empty()) {
            if (encounter.introduction_text.size() < 2 ||
                word(encounter.introduction_text,
                     encounter.introduction_text.size() - 2U) != 0x2424) {
                throw std::runtime_error(
                    "ORC.EXE encounter introduction has no $$ text terminator");
            }
            encounter.introduction_text.resize(
                encounter.introduction_text.size() - 2U);
        }
        cursor += 2;
        if (cursor != finish) {
            throw std::runtime_error("ORC.EXE encounter has unparsed trailing bytes");
        }
        encounter_indices.emplace(encounter.data_offset, result.encounters_.size());
        result.encounters_.push_back(std::move(encounter));
    }

    result.directory_encounters_.resize(result.directory_offsets_.size());
    for (std::size_t index = 0; index < result.directory_offsets_.size(); ++index) {
        if (const auto found = encounter_indices.find(result.directory_offsets_[index]);
            found != encounter_indices.end()) {
            result.directory_encounters_[index] = found->second;
        }
    }
    return result;
}

std::size_t BattleDatabase::occupied_directory_entry_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        directory_offsets_.begin(), directory_offsets_.end(),
        [](std::uint16_t offset) { return offset != 0; }));
}

std::optional<std::reference_wrapper<const BattleEncounter>>
BattleDatabase::encounter_at_directory_offset(std::uint16_t byte_offset) const noexcept {
    if ((byte_offset & 1U) != 0 || byte_offset / 2 >= directory_encounters_.size()) {
        return std::nullopt;
    }
    const auto index = directory_encounters_[byte_offset / 2];
    if (!index) return std::nullopt;
    return std::cref(encounters_[*index]);
}

std::uint16_t select_random_encounter(std::uint16_t map_position,
                                      std::uint16_t viewport_x,
                                      std::uint16_t viewport_y,
                                      std::uint8_t hundredth) noexcept {
    struct Region {
        std::uint16_t map;
        std::uint16_t min_x;
        std::uint16_t max_x;
        std::uint16_t min_y;
        std::uint16_t max_y;
        std::uint16_t base;
    };
    // Transcribed from FIG.EXE 1000:34c2. Bounds are inclusive and the first
    // matching row wins, just as in the original table walk.
    static constexpr Region regions[] = {
        {0x00a,77,140,0,44,100},{0x00a,77,140,45,157,148},
        {0x00a,0,76,45,157,164},{0x00a,0,76,0,44,180},
        {0x00c,0,140,0,157,116},{0x00e,0,140,0,157,132},
        {0x030,0,140,0,157,196},
        {0x034,0,140,0,64,212},{0x034,0,140,65,118,228},
        {0x034,0,140,119,157,212},
        {0x036,0,52,0,157,244},{0x036,53,140,0,157,260},
        {0x048,0,140,0,157,164},
        {0x04a,0,140,76,157,276},{0x04a,0,140,0,75,324},
        {0x04c,0,140,0,157,292},{0x04e,0,140,0,157,308},
        {0x054,0,140,81,157,340},{0x054,0,140,0,80,356},
        {0x058,0,140,68,157,372},{0x058,0,140,0,67,388},
        {0x05c,0,170,116,157,404},{0x05c,0,170,71,115,420},
        {0x05c,70,140,0,70,436},{0x05c,0,69,0,70,452},
        {0x060,0,140,94,157,484},{0x060,0,140,0,93,500},
        {0x064,0,94,0,157,516},{0x064,95,140,0,157,532},
        {0x06c,0,140,0,157,468},
        {0x098,0,140,71,157,548},{0x098,0,140,0,70,560},
        {0x09a,0,40,0,57,580},{0x09c,0,40,0,57,580},
        {0x09e,0,40,0,57,580},{0x0a0,0,140,0,157,596},
        {0x0a2,0,140,0,102,612},
        {0x0a8,0,51,0,64,628},{0x0a8,0,110,65,127,660},
        {0x0a8,52,110,0,64,676},{0x0aa,0,40,0,2,644},
        {0x0b2,0,180,0,180,692},{0x0b4,0,180,0,180,708},
        {0x0b8,0,180,0,180,724},{0x0ba,0,180,0,180,740},
        {0x0bc,0,180,0,180,756},{0x0be,0,180,0,180,772},
        {0x0da,0,127,74,157,788},{0x0da,0,127,0,73,804},
        {0x0da,128,140,0,157,820},{0x0fa,0,180,0,180,836},
        {0x0fe,70,140,49,157,852},{0x0fe,0,140,0,48,868},
        {0x0fe,0,69,49,157,884},
        {0x0f8,0,140,101,157,900},{0x0f8,0,140,51,100,916},
        {0x0f8,0,140,0,50,932},
        {0x100,70,150,0,69,948},{0x100,0,69,0,69,964},
        {0x100,0,69,70,147,980},{0x100,70,150,70,147,996},
        {0x126,0,130,0,230,1012},{0x128,0,130,0,230,1028},
        {0x12a,0,130,0,230,1044},{0x12c,0,130,0,230,1060},
        {0x12e,0,130,0,230,1076},{0x11c,0,130,0,230,1092},
        {0x11e,0,130,0,230,1108},
        {0x11a,0,130,100,230,1092},{0x11a,0,130,0,100,1124},
        {0x120,0,130,0,230,1140},{0x124,0,130,0,230,1156},
        {0x122,0,130,0,230,1172},{0x116,0,130,0,230,1204},
        {0x118,0,130,0,230,1220},
        {0x114,0,130,100,230,1188},{0x114,0,130,0,100,1236},
    };
    const auto map = static_cast<std::uint16_t>(map_position & 0x0fffU);
    std::uint16_t base = 100;
    for (const auto& region : regions) {
        if (region.map == map && viewport_x >= region.min_x && viewport_x <= region.max_x &&
            viewport_y >= region.min_y && viewport_y <= region.max_y) {
            base = region.base;
            break;
        }
    }
    return static_cast<std::uint16_t>(base + (hundredth & 7U) * 2U);
}

}  // namespace swd2
