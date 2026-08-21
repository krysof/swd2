#include "swd2/sdl_platform.hpp"

#include <SDL.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
        // Pump before reading the held level. Otherwise a synthetic release
        // can still be waiting in SDL's virtual joystick backend while the
        // previous direction quite correctly remains latched.
        SDL_PumpEvents();
        const auto action = platform.poll_input();
        if (action != swd2::InputAction::none) return action;
    }
    return swd2::InputAction::none;
}

bool poll_controller_release(swd2::SdlPlatform& platform) {
    // Until SDL has converted the virtual-device change into a controller-up
    // event, poll_input must still report the old held level. Consume bounded
    // frame polls until that event is observed and the level becomes neutral.
    for (int attempt = 0; attempt < 8; ++attempt) {
        SDL_PumpEvents();
        if (platform.poll_input() == swd2::InputAction::none) return true;
    }
    return false;
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

        swd2::HeldDirectionRepeatState held_direction;
        require(held_direction.sample(1, 1, 100) == swd2::InputAction::up,
                "held direction did not preserve its initial press");
        require(held_direction.sample(0, 1, 279) == swd2::InputAction::none &&
                    held_direction.sample(0, 1, 280) == swd2::InputAction::up,
                "held direction did not repeat after exactly 180 ms");
        require(held_direction.sample(0, 1, 364) == swd2::InputAction::none &&
                    held_direction.sample(0, 1, 365) == swd2::InputAction::up,
                "held direction did not continue at the 85 ms cadence");
        require(held_direction.sample(0, 0, 366) == swd2::InputAction::none &&
                    held_direction.sample(2, 0, 400) == swd2::InputAction::left &&
                    held_direction.sample(0, 0, 700) == swd2::InputAction::none,
                "direction release or queued quick tap handling failed");
        swd2::HeldDirectionRepeatState wrapped_direction;
        require(wrapped_direction.sample(4, 4, 0xffffff80U) ==
                    swd2::InputAction::right &&
                    wrapped_direction.sample(0, 4, 0x00000033U) ==
                    swd2::InputAction::none &&
                    wrapped_direction.sample(0, 4, 0x00000034U) ==
                    swd2::InputAction::right,
                "held direction timer failed across the 32-bit tick wrap");

        // This is one continuous one-second hold, not a sequence of synthetic
        // presses: queued_direction is nonzero exactly once at t=0, the held
        // level remains asserted, and release occurs exactly once at t=1000.
        swd2::HeldDirectionRepeatState one_second_hold;
        std::vector<std::uint32_t> hold_actions;
        for (std::uint32_t now = 0; now < 1000; now += 5) {
            const auto action = one_second_hold.sample(
                now == 0 ? 3 : 0, 3, now);
            if (action != swd2::InputAction::none) {
                require(action == swd2::InputAction::down,
                        "continuous hold changed direction");
                hold_actions.push_back(now);
            }
        }
        const std::vector<std::uint32_t> expected_hold_actions{
            0, 180, 265, 350, 435, 520, 605, 690, 775, 860, 945};
        require(hold_actions == expected_hold_actions,
                "one continuous one-second press did not generate all repeats");
        for (std::uint32_t now = 1000; now < 1300; now += 5) {
            require(one_second_hold.sample(0, 0, now) ==
                        swd2::InputAction::none,
                    "continuous direction kept repeating after its one release");
        }

        const std::array<std::pair<SDL_Keycode, swd2::InputAction>, 15> keyboard{{
            {SDLK_UP, swd2::InputAction::up},
            {SDLK_DOWN, swd2::InputAction::down},
            {SDLK_LEFT, swd2::InputAction::left},
            {SDLK_RIGHT, swd2::InputAction::right},
            {SDLK_PAGEUP, swd2::InputAction::page_up},
            {SDLK_PAGEDOWN, swd2::InputAction::page_down},
            {SDLK_HOME, swd2::InputAction::home},
            {SDLK_END, swd2::InputAction::end},
            {SDLK_LCTRL, swd2::InputAction::erase},
            {SDLK_RCTRL, swd2::InputAction::erase},
            {SDLK_INSERT, swd2::InputAction::erase},
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

        // RPG samples a held direction once per rendered world frame. Push a
        // single keydown, perform 69 frame polls without any more key events,
        // then push one keyup. Every one of those 69 polls must see RIGHT.
        push(key(SDLK_RIGHT));
        for (int frame = 0; frame < 69; ++frame) {
            require(platform.poll_input() == swd2::InputAction::right,
                    "one held direction did not remain active for all 69 frames");
        }
        push(key(SDLK_RIGHT, SDL_KEYUP));
        require(platform.poll_input() == swd2::InputAction::none,
                "held 69-frame direction did not stop on its one key release");

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
            require(poll_controller_release(platform),
                    "SDL controller release generated a portable input action");
        }

        // The RPG world reads a direction level on every rendered frame.
        // A physical D-pad must therefore behave like the held keyboard and
        // browser touch paths: one button-down, 69 frame polls, one release.
        require(SDL_JoystickSetVirtualButton(
                    virtual_joystick, 9, SDL_PRESSED) == 0,
                "SDL could not hold the virtual controller D-pad");
        for (int frame = 0; frame < 69; ++frame) {
            require(poll_controller_event(platform) ==
                        swd2::InputAction::right,
                    "one held controller direction did not remain active for all 69 frames");
        }
        require(SDL_JoystickSetVirtualButton(
                    virtual_joystick, 9, SDL_RELEASED) == 0,
                "SDL could not release the held virtual controller D-pad");
        require(poll_controller_release(platform),
                "held controller direction did not stop on its one release");

        // Analogue axes use engage/release hysteresis. They emit one fresh
        // action when a direction is crossed and remain held on every world
        // poll until the stick returns through the release threshold.
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, -17'000) == 0,
                "SDL could not move a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::left,
                "SDL left analogue engage threshold failed");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, -12'000) == 0,
                "SDL could not hold a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::left,
                "SDL analogue hysteresis lost a held direction");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, 17'000) == 0,
                "SDL could not cross a virtual controller axis");
        require(poll_controller_event(platform) == swd2::InputAction::right,
                "SDL analogue direct side change failed");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 0, 0) == 0,
                "SDL could not centre a virtual controller axis");
        require(poll_controller_release(platform),
                "SDL analogue centre release generated an action");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 1, -17'000) == 0,
                "SDL could not move a virtual controller vertical axis");
        require(poll_controller_event(platform) == swd2::InputAction::up,
                "SDL vertical analogue mapping failed");
        for (int frame = 0; frame < 68; ++frame) {
            require(platform.poll_input() == swd2::InputAction::up,
                    "held analogue direction did not remain active for 69 frames");
        }
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 1, 17'000) == 0,
                "SDL could not cross a virtual controller vertical axis");
        require(poll_controller_event(platform) == swd2::InputAction::down,
                "SDL vertical analogue side change failed");
        require(SDL_JoystickSetVirtualAxis(virtual_joystick, 1, 0) == 0,
                "SDL could not centre the virtual controller vertical axis");
        require(poll_controller_release(platform),
                "SDL vertical analogue release remained held");

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

        // Repeated taps during a module's uninterruptible fade must not
        // activate the next menu before it is visible. The handoff fence drops
        // Confirm/Cancel but deliberately keeps a queued choice direction.
        push(key(SDLK_RETURN));
        push(key(SDLK_RETURN, SDL_KEYUP));
        push(key(SDLK_ESCAPE));
        push(key(SDLK_ESCAPE, SDL_KEYUP));
        push(key(SDLK_DOWN));
        push(key(SDLK_DOWN, SDL_KEYUP));
        require(!platform.poll_frontend_quit(),
                "SDL menu-fence setup mistook a key for window close");
        platform.discard_pending_menu_activation();
        require(platform.poll_input() == swd2::InputAction::down,
                "SDL menu fence discarded the queued choice direction");
        require(platform.poll_input() == swd2::InputAction::none,
                "SDL menu fence retained a stale activation key");

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
