#include "swd2/event_vm.hpp"

#include "swd2/event_program.hpp"
#include "swd2/rpg_entity_system.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace swd2 {

namespace {

bool flag_is_set(const SharedState& state, std::uint16_t flag) {
    const auto group = static_cast<std::size_t>(flag >> 4U);
    const auto bit = static_cast<unsigned>(flag & 15U);
    return (state.u16(0x4a2 + group * 2) & (0x8000U >> bit)) != 0;
}

void set_flag(SharedState& state, std::uint16_t flag) {
    const auto group = static_cast<std::size_t>(flag >> 4U);
    const auto bit = static_cast<unsigned>(flag & 15U);
    const auto offset = 0x4a2 + group * 2;
    state.set_u16(offset, static_cast<std::uint16_t>(state.u16(offset) | (0x8000U >> bit)));
}

std::optional<std::size_t> entity_index(const MapAreaRecord& area,
                                        std::uint16_t byte_offset) {
    if ((byte_offset & 1U) != 0) {
        throw std::runtime_error("event references an unaligned entity offset");
    }
    const auto index = static_cast<std::size_t>(byte_offset / 2U);
    if (index >= area.entity_count()) {
        // The DOS arrays reserve 100 words apiece even though 10fd copies and
        // renders only the current area's declared count. CHNA0 entry 370
        // (directory target 740) deliberately runs generic choreography over
        // byte offsets through 22 while its map declares five entities. Those
        // extra BSS writes are outside dd6's visible entity loop. Ignore their
        // typed-area projection rather than treating valid released data as a
        // fatal vector bounds error.
        return std::nullopt;
    }
    return index;
}

void restore_party(SharedState& state) {
    const auto count = std::min<std::size_t>(state.u16(0x10), 4);
    for (std::size_t index = 0; index < count; ++index) {
        const auto base = 0x106 + index * 0x9f;
        state.set_u16(base + 8, 0);
        state.set_u16(base + 0x2d, state.u16(base + 0x2f));
        state.set_u16(base + 0x35, state.u16(base + 0x37));
        state.set_u16(base + 0x55, state.u16(base + 0x57));
    }
}

void request_battle(SharedState& state, EventVmResult& result,
                    std::uint16_t encounter_offset) {
    state.set_u16(0x4a0, encounter_offset);
    result.requested_marker = Marker::open_figure;
}

void compact_inventory(SharedState& state) {
    std::size_t output = 0;
    for (std::size_t input = 0; input < 50; ++input) {
        const auto value = state.u16(0x382 + input * 2U);
        if (value == 0) continue;
        state.set_u16(0x382 + output++ * 2U, value);
    }
    while (output < 50) {
        state.set_u16(0x382 + output++ * 2U, 0);
    }
}

void move_scripted_actor(SharedState& state, std::uint16_t opcode,
                         std::uint16_t steps) {
    const auto direction = opcode == 30 ? 3U : opcode == 31 ? 0U :
                           opcode == 32 ? 6U : 9U;
    state.set_actor_direction(static_cast<std::uint16_t>(direction));
    for (std::uint16_t step = 0; step < steps; ++step) {
        auto screen_x = state.actor_screen_x();
        auto screen_y = state.actor_screen_y();
        auto viewport_x = state.viewport_x();
        auto viewport_y = state.viewport_y();
        const auto previous_viewport_x = viewport_x;
        const auto previous_viewport_y = viewport_y;
        auto viewport_cell = state.u16(0x40d);
        if (opcode == 30) {  // north
            if (screen_y == 0x50 && viewport_y != 0) {
                --viewport_y;
                viewport_cell = static_cast<std::uint16_t>(
                    viewport_cell - state.map_width() * 2U);
            } else if (screen_y != 0) {
                screen_y = static_cast<std::uint16_t>(screen_y - 8U);
            }
        } else if (opcode == 31) {  // south
            if (screen_y == 0x50 &&
                viewport_y + state.viewport_rows() < state.map_height()) {
                ++viewport_y;
                viewport_cell = static_cast<std::uint16_t>(
                    viewport_cell + state.map_width() * 2U);
            } else if (screen_y != 0xb0) {
                screen_y = static_cast<std::uint16_t>(screen_y + 8U);
            }
        } else if (opcode == 32) {  // west
            if (screen_x == 0x26 && viewport_x != 0) {
                --viewport_x;
                viewport_cell = static_cast<std::uint16_t>(viewport_cell - 2U);
            } else if (screen_x != 0) {
                screen_x = static_cast<std::uint16_t>(screen_x - 2U);
            }
        } else {  // east
            if (screen_x == 0x26 &&
                viewport_x + state.viewport_columns() < state.map_width()) {
                ++viewport_x;
                viewport_cell = static_cast<std::uint16_t>(viewport_cell + 2U);
            } else if (screen_x != 0x4a) {
                screen_x = static_cast<std::uint16_t>(screen_x + 2U);
            }
        }
        state.set_actor_screen_x(screen_x);
        state.set_actor_screen_y(screen_y);
        state.set_viewport_x(viewport_x);
        state.set_viewport_y(viewport_y);
        state.set_u16(0x40d, viewport_cell);
        advance_rpg_party_animation(state);
        advance_rpg_party_formation(
            state,
            static_cast<std::int16_t>(
                (static_cast<int>(previous_viewport_x) -
                 static_cast<int>(viewport_x)) * 2),
            static_cast<std::int16_t>(
                (static_cast<int>(previous_viewport_y) -
                 static_cast<int>(viewport_y)) * 8));
    }
}

}  // namespace

