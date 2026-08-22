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
#include <optional>
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
        case SDLK_LCTRL:
        case SDLK_RCTRL:
        case SDLK_INSERT: return InputAction::erase;
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

int direction_action_code(InputAction action) {
    switch (action) {
    case InputAction::up: return 1;
    case InputAction::left: return 2;
    case InputAction::down: return 3;
    case InputAction::right: return 4;
    default: return 0;
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
    InputAction action = InputAction::none;
    if (every_frame) {
        // RPG's field loop samples the held keyboard level once per rendered
        // frame. A held touch must therefore yield one direction on every
        // poll, not desktop text-entry autorepeat at 180/85 ms. The queued
        // value only preserves a tap that ended between two frame polls.
        action = direction_code_action(held != 0 ? held : queued);
    } else {
        action = browser_direction.sample(queued, held, SDL_GetTicks());
    }
    if (action != InputAction::none) {
        EM_ASM({
            if (Module.swd2InputSelfTestEnabled) {
                const deliveries = Module.swd2InputDeliveries;
                if (deliveries.length < 512) {
                    deliveries.push({
                        direction: $0,
                        everyFrame: Boolean($1),
                        milliseconds: performance.now()
                    });
                }
            }
        }, direction_action_code(action), every_frame ? 1 : 0);
    }
    return action;
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
    bool audio_initialized{};
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
    struct ControllerDirectionState {
        int horizontal{};
        int vertical{};
        std::array<bool, 4> dpad{};
        InputAction last_direction{};
        std::uint64_t serial{};
    };
    std::unordered_map<SDL_JoystickID, ControllerDirectionState>
        controller_directions;
    std::uint64_t controller_direction_serial{};
    InputAction held_controller_direction{};

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
            controller_directions.try_emplace(instance);
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
        controller_directions.erase(instance);
        recompute_held_controller_direction();
    }

    static std::optional<std::size_t> direction_index(
        InputAction action) noexcept {
        switch (action) {
        case InputAction::up: return 0U;
        case InputAction::down: return 1U;
        case InputAction::left: return 2U;
        case InputAction::right: return 3U;
        default: return std::nullopt;
        }
    }

    static bool direction_is_active(
        const ControllerDirectionState& state,
        InputAction action) noexcept {
        const auto index = direction_index(action);
        if (!index) return false;
        if (state.dpad[*index]) return true;
        if (action == InputAction::left) return state.horizontal < 0;
        if (action == InputAction::right) return state.horizontal > 0;
        if (action == InputAction::up) return state.vertical < 0;
        return state.vertical > 0;
    }

    static InputAction active_controller_direction(
        const ControllerDirectionState& state) noexcept {
        if (direction_is_active(state, state.last_direction)) {
            return state.last_direction;
        }
        for (const auto action : {InputAction::up, InputAction::down,
                                  InputAction::left, InputAction::right}) {
            if (direction_is_active(state, action)) return action;
        }
        return InputAction::none;
    }

    void recompute_held_controller_direction() noexcept {
        held_controller_direction = InputAction::none;
        auto newest = std::uint64_t{};
        for (auto& [_, state] : controller_directions) {
            const auto active = active_controller_direction(state);
            if (active == InputAction::none) continue;
            if (held_controller_direction == InputAction::none ||
                state.serial >= newest) {
                newest = state.serial;
                held_controller_direction = active;
            }
        }
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
        if (event.type == SDL_CONTROLLERBUTTONDOWN ||
            event.type == SDL_CONTROLLERBUTTONUP) {
            InputAction direction = InputAction::none;
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                direction = InputAction::up;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                direction = InputAction::down;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                direction = InputAction::left;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                direction = InputAction::right;
                break;
            default:
                break;
            }
            if (const auto index = direction_index(direction)) {
                auto& state = controller_directions[event.cbutton.which];
                state.dpad[*index] = event.type == SDL_CONTROLLERBUTTONDOWN;
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    state.last_direction = direction;
                    state.serial = ++controller_direction_serial;
                }
                recompute_held_controller_direction();
            }
            return translate_event(event);
        }
        if (event.type == SDL_CONTROLLERAXISMOTION) {
            auto& state = controller_directions[event.caxis.which];
            auto previous = 0;
            InputAction action = InputAction::none;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                previous = state.horizontal;
                action = update_axis_direction(
                    event.caxis.value, state.horizontal,
                    InputAction::left, InputAction::right);
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                previous = state.vertical;
                action = update_axis_direction(
                    event.caxis.value, state.vertical,
                    InputAction::up, InputAction::down);
            } else {
                return InputAction::none;
            }
            const auto current = event.caxis.axis ==
                    SDL_CONTROLLER_AXIS_LEFTX
                ? state.horizontal : state.vertical;
            if (action != InputAction::none) {
                state.last_direction = action;
                state.serial = ++controller_direction_serial;
            } else if (previous != current &&
                       !direction_is_active(state, state.last_direction)) {
                state.last_direction = active_controller_direction(state);
            }
            recompute_held_controller_direction();
            return action;
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
                        // A RIX loop has milliseconds * rate / 1000 samples,
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
        if (audio_initialized) return;
