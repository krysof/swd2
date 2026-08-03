#include "swd2/field_action_system.hpp"

#include <algorithm>
#include <array>

namespace swd2 {

namespace {

// The table occupies DS:3a8a..3acd; DS:3ace begins the Big5 menu labels.
constexpr std::array<std::uint16_t, 34> travel_directory_offsets{
    0x0046, 0x0048, 0x0042, 0x004c, 0x007c, 0x0086,
    0x008a, 0x00b2, 0x00ca, 0x00d6, 0x00da, 0x0106,
    0x010a, 0x0112, 0x0116, 0x0122, 0x0152, 0x01a4,
    0x01a8, 0x01b4, 0x01d8, 0x028a, 0x028e, 0x02aa,
    0x02d2, 0x02d6, 0x02dc, 0x0302, 0x02fa, 0x02fe,
    0x0322, 0x033e, 0x035a, 0x0372,
};

}  // namespace

bool FieldActionSystem::requires_target(std::uint16_t action_code) noexcept {
    if (action_code == 0 || action_code == 0x28 || action_code == 0x29) return false;
    return action_code <= 0x27 && !affects_all_party(action_code);
}

bool FieldActionSystem::affects_all_party(std::uint16_t action_code) noexcept {
    return action_code == 0x0c || action_code == 0x0d ||
           action_code == 0x0e || action_code == 0x12;
}

bool FieldActionSystem::valid_actor(std::size_t actor) const noexcept {
    return actor < std::min<std::size_t>(state_.u16(0x10), 4);
}

bool FieldActionSystem::restore_percent(std::size_t actor,
                                        std::size_t current_offset,
                                        std::uint16_t percent) {
    const auto base = actor_offset(actor);
    // FUN_1000_38ab refuses dead/special targets, then adds max*percent/100
    // plus one quarter of [DS:35fc]+45 (the action owner, not the target)
    // before clamping to the adjacent maximum word.
    if ((state_.u16(base + 8) & 0xe000U) != 0) return false;
    const auto scaled = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(state_.u16(base + current_offset + 2)) * percent) / 100U);
    const auto source_base = actor_offset(source_actor_);
    auto value = static_cast<std::uint16_t>(
        scaled + (state_.u16(source_base + 0x45) >> 2U));
    value = static_cast<std::uint16_t>(value + state_.u16(base + current_offset));
    value = std::min(value, state_.u16(base + current_offset + 2));
    const auto changed = value != state_.u16(base + current_offset);
    state_.set_u16(base + current_offset, value);
    return changed;
}

bool FieldActionSystem::restore_fixed(std::size_t actor,
                                      std::size_t current_offset,
                                      std::size_t modifier_offset,
                                      std::uint16_t amount) {
    const auto base = actor_offset(actor);
    // FUN_1000_3913 uses the selected actor's modifier (not the menu owner's),
    // and otherwise has the same living test and adjacent-maximum clamp.
    if ((state_.u16(base + 8) & 0xe000U) != 0) return false;
    auto value = static_cast<std::uint16_t>(amount +
                                            (state_.u16(base + modifier_offset) >> 2U));
    value = static_cast<std::uint16_t>(value + state_.u16(base + current_offset));
    value = std::min(value, state_.u16(base + current_offset + 2));
    const auto changed = value != state_.u16(base + current_offset);
    state_.set_u16(base + current_offset, value);
    return changed;
}

bool FieldActionSystem::clear_status(std::size_t actor, std::uint16_t mask) {
    const auto offset = actor_offset(actor) + 8;
    const auto before = state_.u16(offset);
    const auto after = static_cast<std::uint16_t>(before & mask);
    state_.set_u16(offset, after);
    return before != after;
}

bool FieldActionSystem::add_word(std::size_t actor, std::size_t offset,
                                 std::uint16_t amount) {
    const auto address = actor_offset(actor) + offset;
    const auto before = state_.u16(address);
    state_.set_u16(address, static_cast<std::uint16_t>(before + amount));
    return amount != 0;
}

