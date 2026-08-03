#include "swd2/sdl_platform.hpp"
#include "swd2/rix_decoder.hpp"
#include "swd2/voc_decoder.hpp"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

namespace swd2 {

namespace {

[[noreturn]] void fail_sdl(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

std::uint8_t expand_vga_component(std::uint8_t component) {
    return static_cast<std::uint8_t>(
        std::min<unsigned>(255, (static_cast<unsigned>(component) * 255U + 31U) / 63U));
}

InputAction translate_event(const SDL_Event& event) {
    if (event.type == SDL_QUIT) return InputAction::quit;
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
        case SDLK_UP: return InputAction::up;
        case SDLK_DOWN: return InputAction::down;
        case SDLK_LEFT: return InputAction::left;
        case SDLK_RIGHT: return InputAction::right;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
        case SDLK_z: return InputAction::confirm;
        case SDLK_ESCAPE:
        case SDLK_x: return InputAction::cancel;
        default: break;
        }
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return InputAction::up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return InputAction::down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return InputAction::left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return InputAction::right;
        case SDL_CONTROLLER_BUTTON_A: return InputAction::confirm;
        case SDL_CONTROLLER_BUTTON_B: return InputAction::cancel;
        default: break;
        }
    }
    return InputAction::none;
}

}  // namespace

struct SdlPlatform::Impl {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    std::size_t texture_width{};
    std::size_t texture_height{};
    std::vector<std::uint32_t> rgba;
    SDL_AudioDeviceID audio_device{};
    int audio_rate{44'100};
    std::vector<std::int16_t> music_samples;
    std::vector<std::int16_t> voice_samples;
    std::size_t music_cursor{};
    std::size_t voice_cursor{};
    bool loop_music{};

    ~Impl() {
        if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void ensure_texture(std::size_t width, std::size_t height) {
        if (texture && texture_width == width && texture_height == height) return;
        SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    static_cast<int>(width), static_cast<int>(height));
        if (!texture) fail_sdl("SDL_CreateTexture");
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        texture_width = width;
        texture_height = height;
        rgba.resize(width * height);
        SDL_RenderSetLogicalSize(renderer, static_cast<int>(width), static_cast<int>(height));
    }

    static void audio_callback(void* userdata, Uint8* stream, int byte_count) {
        auto& self = *static_cast<Impl*>(userdata);
        auto* output = reinterpret_cast<std::int16_t*>(stream);
        const auto count = static_cast<std::size_t>(byte_count) / sizeof(std::int16_t);
        for (std::size_t index = 0; index < count; ++index) {
            int mixed = 0;
            if (!self.music_samples.empty()) {
                if (self.music_cursor >= self.music_samples.size()) {
                    if (self.loop_music) self.music_cursor = 0;
                }
                if (self.music_cursor < self.music_samples.size()) {
                    mixed += self.music_samples[self.music_cursor++];
                }
            }
            if (self.voice_cursor < self.voice_samples.size()) {
                mixed += self.voice_samples[self.voice_cursor++];
            }
            output[index] = static_cast<std::int16_t>(
                std::clamp(mixed, -32768, 32767));
        }
    }

    void ensure_audio() {
        if (audio_device != 0) return;
        SDL_AudioSpec desired{};
        desired.freq = audio_rate;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 1024;
        desired.callback = &Impl::audio_callback;
        desired.userdata = this;
        SDL_AudioSpec obtained{};
        audio_device = SDL_OpenAudioDevice(
            nullptr, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_device == 0) fail_sdl("SDL_OpenAudioDevice");
        if (obtained.format != AUDIO_S16SYS || obtained.channels != 1) {
            SDL_CloseAudioDevice(audio_device);
            audio_device = 0;
            throw std::runtime_error("SDL audio backend cannot accept mono S16 audio");
        }
        audio_rate = obtained.freq;
        SDL_PauseAudioDevice(audio_device, 0);
    }
};

SdlPlatform::SdlPlatform() : impl_(std::make_unique<Impl>()) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        fail_sdl("SDL_Init");
    }
    impl_->window = SDL_CreateWindow("SWD2 portable rewrite", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 960, 600,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!impl_->window) fail_sdl("SDL_CreateWindow");
    impl_->renderer = SDL_CreateRenderer(
        impl_->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!impl_->renderer) {
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!impl_->renderer) fail_sdl("SDL_CreateRenderer");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetIntegerScale(impl_->renderer, SDL_TRUE);
}

SdlPlatform::~SdlPlatform() = default;