#ifdef __EMSCRIPTEN__
        // Keep browser playback on Web Audio's render thread. Emscripten
        // SDL2's ScriptProcessor callback runs on the JavaScript main thread,
        // so decoding a destination map can starve it and insert an audible
        // gap at every scene change. AudioBufferSourceNode continues rendering
        // while the synchronous DOS-style core is loading the next scene.
        const auto context_rate = EM_ASM_INT({
            const context = Module.SDL2 && Module.SDL2.audioContext;
            return context ? context.sampleRate : 0;
        });
        if (context_rate < 8'000 || context_rate > 192'000) {
            throw std::runtime_error(
                "browser audio context returned an unsupported sample rate");
        }
        audio_rate = context_rate;
        EM_ASM({
            const context = Module.SDL2.audioContext;
            if (!Module.swd2WebAudio) {
                const audio = Module.swd2WebAudio = ({
                    context,
                    musicSource: null,
                    musicKey: null,
                    voiceSource: null,
                    buffers: new Map(),
                    serial: 0,
                    cacheLimit: 4
                });
                audio.stopSource = source => {
                    if (!source) return;
                    source.onended = null;
                    try { source.stop(); } catch (_) {}
                    try { source.disconnect(); } catch (_) {}
                };
                audio.stopMusic = () => {
                    audio.stopSource(audio.musicSource);
                    audio.musicSource = null;
                    audio.musicKey = null;
                };
                audio.stopVoice = () => {
                    audio.stopSource(audio.voiceSource);
                    audio.voiceSource = null;
                };
                audio.startMusic = (key, pointer, length, rate, loop) => {
                    let entry = audio.buffers.get(key);
                    if (!entry) {
                        if (!pointer || !length || rate !== context.sampleRate) {
                            throw new Error('invalid SWD2 Web Audio music buffer');
                        }
                        const buffer = context.createBuffer(1, length, rate);
                        const destination = buffer.getChannelData(0);
                        const source = HEAP16.subarray(pointer >>> 1,
                            (pointer >>> 1) + length);
                        for (let index = 0; index < length; ++index) {
                            destination[index] = source[index] / 32768;
                        }
                        entry = ({ buffer, used: ++audio.serial });
                        audio.buffers.set(key, entry);
                        Module.swd2AudioBufferCacheMisses =
                            (Module.swd2AudioBufferCacheMisses | 0) + 1;
                        while (audio.buffers.size > audio.cacheLimit) {
                            let oldestKey = null;
                            let oldestUse = Infinity;
                            for (const pair of audio.buffers) {
                                const candidateKey = pair[0];
                                const candidate = pair[1];
                                if (candidate.used < oldestUse) {
                                    oldestKey = candidateKey;
                                    oldestUse = candidate.used;
                                }
                            }
                            if (oldestKey === null) break;
                            audio.buffers.delete(oldestKey);
                        }
                    } else {
                        entry.used = ++audio.serial;
                        Module.swd2AudioBufferCacheHits =
                            (Module.swd2AudioBufferCacheHits | 0) + 1;
                    }
                    audio.stopMusic();
                    const node = context.createBufferSource();
                    node.buffer = entry.buffer;
                    node.loop = Boolean(loop);
                    node.connect(context.destination);
                    node.onended = () => {
                        if (audio.musicSource === node) audio.musicSource = null;
                        try { node.disconnect(); } catch (_) {}
                    };
                    node.start();
                    audio.musicSource = node;
                    audio.musicKey = key;
                };
                audio.startVoice = (pointer, length, rate) => {
                    if (!pointer || !length || rate !== context.sampleRate) {
                        throw new Error('invalid SWD2 Web Audio voice buffer');
                    }
                    const buffer = context.createBuffer(1, length, rate);
                    const destination = buffer.getChannelData(0);
                    const source = HEAP16.subarray(pointer >>> 1,
                        (pointer >>> 1) + length);
                    for (let index = 0; index < length; ++index) {
                        destination[index] = source[index] / 32768;
                    }
                    audio.stopVoice();
                    const node = context.createBufferSource();
                    node.buffer = buffer;
                    node.connect(context.destination);
                    node.onended = () => {
                        if (audio.voiceSource === node) audio.voiceSource = null;
                        try { node.disconnect(); } catch (_) {}
                    };
                    node.start();
                    audio.voiceSource = node;
                };
            }
            Module.swd2AudioBackend = 'audio-buffer-source';
            document.documentElement.dataset.audioBackend =
                Module.swd2AudioBackend;
        });
        audio_initialized = true;
        EM_ASM({
            Module.swd2AudioSynthesisRate = $0;
            const context = Module.SDL2 && Module.SDL2.audioContext;
            Module.swd2AudioContextRate = context ? context.sampleRate : 0;
            document.documentElement.dataset.audioSynthesisRate = String($0);
            document.documentElement.dataset.audioContextRate =
                String(Module.swd2AudioContextRate || 0);
        }, audio_rate);
        return;
