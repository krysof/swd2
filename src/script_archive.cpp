#include "swd2/script_archive.hpp"

#include "swd2/mz_executable.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("script archive word is out of bounds");
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

}  // namespace

ScriptArchive ScriptArchive::load(const std::filesystem::path& path) {
    const auto mz = dos::MzExecutable::load(path);
    const auto& header = mz.header();
    if (header.relocation_count != 0 || header.initial_ip != 0 || header.initial_cs != 0) {
        throw std::runtime_error("MZ file is native code rather than a legacy script archive");
    }

    std::ifstream input(path, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(mz.header_size()));
    ScriptArchive result;
    result.image_ = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (result.image_.size() < 4) throw std::runtime_error("script archive image is truncated");

    const auto sentinel = static_cast<std::size_t>(u16(result.image_, 0));
    const auto directory_bytes = static_cast<std::size_t>(u16(result.image_, 2));
    if ((directory_bytes & 1U) != 0 || directory_bytes < 4 || directory_bytes > sentinel ||
        sentinel >= result.image_.size() || result.image_[sentinel] != 0xff) {
        throw std::runtime_error("invalid legacy script archive directory");
    }

    result.offsets_.reserve(directory_bytes / 2);
    for (std::size_t offset = 0; offset < directory_bytes; offset += 2) {
        const auto value = u16(result.image_, offset);
        if (value < directory_bytes || value > sentinel) {
            throw std::runtime_error("legacy script archive entry offset is out of bounds");
        }
        result.offsets_.push_back(value);
    }
    if (result.offsets_.front() != sentinel || result.offsets_.at(1) != directory_bytes) {
        throw std::runtime_error("legacy script archive sentinel/header mismatch");
    }
    return result;
}

bool ScriptArchive::probe(const std::filesystem::path& path) noexcept {
    try {
        static_cast<void>(load(path));
        return true;
    } catch (...) {
        return false;
    }
}

ScriptArchive ScriptArchive::from_records(
    std::span<const std::vector<std::uint8_t>> records) {
    if (records.empty()) {
        throw std::runtime_error("a generated script archive needs at least one record");
    }

    const auto directory_bytes = (records.size() + 1U) * 2U;
    std::size_t image_size = directory_bytes + 1U;  // trailing sentinel byte
    for (const auto& record : records) {
        if (record.size() < 2 || record[record.size() - 2] != 0xff ||
            record.back() != 0xff) {
            throw std::runtime_error("generated script record has no 0xffff terminator");
        }
        image_size += record.size();
    }
    if (image_size > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("generated script archive exceeds the DOS 16-bit format");
    }

    ScriptArchive result;
    result.image_.resize(directory_bytes);
    result.offsets_.resize(records.size() + 1U);
    auto cursor = directory_bytes;
    for (std::size_t i = 0; i < records.size(); ++i) {
        result.offsets_[i + 1U] = static_cast<std::uint16_t>(cursor);
        result.image_.insert(result.image_.end(), records[i].begin(), records[i].end());
        cursor += records[i].size();
    }
    result.offsets_[0] = static_cast<std::uint16_t>(cursor);
    result.image_.push_back(0xff);
    for (std::size_t i = 0; i < result.offsets_.size(); ++i) {
        result.image_[i * 2] = static_cast<std::uint8_t>(result.offsets_[i]);
        result.image_[i * 2 + 1] = static_cast<std::uint8_t>(result.offsets_[i] >> 8U);
    }
    return result;
}

std::size_t ScriptArchive::unique_record_count() const noexcept {
    std::set<std::uint16_t> unique(offsets_.begin() + 1, offsets_.end());
    return unique.size();
}

std::span<const std::uint8_t> ScriptArchive::entry(std::size_t index) const {
    const auto start = static_cast<std::size_t>(offsets_.at(index));
    auto finish = static_cast<std::size_t>(offsets_.front());
    for (const auto offset : offsets_) {
        if (offset > start && offset < finish) finish = offset;
    }
    if (index == 0) finish = image_.size();
    return std::span<const std::uint8_t>(image_).subspan(start, finish - start);
}

}  // namespace swd2
