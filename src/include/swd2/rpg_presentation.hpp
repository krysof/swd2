#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "swd2/platform.hpp"

namespace swd2 {

class SpriteArchive;

// Reproduces the VGA mode-X address-base displacement used by RPG event
// opcode 49. Each VGA byte addresses four pixels through separate planes, so
// adding one address byte moves a pixel four positions (with scanline carry),
// not one packed pixel. Bytes outside the shifted render retain the previous
// framebuffer contents, just as they did in A000 memory.
std::vector<std::uint8_t> composite_mode_x_address_offset(
    std::span<const std::uint8_t> previous,
    std::span<const std::uint8_t> rendered,
    std::uint16_t address_bytes,
    std::size_t width = 320);

// RPG.EXE event opcode 55 is not CD audio: 5c35 calls two far helpers at
// 0dbf:0306/0314. The first snapshots the 768-byte VGA palette; the second
// visits all four mode-X planes and replaces only indices 10h..1fh with the
// inverse-luminance ramp `1fh - ((R + 2*G + B) >> 4)`. Other indices are
// preserved byte-for-byte.
void apply_rpg_event_monochrome_filter(
    std::span<std::uint8_t> pixels,
    std::span<const std::uint8_t, 768> palette);

// Extracts a $$-terminated string from the data segment selected by the
// executable's entry-point `mov ax,DATA; mov ds,ax` prologue.  The returned
// byte stream excludes the terminator and deliberately stays in its original
// Big5 encoding for LegacyFont rendering.
std::vector<std::uint8_t> extract_rpg_embedded_text(
    std::span<const std::uint8_t> load_image,
    std::size_t entry_offset,
    std::uint16_t data_offset);

std::vector<std::uint8_t> extract_rpg_embedded_data(
    std::span<const std::uint8_t> load_image,
    std::size_t entry_offset,
    std::uint16_t data_offset,
    std::size_t size);

// RPG.EXE and FIG.EXE share MENU frames 83..93 for their generic selector
// panel. Coordinates are the original mode-X byte columns (four pixels each).
void draw_rpg_selector_panel(
    std::span<std::uint8_t> surface,
    std::size_t width,
    std::size_t height,
    const SpriteArchive& menu_sprites,
    int left,
    int top,
    int columns,
    int rows);

// RPG.EXE:2781 uses a second MENU-frame family for compact information
// panels.  Frames 80..82 form the top row, 168..170 the repeated 16-line
// body, and 171..173 the bottom.  Unlike the generic selector these pieces
// are copied opaquely.
void draw_rpg_compact_panel(
    std::span<std::uint8_t> surface,
    std::size_t width,
    std::size_t height,
    const SpriteArchive& menu_sprites,
    int left,
    int top,
    int columns,
    int rows);

enum class RpgSaveSelectorResult {
    waiting,
    committed,
    cancelled,
    quit,
};

// Exact input state of RPG.EXE:4e4b/46cc. Slot numbers are zero-based here,
// matching AX on return from 4e4b (the save writer later adds ASCII '1').
class RpgSaveSlotSelector {
public:
    [[nodiscard]] std::uint8_t slot() const noexcept { return slot_; }
    [[nodiscard]] bool confirming() const noexcept { return confirming_; }
    [[nodiscard]] std::uint8_t confirmation_choice() const noexcept {
        return confirmation_choice_;
    }
    RpgSaveSelectorResult input(InputAction action) noexcept;

private:
    std::uint8_t slot_{};
    bool confirming_{};
    std::uint8_t confirmation_choice_{};
};

struct RpgListSelection {
    std::size_t selected{};
    std::size_t first_visible{};
    enum class ScrollCue { none, toward_start, toward_end } scroll_cue{};
};

// Exact viewport/row behavior of RPG.EXE:298d. Up/Down move the cursor until
// an edge and then scroll one entry. PgUp/PgDn/Home/End change only the first
// visible entry, retaining the cursor's row within the panel.
[[nodiscard]] RpgListSelection rpg_list_selection_input(
    RpgListSelection selection, std::size_t item_count,
    std::size_t visible_rows, InputAction action) noexcept;

// RPG.EXE:2907 scrollbar. Frames 94/95 and 97/98 are the normal/pressed
// endpoint pairs, frame 96 is the repeated track, and frame 99 is positioned
// proportionally from top+20 (not on top of the upper endpoint).
void draw_rpg_selector_scrollbar(
    std::span<std::uint8_t> surface,
    std::size_t width, std::size_t height,
    const SpriteArchive& menu_sprites,
    int left, int top, int columns, std::size_t rows,
    std::size_t maximum_first, std::size_t first,
    RpgListSelection::ScrollCue cue = RpgListSelection::ScrollCue::none);

// RPG.EXE:2fa5 maps the four direction keys directly to the four portrait
// positions rather than cycling a cursor: Left/Right/Up/Down => actor 0/1/2/3.
// A direction whose actor is absent is masked and produces no selection.
std::optional<std::size_t> rpg_party_target_for_direction(
    InputAction action,
    std::size_t party_count) noexcept;

// RPG.EXE:2634 calls the shared 21de/772d palette translator after drawing
// the diamond portrait page.  Every non-selected 12-byte x 51-line card is
// mapped through table 3; the selected card remains at its original palette.
// This is the only visual cursor on the target/recipient page.
void apply_rpg_party_target_highlight(
    std::span<std::uint8_t> pixels,
    int width, int height,
    std::span<const std::uint8_t, 768> palette,
    std::size_t selected_actor, std::size_t party_count);

// RPG.EXE:46cc and 54d5 use the same 21de/772d operation for their Yes/No
// cards.  Each option occupies 16 Mode-X bytes by 32 scanlines; the selected
// option stays bright while the other is translated through table 3.
void apply_rpg_binary_choice_highlight(
    std::span<std::uint8_t> pixels,
    int width, int height,
    std::span<const std::uint8_t, 768> palette,
    int first_mode_x_column, int second_mode_x_column, int top,
    std::size_t selected_choice);

}  // namespace swd2
