#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace swd2 {

struct ExtensionStats {
    std::uint64_t files{};
    std::uint64_t bytes{};
};

struct AssetCatalog {
    std::uint64_t files{};
    std::uint64_t bytes{};
    std::map<std::string, ExtensionStats> by_extension;

    static AssetCatalog scan(const std::filesystem::path& game_root);
};

// Converts paths embedded in the DOS programs to paths relative to game/.
// Examples: C:MENU.RSK -> MENU.RSK, C:\\SWD2\\BA\\BA01.RSK -> BA/BA01.RSK.
std::filesystem::path normalize_dos_asset_path(std::string dos_path);

}  // namespace swd2
