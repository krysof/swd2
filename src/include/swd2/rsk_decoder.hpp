#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

struct RskDecodeResult {
    std::vector<std::uint8_t> data;
    bool compressed{};
    std::size_t payload_bytes_read{};
    bool has_standard_footer{};
};

// Decodes the common resource wrapper used by RSK, RS1..RS4, RAP and RRO.
// The wrapper begins with a 16-bit unpacked length and an 8-bit compression flag.
// Compressed blocks use the game's 64 KiB LZ/Huffman variant.
RskDecodeResult decode_rsk_block(std::span<const std::uint8_t> source);

}  // namespace swd2
