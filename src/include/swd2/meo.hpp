#pragma once

#include "swd2/sprite_archive.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace swd2 {

enum class MeoInput {
    up,
    down,
    confirm,
};

enum class MeoStatus {
    waiting,
    accepted,
    rejected,
};

class MeoCopyProtection {
public:
    // The supplied SWD2 release has two NOPs at MEO image offsets 0160-0161,
    // so every confirmation counts as correct. patched=true reproduces that binary.
    explicit MeoCopyProtection(bool patched = true) : patched_(patched) {}

    MeoStatus input(MeoInput input, std::uint8_t expected_color = 1);
    [[nodiscard]] std::size_t choice() const noexcept { return choice_; }
    [[nodiscard]] std::size_t confirmations() const noexcept { return confirmations_; }
    [[nodiscard]] std::size_t correct_confirmations() const noexcept { return correct_; }

private:
    bool patched_{};
    std::size_t choice_{};
    std::size_t confirmations_{};
    std::size_t correct_{};
};

// MEO load-image offsets 0160h..0161h are either the original `JNE +4`
// (75 04) which gates the correct-answer counter, or two NOPs in the shipped
// patched release. Reject unknown bytes instead of silently choosing the
// wrong copy-protection behavior for a different executable.
[[nodiscard]] bool meo_copy_protection_is_patched(
    std::span<const std::uint8_t> load_image);

struct IndexedFrame {
    static constexpr std::size_t width = 320;
    static constexpr std::size_t height = 200;
    std::array<std::uint8_t, width * height> pixels{};
    std::array<std::uint8_t, 768> palette{};
};

struct MeoChallengePosition {
    unsigned x{};
    unsigned y{};
    friend bool operator==(const MeoChallengePosition&,
                           const MeoChallengePosition&) = default;
};

// MEO:008c reads DOS int 21h/AH=2Ch. DL (hundredth) selects the horizontal
// map coordinate and DH (second, clamped at 55) selects the vertical one.
// The names and order matter: using minute/second makes the arrow barely move.
[[nodiscard]] MeoChallengePosition meo_challenge_position(
    unsigned second, unsigned hundredth) noexcept;

// Reproduces MEO.EXE's 320x200 copy-protection frame.
IndexedFrame render_meo_frame(const SpriteArchive& archive, std::size_t choice,
                              unsigned second, unsigned hundredth,
                              bool rejected = false);

// Returns the 1..5 color sampled by the original time-based challenge, or zero
// if the current second/hundredth points outside a uniform 2x2 colored region.
std::uint8_t meo_expected_color(const IndexedFrame& frame, unsigned second,
                                unsigned hundredth);

}  // namespace swd2
