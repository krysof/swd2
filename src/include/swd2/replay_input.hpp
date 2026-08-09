#pragma once

#include "swd2/platform.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace swd2 {

// Deterministic replays may lock an action to the exact portable input
// boundary that is allowed to consume it. This prevents a timed animation
// poll from stealing the confirmation intended for the following menu.
enum class ReplayInputBoundary {
    any,
    wait,
    poll,
    text,
    frontend,
};

struct ReplayInputStep {
    ReplayInputBoundary boundary{ReplayInputBoundary::any};
    InputAction action{InputAction::none};
    bool operator==(const ReplayInputStep&) const = default;
};

// Tokens are separated by commas or ASCII whitespace. Lines may contain a
// '#' comment. The grammar is [BOUNDARY:]ACTION[*COUNT], for example:
//
//   POLL:RIGHT*12, WAIT:CONFIRM, TEXT:NONE*4, FRONTEND:QUIT
//
// Untagged tokens retain the old --run-script behaviour and may be consumed
// by wait/poll/text boundaries. FRONTEND accepts only NONE or QUIT.
[[nodiscard]] std::vector<ReplayInputStep> parse_replay_input(
    std::string_view text);

[[nodiscard]] std::string_view replay_boundary_name(
    ReplayInputBoundary boundary) noexcept;
[[nodiscard]] std::string_view input_action_name(InputAction action) noexcept;

}  // namespace swd2
