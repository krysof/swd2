#pragma once

#include "swd2/map_database.hpp"
#include "swd2/shared_state.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace swd2 {

// A DOS save is a pair: SAVE.DAn contains the 1350-byte inter-module state and
// MAPZ.DAn contains mutable world/entity records. Keeping them together avoids
// the former hard-coded slot-one behavior and gives every frontend one portable
// persistence API.
class SaveSlot {
public:
    static SaveSlot open(const std::filesystem::path& game_root,
                         const std::filesystem::path& save_root,
                         std::uint8_t slot);
    static void save_as(const std::filesystem::path& save_root,
                        std::uint8_t slot, const SharedState& state,
                        const MapDatabase& map_database);

    [[nodiscard]] std::uint8_t slot() const noexcept { return slot_; }
    [[nodiscard]] const std::filesystem::path& state_path() const noexcept {
        return state_path_;
    }
    [[nodiscard]] const std::filesystem::path& map_path() const noexcept {
        return map_path_;
    }
    [[nodiscard]] const SharedState& state() const noexcept { return state_; }
    [[nodiscard]] std::shared_ptr<MapDatabase> map_database() const noexcept {
        return map_database_;
    }

    // Commits both halves through a roll-forward transaction marker. A crash
    // after only MAPZ or SAVE is renamed is completed on the next open, so a
    // caller can observe only the old pair or the complete new pair.
    // GameContext owns the live SharedState and supplies it here.
    void save(const SharedState& state);

private:
    std::uint8_t slot_{};
    std::filesystem::path state_path_;
    std::filesystem::path map_path_;
    SharedState state_;
    std::shared_ptr<MapDatabase> map_database_;
};

}  // namespace swd2
