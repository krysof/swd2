#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swd2 {

// CHNA*, MAP*, ITEM* and DATE2 use an MZ-looking wrapper as a data
// archive.  They are tables of event records, not native programs to launch.
class ScriptArchive {
public:
    static ScriptArchive load(const std::filesystem::path& path);
    static bool probe(const std::filesystem::path& path) noexcept;
    // Builds the same pointer-table representation without an on-disk MZ
    // wrapper.  Useful to generate portable events and focused VM tests; each
    // record must include its terminating 0xffff opcode.
    static ScriptArchive from_records(
        std::span<const std::vector<std::uint8_t>> records);

    [[nodiscard]] std::size_t entry_count() const noexcept { return offsets_.size(); }
    [[nodiscard]] std::size_t unique_record_count() const noexcept;
    [[nodiscard]] std::uint16_t sentinel_offset() const noexcept { return offsets_.front(); }
    [[nodiscard]] const std::vector<std::uint16_t>& offsets() const noexcept { return offsets_; }
    [[nodiscard]] std::span<const std::uint8_t> entry(std::size_t index) const;

private:
    std::vector<std::uint8_t> image_;
    std::vector<std::uint16_t> offsets_;
};

}  // namespace swd2
