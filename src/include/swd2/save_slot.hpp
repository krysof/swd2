#pragma once

#include "swd2/map_database.hpp"
#include "swd2/shared_state.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace swd2 {

// A DOS save slot has three coupled files: SAVE.DAn contains the 1350-byte
// inter-module state, MAPZ.DAn contains mutable world/entity records, and
// NAME<n>.DSK contains the sixteen user-editable name glyphs. Keeping all three
// together avoids cross-slot name leakage and gives every frontend one portable
// persistence API.
class SaveSlot {
public:
    static SaveSlot open(const std::filesystem::path& game_root,
                         const std::filesystem::path& save_root,
                         std::uint8_t slot);
    static void save_as(const std::filesystem::path& save_root,
                        std::uint8_t slot, const SharedState& state,
                        const MapDatabase& map_database,
                        std::span<const std::uint8_t> name_font);

    [[nodiscard]] std::uint8_t slot() const noexcept { return slot_; }
    [[nodiscard]] const std::filesystem::path& state_path() const noexcept {
        return state_path_;
    }
    [[nodiscard]] const std::filesystem::path& map_path() const noexcept {
        return map_path_;
    }
    [[nodiscard]] const std::filesystem::path& name_path() const noexcept {
        return name_path_;
    }
    [[nodiscard]] const SharedState& state() const noexcept { return state_; }
    [[nodiscard]] std::shared_ptr<MapDatabase> map_database() const noexcept {
        return map_database_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& name_font() const noexcept {
        return name_font_;
    }

    // Commits all three files through a roll-forward transaction marker. A
    // crash after only NAME, MAPZ or SAVE is renamed is completed on the next
    // open, so a caller observes only the old slot or the complete new slot.
    // GameContext owns the live SharedState and supplies it here.
    void save(const SharedState& state);
    // The live MAPZ database may have been replaced wholesale by New Game or
    // Continue after this SaveSlot object was opened.  Frontends performing
    // an optional exit checkpoint must supply that live half explicitly;
    // otherwise they could combine a new SAVE block with the stale MAPZ half
    // that originally seeded the frontend.
    void save(const SharedState& state, const MapDatabase& map_database);
    void save(const SharedState& state, const MapDatabase& map_database,
              std::span<const std::uint8_t> name_font);

private:
    std::uint8_t slot_{};
    std::filesystem::path state_path_;
    std::filesystem::path map_path_;
    std::filesystem::path name_path_;
    SharedState state_;
    std::shared_ptr<MapDatabase> map_database_;
    std::vector<std::uint8_t> name_font_;
};

}  // namespace swd2