#else
        SDL_AudioSpec desired{};
        desired.freq = audio_rate;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 1024;
        desired.callback = &Impl::audio_callback;
        desired.userdata = this;
        SDL_AudioSpec obtained{};
        constexpr auto allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
        audio_device = SDL_OpenAudioDevice(
            nullptr, 0, &desired, &obtained, allowed_changes);
        if (audio_device == 0) fail_sdl("SDL_OpenAudioDevice");
        if (obtained.format != AUDIO_S16SYS || obtained.channels != 1) {
            SDL_CloseAudioDevice(audio_device);
            audio_device = 0;
            throw std::runtime_error("SDL audio backend cannot accept mono S16 audio");
        }
        audio_rate = obtained.freq;
        SDL_PauseAudioDevice(audio_device, 0);
        audio_initialized = true;
#endif
    }
};

SdlPlatform::SdlPlatform() : impl_(std::make_unique<Impl>()) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        fail_sdl("SDL_Init");
    }
    // The browser shell rotates its landscape layout with CSS while iOS stays
    // in a portrait viewport.  Emscripten treats a resizable, externally sized
    // canvas as the transformed portrait bounding box and changes the backing
    // store to (for example) 600x960. SDL then letterboxes 320x200 into that
    // portrait buffer, after which CSS stretches the whole buffer back to a
    // landscape box: the result is the thin, vertically crushed strip seen on
    // iOS. Keep the Web backing store at a fixed 960x600; CSS alone scales and
    // rotates it. Native windows remain freely resizable and HiDPI-aware.
#ifdef __EMSCRIPTEN__
    constexpr auto window_flags = std::uint32_t{0};
#else
    constexpr auto window_flags =
        std::uint32_t{SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI};
#endif
    impl_->window = SDL_CreateWindow("轩辕剑2 · SWD2", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 960, 600,
                                     window_flags);
    if (!impl_->window) fail_sdl("SDL_CreateWindow");
    impl_->renderer = SDL_CreateRenderer(
        impl_->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!impl_->renderer) {
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!impl_->renderer) fail_sdl("SDL_CreateRenderer");
#ifdef __EMSCRIPTEN__
    int backing_width = 0;
    int backing_height = 0;
    if (SDL_GetRendererOutputSize(impl_->renderer, &backing_width,
                                  &backing_height) != 0) {
        fail_sdl("SDL_GetRendererOutputSize");
    }
    if (backing_width != 960 || backing_height != 600) {
        throw std::runtime_error(
            "browser SDL canvas backing store is not fixed at 960x600");
    }
#endif
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
    // synchronous. DOM touch directions do not create SDL events, so the JS
    // held/quick-tap state must be sampled again after every suspension. A
    // one-time sample before entering this loop leaves a direction pressed
    // while a menu is already waiting permanently invisible to the core.
    for (;;) {
        if (const auto pending = impl_->take_pending_action(false);
            pending != InputAction::none) {
            return pending;
        }
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
    if (impl_->held_keyboard_direction != InputAction::none) {
        return impl_->held_keyboard_direction;
    }
    return impl_->held_controller_direction;
}

bool SdlPlatform::poll_frontend_quit() {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        impl_->retain_event_action(event);
    }
    return impl_->frontend_quit;
}

void SdlPlatform::discard_pending_menu_activation() {
    // Capture presses posted since the timed sequence's final lifecycle poll,
    // then fence only activation keys. Directions remain queued so a player
    // may already select Continue while the title palette is fading in.
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        impl_->retain_event_action(event);
    }
    std::erase_if(impl_->pending_actions, [](InputAction action) {
        return action == InputAction::confirm ||
               action == InputAction::cancel;
    });
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
    auto hundredth = static_cast<unsigned>(milliseconds.count() / 10);
