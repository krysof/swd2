#pragma once

#include "swd2/map_database.hpp"
#include "swd2/map_resource.hpp"
#include "swd2/shared_state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace swd2 {

// Transient structure-of-arrays state used by RPG.EXE:506d. It deliberately
// lives outside MAPZ: changing maps reloads entity records, while these fixed
// BSS arrays and the code-stream cursor survived inside one RPG process.
struct RpgEntityRuntime {
    std::vector<std::uint16_t> delay_remaining;
    std::vector<std::uint16_t> roam_x;
    std::vector<std::uint16_t> roam_y;
    std::uint16_t code_stream_offset{};
};

// Advances every autonomous map entity by one original main-loop iteration.
// RPG.EXE used words from its own code bytes at load-image offset 4f1ch as a
// deterministic movement stream, so the caller supplies that load image as
// inert data. No x86 code is executed.
void advance_rpg_entities(MapAreaRecord& area, const MapResource& map,
                          const SharedState& state, RpgEntityRuntime& runtime,
                          std::span<const std::uint8_t> rpg_load_image);

// RPG:1f42 shifts the twelve formation slots after every successful leader
// step.  Slots 0/3/6/9 are the four rendered party members; the intervening
// slots retain two direction/position delays so the party follows corners.
void advance_rpg_party_formation(SharedState& state,
                                 std::int16_t viewport_screen_dx = 0,
                                 std::int16_t viewport_screen_dy = 0);

// RPG:1e63 advances all three formation slots belonging to each active party
// member whenever a direction key is handled, including a blocked step.
void advance_rpg_party_animation(SharedState& state);

}  // namespace swd2
