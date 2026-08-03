#include "swd2/rsk_decoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace swd2 {

namespace {

constexpr std::size_t kLiteralSymbols = 510;
constexpr std::size_t kPositionSymbols = 17;
constexpr std::size_t kTemporarySymbols = 19;

class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint32_t read(unsigned count) {
        if (count > 24) {
            throw std::runtime_error("resource bit read is too wide");
        }
        while (bits_ < count) {
            if (position_ == data_.size()) {
                throw std::runtime_error("truncated compressed resource bitstream");
            }
            buffer_ = (buffer_ << 8U) | data_[position_++];
            bits_ += 8;
        }
        const auto shift = bits_ - count;
        const std::uint32_t mask = count == 0 ? 0U : ((1U << count) - 1U);
        const auto result = (buffer_ >> shift) & mask;
        bits_ -= count;
        if (bits_ == 0) {
            buffer_ = 0;
        } else {
            buffer_ &= (1U << bits_) - 1U;
        }
        return result;
    }

    [[nodiscard]] std::size_t bytes_read() const noexcept { return position_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t position_{};
    std::uint32_t buffer_{};
    unsigned bits_{};
};

class HuffmanTable {
public:
    static HuffmanTable single(std::size_t symbol) {
        HuffmanTable table;
        table.single_symbol_ = symbol;
        return table;
    }

    static HuffmanTable canonical(const std::vector<std::uint8_t>& lengths) {
        HuffmanTable table;
        for (const auto length : lengths) {
            if (length > 16) {
                throw std::runtime_error("resource Huffman code exceeds 16 bits");
            }
            if (length != 0) {
                ++table.counts_[length];
                table.max_length_ = std::max<unsigned>(table.max_length_, length);
            }
        }
        if (table.max_length_ == 0) {
            throw std::runtime_error("empty resource Huffman table");
        }

        std::uint32_t code = 0;
        for (unsigned length = 1; length <= 16; ++length) {
            code = (code + table.counts_[length - 1]) << 1U;
            table.first_code_[length] = code;
            table.first_symbol_[length] = table.symbols_.size();
            for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
                if (lengths[symbol] == length) {
                    table.symbols_.push_back(symbol);
                }
            }
        }

        const auto used = code + table.counts_[16];
        if (used > (1U << 16U)) {
            throw std::runtime_error("oversubscribed resource Huffman table");
        }
        return table;
    }

    std::size_t decode(BitReader& bits) const {
        if (single_symbol_ != no_symbol) {
            return single_symbol_;
        }

        std::uint32_t code = 0;
        for (unsigned length = 1; length <= max_length_; ++length) {
            code = (code << 1U) | bits.read(1);
            if (code < first_code_[length]) {
                continue;
            }
            const auto offset = code - first_code_[length];
            if (offset < counts_[length]) {
                return symbols_.at(first_symbol_[length] + offset);
            }
        }
        throw std::runtime_error("invalid resource Huffman code");
    }

private:
    static constexpr std::size_t no_symbol = std::numeric_limits<std::size_t>::max();
    std::array<std::uint32_t, 17> counts_{};
    std::array<std::uint32_t, 17> first_code_{};
    std::array<std::size_t, 17> first_symbol_{};
    std::vector<std::size_t> symbols_;
    std::size_t single_symbol_{no_symbol};
    unsigned max_length_{};
};

HuffmanTable read_position_lengths(BitReader& bits, std::size_t symbols, unsigned count_bits,
                                   int zero_run_index) {
    const auto encoded_count = bits.read(count_bits);
    if (encoded_count == 0) {
        return HuffmanTable::single(bits.read(count_bits));
    }
    if (encoded_count > symbols) {
        throw std::runtime_error("resource Huffman length count is out of range");
    }

    std::vector<std::uint8_t> lengths(symbols);
    std::size_t index = 0;
    while (index < encoded_count) {
        unsigned length = bits.read(3);
        if (length == 7) {
            while (bits.read(1) != 0) {
                if (++length > 16) {
                    throw std::runtime_error("resource Huffman length is out of range");
                }
            }
        }
        lengths[index++] = static_cast<std::uint8_t>(length);
        if (static_cast<int>(index) == zero_run_index) {
            const auto zeros = bits.read(2);
            if (index + zeros > symbols) {
                throw std::runtime_error("resource Huffman zero run is out of range");
            }
            index += zeros;
        }
    }
    return HuffmanTable::canonical(lengths);
}

