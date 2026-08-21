#pragma once

#include "swd2/map_database.hpp"
#include "swd2/runtime.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace swd2 {

class MapResource;

class RpgModule final : public GameModule {
public:
    explicit RpgModule(bool start_from_loaded_save = false) noexcept
        : start_from_loaded_save_(start_from_loaded_save) {}

    [[nodiscard]] Module module() const noexcept override { return Module::rpg; }
    Marker run(GameContext& context, Marker input) override;

private:
    [[nodiscard]] std::shared_ptr<const MapResource> load_map_resource(
        const std::filesystem::path& graphics_base_path,
        const std::filesystem::path& layout_base_path);

    std::optional<MapDatabase> map_database_;
    std::filesystem::path map_database_path_;
    std::map<
        std::pair<std::filesystem::path, std::filesystem::path>,
        std::shared_ptr<const MapResource>> map_resource_cache_;
    bool pending_map_reload_{};
    bool music_enabled_{true};
    bool sound_enabled_{true};
    bool start_from_loaded_save_{};
};

}  // namespace swd2
