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

void seed_pair(const std::filesystem::path& game_root,
               const std::filesystem::path& save_root, const std::string& number) {
    const auto state_source = game_root / ("SAVE.DA" + number);
    const auto map_source = game_root / ("MAPZ.DA" + number);
    const auto state_target = save_root / ("SAVE.DA" + number);
    const auto map_target = save_root / ("MAPZ.DA" + number);
    const auto state_exists = std::filesystem::exists(state_target);
    const auto map_exists = std::filesystem::exists(map_target);
    if (state_exists != map_exists) {
        throw std::runtime_error("save slot has only one of SAVE/MAPZ pair");
    }
    if (state_exists) return;
    if (!std::filesystem::exists(state_source) || !std::filesystem::exists(map_source)) {
        throw std::runtime_error("original save slot pair is missing");
    }
    std::filesystem::create_directories(save_root);
    std::filesystem::copy_file(state_source, state_target,
                               std::filesystem::copy_options::none);
    try {
        std::filesystem::copy_file(map_source, map_target,
                                   std::filesystem::copy_options::none);
    } catch (...) {
        std::filesystem::remove(state_target);
        throw;
    }
}

void save_state_atomic(const SharedState& state,
                       const std::filesystem::path& path) {
    auto temporary = path;
    temporary += ".tmp";
    state.save(temporary);
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot install SAVE state: " + error.message());
    }
}

void save_map_atomic(const MapDatabase& map_database,
                     const std::filesystem::path& path) {
    auto temporary = path;
    temporary += ".tmp";
    map_database.save(temporary);
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot install MAPZ state: " + error.message());
    }
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
    std::filesystem::create_directories(save_root);
    const auto state_path = save_root / ("SAVE.DA" + number);
    const auto map_path = save_root / ("MAPZ.DA" + number);
    // MAPZ first preserves the same interruption contract as SaveSlot::save.
    save_map_atomic(map_database, map_path);
    save_state_atomic(state, state_path);
}

void SaveSlot::save(const SharedState& state) {
    if (!map_database_) throw std::runtime_error("save slot has no MAPZ database");
    // Write MAPZ first and SAVE second: a location pointer in old SAVE remains
    // valid in the updated same-layout MAPZ if the process is interrupted.
    save_map_atomic(*map_database_, map_path_);
    save_state_atomic(state, state_path_);
    state_ = state;
}

}  // namespace swd2
