#include "swd2/sdl_platform.hpp"
#include "swd2/audio_loop_clock.hpp"
#include "swd2/rix_decoder.hpp"
#include "swd2/voc_decoder.hpp"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <deque>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
        case SDLK_PAGEUP: return InputAction::page_up;
        case SDLK_PAGEDOWN: return InputAction::page_down;
        case SDLK_HOME: return InputAction::home;
        case SDLK_END: return InputAction::end;
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
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return InputAction::page_up;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return InputAction::page_down;
        case SDL_CONTROLLER_BUTTON_X: return InputAction::home;
        case SDL_CONTROLLER_BUTTON_Y: return InputAction::end;
        case SDL_CONTROLLER_BUTTON_A: return InputAction::confirm;
        case SDL_CONTROLLER_BUTTON_B: return InputAction::cancel;
        default: break;
        }
    }
    return InputAction::none;
}

InputAction direction_code_action(int direction) {
    switch (direction) {
    case 1: return InputAction::up;
    case 2: return InputAction::left;
    case 3: return InputAction::down;
    case 4: return InputAction::right;
    default: return InputAction::none;
    }
}

#ifdef __EMSCRIPTEN__
HeldDirectionRepeatState browser_direction;

InputAction take_browser_direction_action(bool every_frame) {
    // DOM callbacks only update ordinary JavaScript state. Polling it while
    // the game is already executing avoids re-entering WebAssembly during an
    // ASYNCIFY sleep, which Safari can reject. A short JS queue preserves a
    // quick tap even when press and release both occur between world frames.
    const auto queued = EM_ASM_INT({
        const queue = Module.swd2DirectionQueue;
        return queue && queue.length ? queue.shift() | 0 : 0;
    });
    const auto held = EM_ASM_INT({
        return Module.swd2HeldDirection | 0;
    });
    if (every_frame) {
        // RPG's field loop samples the held keyboard level once per rendered
        // frame. A held touch must therefore yield one direction on every
        // poll, not desktop text-entry autorepeat at 180/85 ms. The queued
        // value only preserves a tap that ended between two frame polls.
        return direction_code_action(held != 0 ? held : queued);
    }
    return browser_direction.sample(queued, held, SDL_GetTicks());
}
#endif

}  // namespace

