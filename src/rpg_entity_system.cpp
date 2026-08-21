#include "swd2/rpg_entity_system.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace swd2 {

namespace {

constexpr std::size_t code_stream_base = 0x4f1c;
constexpr std::uint16_t code_stream_wrap = 0x1000;

std::uint16_t read_word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("RPG entity movement code stream is truncated");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

bool entity_contains(const MapAreaRecord& area, std::size_t excluded,
                     std::uint16_t cell_offset) {
    for (std::size_t index = 0; index < area.entity_count(); ++index) {
        if (index == excluded || area.entity_fields[3][index] == 3) continue;
        const auto anchor = area.entity_fields[2][index];
        if (cell_offset == anchor || cell_offset == anchor + 2U ||
            cell_offset == anchor + 4U) {
            return true;
        }
    }
    return false;
}

bool actor_contains(const SharedState& state, std::uint16_t map_width,
                    std::uint16_t cell_base, std::uint16_t cell_offset) {
    // 1e63 marks (party members * three formation records) with high-byte bit
    // 20h before autonomous entities move. The arrays contain twelve slots.
    const auto formation_count = std::min<std::size_t>(
        12, static_cast<std::size_t>(state.u16(0x10) + state.u16(0x102)) * 3U);
    for (std::size_t index = 0; index < formation_count; ++index) {
        const auto screen_x = state.u16(0x12 + index * 2U);
        const auto screen_y = state.u16(0x2a + index * 2U);
        const auto world_x = static_cast<std::size_t>(state.viewport_x()) +
                             ((screen_x + 2U) >> 1U);
        const auto world_y = static_cast<std::size_t>(state.viewport_y()) +
                             ((screen_y + 16U) >> 3U);
        const auto offset = static_cast<std::size_t>(cell_base) +
                            (world_y * map_width + world_x) * 2U;
        if (offset <= std::numeric_limits<std::uint16_t>::max() &&
            static_cast<std::uint16_t>(offset) == cell_offset) {
            return true;
        }
    }
    return false;
}

bool destination_blocked(const MapAreaRecord& area, std::size_t moving_entity,
                         const MapResource& map, const SharedState& state,
                         std::uint16_t anchor) {
    const auto base = state.u16(0x40f);
    for (std::uint16_t word = 0; word < 3; ++word) {
        const auto wide_offset = static_cast<std::size_t>(anchor) + word * 2U;
        if (wide_offset > std::numeric_limits<std::uint16_t>::max()) return true;
        const auto offset = static_cast<std::uint16_t>(wide_offset);
        if (offset < base || ((offset - base) & 1U) != 0) return true;
        const auto cell = static_cast<std::size_t>((offset - base) / 2U);
        if (cell >= map.cells().size() || (map.cells()[cell] & 0xf800U) != 0 ||
            entity_contains(area, moving_entity, offset) ||
            actor_contains(state, map.layout().width, base, offset)) {
            return true;
        }
    }
    return false;
}

void ensure_size(RpgEntityRuntime& runtime, std::size_t size) {
    // These emulate fixed BSS arrays rather than storage owned by the current
    // MAPZ area. A smaller destination area must not discard higher-index
    // values that can become visible again after a later map change.
    if (runtime.delay_remaining.size() < size) {
        runtime.delay_remaining.resize(size);
    }
    if (runtime.roam_x.size() < size) runtime.roam_x.resize(size);
    if (runtime.roam_y.size() < size) runtime.roam_y.resize(size);
}

}  // namespace

void perturb_rpg_load_cursor(SharedState& state, unsigned hundredth) {
    state.set_u16(0x49c, static_cast<std::uint16_t>(
        state.u16(0x49c) + static_cast<std::uint8_t>(hundredth)));
}

void advance_rpg_entities(MapAreaRecord& area, const MapResource& map,
                          const SharedState& state, RpgEntityRuntime& runtime,
                          std::span<const std::uint8_t> rpg_load_image) {
    ensure_size(runtime, area.entity_count());

    // RPG.EXE computes BX before resetting its 1000h cursor. Preserve that
    // observable wrap quirk rather than replacing it with modulo arithmetic.
    auto code_cursor = code_stream_base + runtime.code_stream_offset;
    if (runtime.code_stream_offset >= code_stream_wrap) {
        runtime.code_stream_offset = 0;
    }

    for (std::size_t index = 0; index < area.entity_count(); ++index) {
        const auto behavior = area.entity_fields[3][index];
        if (behavior == 3) continue;
        if (runtime.delay_remaining[index] != 0) {
            --runtime.delay_remaining[index];
            continue;
        }
        if (behavior == 7 || behavior == 1 || behavior == 4 || behavior == 6) {
            continue;
        }
        if (behavior == 2 || behavior == 5) {
            runtime.delay_remaining[index] = area.entity_fields[4][index];
            auto& frame = area.entity_fields[10][index];
            ++frame;
            if (frame == area.entity_fields[7][index]) frame = 0;
            continue;
        }

        const auto random = read_word(rpg_load_image, code_cursor);
        code_cursor += 2;
        runtime.code_stream_offset =
            static_cast<std::uint16_t>(runtime.code_stream_offset + 2U);
        if ((random & 4U) == 0) continue;

        const auto choice = static_cast<std::uint16_t>(random & 3U);
        auto destination = area.entity_fields[2][index];
        std::int16_t roam_dx = 0;
        std::int16_t roam_dy = 0;
        if (choice == 3) {
            if (runtime.roam_y[index] == (area.entity_fields[8][index] & 0xffU)) continue;
            area.entity_fields[1][index] = 0;
            destination = static_cast<std::uint16_t>(
                destination + static_cast<std::uint16_t>(map.layout().width * 2U));
            roam_dy = 1;
        } else if (choice == 1) {
            if (runtime.roam_y[index] == 0) continue;
            area.entity_fields[1][index] = 3;
            destination = static_cast<std::uint16_t>(
                destination - static_cast<std::uint16_t>(map.layout().width * 2U));
            roam_dy = -1;
        } else if (choice == 2) {
            if (runtime.roam_x[index] == area.entity_fields[7][index]) continue;
            area.entity_fields[1][index] = 9;
            destination = static_cast<std::uint16_t>(destination + 2U);
            roam_dx = 1;
        } else {
            if (runtime.roam_x[index] == 0) continue;
            area.entity_fields[1][index] = 6;
            destination = static_cast<std::uint16_t>(destination - 2U);
            roam_dx = -1;
        }

        if (destination_blocked(area, index, map, state, destination)) continue;
        area.entity_fields[2][index] = destination;
        auto& frame = area.entity_fields[10][index];
        ++frame;
        if (frame == 4) frame = 0;
        runtime.delay_remaining[index] = area.entity_fields[4][index];
        runtime.roam_x[index] = static_cast<std::uint16_t>(
            runtime.roam_x[index] + roam_dx);
        runtime.roam_y[index] = static_cast<std::uint16_t>(
            runtime.roam_y[index] + roam_dy);
    }
}

