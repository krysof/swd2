#include "swd2/save_slot.hpp"

#include "swd2/legacy_font.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace swd2 {

namespace {

std::string suffix(std::uint8_t slot) {
    if (slot < 1 || slot > 5) {
        throw std::out_of_range("save slot must be in the range 1..5");
    }
    return std::to_string(static_cast<unsigned>(slot));
}

struct PairPaths {
    std::filesystem::path state;
    std::filesystem::path map;
    std::filesystem::path name;
    std::filesystem::path state_temporary;
    std::filesystem::path map_temporary;
    std::filesystem::path name_temporary;
    std::filesystem::path transaction;
    std::filesystem::path transaction_temporary;
};

PairPaths pair_paths(const std::filesystem::path& root,
                     const std::string& number) {
    const auto prefix = root / (".swd2-slot" + number);
    return {
        root / ("SAVE.DA" + number),
        root / ("MAPZ.DA" + number),
        root / ("NAME" + number + ".DSK"),
        std::filesystem::path(prefix.string() + "-save.tmp"),
        std::filesystem::path(prefix.string() + "-map.tmp"),
        std::filesystem::path(prefix.string() + "-name.tmp"),
        std::filesystem::path(prefix.string() + ".txn"),
        std::filesystem::path(prefix.string() + ".txn.tmp"),
    };
}

void replace_file(const std::filesystem::path& temporary,
                  const std::filesystem::path& target,
    const char* label) {
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(target, error);
        error.clear();
        std::filesystem::rename(temporary, target, error);
    }
    if (error) {
        throw std::runtime_error(std::string("cannot install ") + label +
                                 ": " + error.message());
    }
}

void remove_file(const std::filesystem::path& path, const char* label) {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        throw std::runtime_error(std::string("cannot remove ") + label +
                                 ": " + error.message());
    }
}

void recover_pair(const PairPaths& paths) {
    if (!std::filesystem::exists(paths.transaction)) {
        // These files can only predate the atomic transaction marker. In that
        // state neither canonical half has been replaced, so discard them.
        remove_file(paths.transaction_temporary, "stale save marker");
        remove_file(paths.state_temporary, "stale SAVE temporary");
        remove_file(paths.map_temporary, "stale MAPZ temporary");
        remove_file(paths.name_temporary, "stale NAME temporary");
        return;
    }

    std::ifstream marker(paths.transaction, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(marker),
                               std::istreambuf_iterator<char>()};
    const auto old_pair = contents == "SWD2PAIR1\n";
    const auto triple = contents == "SWD2SLOT2\n";
    if (!old_pair && !triple) {
        throw std::runtime_error("save slot has an invalid pair transaction marker");
    }
    // The marker is installed only after both temporaries are complete. A
    // missing temporary therefore means that rename already committed that
    // half. Roll forward any remaining half in the fixed MAPZ-then-SAVE order.
    if (triple && std::filesystem::exists(paths.name_temporary)) {
        replace_file(paths.name_temporary, paths.name, "recovered NAME font");
    }
    if (std::filesystem::exists(paths.map_temporary)) {
        replace_file(paths.map_temporary, paths.map, "recovered MAPZ state");
    }
    if (std::filesystem::exists(paths.state_temporary)) {
        replace_file(paths.state_temporary, paths.state, "recovered SAVE state");
    }
    if (!std::filesystem::is_regular_file(paths.state) ||
        !std::filesystem::is_regular_file(paths.map) ||
        (triple && !std::filesystem::is_regular_file(paths.name))) {
        throw std::runtime_error("save pair transaction cannot be recovered");
    }
    remove_file(paths.transaction, "completed save marker");
    remove_file(paths.transaction_temporary, "stale save marker temporary");
    if (old_pair) remove_file(paths.name_temporary, "stale NAME temporary");
}

void commit_prepared_pair(const PairPaths& paths) {
    if (!std::filesystem::is_regular_file(paths.state_temporary) ||
        !std::filesystem::is_regular_file(paths.map_temporary) ||
        !std::filesystem::is_regular_file(paths.name_temporary)) {
        throw std::runtime_error("save pair transaction has incomplete temporaries");
    }
    {
        std::ofstream marker(paths.transaction_temporary,
                             std::ios::binary | std::ios::trunc);
        marker << "SWD2SLOT2\n";
        marker.flush();
        if (!marker) {
            throw std::runtime_error("cannot prepare save pair transaction marker");
        }
    }
    replace_file(paths.transaction_temporary, paths.transaction,
                 "save pair transaction marker");
    // Once the marker exists, leave it and any remaining temporary in place
    // on failure. The next SaveSlot::open/save rolls the same pair forward.
    replace_file(paths.name_temporary, paths.name, "NAME font");
    replace_file(paths.map_temporary, paths.map, "MAPZ state");
    replace_file(paths.state_temporary, paths.state, "SAVE state");
    remove_file(paths.transaction, "save pair transaction marker");
}

