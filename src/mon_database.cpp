#include "swd2/mon_database.hpp"

#include "swd2/mz_executable.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <numeric>
#include <span>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("MON.EXE word is out of bounds");
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

}  // namespace

MonDatabase MonDatabase::load(const std::filesystem::path& path) {
    const auto executable = dos::MzExecutable::load(path);
    const auto& header = executable.header();
    if (header.relocation_count != 0 || header.initial_ip != 0 || header.initial_cs != 0) {
        throw std::runtime_error("MON.EXE contains native code rather than table data");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open MON.EXE: " + path.string());
    input.seekg(static_cast<std::streamoff>(executable.header_size()));
    const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>()};
    constexpr std::size_t directory_words = table_count + 1;
    constexpr std::size_t directory_bytes = directory_words * 2;
    if (image.size() < directory_bytes) throw std::runtime_error("MON.EXE directory is truncated");

    std::array<std::uint16_t, directory_words> offsets{};
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        offsets[index] = word(image, index * 2);
    }
    if (offsets[0] >= image.size() || image[offsets[0]] != 0xff ||
        offsets[1] != directory_bytes + matrix_extent * matrix_extent * 2 ||
        !std::is_sorted(offsets.begin() + 1, offsets.end())) {
        throw std::runtime_error("MON.EXE directory/matrix boundary is invalid");
    }

    MonDatabase result;
    auto cursor = directory_bytes;
    for (auto& row : result.interaction_matrix_) {
        for (auto& value : row) {
            value = word(image, cursor);
            cursor += 2;
        }
    }
    for (std::size_t y = 0; y < matrix_extent; ++y) {
        for (std::size_t x = 0; x < matrix_extent; ++x) {
            if (result.interaction_matrix_[y][x] != result.interaction_matrix_[x][y]) {
                throw std::runtime_error("MON.EXE interaction matrix is not symmetric");
            }
        }
    }

    for (std::size_t table = 0; table < table_count; ++table) {
        cursor = offsets[table + 1];
        const auto finish = table + 2 < offsets.size() ? offsets[table + 2] : offsets[0];
        while (cursor + 2 <= finish && word(image, cursor) != 0xffff) {
            if (cursor + 4 > finish) throw std::runtime_error("MON.EXE value pair is truncated");
            result.value_tables_[table].push_back({word(image, cursor), word(image, cursor + 2)});
            cursor += 4;
        }
        if (cursor + 2 != finish || word(image, cursor) != 0xffff) {
            throw std::runtime_error("MON.EXE value list terminator/boundary mismatch");
        }
    }
    return result;
}

std::uint16_t MonDatabase::interaction(std::size_t first, std::size_t second) const {
    return interaction_matrix_.at(first).at(second);
}

std::size_t MonDatabase::value_entry_count() const noexcept {
    return std::accumulate(value_tables_.begin(), value_tables_.end(), std::size_t{},
                           [](std::size_t total, const auto& table) {
                               return total + table.size();
                           });
}

}  // namespace swd2
