#include "swd2/sdl_platform.hpp"

#include <SDL.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void push(SDL_Event event) {
    require(SDL_PushEvent(&event) == 1, "SDL rejected a synthetic input event");
}

swd2::InputAction poll_controller_event(swd2::SdlPlatform& platform) {
    // SDL2's virtual joystick backend may append its logical controller event
    // during the first PumpEvents performed by PollEvent. A real game loop
    // naturally reaches it on the next tick; give the test the same bounded
    // opportunity without sleeping or changing the platform implementation.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto action = platform.poll_input();
        if (action != swd2::InputAction::none) return action;
        SDL_PumpEvents();
    }
    return swd2::InputAction::none;
}

swd2::InputAction poll_text_event(swd2::SdlPlatform& platform) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto action = platform.poll_text_input();
        if (action != swd2::InputAction::none) return action;
        SDL_PumpEvents();
    }
    return swd2::InputAction::none;
}

bool poll_frontend_event(swd2::SdlPlatform& platform) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (platform.poll_frontend_quit()) return true;
        SDL_PumpEvents();
    }
    return false;
}

SDL_Event key(SDL_Keycode code, Uint32 type = SDL_KEYDOWN, Uint8 repeat = 0) {
    SDL_Event event{};
    event.type = type;
    event.key.type = type;
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = repeat;
    event.key.keysym.sym = code;
    return event;
}

}  // namespace