void save_pair(const PairPaths& paths, const SharedState& state,
               const MapDatabase& map_database,
               std::span<const std::uint8_t> name_font) {
    std::filesystem::create_directories(paths.state.parent_path());
    recover_pair(paths);
    // Reject a malformed or unrelated file before installing the transaction
    // marker. The exact byte stream is preserved after this structural check.
    static_cast<void>(LegacyFont::parse(name_font));
    state.save(paths.state_temporary);
    try {
        map_database.save(paths.map_temporary);
        std::ofstream output(paths.name_temporary,
                             std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(name_font.data()),
                     static_cast<std::streamsize>(name_font.size()));
        output.flush();
        if (!output) throw std::runtime_error("cannot prepare NAME font");
    } catch (...) {
        remove_file(paths.state_temporary, "uncommitted SAVE temporary");
        remove_file(paths.map_temporary, "uncommitted MAPZ temporary");
        remove_file(paths.name_temporary, "uncommitted NAME temporary");
        throw;
    }
    commit_prepared_pair(paths);
}

void seed_pair(const std::filesystem::path& game_root,
               const std::filesystem::path& save_root, const std::string& number) {
    std::filesystem::create_directories(save_root);
    const auto paths = pair_paths(save_root, number);
    recover_pair(paths);
    const auto state_exists = std::filesystem::exists(paths.state);
    const auto map_exists = std::filesystem::exists(paths.map);
    const auto name_exists = std::filesystem::exists(paths.name);
    if (state_exists != map_exists) {
        throw std::runtime_error("save slot has only one of SAVE/MAPZ pair");
    }
    if (state_exists && name_exists) return;
    const auto state_source = game_root / ("SAVE.DA" + number);
    const auto map_source = game_root / ("MAPZ.DA" + number);
    const auto name_source = game_root / ("NAME" + number + ".DSK");
    if (!std::filesystem::exists(state_source) ||
        !std::filesystem::exists(map_source) ||
        !std::filesystem::exists(name_source)) {
        throw std::runtime_error("original save slot triple is missing");
    }
    if (state_exists) {
        // This is a save root created by the older two-file frontend. Preserve
        // its SAVE/MAPZ pair and add the released slot's matching NAME file.
        // Never copy directly to the canonical filename: an interrupted
        // browser filesystem write could otherwise turn the missing optional
        // file into a permanently malformed, apparently-present NAME.
        static_cast<void>(LegacyFont::load(name_source));
        try {
            std::filesystem::copy_file(
                name_source, paths.name_temporary,
                std::filesystem::copy_options::overwrite_existing);
        } catch (...) {
            remove_file(paths.name_temporary, "uncommitted NAME upgrade");
            throw;
        }
        replace_file(paths.name_temporary, paths.name, "upgraded NAME font");
        return;
    }
    std::filesystem::copy_file(state_source, paths.state_temporary,
                               std::filesystem::copy_options::overwrite_existing);
    try {
        std::filesystem::copy_file(map_source, paths.map_temporary,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(name_source, paths.name_temporary,
                                   std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        remove_file(paths.state_temporary, "uncommitted seed SAVE temporary");
        remove_file(paths.map_temporary, "uncommitted seed MAPZ temporary");
        remove_file(paths.name_temporary, "uncommitted seed NAME temporary");
        throw;
    }
    commit_prepared_pair(paths);
}

}  // namespace

SaveSlot SaveSlot::open(const std::filesystem::path& game_root,
                        const std::filesystem::path& save_root,
                        std::uint8_t slot) {
    const auto number = suffix(slot);
    if (save_root.empty()) throw std::invalid_argument("save root cannot be empty");
    seed_pair(game_root, save_root, number);

    SaveSlot result;
    result.slot_ = slot;
    result.state_path_ = save_root / ("SAVE.DA" + number);
    result.map_path_ = save_root / ("MAPZ.DA" + number);
    result.name_path_ = save_root / ("NAME" + number + ".DSK");
    result.state_ = SharedState::load(result.state_path_);
    result.map_database_ =
        std::make_shared<MapDatabase>(MapDatabase::load(result.map_path_));
    {
        std::ifstream input(result.name_path_, std::ios::binary);
        result.name_font_.assign(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
        static_cast<void>(LegacyFont::parse(result.name_font_));
    }
    return result;
}

void SaveSlot::save_as(const std::filesystem::path& save_root,
                       std::uint8_t slot, const SharedState& state,
                       const MapDatabase& map_database,
                       std::span<const std::uint8_t> name_font) {
    const auto number = suffix(slot);
    if (save_root.empty()) throw std::invalid_argument("save root cannot be empty");
    save_pair(pair_paths(save_root, number), state, map_database, name_font);
}

void SaveSlot::save(const SharedState& state) {
    if (!map_database_) throw std::runtime_error("save slot has no MAPZ database");
    save(state, *map_database_, name_font_);
}

void SaveSlot::save(const SharedState& state,
                    const MapDatabase& map_database) {
    save(state, map_database, name_font_);
}

void SaveSlot::save(const SharedState& state,
                    const MapDatabase& map_database,
                    std::span<const std::uint8_t> name_font) {
    save_pair(pair_paths(state_path_.parent_path(), suffix(slot_)), state,
              map_database, name_font);
    state_ = state;
    // Keep later one-argument saves on the same committed pair even when the
    // supplied database came from SAVE.DAQ/MAPZ.DAQ or another selected slot.
    map_database_ = std::make_shared<MapDatabase>(MapDatabase::load(map_path_));
    name_font_.assign(name_font.begin(), name_font.end());
}

}  // namespace swd2
