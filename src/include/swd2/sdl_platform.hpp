#pragma once

#include "swd2/platform.hpp"

#include <memory>

namespace swd2 {

// Portable desktop frontend.  SDL remains outside swd2_core so console,
// mobile and WebAssembly ports can supply a different PlatformBackend.
class SdlPlatform final : public PlatformBackend {
public:
    SdlPlatform();
    ~SdlPlatform() override;
    SdlPlatform(const SdlPlatform&) = delete;
    SdlPlatform& operator=(const SdlPlatform&) = delete;

    void present(const IndexedSurfaceView& surface) override;
    InputAction wait_for_input() override;
    InputAction poll_input() override;
    void delay_for(std::chrono::milliseconds duration) override;
    ClockTime clock_time() const override;
    void play_music(std::span<const std::uint8_t> rix_data, bool loop) override;
    void play_voice(std::span<const std::uint8_t> voc_data) override;
    void stop_music() override;
    void stop_audio() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace swd2
