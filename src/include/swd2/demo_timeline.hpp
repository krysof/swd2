#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace swd2 {

// SWORD6.RSK and SWORD7.RSK contain a second, very small PackBits-like layer.
// DEMO.EXE expands them over the no-longer-needed SWORD3/SWORD4 buffers when
// the final group enters the frame.
std::vector<std::uint8_t> decode_demo_rle(std::span<const std::uint8_t> source);

// DEMO.EXE:0278 performs its title reveal by scanning 4000 bytes at physical
// DOS address 0000:0220. Each byte gates eight pixels in VGA offsets
// 3200h..aeffh, and only every third matching pixel is copied. Physical low
// memory was machine/DOS dependent, so callers provide its 4000-byte image;
// portable_demo_reveal_mask() supplies a stable cross-platform substitute.
class DemoByteMaskReveal {
public:
    static constexpr std::size_t mask_size = 4'000;
    static constexpr std::size_t region_offset = 0x3200;
    static constexpr std::size_t region_size = mask_size * 8;
    static constexpr std::size_t frame_count = 20;

    explicit DemoByteMaskReveal(std::span<const std::uint8_t, mask_size> mask) {
        for (std::size_t i = 0; i < mask_size; ++i) mask_[i] = mask[i];
    }

    // Returns false after the twentieth frame. Source and destination are
    // linear 320x200 chunky pages corresponding to ES:3200 and A000:3200.
    bool step(std::span<const std::uint8_t> source,
              std::span<std::uint8_t> destination);

    [[nodiscard]] std::size_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::uint8_t selector() const noexcept { return selector_; }

private:
    std::array<std::uint8_t, mask_size> mask_{};
    std::size_t frame_{};
    std::uint8_t selector_{1};
};

[[nodiscard]] std::array<std::uint8_t, DemoByteMaskReveal::mask_size>
portable_demo_reveal_mask() noexcept;

enum class DemoSpriteSource : std::uint8_t {
    sword1,
    sword2,
    sword3,
    sword4,
    sword5,
    sword6_expanded,
    sword7_expanded,
    sword8,
};

struct DemoDrawCommand {
    DemoSpriteSource source{};
    std::uint8_t sprite{};
    int x{};
    int y{};
    bool transparent{};  // true is DEMO.EXE's colour-0xfe masked blitter.

    bool operator==(const DemoDrawCommand&) const = default;
};

struct DemoFrame {
    std::size_t tick{};
    std::vector<DemoDrawCommand> draws;
    bool start_music{};
};

// Exact state machine for DEMO.EXE:0139.  Keeping the state transitions out
// of the renderer makes the 960-frame DOS timeline deterministic and testable.
class DemoTimeline {
public:
    static constexpr std::size_t frame_count = 0x3c0;

    std::optional<DemoFrame> next();

    [[nodiscard]] std::size_t tick() const noexcept { return tick_; }
    [[nodiscard]] int first_x() const noexcept { return road_x_[0]; }
    [[nodiscard]] int actor_x() const noexcept { return actor_x_; }
    [[nodiscard]] int procession_x() const noexcept { return procession_x_; }
    [[nodiscard]] std::uint8_t actor_state() const noexcept { return actor_state_; }
    [[nodiscard]] std::uint8_t procession_state() const noexcept {
        return procession_state_;
    }
    [[nodiscard]] bool final_group_started() const noexcept { return final_group_started_; }

private:
    void append_sword5(DemoFrame& frame);
    void append_actor(DemoFrame& frame);
    void append_final_group(DemoFrame& frame);

    std::size_t tick_{};
    int road_x_[5]{320, 620, 940, 1045, 1095};
    int actor_x_{1380};
    int procession_x_{1180};
    int final_group_x_{2219};
    std::uint8_t actor_state_{};
    std::uint8_t procession_state_{};
    std::uint8_t final_group_state_{};
    bool actor_started_{};
    bool procession_started_{};
    bool final_group_started_{};
    bool alternate_tick_{};
};

}  // namespace swd2