EventVmResult execute_event(const ScriptArchive& archive, std::uint16_t directory_offset,
                            SharedState& state, MapAreaRecord* area,
                            std::size_t current_entity, EventVmHost& host,
                            std::size_t instruction_limit,
                            MapDatabase* map_database) {
    EventVmResult result;
    const auto host_failure_status = [&host] {
        return host.abort_requested() ? EventVmStatus::host_abort
                                      : EventVmStatus::unsupported_opcode;
    };
    auto target = directory_offset;
    while (true) {
        if ((target & 1U) != 0 || target / 2 >= archive.entry_count()) {
            throw std::runtime_error("event branch target is outside the archive directory");
        }
        const auto record = decode_event_record(archive.event_stream(target / 2));
        bool branched = false;
        for (const auto& command : record.commands) {
            if (result.commands_executed == instruction_limit) {
                result.status = EventVmStatus::instruction_limit;
                return result;
            }
            ++result.commands_executed;
            result.last_opcode = command.opcode;
            const auto arg = [&](std::size_t index) { return command.arguments.at(index); };
            switch (command.opcode) {
            case 0:
            case 18:
            case 20:
            case 46:
                host.show_dialogue(command.opcode, command.text);
                break;
            case 1:
                if (area && current_entity < area->entity_count()) {
                    area->entity_fields[3][current_entity] = 3;
                }
                break;
            case 2:
                if (area && current_entity < area->entity_count()) {
                    area->entity_fields[9][current_entity] = arg(0);
                }
                break;
            case 3:
                if (area && current_entity < area->entity_count() && arg(0) < 11) {
                    area->entity_fields[arg(0)][current_entity] = arg(1);
                    if (map_database) {
                        // RPG 53ef opens MAPZ, addresses the current area's
                        // structure-of-arrays word as
                        // area+6+field*count*2+entity_byte_offset, then calls
                        // 7cb6 to rewrite the database. Keep the transient
                        // area copy in sync above, but also patch the source
                        // image used by later map loads and save slots.
                        map_database->mutate_area_word(
                            state.map_location_directory_offset(), arg(0),
                            static_cast<std::int16_t>(current_entity * 2U),
                            arg(1), false);
                    }
                }
                break;
            case 4:
                if (flag_is_set(state, arg(0))) {
                    target = arg(1);
                    branched = true;
                }
                break;
            case 5:
            case 6:
            case 7:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 8:
                host.delay(arg(0));
                break;
            case 9:
                set_flag(state, arg(0));
                break;
            case 10: {
                // RPG.EXE implements this as opcode 11 followed by a clamp to
                // the adjacent maximum word.  The first argument is a byte
                // offset within the first party member's 0x9f-byte record.
                const auto offset = static_cast<std::size_t>(0x106 + arg(0));
                const auto sum = static_cast<unsigned>(state.u16(offset)) + arg(1);
                state.set_u16(offset, static_cast<std::uint16_t>(
                                          std::min<unsigned>(sum, 0xffffU)));
                state.set_u16(offset, std::min(state.u16(offset), state.u16(offset + 2)));
                break;
            }
            case 11: {
                const auto offset = static_cast<std::size_t>(0x106 + arg(0));
                const auto sum = static_cast<unsigned>(state.u16(offset)) + arg(1);
                state.set_u16(offset, static_cast<std::uint16_t>(
                                          std::min<unsigned>(sum, 0xffffU)));
                break;
            }
            case 12:
                restore_party(state);
                break;
            case 13: {
                const auto accepted = host.confirm_event_branch(state);
                if (!accepted) {
                    result.status = host_failure_status();
                    return result;
                }
                if (*accepted) {
                    target = arg(0);
                    branched = true;
                }
                break;
            }
            case 14:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 15:
                if (arg(0) <= state.u16(0x104)) {
                    state.set_u16(0x104, static_cast<std::uint16_t>(state.u16(0x104) - arg(0)));
                    target = arg(1);
                    branched = true;
                }
                break;
            case 16:
                state.set_u16(0x40d, arg(0));
                state.set_viewport_x(arg(1));
                state.set_viewport_y(arg(2));
                for (std::size_t i = 0; i < 12; ++i) {
                    state.set_u16(0x12 + i * 2, arg(3));
                    state.set_u16(0x2a + i * 2, arg(4));
                    state.set_u16(0xba + i * 2, 7);
                    state.set_u16(0xa2 + i * 2, arg(5));
                }
                break;
            case 17: {
                if (command.arguments.empty()) {
                    result.status = EventVmStatus::unsupported_opcode;
                    return result;
                }
                const auto completed = host.run_combined_shop(
                    std::span<const std::uint16_t>(command.arguments).subspan(1),
                    state);
                if (!completed) {
                    result.status = host_failure_status();
                    return result;
                }
                if (!*completed) break;
                // RPG.EXE reloads the current entity's event after leaving
                // the combined sell/buy UI. Command 2 commonly changes this
                // field immediately before opcode 17. Directory offset zero
                // is the valid empty first CHNA entry, not a null pointer.
                if (area && current_entity < area->entity_count()) {
                    target = area->entity_fields[9][current_entity];
                    branched = true;
                }
                break;
            }
            case 19:
                if (command.arguments.empty()) {
                    result.status = EventVmStatus::unsupported_opcode;
                    return result;
                }
                if (!host.run_shop(
                        std::span<const std::uint16_t>(command.arguments).subspan(1),
                        state)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 21: {
                bool present = false;
                const auto party_count = std::min<std::size_t>(state.u16(0x10), 4);
                for (std::size_t i = 0; i < party_count; ++i) {
                    present = present || state.u16(0x72 + i * 6) == arg(0);
                }
                if (present) {
                    target = arg(1);
                    branched = true;
                }
                break;
            }
            case 22:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 23:
            case 24:
            case 25:
            case 26:
                if (area) {
                    const auto index = entity_index(*area, arg(0));
                    if (!index) break;
                    auto& cell = area->entity_fields[2][*index];
                    auto& direction = area->entity_fields[1][*index];
                    if (command.opcode == 23) {
                        cell = static_cast<std::uint16_t>(cell - state.map_width() * 2U);
                        direction = 3;
                    } else if (command.opcode == 24) {
                        cell = static_cast<std::uint16_t>(cell + state.map_width() * 2U);
                        direction = 0;
                    } else if (command.opcode == 25) {
                        cell = static_cast<std::uint16_t>(cell - 2U);
                        direction = 6;
                    } else {
                        cell = static_cast<std::uint16_t>(cell + 2U);
                        direction = 9;
                    }
                    area->entity_fields[10][*index] =
                        static_cast<std::uint16_t>(
                            (area->entity_fields[10][*index] + 1U) & 3U);
                }
                break;
            case 27:
                if (area) {
                    const auto index = entity_index(*area, arg(0));
                    if (index) area->entity_fields[3][*index] = arg(1);
                }
                break;
            case 28:
                request_battle(state, result, arg(0));
                if (!host.present_battle_transition(command.opcode)) {
                    result.status = host_failure_status();
                }
                return result;
            case 29:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 30:
            case 31:
            case 32:
            case 33:
                // RPG.EXE renders, waits one video tick and advances palette
                // effects after every individual scripted step (5c86..5d3e),
                // not once after the whole command.
                for (std::uint16_t step = 0; step < arg(0); ++step) {
                    move_scripted_actor(state, command.opcode, 1);
                    constexpr std::array<std::uint16_t, 1> one_step{1};
                    if (!host.present_event_command(command.opcode, one_step)) {
                        result.status = host_failure_status();
                        return result;
                    }
                }
                break;
            case 34: {
                if (!map_database) {
                    result.status = EventVmStatus::unsupported_opcode;
                    return result;
                }
                std::size_t cursor = 0;
                while (cursor < command.arguments.size()) {
                    const auto location_offset = command.arguments[cursor++];
                    if (location_offset == 0xf800) break;
                    if (cursor + 3 > command.arguments.size()) {
                        throw std::runtime_error("decoded event MAPZ mutation is truncated");
                    }
                    const auto field = command.arguments[cursor++];
                    const auto byte_offset =
                        static_cast<std::int16_t>(command.arguments[cursor++]);
                    const auto operation = command.arguments[cursor++];
                    const auto additive = operation == 0x4144;
                    auto value = operation;
                    if (additive) {
                        if (cursor == command.arguments.size()) {
                            throw std::runtime_error("decoded additive MAPZ mutation is truncated");
                        }
                        value = command.arguments[cursor++];
                    }
                    map_database->mutate_area_word(location_offset, field, byte_offset,
                                                   value, additive);
                }
                break;
            }
            case 35:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 36:
                // Cycles the loaded RAP layout pointer. The DOS renderer sets
                // the viewport cell pointer back to the layout base for every
                // frame. RPG 5a85..5a98 renders with the current SAVE+0x411
                // selector and advances it only after the page has been
                // presented; incrementing first skips frame zero and leaves
                // every scripted sequence one frame ahead.
                for (std::uint16_t i = 0; i < arg(0); ++i) {
                    state.set_u16(0x40d, state.u16(0x40f));
                    constexpr std::array<std::uint16_t, 1> one_frame{1};
                    if (!host.present_event_command(command.opcode, one_frame)) {
                        result.status = host_failure_status();
                        return result;
                    }
                    state.set_u16(0x411,
                                  static_cast<std::uint16_t>(state.u16(0x411) + 1U));
                }
                break;
            case 37: {
                if (!map_database) {
                    result.status = EventVmStatus::unsupported_opcode;
                    return result;
                }
                const auto position_only = (arg(0) & 0x8000U) != 0U;
                // 5aa7 clears the old music path before calling edc for both
                // forms. This forces a full destination load to compare
                // unequal. edc's 8000h form sets SAVE+426 and 10fd returns at
                // 1160 without copying a replacement, so its observable
                // result is deliberately an empty SAVE+459 string even while
                // the already-playing RIX continues.
                state.set_u8(0x459, 0);
                install_map_location(state, *map_database, arg(0));
                if (position_only) break;
                result.relocated_area = map_database->location_at_directory_offset(
                    static_cast<std::uint16_t>(arg(0) & 0x1fffU)).area;
                area = &*result.relocated_area;
                host.map_relocated(*area);
                result.requested_map_reload = true;
                break;
            }
            case 38:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 39:
                if (area) {
                    const auto index = entity_index(*area, arg(0));
                    if (index) area->entity_fields[0][*index] = arg(1);
                }
                // RPG 5ac7 rebuilds the off-screen map, waits the event frame
                // interval and flips pages after changing the entity sprite.
                // This is what makes the many 39/8 story sequences animate;
                // it is not merely a state mutation awaiting a later opcode22.
                if (!host.present_event_command(command.opcode,
                                                command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 40: {
                // Conditional inventory replacement.  RPG.EXE searches fifty
                // 16-bit slots at SAVE+0x382.  Missing items branch to arg1;
                // a replacement value of 0xf800 means "test only".
                bool found = false;
                for (std::size_t i = 0; i < 50; ++i) {
                    const auto offset = 0x382 + i * 2;
                    if (state.u16(offset) != arg(0)) continue;
                    found = true;
                    if (arg(2) != 0xf800) {
                        state.set_u16(offset, arg(2));
                        // 5b18 calls 3ced even for nonzero replacements. It
                        // becomes observable when a shipped removal writes
                        // zero (CHNA2 item 75 and CHNA5 item 257).
                        compact_inventory(state);
                    }
                    break;
                }
                if (!found) {
                    target = arg(1);
                    branched = true;
                }
                break;
            }
            case 41: {
                const auto before = state.u16(0x104);
                const auto sum = static_cast<unsigned>(before) + arg(0);
                state.set_u16(0x104, static_cast<std::uint16_t>(
                                          std::min<unsigned>(sum, 0xffffU)));
                break;
            }
            case 42:
                // Change the inventory/equipment page and discard the
                // temporary battle-only item range (ids >= 314). RPG 5b34
                // then calls 3ced, whose repeated left shifts are equivalent
                // to a stable compaction of all fifty physical words.
                state.set_u8(0x3f1, static_cast<std::uint8_t>(arg(0)));
                for (std::size_t i = 0; i < 50; ++i) {
                    const auto offset = 0x382 + i * 2;
                    if (state.u16(offset) >= 0x013a) state.set_u16(offset, 0);
                }
                compact_inventory(state);
                break;
            case 43:
            case 44:
            case 45:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 47:
                state.set_u16(0x10, std::min<std::uint16_t>(arg(0), 4));
                restore_party(state);
                for (std::size_t i = state.u16(0x10); i < 4; ++i) {
                    state.set_u16(0x106 + i * 0x9f + 8, 0x2000);
                }
                break;
            case 48:
                state.set_u16(0x51c, arg(0));
                request_battle(state, result, arg(1));
                if (!host.present_battle_transition(command.opcode)) {
                    result.status = host_failure_status();
                }
                return result;
            case 49:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 50:
                for (std::size_t i = 0; i < 0x9f; ++i) {
                    const auto first = state.u8(0x244 + i);
                    state.set_u8(0x244 + i, state.u8(0x2e3 + i));
                    state.set_u8(0x2e3 + i, first);
                }
                {
                    const auto first = state.u16(0x7e);
                    state.set_u16(0x7e, state.u16(0x84));
                    state.set_u16(0x84, first);
                }
                break;
            case 51:
                for (std::size_t i = 0; i < 12; ++i) {
                    state.set_u16(0xa2 + i * 2, arg(0));
                }
                break;
            case 52:
                // RPG:5bfc calls 01c9, whose final instruction is DOS
                // INT 21h/AH=4ch. Unlike opcode 37 this does not install or
                // reload a map; the child exits without a transfer marker.
                result.requested_program_exit = true;
                return result;
            case 53:
                if (!host.show_positioned_text(arg(0), arg(1), command.text)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 54: {
                auto offset = static_cast<std::size_t>(0x106 + arg(0));
                for (std::size_t i = 0; i < 50; ++i, ++offset) {
                    // RPG:5c2a uses a word CMP while advancing DI by one
                    // byte. A slot is free only when this byte and its
                    // successor are both zero; 5c32 then stores AL only.
                    if (state.u16(offset) == 0) {
                        state.set_u8(offset, static_cast<std::uint8_t>(arg(1)));
                        break;
                    }
                }
                break;
            }
            case 55:
            case 56:
            case 57:
                if (!host.present_event_command(command.opcode, command.arguments)) {
                    result.status = host_failure_status();
                    return result;
                }
                break;
            case 58:
                request_battle(state, result, arg(0));
                if (!host.present_battle_transition(command.opcode)) {
                    result.status = host_failure_status();
                }
                return result;
            case 59:
            case 60:
                state.set_u16(0x51c, arg(0));
                request_battle(state, result, arg(1));
                if (!host.present_battle_transition(command.opcode)) {
                    result.status = host_failure_status();
                }
                return result;
            case 61:
                // One-off party/story transformation reproduced literally
                // from RPG.EXE's final dispatch-table handler.
                state.set_u8(0x11a, 0xa4);
                state.set_u8(0x11c, 0xa4);
                state.set_u8(0x132, 1);
                state.set_u8(0x112, static_cast<std::uint8_t>(state.u8(0x112) - 0x5bU));
                state.set_u8(0x163, static_cast<std::uint8_t>(state.u8(0x163) + 0x0fU));
                state.set_u8(0x114, static_cast<std::uint8_t>(state.u8(0x114) + 0x0fU));
                break;
            default:
                result.status = EventVmStatus::unsupported_opcode;
                return result;
            }
            if (host.abort_requested()) {
                result.status = EventVmStatus::host_abort;
                return result;
            }
            if (branched) break;
        }
        if (!branched) return result;
    }
}

}  // namespace swd2