void advance_rpg_party_formation(SharedState& state,
                                 std::int16_t viewport_screen_dx,
                                 std::int16_t viewport_screen_dy) {
    state.set_u16(0xba, state.actor_direction());
    for (std::size_t slot = 11; slot != 0; --slot) {
        const auto old_direction = state.u16(0xba + slot * 2U);
        state.set_u16(0xba + slot * 2U,
                      state.u16(0xba + (slot - 1U) * 2U));
        auto x = static_cast<std::uint16_t>(
            state.u16(0x12 + slot * 2U) + viewport_screen_dx);
        auto y = static_cast<std::uint16_t>(
            state.u16(0x2a + slot * 2U) + viewport_screen_dy);
        if (old_direction == 0U) {
            y = static_cast<std::uint16_t>(y + 8U);
        } else if (old_direction == 9U) {
            x = static_cast<std::uint16_t>(x + 2U);
        } else if (old_direction == 6U) {
            x = static_cast<std::uint16_t>(x - 2U);
        } else if (old_direction == 3U) {
            y = static_cast<std::uint16_t>(y - 8U);
        }
        state.set_u16(0x12 + slot * 2U, x);
        state.set_u16(0x2a + slot * 2U, y);
        if (old_direction == 0U || old_direction == 9U ||
            old_direction == 6U || old_direction == 3U) {
            state.set_u16(0xa2 + slot * 2U, old_direction);
        }
    }
}

void advance_rpg_party_animation(SharedState& state) {
    const auto slots = std::min<std::size_t>(
        12U, static_cast<std::size_t>(state.u16(0x10) + state.u16(0x102)) * 3U);
    for (std::size_t slot = 0; slot < slots; ++slot) {
        state.set_u16(0x8a + slot * 2U,
                      static_cast<std::uint16_t>(
                          (state.u16(0x8a + slot * 2U) + 1U) & 3U));
    }
}

RpgWorldStepResult advance_rpg_world_step(
    SharedState& state, RpgWorldStepRuntime& runtime,
    std::span<const std::uint8_t> rpg_load_image,
    bool encounters_enabled) {
    RpgWorldStepResult result;
    if (!encounters_enabled) return result;

    ++runtime.poison_steps;
    if (runtime.poison_steps >= 10U) {
        runtime.poison_steps = 0;
        const auto party_count = std::min<std::size_t>(4U, state.u16(0x10));
        for (std::size_t actor = 0; actor < party_count; ++actor) {
            const auto base = 0x106U + actor * 0x9fU;
            const auto status = state.u16(base + 8U);
            if ((status & 0x2000U) != 0U || (status & 0x0200U) == 0U) continue;
            result.poison_flash = true;
            const auto hit_points = static_cast<std::uint16_t>(
                state.u16(base + 0x2dU) - 1U);
            state.set_u16(base + 0x2dU, hit_points);
            if (hit_points == 0U) {
                // 1ff4 overwrites the whole status word, clearing poison and
                // every other field status when the pulse knocks an actor out.
                state.set_u16(base + 8U, 0x2000U);
                result.defeated_party_members.push_back(actor);
            }
        }
    }

    ++runtime.encounter_steps;
    if (runtime.encounter_steps < 40U) return result;

    const auto cursor = state.u16(0x49c);
    const auto random = read_word(rpg_load_image, code_stream_base + cursor);
    state.set_u16(0x49c, static_cast<std::uint16_t>(cursor + 2U));
    if ((random & 4U) == 0U) return result;

    runtime.encounter_steps = static_cast<std::uint8_t>(random & 0x1fU);
    ++runtime.encounter_hits;
    if (runtime.encounter_hits >= 4U) {
        // FIG interprets encounter offset zero as the area/time-selected
        // random table rather than a fixed ORC directory entry.
        state.set_u16(0x4a0, 0U);
        result.random_encounter = true;
    }
    return result;
}

}  // namespace swd2