#ifdef __EMSCRIPTEN__
    // The real-browser regression can lock the otherwise wall-clock-dependent
    // RPG load perturbation to an odd value. Production pages leave this at
    // -1 and continue to use the actual DOS-style hundredth.
    const auto clock_override = EM_ASM_INT({
        return Number.isInteger(Module.swd2ClockHundredthSelfTest)
            ? Module.swd2ClockHundredthSelfTest : -1;
    });
    if (clock_override >= 0 && clock_override <= 99) {
        hundredth = static_cast<unsigned>(clock_override);
    }
#endif
    return {static_cast<unsigned>(local.tm_min), static_cast<unsigned>(local.tm_sec),
            hundredth};
}

void SdlPlatform::play_music(std::span<const std::uint8_t> rix_data, bool loop) {
    impl_->ensure_audio();
#ifdef __EMSCRIPTEN__
    // Use the complete RIX bytes, not a collision-prone digest, as the
    // in-session key for the four-entry decoded AudioBuffer LRU. The cache is
    // deliberately checked before OPL synthesis, so returning from a battle
    // or revisiting a map does not stall the scene transition to rebuild the
    // same multi-minute PCM stream.
    constexpr auto hex = "0123456789abcdef";
    std::string cache_key(rix_data.size() * 2U, '0');
    for (std::size_t index = 0; index < rix_data.size(); ++index) {
        cache_key[index * 2U] = hex[rix_data[index] >> 4U];
        cache_key[index * 2U + 1U] = hex[rix_data[index] & 0x0fU];
    }
    const auto cached = EM_ASM_INT({
        const audio = Module.swd2WebAudio;
        const key = UTF8ToString($0);
        return audio && audio.buffers.has(key) ? 1 : 0;
    }, cache_key.c_str());
    if (cached != 0) {
        EM_ASM({
            const key = UTF8ToString($0);
            Module.swd2WebAudio.startMusic(key, 0, 0, $1, $2);
        }, cache_key.c_str(), impl_->audio_rate, loop ? 1 : 0);
        return;
    }
#endif
    const auto sequence = decode_rix(rix_data);
    const auto rate = static_cast<std::uint32_t>(impl_->audio_rate);
    auto music = synthesize_rix(sequence, rate);
    AudioLoopClock loop_clock(sequence.total_milliseconds, rate);
    if (loop_clock.samples_per_loop() != music.mono_samples.size()) {
        throw std::runtime_error("RIX PCM and loop clock duration disagree");
    }
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const key = UTF8ToString($0);
        Module.swd2WebAudio.startMusic(key, $1, $2, $3, $4);
    }, cache_key.c_str(), music.mono_samples.data(), music.mono_samples.size(),
       impl_->audio_rate, loop ? 1 : 0);
#else
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples = std::move(music.mono_samples);
    impl_->music_cursor = 0;
    impl_->music_loop_clock = loop_clock;
    impl_->loop_music = loop;
    SDL_UnlockAudioDevice(impl_->audio_device);
#endif
}

void SdlPlatform::play_voice(std::span<const std::uint8_t> voc_data) {
    impl_->ensure_audio();
    auto voice = resample_voice(
        decode_voc(voc_data), static_cast<std::uint32_t>(impl_->audio_rate));
#ifdef __EMSCRIPTEN__
    EM_ASM({
        Module.swd2WebAudio.startVoice($0, $1, $2);
    }, voice.mono_samples.data(), voice.mono_samples.size(), impl_->audio_rate);
#else
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->voice_samples = std::move(voice.mono_samples);
    impl_->voice_cursor = 0;
    SDL_UnlockAudioDevice(impl_->audio_device);
#endif
}

void SdlPlatform::stop_music() {
#ifdef __EMSCRIPTEN__
    if (!impl_->audio_initialized) return;
    EM_ASM({ Module.swd2WebAudio.stopMusic(); });
#else
    if (impl_->audio_device == 0) return;
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples.clear();
    impl_->music_cursor = 0;
    impl_->music_loop_clock = {};
    impl_->loop_music = false;
    SDL_UnlockAudioDevice(impl_->audio_device);
#endif
}

void SdlPlatform::stop_audio() {
#ifdef __EMSCRIPTEN__
    if (!impl_->audio_initialized) return;
    EM_ASM({
        Module.swd2WebAudio.stopMusic();
        Module.swd2WebAudio.stopVoice();
    });
#else
    if (impl_->audio_device == 0) return;
    SDL_LockAudioDevice(impl_->audio_device);
    impl_->music_samples.clear();
    impl_->voice_samples.clear();
    impl_->music_cursor = 0;
    impl_->voice_cursor = 0;
    impl_->music_loop_clock = {};
    impl_->loop_music = false;
    SDL_UnlockAudioDevice(impl_->audio_device);
#endif
}

}  // namespace swd2
