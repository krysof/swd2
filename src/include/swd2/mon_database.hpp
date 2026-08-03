#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace swd2 {

struct MonValueEntry {
    std::uint16_t definition_id{};
    std::uint16_t value{};
};

// Structural decoder for the data-only MON.EXE image.  Field semantics beyond
// the confirmed 17x17 symmetric interaction matrix and fifteen terminated
// value lists remain named conservatively until all FIG formulas are mapped.
class MonDatabase {
public:
    static constexpr std::size_t matrix_extent = 17;
    static constexpr std::size_t table_count = 15;

    static MonDatabase load(const std::filesystem::path& path);

    [[nodiscard]] std::uint16_t interaction(std::size_t first,
                                            std::size_t second) const;
    [[nodiscard]] const std::array<std::array<std::uint16_t, matrix_extent>,
                                   matrix_extent>& interaction_matrix() const noexcept {
        return interaction_matrix_;
    }
    [[nodiscard]] const std::array<std::vector<MonValueEntry>, table_count>&
    value_tables() const noexcept {
        return value_tables_;
    }
    [[nodiscard]] std::size_t value_entry_count() const noexcept;

private:
    std::array<std::array<std::uint16_t, matrix_extent>, matrix_extent>
        interaction_matrix_{};
    std::array<std::vector<MonValueEntry>, table_count> value_tables_;
};

}  // namespace swd2