HuffmanTable read_literal_lengths(BitReader& bits, const HuffmanTable& temporary) {
    const auto encoded_count = bits.read(9);
    if (encoded_count == 0) {
        return HuffmanTable::single(bits.read(9));
    }
    if (encoded_count > kLiteralSymbols) {
        throw std::runtime_error("resource literal length count is out of range");
    }

    std::vector<std::uint8_t> lengths(kLiteralSymbols);
    std::size_t index = 0;
    while (index < encoded_count) {
        const auto symbol = temporary.decode(bits);
        if (symbol <= 2) {
            std::size_t zeros = 1;
            if (symbol == 1) {
                zeros = bits.read(4) + 3;
            } else if (symbol == 2) {
                zeros = bits.read(9) + 20;
            }
            if (index + zeros > kLiteralSymbols) {
                throw std::runtime_error("resource literal zero run is out of range");
            }
            index += zeros;
        } else {
            const auto length = symbol - 2;
            if (length > 16) {
                throw std::runtime_error("resource literal code exceeds 16 bits");
            }
            lengths[index++] = static_cast<std::uint8_t>(length);
        }
    }
    return HuffmanTable::canonical(lengths);
}

std::vector<std::uint8_t> decode_compressed(std::span<const std::uint8_t> payload,
                                            std::size_t output_size, std::size_t& bytes_read) {
    BitReader bits(payload);
    std::vector<std::uint8_t> output;
    output.reserve(output_size);

    std::uint32_t block_symbols = 0;
    auto literals = HuffmanTable::single(0);
    auto positions = HuffmanTable::single(0);

    while (output.size() < output_size) {
        if (block_symbols == 0) {
            block_symbols = bits.read(16);
            if (block_symbols == 0) {
                throw std::runtime_error("compressed resource contains an empty symbol block");
            }
            const auto temporary = read_position_lengths(bits, kTemporarySymbols, 5, 3);
            literals = read_literal_lengths(bits, temporary);
            positions = read_position_lengths(bits, kPositionSymbols, 5, -1);
        }
        --block_symbols;

        const auto symbol = literals.decode(bits);
        if (symbol < 256) {
            output.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        if (symbol >= kLiteralSymbols) {
            throw std::runtime_error("resource literal symbol is out of range");
        }

        const auto length = symbol - 253;  // 256 - the original threshold of 3.
        auto distance = positions.decode(bits);
        if (distance != 0) {
            const auto extra_bits = static_cast<unsigned>(distance - 1);
            distance = (std::size_t{1} << extra_bits) + bits.read(extra_bits);
        }
        ++distance;
        if (distance > output.size()) {
            throw std::runtime_error("resource back-reference precedes output buffer");
        }

        auto source = output.size() - distance;
        for (std::size_t i = 0; i < length && output.size() < output_size; ++i) {
            output.push_back(output[source++]);
        }
    }

    bytes_read = bits.bytes_read();
    return output;
}

}  // namespace

RskDecodeResult decode_rsk_block(std::span<const std::uint8_t> source) {
    if (source.size() < 3) {
        throw std::runtime_error("resource is shorter than its three-byte wrapper");
    }

    const auto output_size = static_cast<std::size_t>(source[0]) |
                             (static_cast<std::size_t>(source[1]) << 8U);
    const auto mode = source[2];
    RskDecodeResult result;
    result.compressed = mode != 0;
    result.has_standard_footer =
        source.size() >= 4 && source[source.size() - 4] == 0x60 && source[source.size() - 3] == 0xea &&
        source[source.size() - 2] == 0x00 && source[source.size() - 1] == 0x00;

    if (mode == 0) {
        if (source.size() < output_size + 3) {
            throw std::runtime_error("uncompressed resource payload is truncated");
        }
        result.data.assign(source.begin() + 3,
                           source.begin() + static_cast<std::ptrdiff_t>(3 + output_size));
        result.payload_bytes_read = output_size;
        return result;
    }
    if (mode != 1) {
        throw std::runtime_error("unknown resource compression mode: " + std::to_string(mode));
    }

    result.data = decode_compressed(source.subspan(3), output_size, result.payload_bytes_read);
    return result;
}

}  // namespace swd2
