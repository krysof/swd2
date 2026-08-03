#include "swd2/shared_state.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace swd2 {

namespace {

void require_range(std::size_t offset, std::size_t length) {
    if (offset > SharedState::byte_size || length > SharedState::byte_size - offset) {
        throw std::out_of_range("shared state field is out of bounds");
    }
}

}  // namespace

SharedState SharedState::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open shared state: " + path.string());
    }
    std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
    return from_bytes(data);
}

SharedState SharedState::from_bytes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != byte_size) {
        throw std::runtime_error("shared state must contain exactly 1350 bytes");
    }
    SharedState state;
    std::copy(bytes.begin(), bytes.end(), state.bytes_.begin());
    return state;
}

void SharedState::save(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create shared state: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes_.data()),
                 static_cast<std::streamsize>(bytes_.size()));
    if (!output) {
        throw std::runtime_error("failed to write shared state: " + path.string());
    }
}

std::uint8_t SharedState::u8(std::size_t offset) const {
    require_range(offset, 1);
    return bytes_[offset];
}

std::uint16_t SharedState::u16(std::size_t offset) const {
    require_range(offset, 2);
    return static_cast<std::uint16_t>(bytes_[offset]) |
           (static_cast<std::uint16_t>(bytes_[offset + 1]) << 8U);
}

std::int16_t SharedState::i16(std::size_t offset) const {
    return static_cast<std::int16_t>(u16(offset));
}

void SharedState::set_u8(std::size_t offset, std::uint8_t value) {
    require_range(offset, 1);
    bytes_[offset] = value;
}

void SharedState::set_u16(std::size_t offset, std::uint16_t value) {
    require_range(offset, 2);
    bytes_[offset] = static_cast<std::uint8_t>(value);
    bytes_[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

std::string SharedState::dos_string(std::size_t offset, std::size_t capacity) const {
    require_range(offset, capacity);
    const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, begin + static_cast<std::ptrdiff_t>(capacity), 0);
    return {begin, end};
}

void SharedState::set_dos_string(std::size_t offset, std::size_t capacity,
                                 const std::string& value) {
    require_range(offset, capacity);
    if (value.size() >= capacity) {
        throw std::length_error("DOS path does not fit shared state field");
    }
    auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
    std::fill(begin, begin + static_cast<std::ptrdiff_t>(capacity), 0);
    std::copy(value.begin(), value.end(), begin);
}

SharedTransfer SharedTransfer::from_bytes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != byte_size) {
        throw std::runtime_error("shared transfer must contain exactly 1352 bytes");
    }
    const auto marker_word = static_cast<std::uint16_t>(bytes[0]) |
                             (static_cast<std::uint16_t>(bytes[1]) << 8U);
    return {static_cast<Marker>(marker_word), SharedState::from_bytes(bytes.subspan(2))};
}

std::array<std::uint8_t, SharedTransfer::byte_size> SharedTransfer::bytes() const {
    std::array<std::uint8_t, byte_size> result{};
    const auto marker_word = static_cast<std::uint16_t>(marker);
    result[0] = static_cast<std::uint8_t>(marker_word);
    result[1] = static_cast<std::uint8_t>(marker_word >> 8U);
    std::copy(state.bytes().begin(), state.bytes().end(), result.begin() + 2);
    return result;
}

}  // namespace swd2