FieldActionResult FieldActionSystem::apply(
    std::uint16_t action_code, std::optional<std::size_t> target_actor) {
    FieldActionResult result{FieldActionStatus::no_effect, action_code};
    if (action_code > 0x29) {
        result.status = FieldActionStatus::invalid_action;
        return result;
    }
    if (action_code == 0) return result;

    if (action_code == 0x28) {
        result.status = (state_.u16(0x408) & 0x4000U) == 0
                            ? FieldActionStatus::travel_current
                            : FieldActionStatus::map_restricted;
        return result;
    }
    if (action_code == 0x29) {
        result.status = (state_.u16(0x408) & 0x8000U) != 0
                            ? FieldActionStatus::travel_select
                            : FieldActionStatus::map_restricted;
        return result;
    }

    // Literal DS:379c/379e writes made by 3407..3707. Deliberately do not
    // normalize these into per-call locals: handler 1ch writes primary twice
    // and therefore inherits the previous secondary operand.
    switch (action_code) {
    case 0x01: runtime_->primary_operand = 25; runtime_->secondary_operand = 25; break;
    case 0x02: runtime_->primary_operand = 45; runtime_->secondary_operand = 45; break;
    case 0x03: runtime_->primary_operand = 70; runtime_->secondary_operand = 100; break;
    case 0x04: runtime_->primary_operand = 100; runtime_->secondary_operand = 100; break;
    case 0x05: runtime_->primary_operand = 25; break;
    case 0x06: runtime_->primary_operand = 45; break;
    case 0x07: runtime_->primary_operand = 70; break;
    case 0x08: runtime_->primary_operand = 100; break;
    case 0x09: runtime_->primary_operand = 25; break;
    case 0x0a: runtime_->primary_operand = 45; break;
    case 0x0b: runtime_->primary_operand = 70; break;
    case 0x0c: runtime_->primary_operand = 25; runtime_->secondary_operand = 25; break;
    case 0x0d: runtime_->primary_operand = 45; runtime_->secondary_operand = 45; break;
    case 0x0e: runtime_->primary_operand = 70; runtime_->secondary_operand = 100; break;
    case 0x10: runtime_->primary_operand = 30; runtime_->secondary_operand = 30; break;
    case 0x11:
    case 0x12: runtime_->primary_operand = 100; runtime_->secondary_operand = 100; break;
    case 0x13: runtime_->primary_operand = 50; runtime_->secondary_operand = 50; break;
    case 0x14: runtime_->primary_operand = 200; runtime_->secondary_operand = 200; break;
    case 0x15: runtime_->primary_operand = 400; runtime_->secondary_operand = 400; break;
    case 0x16: runtime_->primary_operand = 50; break;
    case 0x17: runtime_->primary_operand = 200; break;
    case 0x18: runtime_->primary_operand = 400; break;
    case 0x19: runtime_->primary_operand = 50; break;
    case 0x1a: runtime_->primary_operand = 200; break;
    case 0x1b: runtime_->primary_operand = 400; break;
    case 0x1c: runtime_->primary_operand = 10; break;
    case 0x22: runtime_->primary_operand = 0x2f; break;
    case 0x23: runtime_->primary_operand = 0x37; break;
    case 0x24: runtime_->primary_operand = 0x3d; break;
    case 0x25: runtime_->primary_operand = 0x45; break;
    case 0x26: runtime_->primary_operand = 0x4f; break;
    case 0x27: runtime_->primary_operand = 0x57; break;
    default: break;
    }

    if (affects_all_party(action_code)) {
        bool changed = false;
        const auto count = std::min<std::size_t>(state_.u16(0x10), 4);
        for (std::size_t actor = 0; actor < count; ++actor) {
            if (action_code == 0x0c) {
                changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
                changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
            } else if (action_code == 0x0d) {
                changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
                changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
            } else if (action_code == 0x0e) {
                changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
                changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
            } else {  // 12h
                changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
                changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
                if ((state_.u16(actor_offset(actor) + 8) & 0xe000U) == 0) {
                    changed |= clear_status(actor, 0);
                }
            }
        }
        result.status = changed ? FieldActionStatus::applied : FieldActionStatus::no_effect;
        return result;
    }

    if (!target_actor || !valid_actor(*target_actor)) {
        result.status = FieldActionStatus::invalid_actor;
        return result;
    }
    const auto actor = *target_actor;
    const auto base = actor_offset(actor);
    bool changed = false;

    switch (action_code) {
    case 0x01: changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
               changed |= restore_percent(actor, 0x35, runtime_->secondary_operand); break;
    case 0x02: changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
               changed |= restore_percent(actor, 0x35, runtime_->secondary_operand); break;
    case 0x03: changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
               changed |= restore_percent(actor, 0x35, runtime_->secondary_operand); break;
    case 0x04: changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
               changed |= restore_percent(actor, 0x35, runtime_->secondary_operand); break;
    case 0x05: changed |= restore_percent(actor, 0x55, runtime_->primary_operand); break;
    case 0x06: changed |= restore_percent(actor, 0x55, runtime_->primary_operand); break;
    case 0x07: changed |= restore_percent(actor, 0x55, runtime_->primary_operand); break;
    case 0x08: changed |= restore_percent(actor, 0x55, runtime_->primary_operand); break;
    case 0x09: changed |= restore_percent(actor, 0x35, runtime_->primary_operand); break;
    case 0x0a: changed |= restore_percent(actor, 0x35, runtime_->primary_operand); break;
    case 0x0b: changed |= restore_percent(actor, 0x35, runtime_->primary_operand); break;
    case 0x0f:
        if ((state_.u16(base + 8) & 0xe000U) == 0) changed |= clear_status(actor, 0);
        break;
    case 0x10:
        changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
        changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
        if ((state_.u16(base + 8) & 0xe000U) == 0) changed |= clear_status(actor, 0);
        break;
    case 0x11:
        changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
        changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
        if ((state_.u16(base + 8) & 0xe000U) == 0) changed |= clear_status(actor, 0);
        break;
    case 0x13: changed |= restore_fixed(actor, 0x2d, 0x35, runtime_->primary_operand);
               changed |= restore_fixed(actor, 0x35, 0x35, runtime_->secondary_operand); break;
    case 0x14: changed |= restore_fixed(actor, 0x2d, 0x35, runtime_->primary_operand);
               changed |= restore_fixed(actor, 0x35, 0x35, runtime_->secondary_operand); break;
    case 0x15: changed |= restore_fixed(actor, 0x2d, 0x35, runtime_->primary_operand);
               changed |= restore_fixed(actor, 0x35, 0x35, runtime_->secondary_operand); break;
    case 0x16: changed |= restore_fixed(actor, 0x35, 0x35, runtime_->primary_operand); break;
    case 0x17: changed |= restore_fixed(actor, 0x35, 0x35, runtime_->primary_operand); break;
    case 0x18: changed |= restore_fixed(actor, 0x35, 0x35, runtime_->primary_operand); break;
    case 0x19: changed |= restore_fixed(actor, 0x55, 0x45, runtime_->primary_operand); break;
    case 0x1a: changed |= restore_fixed(actor, 0x55, 0x45, runtime_->primary_operand); break;
    case 0x1b: changed |= restore_fixed(actor, 0x55, 0x45, runtime_->primary_operand); break;
    case 0x1c:
        if ((state_.u16(base + 8) & 0x2000U) != 0) {
            changed |= clear_status(actor, 0);
            changed |= restore_percent(actor, 0x2d, runtime_->primary_operand);
            changed |= restore_percent(actor, 0x35, runtime_->secondary_operand);
        }
        break;
    case 0x1d: changed |= clear_status(actor, 0xfeffU); break;
    case 0x1e: changed |= clear_status(actor, 0xfdffU); break;
    case 0x1f: changed |= clear_status(actor, 0xfcffU); break;
    case 0x20: changed |= clear_status(actor, 0xfbffU); break;
    case 0x21: changed |= clear_status(actor, 0xf7ffU); break;
    case 0x22: changed |= add_word(actor, 0x2f, 3); break;
    case 0x23: changed |= add_word(actor, 0x37, 3); break;
    case 0x24: changed |= add_word(actor, 0x3d, 3);
               changed |= add_word(actor, 0x0c, 3); break;
    case 0x25: changed |= add_word(actor, 0x45, 3); break;
    case 0x26: changed |= add_word(actor, 0x4f, 3);
               changed |= add_word(actor, 0x5d, 3); break;
    case 0x27: changed |= add_word(actor, 0x57, 3); break;
    default:
        result.status = FieldActionStatus::invalid_action;
        return result;
    }

    result.status = changed ? FieldActionStatus::applied : FieldActionStatus::no_effect;
    return result;
}

std::vector<std::uint8_t> FieldActionSystem::unlocked_travel_indices() const {
    std::vector<std::uint8_t> result;
    for (std::size_t index = 0; index < travel_directory_offsets.size(); ++index) {
        const auto value = state_.u8(0x51e + index);
        if (value == 0x0f) break;
        if (value == 1) result.push_back(static_cast<std::uint8_t>(index));
    }
    return result;
}

std::optional<std::uint16_t> FieldActionSystem::travel_directory_offset(
    std::uint8_t travel_index) noexcept {
    if (travel_index >= travel_directory_offsets.size()) return std::nullopt;
    return travel_directory_offsets[travel_index];
}

}  // namespace swd2
