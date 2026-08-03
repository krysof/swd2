#pragma once

#include "swd2/shared_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace swd2 {

// RPG.EXE:3407..3725 is an indirect 42-entry dispatcher shared by field
// abilities and usable ITEM records.  These statuses distinguish a dispatched
// action (which consumes a consumable even if its numerical effect is zero)
// from a cancelled/blocked action (which does not).
enum class FieldActionStatus {
    applied,
    no_effect,
    invalid_actor,
    invalid_action,
    map_restricted,
    travel_current,
    travel_select,
};

struct FieldActionResult {
    FieldActionStatus status{FieldActionStatus::invalid_action};
    std::uint16_t action_code{};

    [[nodiscard]] bool dispatched() const noexcept {
        return status == FieldActionStatus::applied ||
               status == FieldActionStatus::no_effect ||
               status == FieldActionStatus::travel_current ||
               status == FieldActionStatus::travel_select;
    }
};

// RPG.EXE keeps restorative operands in DS:379c/379e instead of local
// variables. Most handlers overwrite one or both, but resurrection 1ch has
// the same duplicated-primary typo as FIG and consumes stale secondary state.
struct FieldActionRuntime {
    std::uint16_t primary_operand{};
    std::uint16_t secondary_operand{};
};

class FieldActionSystem {
public:
    explicit FieldActionSystem(SharedState& state,
                               FieldActionRuntime* runtime = nullptr,
                               std::size_t source_actor = 0)
        : state_(state), runtime_(runtime == nullptr ? &owned_runtime_ : runtime),
          source_actor_(source_actor) {}

    [[nodiscard]] static bool requires_target(std::uint16_t action_code) noexcept;
    [[nodiscard]] static bool affects_all_party(std::uint16_t action_code) noexcept;

    // target_actor is ignored by all-party and travel actions.  The arithmetic
    // intentionally wraps at 16 bits before the original unsigned max clamp.
    FieldActionResult apply(std::uint16_t action_code,
                            std::optional<std::size_t> target_actor = std::nullopt);

    // Action 29h lists every SAVE+51e byte equal to one until the 0fh sentinel.
    [[nodiscard]] std::vector<std::uint8_t> unlocked_travel_indices() const;

    // The original DS:3a8a table maps the travel byte index to a MAPA/MAPZ
    // directory byte offset. Action 28h uses SAVE+40a; action 29h uses the
    // selected byte index.
    [[nodiscard]] static std::optional<std::uint16_t>
    travel_directory_offset(std::uint8_t travel_index) noexcept;

private:
    static constexpr std::size_t actor_offset(std::size_t actor) noexcept {
        return 0x106 + actor * 0x9f;
    }

    [[nodiscard]] bool valid_actor(std::size_t actor) const noexcept;
    bool restore_percent(std::size_t actor, std::size_t current_offset,
                         std::uint16_t percent);
    bool restore_fixed(std::size_t actor, std::size_t current_offset,
                       std::size_t modifier_offset, std::uint16_t amount);
    bool clear_status(std::size_t actor, std::uint16_t mask);
    bool add_word(std::size_t actor, std::size_t offset, std::uint16_t amount);

    SharedState& state_;
    FieldActionRuntime owned_runtime_;
    FieldActionRuntime* runtime_{};
    std::size_t source_actor_{};
};

}  // namespace swd2
