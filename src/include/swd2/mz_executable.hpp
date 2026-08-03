#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace swd2::dos {

struct MzHeader {
    std::uint16_t signature{};
    std::uint16_t bytes_on_last_page{};
    std::uint16_t pages_in_file{};
    std::uint16_t relocation_count{};
    std::uint16_t header_paragraphs{};
    std::uint16_t min_extra_paragraphs{};
    std::uint16_t max_extra_paragraphs{};
    std::uint16_t initial_ss{};
    std::uint16_t initial_sp{};
    std::uint16_t checksum{};
    std::uint16_t initial_ip{};
    std::uint16_t initial_cs{};
    std::uint16_t relocation_table_offset{};
    std::uint16_t overlay_number{};
};

struct Relocation {
    std::uint16_t offset{};
    std::uint16_t segment{};
};

class MzExecutable {
public:
    static MzExecutable load(const std::filesystem::path& path);

    [[nodiscard]] const MzHeader& header() const noexcept { return header_; }
    [[nodiscard]] const std::vector<Relocation>& relocations() const noexcept { return relocations_; }
    [[nodiscard]] std::uint64_t actual_size() const noexcept { return actual_size_; }
    [[nodiscard]] std::uint64_t declared_size() const noexcept;
    [[nodiscard]] std::uint64_t header_size() const noexcept;
    [[nodiscard]] std::uint64_t load_image_size() const noexcept;
    [[nodiscard]] std::uint64_t overlay_size() const noexcept;
    [[nodiscard]] std::uint64_t entry_file_offset() const noexcept;

private:
    MzHeader header_{};
    std::vector<Relocation> relocations_;
    std::uint64_t actual_size_{};
};

}  // namespace swd2::dos
