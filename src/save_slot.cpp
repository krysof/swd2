#include "swd2/save_slot.hpp"

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
    std::filesystem::path state_temporary;
    std::filesystem::path map_temporary;
    std::filesystem::path transaction;
    std::filesystem::path transaction_temporary;
};

PairPaths pair_paths(const std::filesystem::path& root,
                     const std::string& number) {
    const auto prefix = root / (".swd2-slot" + number);
    return {
        root / ("SAVE.DA" + number),
        root / ("MAPZ.DA" + number),
        std::filesystem::path(prefix.string() + "-save.tmp"),
        std::filesystem::path(prefix.string() + "-map.tmp"),
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
        return;
    }

    std::ifstream marker(paths.transaction, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(marker),
                               std::istreambuf_iterator<char>()};
    if (contents != "SWD2PAIR1\n") {
        throw std::runtime_error("save slot has an invalid pair transaction marker");
    }
    // The marker is installed only after both temporaries are complete. A
    // missing temporary therefore means that rename already committed that
    // half. Roll forward any remaining half in the fixed MAPZ-then-SAVE order.
    if (std::filesystem::exists(paths.map_temporary)) {
        replace_file(paths.map_temporary, paths.map, "recovered MAPZ state");
    }
    if (std::filesystem::exists(paths.state_temporary)) {
        replace_file(paths.state_temporary, paths.state, "recovered SAVE state");
    }
    if (!std::filesystem::is_regular_file(paths.state) ||
        !std::filesystem::is_regular_file(paths.map)) {
        throw std::runtime_error("save pair transaction cannot be recovered");
    }
    remove_file(paths.transaction, "completed save marker");
    remove_file(paths.transaction_temporary, "stale save marker temporary");
}

void commit_prepared_pair(const PairPaths& paths) {
    if (!std::filesystem::is_regular_file(paths.state_temporary) ||
        !std::filesystem::is_regular_file(paths.map_temporary)) {
        throw std::runtime_error("save pair transaction has incomplete temporaries");
    }
    {
        std::ofstream marker(paths.transaction_temporary,
                             std::ios::binary | std::ios::trunc);
        marker << "SWD2PAIR1\n";
        marker.flush();
        if (!marker) {
            throw std::runtime_error("cannot prepare save pair transaction marker");
        }
    }
    replace_file(paths.transaction_temporary, paths.transaction,
                 "save pair transaction marker");
    // Once the marker exists, leave it and any remaining temporary in place
    // on failure. The next SaveSlot::open/save rolls the same pair forward.
    replace_file(paths.map_temporary, paths.map, "MAPZ state");
    replace_file(paths.state_temporary, paths.state, "SAVE state");
    remove_file(paths.transaction, "save pair transaction marker");
}

void save_pair(const PairPaths& paths, const SharedState& state,
               const MapDatabase& map_database) {
    std::filesystem::create_directories(paths.state.parent_path());
    recover_pair(paths);
    state.save(paths.state_temporary);
    try {
        map_database.save(paths.map_temporary);
    } catch (...) {
        remove_file(paths.state_temporary, "uncommitted SAVE temporary");
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
    if (state_exists != map_exists) {
        throw std::runtime_error("save slot has only one of SAVE/MAPZ pair");
    }
    if (state_exists) return;
    const auto state_source = game_root / ("SAVE.DA" + number);
    const auto map_source = game_root / ("MAPZ.DA" + number);
    if (!std::filesystem::exists(state_source) ||
        !std::filesystem::exists(map_source)) {
        throw std::runtime_error("original save slot pair is missing");
    }
    std::filesystem::copy_file(state_source, paths.state_temporary,
                               std::filesystem::copy_options::overwrite_existing);
    try {
        std::filesystem::copy_file(map_source, paths.map_temporary,
                                   std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        remove_file(paths.state_temporary, "uncommitted seed SAVE temporary");
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
    result.state_ = SharedState::load(result.state_path_);
    result.map_database_ =
        std::make_shared<MapDatabase>(MapDatabase::load(result.map_path_));
    return result;
}

void SaveSlot::save_as(const std::filesystem::path& save_root,
                       std::uint8_t slot, const SharedState& state,
                       const MapDatabase& map_database) {
    const auto number = suffix(slot);
    if (save_root.empty()) throw std::invalid_argument("save root cannot be empty");
    save_pair(pair_paths(save_root, number), state, map_database);
}

void SaveSlot::save(const SharedState& state) {
    if (!map_database_) throw std::runtime_error("save slot has no MAPZ database");
    save(state, *map_database_);
}

void SaveSlot::save(const SharedState& state,
                    const MapDatabase& map_database) {
    save_pair(pair_paths(state_path_.parent_path(), suffix(slot_)), state,
              map_database);
    state_ = state;
    // Keep later one-argument saves on the same committed pair even when the
    // supplied database came from SAVE.DAQ/MAPZ.DAQ or another selected slot.
    map_database_ = std::make_shared<MapDatabase>(MapDatabase::load(map_path_));
}

}  // namespace swd2