void SdlPlatform::present(const IndexedSurfaceView& surface) {
    if (surface.width == 0 || surface.height == 0 ||
        surface.pixels.size() != surface.width * surface.height) {
        throw std::runtime_error("invalid indexed surface submitted to SDL");
    }
    impl_->ensure_texture(surface.width, surface.height);
    std::array<std::uint32_t, 256> colors{};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const auto red = expand_vga_component(surface.palette[index * 3]);
        const auto green = expand_vga_component(surface.palette[index * 3 + 1]);
        const auto blue = expand_vga_component(surface.palette[index * 3 + 2]);
        colors[index] = 0xff000000U | (static_cast<std::uint32_t>(red) << 16U) |
                        (static_cast<std::uint32_t>(green) << 8U) | blue;
    }
    for (std::size_t index = 0; index < surface.pixels.size(); ++index) {
        impl_->rgba[index] = colors[surface.pixels[index]];
    }
    if (SDL_UpdateTexture(impl_->texture, nullptr, impl_->rgba.data(),
                          static_cast<int>(surface.width * sizeof(std::uint32_t))) != 0) {
        fail_sdl("SDL_UpdateTexture");
    }
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);
    SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr);
    SDL_RenderPresent(impl_->renderer);
}

InputAction SdlPlatform::wait_for_input() {
    SDL_Event event{};
#ifdef __EMSCRIPTEN__
    // A browser cannot block its main thread in SDL_WaitEvent. ASYNCIFY turns
    // emscripten_sleep into a cooperative suspension, allowing DOM events,
    // rendering and WebAudio to continue while the portable core remains
    // synchronous.
    for (;;) {
        while (SDL_PollEvent(&event) != 0) {
            if (const auto action = translate_event(event); action != InputAction::none) {
                return action;
            }
        }
        emscripten_sleep(10);
    }
#else
    while (SDL_WaitEvent(&event) != 0) {
        if (const auto action = translate_event(event); action != InputAction::none) return action;
    }
    fail_sdl("SDL_WaitEvent");
#endif
}

InputAction SdlPlatform::poll_input() {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (const auto action = translate_event(event); action != InputAction::none) return action;
    }
    return InputAction::none;
}

void SdlPlatform::delay_for(std::chrono::milliseconds duration) {
    const auto milliseconds = static_cast<unsigned>(
        std::max<std::int64_t>(0, duration.count()));
#ifdef __EMSCRIPTEN__
    if (milliseconds != 0) emscripten_sleep(milliseconds);
#else
    SDL_Delay(static_cast<Uint32>(milliseconds));
#endif
}

ClockTime SdlPlatform::clock_time() const {
    const auto point = std::chrono::system_clock::now();
    const auto now = std::chrono::system_clock::to_time_t(point);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  point.time_since_epoch()) %
                              std::chrono::seconds(1);
    return {static_cast<unsigned>(local.tm_min), static_cast<unsigned>(local.tm_sec),
            static_cast<unsigned>(milliseconds.count() / 10)};
}

void SdlPlatform::play_music(std::span<const std::uint8_t> rix_data, bool loop) {
    impl_->ensure_audio();
    auto music = synthesize_rix(decode_rix(rix_data),
                                static_cast<std::uint32_t>(impl_->audio_rate));
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples = std::move(music.mono_samples);
    impl_->music_cursor = 0;
    impl_->loop_music = loop;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

void SdlPlatform::play_voice(std::span<const std::uint8_t> voc_data) {
    const auto voice = decode_voc(voc_data);
    impl_->ensure_audio();
    const auto output_count = static_cast<std::size_t>(
        (static_cast<unsigned long long>(voice.mono_samples.size()) *
         static_cast<unsigned>(impl_->audio_rate)) /
        voice.sample_rate);
    std::vector<std::int16_t> resampled(output_count);
    for (std::size_t index = 0; index < output_count; ++index) {
        const auto position =
            (static_cast<double>(index) * voice.sample_rate) / impl_->audio_rate;
        const auto first = std::min<std::size_t>(
            static_cast<std::size_t>(position), voice.mono_samples.size() - 1U);
        const auto second = std::min(first + 1U, voice.mono_samples.size() - 1U);
        const auto fraction = position - static_cast<double>(first);
        const auto value = static_cast<double>(voice.mono_samples[first]) * (1.0 - fraction) +
                           static_cast<double>(voice.mono_samples[second]) * fraction;
        resampled[index] = static_cast<std::int16_t>(std::lround(value));
    }
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->voice_samples = std::move(resampled);
    impl_->voice_cursor = 0;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

void SdlPlatform::stop_audio() {
    if (impl_->audio_device == 0) return;
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples.clear();
    impl_->voice_samples.clear();
    impl_->music_cursor = 0;
    impl_->voice_cursor = 0;
    impl_->loop_music = false;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

}  // namespace swd2
