#include "swd2/asset_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace swd2 {

namespace {

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

}  // namespace

AssetCatalog AssetCatalog::scan(const std::filesystem::path& game_root) {
    if (!std::filesystem::is_directory(game_root)) {
        throw std::runtime_error("game directory not found: " + game_root.string());
    }

    AssetCatalog result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(game_root)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto size = entry.file_size();
        auto extension = uppercase(entry.path().extension().string());
        if (extension.empty()) {
            extension = "<none>";
        }

        ++result.files;
        result.bytes += size;
        auto& stats = result.by_extension[extension];
        ++stats.files;
        stats.bytes += size;
    }
    return result;
}

std::filesystem::path normalize_dos_asset_path(std::string dos_path) {
    std::replace(dos_path.begin(), dos_path.end(), '\\', '/');

    if (dos_path.size() >= 2 && dos_path[1] == ':') {
        dos_path.erase(0, 2);
    }
    while (!dos_path.empty() && dos_path.front() == '/') {
        dos_path.erase(dos_path.begin());
    }

    const auto upper = uppercase(dos_path);
    if (upper.rfind("SWD2/", 0) == 0) {
        dos_path.erase(0, 5);
    }
    return std::filesystem::path(dos_path).lexically_normal();
}

}  // namespace swd2
