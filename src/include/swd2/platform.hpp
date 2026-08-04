#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <chrono>

namespace swd2 {

enum class InputAction {
    none,
    up,
    down,
    left,
    right,
    page_up,
    page_down,
    home,
    end,
    confirm,
    cancel,
    quit,
};

struct ClockTime {
    unsigned minute{};
    unsigned second{};
    unsigned hundredth{};
};

struct IndexedSurfaceView {
    std::size_t width{};
    std::size_t height{};
    std::span<const std::uint8_t> pixels;
    std::span<const std::uint8_t, 768> palette;
};

// The portable game core only depends on this interface. SDL, Win32, Cocoa,
// consoles and mobile ports can each supply a backend without changing game logic.
class PlatformBackend {
public:
    virtual ~PlatformBackend() = default;
    virtual void present(const IndexedSurfaceView& surface) = 0;
    virtual InputAction wait_for_input() = 0;
    // Non-blocking input and pacing are used by former DEMO/FIG animation
    // loops. Defaults keep headless/test backends source-compatible.
    virtual InputAction poll_input() { return InputAction::none; }
    virtual void delay_for(std::chrono::milliseconds) {}
    virtual ClockTime clock_time() const = 0;
    virtual void play_music(std::span<const std::uint8_t> rix_data, bool loop) = 0;
    virtual void play_voice(std::span<const std::uint8_t> voc_data) = 0;
    virtual void stop_audio() = 0;
};

}  // namespace swd2
