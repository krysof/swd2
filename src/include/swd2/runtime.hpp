#pragma once

#include "swd2/launcher.hpp"
#include "swd2/map_database.hpp"
#include "swd2/platform.hpp"
#include "swd2/shared_state.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace swd2 {

using SaveSlotWriter = std::function<void(std::uint8_t, const SharedState&,
                                          const MapDatabase&,
                                          std::span<const std::uint8_t>)>;

struct LoadedSaveSlot {
    SharedState state;
    std::shared_ptr<MapDatabase> map_database;
    std::vector<std::uint8_t> name_font;
};

using SaveSlotLoader = std::function<LoadedSaveSlot(std::uint8_t)>;

struct GameContext {
    std::filesystem::path game_root;
    SharedState shared_state;
    PlatformBackend& platform;
    // Set by SaveSlot-aware frontends. Tests/tools may leave it null and the
    // RPG module will fall back to the original game-root MAPZ.DA1.
    std::shared_ptr<MapDatabase> map_database;
    // Optional frontend persistence hook used by RPG's field-save item. It is
    // deliberately platform-neutral: the core chooses slot 1..5 and supplies
    // both live halves of the DOS save pair.
    SaveSlotWriter save_slot;
    // Optional matching load hook used by RPG's System/Read row. The core
    // replaces both halves atomically at the next map-resource boundary.
    SaveSlotLoader load_slot;
    // Active NAMEQ.DSK image. RPG's new-game editor changes its sixteen glyph
    // bitmaps, Continue replaces it from NAME<n>.DSK, and Record persists it
    // beside SAVE/MAPZ. Tests which omit it fall back to released NAME.DSK.
    std::vector<std::uint8_t> name_font;
};

class GameModule {
public:
    virtual ~GameModule() = default;
    [[nodiscard]] virtual Module module() const noexcept = 0;
    virtual Marker run(GameContext& context, Marker input) = 0;
};

class ModuleRegistry {
public:
    void add(std::unique_ptr<GameModule> module);
    [[nodiscard]] GameModule* find(Module module) const noexcept;

private:
    std::array<std::unique_ptr<GameModule>, 4> modules_;
};

// Executes the former MEO/RPG/FIG/DEMO programs as ordinary in-process C++
// calls. There is intentionally no process creation or DOS shared-memory API.
class MonolithicRuntime {
public:
    explicit MonolithicRuntime(ModuleRegistry registry);
    [[nodiscard]] LaunchResult run(GameContext& context) const;

private:
    ModuleRegistry registry_;
};

}  // namespace swd2