int main() {
    try {
        // A real SDL virtual joystick makes the test traverse SDL's physical
        // controller mapping layer instead of injecting already-translated
        // SDL_CONTROLLERBUTTONDOWN records with a nonexistent device id.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        require(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK |
                         SDL_INIT_GAMECONTROLLER) == 0,
                "SDL could not initialize the virtual controller harness");
        const auto virtual_device = SDL_JoystickAttachVirtual(
            SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 10, 0);
        require(virtual_device >= 0, "SDL could not attach a virtual controller");
        char guid_text[64]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(virtual_device),
                                  guid_text, sizeof(guid_text));
        const auto mapping = std::string(guid_text) +
            ",SWD2 Input Test,a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,"
            "rightshoulder:b5,dpup:b6,dpdown:b7,dpleft:b8,dpright:b9,"
            "leftx:a0,lefty:a1,platform:Mac OS X,";
        require(SDL_GameControllerAddMapping(mapping.c_str()) >= 0 &&
                    SDL_IsGameController(virtual_device) == SDL_TRUE,
                "SDL could not map the virtual controller");

        swd2::SdlPlatform platform;
        auto* virtual_joystick = SDL_JoystickOpen(virtual_device);
        require(virtual_joystick != nullptr,
                "SDL could not open the virtual controller test device");

        const std::array<std::pair<SDL_Keycode, swd2::InputAction>, 12> keyboard{{
            {SDLK_UP, swd2::InputAction::up},
            {SDLK_DOWN, swd2::InputAction::down},
            {SDLK_LEFT, swd2::InputAction::left},
            {SDLK_RIGHT, swd2::InputAction::right},
            {SDLK_PAGEUP, swd2::InputAction::page_up},
            {SDLK_PAGEDOWN, swd2::InputAction::page_down},
            {SDLK_HOME, swd2::InputAction::home},
            {SDLK_END, swd2::InputAction::end},
            {SDLK_RETURN, swd2::InputAction::confirm},
            {SDLK_SPACE, swd2::InputAction::confirm},
            {SDLK_z, swd2::InputAction::confirm},
            {SDLK_x, swd2::InputAction::cancel},
        }};
        for (std::size_t index = 0; index < keyboard.size(); ++index) {
            const auto [code, expected] = keyboard[index];
            push(key(code));
            push(key(code, SDL_KEYUP));
            const auto actual = platform.poll_input();
            if (actual != expected) {
                throw std::runtime_error(
                    "SDL keyboard mapping differs at case " +
                    std::to_string(index) + ": expected " +
                    std::to_string(static_cast<int>(expected)) + ", got " +
                    std::to_string(static_cast<int>(actual)));
            }
            require(platform.poll_input() == swd2::InputAction::none,
                    "SDL key release generated a portable input action");
        }

        // Repeated keydown events must not scroll DOS selectors. The blocking
        // boundary skips the repeat and returns the next fresh action.
        push(key(SDLK_DOWN, SDL_KEYDOWN, 1));
        push(key(SDLK_DOWN, SDL_KEYUP));
        push(key(SDLK_ESCAPE));
        push(key(SDLK_ESCAPE, SDL_KEYUP));
        require(platform.wait_for_input() == swd2::InputAction::cancel,
                "SDL blocking input accepted a repeated keydown");

        const std::array<std::pair<int, swd2::InputAction>, 10> controller{{
            {6, swd2::InputAction::up},
            {7, swd2::InputAction::down},
            {8, swd2::InputAction::left},
            {9, swd2::InputAction::right},
            {4, swd2::InputAction::page_up},
            {5, swd2::InputAction::page_down},
            {2, swd2::InputAction::home},
            {3, swd2::InputAction::end},
            {0, swd2::InputAction::confirm},
            {1, swd2::InputAction::cancel},
        }};
        for (std::size_t index = 0; index < controller.size(); ++index) {
            const auto [physical_button, expected] = controller[index];
            require(SDL_JoystickSetVirtualButton(
                        virtual_joystick, physical_button, SDL_PRESSED) == 0,
                    "SDL could not press a virtual controller button");
            const auto actual = poll_controller_event(platform);
            if (actual != expected) {
                throw std::runtime_error(
                    "SDL controller mapping differs at case " +
                    std::to_string(index) + ": expected " +
                    std::to_string(static_cast<int>(expected)) + ", got " +
                    std::to_string(static_cast<int>(actual)));
            }
            require(SDL_JoystickSetVirtualButton(
                        virtual_joystick, physical_button, SDL_RELEASED) == 0,
                    "SDL could not release a virtual controller button");
            require(poll_controller_event(platform) == swd2::InputAction::none,
                    "SDL controller release generated a portable input action");
        }

        // Analogue axes use engage/release hysteresis and emit exactly one
        // action when a direction is crossed, including a direct side change.
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, -17'000) == 0,
                "SDL could not move a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::left,
                "SDL left analogue engage threshold failed");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, -12'000) == 0,
                "SDL could not hold a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::none,
                "SDL analogue hysteresis repeated a held direction");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, 17'000) == 0,
                "SDL could not cross a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::right,
                "SDL analogue direct side change failed");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, 0) == 0,
                "SDL could not centre a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::none,
                "SDL analogue centre release generated an action");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 1, -17'000) == 0,
                "SDL could not move a virtual controller vertical axis");
        require(poll_controller_event(platform) == swd2::InputAction::up,
                "SDL vertical analogue mapping failed");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 1, 17'000) == 0,
                "SDL could not cross a virtual controller vertical axis");
        require(poll_controller_event(platform) == swd2::InputAction::down,
                "SDL vertical analogue side change failed");

        // The typewriter polling boundary uses the same physical mapping but
        // remains a distinct virtual call in deterministic replay backends.
        push(key(SDLK_RETURN));
        push(key(SDLK_RETURN, SDL_KEYUP));
        require(poll_text_event(platform) == swd2::InputAction::confirm,
                "SDL text-skip boundary did not dispatch through poll_input");

        // A frontend lifecycle probe may drain SDL events, but must retain a
        // gameplay action for the next actual input boundary.
        push(key(SDLK_LEFT));
        push(key(SDLK_LEFT, SDL_KEYUP));
        require(!platform.poll_frontend_quit(),
                "SDL frontend probe mistook a gameplay key for window close");
        require(platform.poll_input() == swd2::InputAction::left,
                "SDL frontend probe consumed a queued gameplay action");

        SDL_Event quit{};
        quit.type = SDL_QUIT;
        push(quit);
        require(poll_frontend_event(platform) &&
                    platform.wait_for_input() == swd2::InputAction::quit,
                "SDL window close was not latched across input boundaries");

        SDL_JoystickClose(virtual_joystick);

        std::cout << "SDL portable input tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "SDL input test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
