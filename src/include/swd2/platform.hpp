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
    // RPG:1880 treats the DOS Ctrl and Insert scan codes as a distinct
    // "clear this bitmap pixel" command. It cannot be collapsed into Cancel
    // because Escape leaves the editor without changing the selected pixel.
    erase,
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
    // RPG/FIG write each dialogue glyph directly into the already displayed
    // VGA page instead of flipping a new page. Backends normally repaint the
    // submitted indexed surface; replay/test frontends may account for these
    // incremental writes separately from page presentations.
    virtual void present_direct_update(const IndexedSurfaceView& surface) {
        present(surface);
    }
    virtual InputAction wait_for_input() = 0;
    // Non-blocking input and pacing are used by former DEMO/FIG animation
    // loops. Defaults keep headless/test backends source-compatible.
    virtual InputAction poll_input() { return InputAction::none; }
    // Kept distinct so deterministic replay streams can model the key which
    // skips typewriter delay separately from the later acknowledgement key.
    virtual InputAction poll_text_input() { return poll_input(); }
    // Window/tab shutdown is a host lifecycle event, not a DOS keyboard key.
    // Timed original sequences can probe it without consuming a direction or
    // confirmation which must remain queued for the next interactive screen.
    virtual bool poll_frontend_quit() { return false; }
    virtual void delay_for(std::chrono::milliseconds) {}
    virtual ClockTime clock_time() const = 0;
    virtual void play_music(std::span<const std::uint8_t> rix_data, bool loop) = 0;
    virtual void play_voice(std::span<const std::uint8_t> voc_data) = 0;
    // RIX/MIDI stop commands do not silence an independently playing VOC.
    // Backends predating the split retain safe stop-all behavior by default.
    virtual void stop_music() { stop_audio(); }
    virtual void stop_audio() = 0;
};

}  // namespace swd2
