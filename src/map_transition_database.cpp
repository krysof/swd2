#include "swd2/map_transition_database.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace swd2 {

namespace {

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2U > bytes.size()) {
        throw std::runtime_error("MAP0 word is out of bounds");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

}  // namespace

MapTransitionDatabase MapTransitionDatabase::load(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open MAP0 transition database: " +
                                 path.string());
    }
    const std::vector<std::uint8_t> file{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (file.size() < 28U || u16(file, 0) != 0x5a4dU) {
        throw std::runtime_error("MAP0 transition database has no MZ wrapper");
    }
    const auto header_size = static_cast<std::size_t>(u16(file, 8)) * 16U;
    if (header_size > file.size() || u16(file, 6) != 0U ||
        u16(file, 0x14) != 0U || u16(file, 0x16) != 0U) {
        throw std::runtime_error(
            "MAP0 wrapper describes native or truncated code");
    }
    const auto image = std::span<const std::uint8_t>(file).subspan(header_size);
    if (image.size() < 12U) {
        throw std::runtime_error("MAP0 load image is truncated");
    }
    const auto image_end = static_cast<std::size_t>(u16(image, 0));
    const auto directory_bytes = static_cast<std::size_t>(u16(image, 2));
    if ((directory_bytes & 1U) != 0U || directory_bytes < 10U ||
        directory_bytes > image_end || image_end + 2U != image.size() ||
        u16(image, image_end) != 0xffffU) {
        throw std::runtime_error("MAP0 pointer directory is invalid");
    }

    MapTransitionDatabase result;
    result.directory_.resize(directory_bytes / 2U);
    // Slots 0..4 are the image end and three loader metadata lists.  MAPZ
    // area flags use the remaining 152 even byte offsets directly.
    for (std::size_t directory_offset = 10U;
         directory_offset < directory_bytes; directory_offset += 2U) {
        auto cursor = static_cast<std::size_t>(
            u16(image, directory_offset));
        if (cursor < directory_bytes || cursor >= image_end) {
            throw std::runtime_error("MAP0 transition-list pointer is invalid");
        }
        auto& records = result.directory_[directory_offset / 2U];
        while (u16(image, cursor) != 0xf000U) {
            if (cursor + 8U > image_end) {
                throw std::runtime_error("MAP0 transition list is truncated");
            }
            const auto packed_rows = u16(image, cursor);
            MapTransitionRecord record;
            record.row_count = static_cast<std::uint8_t>(packed_rows);
            record.flag_index = static_cast<std::uint8_t>(packed_rows >> 8U);
            record.first_cell = u16(image, cursor + 2U);
            record.last_cell = u16(image, cursor + 4U);
            record.action = u16(image, cursor + 6U);
            if (record.row_count == 0U ||
                record.first_cell > record.last_cell) {
                throw std::runtime_error("MAP0 transition record is invalid");
            }
            records.push_back(record);
            ++result.record_count_;
            cursor += 8U;
        }
        ++result.area_count_;
    }
    return result;
}

std::span<const MapTransitionRecord> MapTransitionDatabase::records(
    std::uint16_t area_flags) const {
    const auto directory_offset =
        static_cast<std::size_t>(area_flags & 0x0fffU);
    if ((directory_offset & 1U) != 0U ||
        directory_offset / 2U >= directory_.size()) {
        return {};
    }
    return directory_[directory_offset / 2U];
}

std::optional<MapTransitionRecord> MapTransitionDatabase::match(
    std::uint16_t area_flags, std::uint16_t actor_cell,
    std::uint16_t map_width) const {
    const auto row_stride = static_cast<std::uint32_t>(map_width) * 2U;
    for (const auto& record : records(area_flags)) {
        auto first = static_cast<std::uint32_t>(record.first_cell);
        auto last = static_cast<std::uint32_t>(record.last_cell);
        for (std::uint16_t row = 0; row < record.row_count; ++row) {
            if (actor_cell >= first && actor_cell <= last) return record;
            first += row_stride;
            last += row_stride;
        }
    }
    return std::nullopt;
}

}  // namespace swd2
