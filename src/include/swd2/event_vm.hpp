#pragma once

#include "swd2/launcher.hpp"
#include "swd2/map_database.hpp"
#include "swd2/script_archive.hpp"
#include "swd2/shared_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace swd2 {

enum class InventoryUiResult {
    cancelled,
    empty_slot,
    occupied_slot,
    map_reload,
};

enum class InventoryUiMode {
    general,
    sell,
};

class EventVmHost {
public:
    virtual ~EventVmHost() = default;
    virtual void show_dialogue(std::uint16_t opcode,
                               std::span<const std::uint8_t> big5_text) = 0;
    virtual void delay(std::uint16_t ticks) = 0;
    // Presentation-only opcodes are delegated instead of silently discarded.
    // A headless host may return false to make the VM stop explicitly.
    virtual bool present_event_command(std::uint16_t,
                                       std::span<const std::uint16_t>) {
        return false;
    }
    // Opcode 53 stores two mode-X coordinates followed by inline Big5 text.
    // Unlike dialogue it draws in-place, waits three DOS hundredths, and does
    // not wait for input.
    virtual bool show_positioned_text(std::uint16_t, std::uint16_t,
                                      std::span<const std::uint8_t>) {
        return false;
    }
    // Opcode 19's variable list is the shop inventory. The host owns input
    // and presentation while the portable implementation mutates SAVE state.
    virtual bool run_shop(std::span<const std::uint16_t>, SharedState&) {
        return false;
    }
    // Opcode 13 branches to its argument only when the inventory UI returns
    // an empty selection. nullopt means this host has no inventory UI.
    virtual std::optional<InventoryUiResult> run_inventory(SharedState&) {
        return std::nullopt;
    }
    virtual std::optional<InventoryUiResult> run_inventory(SharedState& state,
                                                           InventoryUiMode) {
        return run_inventory(state);
    }
    // Opcode 37 replaces RPG.EXE's transient current-area arrays immediately;
    // later commands in the same event render and mutate the destination.
    virtual void map_relocated(const MapAreaRecord&) {}
};

enum class EventVmStatus {
    completed,
    unsupported_opcode,
    instruction_limit,
};

struct EventVmResult {
    EventVmStatus status{EventVmStatus::completed};
    std::size_t commands_executed{};
    std::uint16_t last_opcode{0xffff};
    // RPG.EXE returned to SWD2.EXE when opcodes 28/48 requested FIG.EXE.
    // The rewrite exposes that transition without launching a child process.
    Marker requested_marker{Marker::none};
    // Map-changing commands are reloaded by the in-process RPG module rather
    // than terminating and re-executing RPG.EXE.
    bool requested_map_reload{};
    // Transient current-area state after opcode 37. Direct entity operations
    // after relocation must survive the portable module's resource-loop
    // restart, but remain separate from persistent opcode-34 MAPZ writes.
    std::optional<MapAreaRecord> relocated_area;
};

// Executes the stateful, platform-independent subset of RPG.EXE's 62-opcode
// event VM. Event targets are byte offsets into ScriptArchive's pointer table,
// matching the values stored by MAPA/MAPZ. Unsupported presentation-heavy
// commands stop explicitly rather than being silently ignored.
EventVmResult execute_event(const ScriptArchive& archive, std::uint16_t directory_offset,
                            SharedState& state, MapAreaRecord* area,
                            std::size_t current_entity, EventVmHost& host,
                            std::size_t instruction_limit = 10'000,
                            MapDatabase* map_database = nullptr);

}  // namespace swd2
