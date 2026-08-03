#include "swd2/mz_executable.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace swd2::dos {

namespace {

std::uint16_t read_u16(const std::array<std::uint8_t, 28>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes.at(offset)) |
           (static_cast<std::uint16_t>(bytes.at(offset + 1)) << 8U);
}

std::uint16_t read_u16(std::istream& stream, const std::filesystem::path& path) {
    std::array<unsigned char, 2> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("truncated MZ relocation table: " + path.string());
    }
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

}  // namespace

MzExecutable MzExecutable::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open executable: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine executable size: " + path.string());
    }
    input.seekg(0);

    std::array<std::uint8_t, 28> raw{};
    input.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!input) {
        throw std::runtime_error("truncated DOS executable header: " + path.string());
    }

    MzExecutable result;
    result.actual_size_ = static_cast<std::uint64_t>(end);
    auto& h = result.header_;
    h.signature = read_u16(raw, 0x00);
    h.bytes_on_last_page = read_u16(raw, 0x02);
    h.pages_in_file = read_u16(raw, 0x04);
    h.relocation_count = read_u16(raw, 0x06);
    h.header_paragraphs = read_u16(raw, 0x08);
    h.min_extra_paragraphs = read_u16(raw, 0x0a);
    h.max_extra_paragraphs = read_u16(raw, 0x0c);
    h.initial_ss = read_u16(raw, 0x0e);
    h.initial_sp = read_u16(raw, 0x10);
    h.checksum = read_u16(raw, 0x12);
    h.initial_ip = read_u16(raw, 0x14);
    h.initial_cs = read_u16(raw, 0x16);
    h.relocation_table_offset = read_u16(raw, 0x18);
    h.overlay_number = read_u16(raw, 0x1a);

    if (h.signature != 0x5a4d) {
        throw std::runtime_error("not an MZ executable: " + path.string());
    }
    if (result.header_size() > result.actual_size_ || result.declared_size() > result.actual_size_) {
        throw std::runtime_error("invalid MZ size fields: " + path.string());
    }

    const auto relocation_bytes = static_cast<std::uint64_t>(h.relocation_count) * 4U;
    if (static_cast<std::uint64_t>(h.relocation_table_offset) + relocation_bytes > result.header_size()) {
        throw std::runtime_error("invalid MZ relocation table: " + path.string());
    }

    input.clear();
    input.seekg(h.relocation_table_offset);
    result.relocations_.reserve(h.relocation_count);
    for (std::uint16_t i = 0; i < h.relocation_count; ++i) {
        const auto offset = read_u16(input, path);
        const auto segment = read_u16(input, path);
        result.relocations_.push_back({offset, segment});
    }
    return result;
}

std::uint64_t MzExecutable::declared_size() const noexcept {
    if (header_.pages_in_file == 0) {
        return 0;
    }
    const auto full_pages = static_cast<std::uint64_t>(header_.pages_in_file - 1U) * 512U;
    return full_pages + (header_.bytes_on_last_page == 0 ? 512U : header_.bytes_on_last_page);
}

std::uint64_t MzExecutable::header_size() const noexcept {
    return static_cast<std::uint64_t>(header_.header_paragraphs) * 16U;
}

std::uint64_t MzExecutable::load_image_size() const noexcept {
    return declared_size() >= header_size() ? declared_size() - header_size() : 0;
}

std::uint64_t MzExecutable::overlay_size() const noexcept {
    return actual_size_ >= declared_size() ? actual_size_ - declared_size() : 0;
}

std::uint64_t MzExecutable::entry_file_offset() const noexcept {
    return header_size() + static_cast<std::uint64_t>(header_.initial_cs) * 16U +
           header_.initial_ip;
}

}  // namespace swd2::dos
