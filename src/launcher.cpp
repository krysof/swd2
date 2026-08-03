#include "swd2/launcher.hpp"

#include <stdexcept>

namespace swd2 {

Launcher::Launcher(std::size_t transition_limit) : transition_limit_(transition_limit) {
    if (transition_limit == 0) {
        throw std::invalid_argument("transition limit must be greater than zero");
    }
}

LaunchResult Launcher::run(const ModuleRunner& runner) const {
    LaunchResult result;
    Marker marker = Marker::none;

    const auto invoke = [&](Module module) {
        const Marker input = marker;
        const auto child = runner(module, input);
        marker = child.marker;
        result.transitions.push_back({module, input, marker, child.launched});
        return child.launched;
    };

    if (!invoke(Module::menu)) {
        result.reason = StopReason::child_launch_failed;
        result.final_marker = marker;
        return result;
    }
    if (marker != Marker::menu_ready) {
        result.reason = StopReason::menu_rejected;
        result.final_marker = marker;
        return result;
    }

    while (result.transitions.size() < transition_limit_) {
        if (!invoke(Module::rpg)) {
            result.reason = StopReason::child_launch_failed;
            result.final_marker = marker;
            return result;
        }

        if (marker == Marker::open_demo) {
            // SWD2.EXE ignores DEMO.EXE's marker and writes "OM" itself.
            if (!invoke(Module::demo)) {
                result.reason = StopReason::child_launch_failed;
                result.final_marker = marker;
                return result;
            }
            marker = Marker::returned_from_demo;
            continue;
        }

        if (marker == Marker::open_figure) {
            if (!invoke(Module::figure)) {
                result.reason = StopReason::child_launch_failed;
                result.final_marker = marker;
                return result;
            }
            if (marker == Marker::continue_rpg) {
                continue;
            }
        }

        result.reason = StopReason::module_requested_exit;
        result.final_marker = marker;
        return result;
    }

    result.reason = StopReason::transition_limit;
    result.final_marker = marker;
    return result;
}

std::string_view module_name(Module module) noexcept {
    switch (module) {
    case Module::menu: return "MEO.EXE";
    case Module::rpg: return "RPG.EXE";
    case Module::figure: return "FIG.EXE";
    case Module::demo: return "DEMO.EXE";
    }
    return "?";
}

std::string_view marker_name(Marker marker) noexcept {
    switch (marker) {
    case Marker::none: return "--";
    case Marker::menu_ready: return "MT";
    case Marker::open_figure: return "IF";
    case Marker::open_demo: return "ED";
    case Marker::continue_rpg: return "OC";
    case Marker::returned_from_demo: return "OM";
    }
    return "??";
}

std::string_view stop_reason_name(StopReason reason) noexcept {
    switch (reason) {
    case StopReason::child_launch_failed: return "child launch failed";
    case StopReason::menu_rejected: return "menu did not return MT";
    case StopReason::module_requested_exit: return "module requested exit";
    case StopReason::transition_limit: return "transition limit reached";
    }
    return "unknown";
}

}  // namespace swd2