InputAction HeldDirectionRepeatState::sample(
    int queued_direction, int held_direction, std::uint32_t now) noexcept {
    if (queued_direction != 0) {
        held_direction_ = held_direction;
        repeat_at_ = now + 180U;
        return direction_code_action(queued_direction);
    }
    if (held_direction == 0) {
        held_direction_ = 0;
        return InputAction::none;
    }
    if (held_direction != held_direction_) {
        held_direction_ = held_direction;
        repeat_at_ = now + 180U;
        return direction_code_action(held_direction);
    }
    if (static_cast<std::int32_t>(now - repeat_at_) >= 0) {
        repeat_at_ = now + 85U;
        return direction_code_action(held_direction);
    }
    return InputAction::none;
}

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
    AudioLoopClock music_loop_clock;
    bool loop_music{};
    std::vector<SDL_GameController*> controllers;
    std::deque<InputAction> pending_actions;
    bool frontend_quit{};
    InputAction held_keyboard_direction{};
    struct ControllerAxisState {
        int horizontal{};
        int vertical{};
    };
    std::unordered_map<SDL_JoystickID, ControllerAxisState> controller_axes;

    ~Impl() {
        for (auto* controller : controllers) {
            SDL_GameControllerClose(controller);
        }
        if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void open_controller(int device_index) {
        if (device_index < 0 || SDL_IsGameController(device_index) != SDL_TRUE) return;
        auto* controller = SDL_GameControllerOpen(device_index);
        if (!controller) return;
        const auto instance = SDL_JoystickInstanceID(
            SDL_GameControllerGetJoystick(controller));
        const auto duplicate = std::any_of(
            controllers.begin(), controllers.end(), [&](auto* existing) {
                return SDL_JoystickInstanceID(
                           SDL_GameControllerGetJoystick(existing)) == instance;
            });
        if (duplicate) {
            SDL_GameControllerClose(controller);
        } else {
            controllers.push_back(controller);
        }
    }

    void close_controller(SDL_JoystickID instance) {
        const auto found = std::find_if(
            controllers.begin(), controllers.end(), [&](auto* controller) {
                return SDL_JoystickInstanceID(
                           SDL_GameControllerGetJoystick(controller)) == instance;
            });
        if (found == controllers.end()) return;
        SDL_GameControllerClose(*found);
        controllers.erase(found);
        controller_axes.erase(instance);
    }

    static InputAction update_axis_direction(
        Sint16 value, int& latched, InputAction negative,
        InputAction positive) {
        // Use separate engage/release thresholds so a noisy centred stick
        // cannot scroll a DOS selector repeatedly. Crossing through centre or
        // directly into the opposite side re-arms exactly one direction.
        constexpr auto engage = 16'000;
        constexpr auto release = 8'000;
        const auto next = value <= -engage ? -1 : value >= engage ? 1 :
                          (value >= -release && value <= release ? 0 : latched);
        if (next == latched) return InputAction::none;
        latched = next;
        return next < 0 ? negative : next > 0 ? positive : InputAction::none;
    }

    InputAction process_event(const SDL_Event& event) {
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            open_controller(event.cdevice.which);
            return InputAction::none;
        }
        if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            close_controller(event.cdevice.which);
            return InputAction::none;
        }
        if (event.type == SDL_CONTROLLERAXISMOTION) {
            auto& axes = controller_axes[event.caxis.which];
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                return update_axis_direction(
                    event.caxis.value, axes.horizontal,
                    InputAction::left, InputAction::right);
            }
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                return update_axis_direction(
                    event.caxis.value, axes.vertical,
                    InputAction::up, InputAction::down);
            }
            return InputAction::none;
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            InputAction direction = InputAction::none;
            switch (event.key.keysym.sym) {
            case SDLK_UP: direction = InputAction::up; break;
            case SDLK_DOWN: direction = InputAction::down; break;
            case SDLK_LEFT: direction = InputAction::left; break;
            case SDLK_RIGHT: direction = InputAction::right; break;
            default: break;
            }
            if (direction != InputAction::none) {
                if (event.type == SDL_KEYDOWN) {
                    held_keyboard_direction = direction;
                } else if (held_keyboard_direction == direction) {
                    held_keyboard_direction = InputAction::none;
                }
            }
        }
        return translate_event(event);
    }

    InputAction take_pending_action(bool every_frame_direction) {
        if (frontend_quit) return InputAction::quit;
#ifdef __EMSCRIPTEN__
        if (const auto action = take_browser_direction_action(every_frame_direction);
            action != InputAction::none) return action;
#endif
        if (pending_actions.empty()) return InputAction::none;
        const auto action = pending_actions.front();
        pending_actions.pop_front();
        return action;
    }

    void retain_event_action(const SDL_Event& event) {
        const auto action = process_event(event);
        if (action == InputAction::quit) {
            frontend_quit = true;
        } else if (action != InputAction::none) {
            pending_actions.push_back(action);
        }
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
                    if (self.loop_music) {
                        self.music_cursor = 0;
                        // A RIX loop has timer_ticks * rate / 70 samples,
                        // which is usually fractional. Repeating only the
                        // floored PCM body loses that fraction on every loop.
                        // Carry it across boundaries and hold the final sample
                        // for one device period whenever it reaches 1.0.
                        if (self.music_loop_clock.advance_loop_boundary()) {
                            mixed += self.music_samples.back();
                        } else {
                            mixed += self.music_samples[self.music_cursor++];
                        }
                    }
                }
                else {
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
    impl_->window = SDL_CreateWindow("轩辕剑2 · SWD2", SDL_WINDOWPOS_CENTERED,
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
    for (auto device = 0; device < SDL_NumJoysticks(); ++device) {
        impl_->open_controller(device);
    }
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
    if (const auto pending = impl_->take_pending_action(false);
        pending != InputAction::none) {
        return pending;
    }
#ifdef __EMSCRIPTEN__
    // A browser cannot block its main thread in SDL_WaitEvent. ASYNCIFY turns
    // emscripten_sleep into a cooperative suspension, allowing DOM events,
    // rendering and WebAudio to continue while the portable core remains
    // synchronous.
    for (;;) {
        while (SDL_PollEvent(&event) != 0) {
            if (const auto action = impl_->process_event(event);
                action != InputAction::none) {
                if (action == InputAction::quit) impl_->frontend_quit = true;
                return action;
            }
        }
        emscripten_sleep(10);
    }
#else
    while (SDL_WaitEvent(&event) != 0) {
        if (const auto action = impl_->process_event(event);
            action != InputAction::none) {
            if (action == InputAction::quit) impl_->frontend_quit = true;
            return action;
        }
    }
    fail_sdl("SDL_WaitEvent");
#endif
}

InputAction SdlPlatform::poll_input() {
    if (const auto pending = impl_->take_pending_action(true);
        pending != InputAction::none) {
        return pending;
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (const auto action = impl_->process_event(event);
            action != InputAction::none) {
            if (action == InputAction::quit) impl_->frontend_quit = true;
            return action;
        }
    }
    return impl_->held_keyboard_direction;
}

bool SdlPlatform::poll_frontend_quit() {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        impl_->retain_event_action(event);
    }
    return impl_->frontend_quit;
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
    const auto sequence = decode_rix(rix_data);
    const auto rate = static_cast<std::uint32_t>(impl_->audio_rate);
    auto music = synthesize_rix(sequence, rate);
    AudioLoopClock loop_clock(sequence.total_timer_ticks, rate);
    if (loop_clock.samples_per_loop() != music.mono_samples.size()) {
        throw std::runtime_error("RIX PCM and loop clock duration disagree");
    }
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples = std::move(music.mono_samples);
    impl_->music_cursor = 0;
    impl_->music_loop_clock = loop_clock;
    impl_->loop_music = loop;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

void SdlPlatform::play_voice(std::span<const std::uint8_t> voc_data) {
    impl_->ensure_audio();
    auto voice = resample_voice(
        decode_voc(voc_data), static_cast<std::uint32_t>(impl_->audio_rate));
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->voice_samples = std::move(voice.mono_samples);
    impl_->voice_cursor = 0;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

void SdlPlatform::stop_music() {
    if (impl_->audio_device == 0) return;
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples.clear();
    impl_->music_cursor = 0;
    impl_->music_loop_clock = {};
    impl_->loop_music = false;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

void SdlPlatform::stop_audio() {
    if (impl_->audio_device == 0) return;
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples.clear();
    impl_->voice_samples.clear();
    impl_->music_cursor = 0;
    impl_->voice_cursor = 0;
    impl_->music_loop_clock = {};
    impl_->loop_music = false;
    SDL_UnlockAudioDevice(impl_->audio_device);
}

}  // namespace swd2
