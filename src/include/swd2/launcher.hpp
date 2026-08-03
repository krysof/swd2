#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace swd2 {

constexpr std::uint16_t dos_tag(char first_byte, char second_byte) noexcept {
    return static_cast<std::uint8_t>(first_byte) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(second_byte)) << 8U);
}

// These are the literal byte pairs stored at DOS address 4000:0000.
enum class Marker : std::uint16_t {
    none = 0,
    menu_ready = dos_tag('M', 'T'),
    open_figure = dos_tag('I', 'F'),
    open_demo = dos_tag('E', 'D'),
    continue_rpg = dos_tag('O', 'C'),
    returned_from_demo = dos_tag('O', 'M'),
};

enum class Module {
    menu,
    rpg,
    figure,
    demo,
};

struct ModuleResult {
    bool launched{true};
    Marker marker{Marker::none};
};

using ModuleRunner = std::function<ModuleResult(Module, Marker)>;

enum class StopReason {
    child_launch_failed,
    menu_rejected,
    module_requested_exit,
    transition_limit,
};

struct Transition {
    Module module{};
    Marker input{};
    Marker output{};
    bool launched{};
};

struct LaunchResult {
    StopReason reason{};
    Marker final_marker{};
    std::vector<Transition> transitions;
};

class Launcher {
public:
    explicit Launcher(std::size_t transition_limit = 10'000);
    [[nodiscard]] LaunchResult run(const ModuleRunner& runner) const;

private:
    std::size_t transition_limit_;
};

std::string_view module_name(Module module) noexcept;
std::string_view marker_name(Marker marker) noexcept;
std::string_view stop_reason_name(StopReason reason) noexcept;

}  // namespace swd2
