#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace swd2 {

struct BattleSessionEvent;
class SpriteArchive;

// Consecutive per-target/per-selector events can belong to one original FIG
// command. The actor setup is shared across a 6b composite, while each nested
// selector still owns one voice/effect phase and multi-target results do not.
[[nodiscard]] bool fig_same_presented_action(
    const BattleSessionEvent& left,
    const BattleSessionEvent& right) noexcept;
[[nodiscard]] bool fig_same_effect_phase(
    const BattleSessionEvent& left,
    const BattleSessionEvent& right) noexcept;

enum class FigVoiceFile {
    sp,
    k1,
    sv3,
};

enum class FigVoiceTiming {
    before_action,
    after_first_pose,
    after_pose,
    after_action,
};

enum class FigMonsterReactionPhase {
    none,
    weapon_frame,
    first_result_frame,
};

// Selectors 01..30 do not share the generic damage-number tail. Their
// handlers either redraw one target around the state commit (5841/58a9),
// redraw all party cards around an atomic group commit (452a/45c7), commit a
// permanent +3 change after only the 5841 preview, or point at a literal RET.
enum class FigSupportPresentation {
    none,
    single_target,
    all_targets,
    commit_without_redraw,
};

struct FigVoiceCue {
    FigVoiceFile file{FigVoiceFile::sp};
    std::uint16_t sp_number{};
    FigVoiceTiming timing{FigVoiceTiming::before_action};
    bool operator==(const FigVoiceCue&) const = default;
};

// FIG keeps horizontal draw coordinates in Mode-X byte columns (one column
// addresses four physical pixels).  Effect handlers either use an absolute
// screen coordinate or add an offset to DS:31e3, the current target anchor.
// Vertical coordinates use normal pixels and are likewise either absolute or
// relative to the target centre stored in the monster runtime record.
enum class FigEffectOrigin {
    screen,
    target,
};

struct FigEffectLayer {
    std::uint16_t resource{};
    std::size_t sprite_frame{};
    FigEffectOrigin horizontal_origin{FigEffectOrigin::target};
    int horizontal{};  // Mode-X byte columns.
    FigEffectOrigin vertical_origin{FigEffectOrigin::screen};
    int vertical{};    // Physical scan lines.
    bool operator==(const FigEffectLayer&) const = default;
};

// One call to FIG's page-present routine.  Most steps contain one sprite, but
// effects 5d and 65 deliberately composite several archives before presenting
// the page.  Keeping that grouping here prevents frontends from serialising
// layers which were simultaneous in the DOS renderer.
struct FigEffectStep {
    std::vector<FigEffectLayer> layers;
    bool operator==(const FigEffectStep&) const = default;
};

struct FigEffectPlacement {
    int left{};
    int top{};
    bool operator==(const FigEffectPlacement&) const = default;
};

struct FigNumberPlacement {
    int mode_x_column{};
    int top{};
    bool operator==(const FigNumberPlacement&) const = default;
};

// FIG 10fc draws summoned-monster action cards at one of the two fixed
// Mode-X positions assigned by 5de4/5d24. The opaque MENU frame begins at
// y=15 and its three-glyph label begins two columns/eight pixels inward.
struct FigSummonedActionCardPlacement {
    int left{};
    int top{};
    int text_left{};
    int text_top{};
    bool operator==(const FigSummonedActionCardPlacement&) const = default;
};

struct FigSummonedNameCardPlacement {
    int mode_x_column{};
    int top{};
    int columns{};
    int rows{};
    int text_left{};
    int text_top{};
    bool operator==(const FigSummonedNameCardPlacement&) const = default;
};

// FIG 2deb/2eb3 places the five persistent monster-status icons in a small
// vertical MENU stack. The first active status creates frame 177's backing
// card at monster-left+4 Mode-X columns; icons begin another four columns to
// the right and advance eight scan lines for each active (not physical) slot.
struct FigMonsterStatusIconPlacement {
    int backing_left{};
    int backing_top{};
    int icon_left{};
    int icon_top{};
    bool operator==(const FigMonsterStatusIconPlacement&) const = default;
};

struct FigPlayerEscapeFailurePlacement {
    int mode_x_column{};
    int top{};
    int columns{};
    int text_mode_x_column{};
    int text_top{};
    bool operator==(const FigPlayerEscapeFailurePlacement&) const = default;
};

// FIG 1000:314d builds four 256-entry colour-translation tables from the
// current battle palette.  Tables 1..3 blend every source colour towards
// palette entry zero by 40/64, 30/64 and 20/64 respectively; table 4 blends
// towards entry 106 by 35/64.  The DOS routine then chooses the closest
// existing DAC entry by Manhattan RGB distance (first entry wins ties).
// Exposing the generated table keeps the original palette-index operation
// reusable by linear, planar, SDL and test frontends.
[[nodiscard]] std::array<std::uint8_t, 256> fig_palette_translation(
    std::span<const std::uint8_t, 768> palette,
    std::uint8_t table_index);

// Core of FIG 1000:771b.  The caller supplies the palette entry to blend
// towards and the 0..64 interpolation numerator.  FIG also uses this routine
// dynamically for MENU frame 154: every non-transparent source colour gets a
// private 35/64 table which is then applied to the portrait below it.
[[nodiscard]] std::array<std::uint8_t, 256>
fig_palette_blend_translation(
    std::span<const std::uint8_t, 768> palette,
    std::uint8_t target_color, std::uint8_t blend);

// Shared FIG/RPG 798b/78c9 special compositor. FE is transparent, mask_color
// shades the destination through table 1 (40/64 towards palette entry zero),
// and all other source pixels copy directly.
void composite_legacy_masked_sprite(
    std::span<std::uint8_t> destination, int width, int height,
    std::span<const std::uint8_t, 768> palette,
    const SpriteArchive& archive, std::size_t sprite_index,
    int left, int top, std::uint8_t mask_color);

// Shared FIG/RPG 799b/78d9 normal palette compositor after 7900/783e has
// assigned every non-FE source colour a private 35/64 771b/7659 table.
void composite_legacy_translucent_sprite(
    std::span<std::uint8_t> destination, int width, int height,
    std::span<const std::uint8_t, 768> palette,
    const SpriteArchive& archive, std::size_t sprite_index,
    int left, int top);

// Literal linear-framebuffer equivalent of FIG 1000:77ef.  The DOS renderer
// transforms a 16-byte-wide Mode-X rectangle (64 physical pixels) for the
// requested number of scan lines.  Pixels outside the supplied surface are
// clipped by the portable frontend; valid in-game calls are wholly in bounds.
void apply_fig_palette_translation(
    std::span<std::uint8_t> pixels, int width, int height,
    std::span<const std::uint8_t, 768> palette,
    int mode_x_column, int top, int mode_x_width, int lines,
    std::uint8_t table_index);

// FIG has two distinct ten-frame result-number paths. 144e right-aligns a
// monster number on its Mode-X centre and moves it up four scan lines per
// frame, starting at centre_y-5. 2ac7 leaves party numbers left-aligned at
// actor*18+12 and moves them up two lines per frame from y=160.
[[nodiscard]] std::array<FigNumberPlacement, 10>
fig_monster_number_timeline(int target_mode_x_center,
                            int target_vertical_center,
                            std::uint16_t value) noexcept;
[[nodiscard]] std::array<FigNumberPlacement, 10>
fig_party_number_timeline(std::size_t party_index) noexcept;

[[nodiscard]] FigSummonedActionCardPlacement
fig_summoned_action_card_placement(std::size_t summon_slot) noexcept;
[[nodiscard]] FigSummonedNameCardPlacement
fig_summoned_name_card_placement(std::size_t summon_slot) noexcept;
[[nodiscard]] std::uint16_t fig_monster_status_icon_frame(
    std::size_t status_slot) noexcept;
[[nodiscard]] FigMonsterStatusIconPlacement
fig_monster_status_icon_placement(int monster_mode_x_left,
                                  std::size_t active_ordinal) noexcept;
[[nodiscard]] FigMonsterReactionPhase fig_monster_reaction_phase(
    const BattleSessionEvent& event) noexcept;

// 59a1 copies one of FIG DATA:2f5b/2f6a/2f79 into DAC entries E0h..E4h
// immediately before 144e.  5ed8 then rotates those five RGB triplets every
// second result page; page_index is zero-based within the ten-page rise.
void apply_fig_monster_result_palette(
    std::array<std::uint8_t, 768>& palette,
    std::uint16_t canonical_target_flags,
    std::size_t page_index) noexcept;
[[nodiscard]] std::optional<std::uint16_t> fig_player_status_bit(
    std::uint16_t effect_code) noexcept;
[[nodiscard]] constexpr std::array<std::size_t, 2>
fig_player_escape_poses() noexcept {
    return {0, 5};
}
// 4338 performs the common player ability/item setup at the actor's base
// frame, then advances DS:31e1 by four before effect voice/dispatch.
[[nodiscard]] constexpr std::array<std::size_t, 2>
fig_player_ability_poses() noexcept {
    return {0, 4};
}
[[nodiscard]] constexpr std::array<std::uint16_t, 3>
fig_player_attack_pose_ticks() noexcept {
    return {3, 1, 1};
}
[[nodiscard]] constexpr std::array<std::uint16_t, 2>
fig_player_ability_pose_ticks() noexcept {
    return {3, 3};
}
[[nodiscard]] constexpr std::uint16_t
fig_player_attack_result_hold_ticks() noexcept {
    return 5;
}
[[nodiscard]] constexpr FigSupportPresentation
fig_support_presentation(std::uint16_t effect_code) noexcept {
    if (effect_code == 0 || effect_code > 0x30 || effect_code == 0x28 ||
        effect_code == 0x29) {
        return FigSupportPresentation::none;
    }
    if ((effect_code >= 0x0c && effect_code <= 0x0e) ||
        effect_code == 0x12) {
        return FigSupportPresentation::all_targets;
    }
    if (effect_code >= 0x22 && effect_code <= 0x27) {
        return FigSupportPresentation::commit_without_redraw;
    }
    return FigSupportPresentation::single_target;
}
[[nodiscard]] constexpr bool
fig_selector_is_immediate_return(std::uint16_t effect_code) noexcept {
    // DS:2bbd[0] is 4497 (RET); entries 28/29 deliberately point at the RETs
    // immediately following handlers 24/27 rather than at new handlers.
    return effect_code == 0 || effect_code == 0x28 || effect_code == 0x29;
}
[[nodiscard]] constexpr bool
fig_effect_leaves_player_status_card(std::uint16_t effect_code) noexcept {
    return effect_code == 0x63 ||
           (effect_code >= 0x66 && effect_code <= 0x69);
}
[[nodiscard]] constexpr std::uint16_t
fig_effect_tail_hold_ticks(std::uint16_t effect_code) noexcept {
    // Learned/item selector 62 waits four ticks after its four SP338 frames
    // and returns with that last frame still visible.
    return effect_code == 0x62 ? 4 : 0;
}
[[nodiscard]] constexpr std::uint16_t
fig_monster_status_damage_tail_hold_ticks() noexcept {
    return 4;
}
[[nodiscard]] constexpr std::uint8_t
fig_monster_ability_flash_color(std::uint16_t target_flags) noexcept {
    switch (target_flags & 0x0007U) {
    case 1: return 0x5c;
    case 2:
    case 5: return 0x81;
    case 3: return 0xaa;
    case 4: return 0x7c;
    default: return 0x8d;
    }
}
[[nodiscard]] constexpr std::uint16_t
fig_monster_ability_flash_steps() noexcept {
    return 8;
}
[[nodiscard]] constexpr std::uint16_t
fig_monster_attack_shake_steps() noexcept {
    return 8;
}
[[nodiscard]] constexpr int
fig_monster_attack_shake_scanlines() noexcept {
    return 3;
}
[[nodiscard]] constexpr std::array<std::uint16_t, 2>
fig_monster_turn_tail_ticks() noexcept {
    return {5, 3};
}
[[nodiscard]] constexpr std::size_t
fig_all_target_slot_span(std::size_t current_target,
                         std::optional<std::size_t> next_target,
                         std::size_t party_count) noexcept {
    const auto end = next_target.value_or(party_count);
    return end > current_target ? end - current_target : 1U;
}
[[nodiscard]] constexpr std::uint16_t
fig_support_pre_commit_ticks(FigSupportPresentation presentation) noexcept {
    switch (presentation) {
    case FigSupportPresentation::single_target:
    case FigSupportPresentation::commit_without_redraw: return 3;
    case FigSupportPresentation::all_targets: return 9;
    case FigSupportPresentation::none: return 0;
    }
    return 0;
}
[[nodiscard]] constexpr std::uint16_t
fig_support_post_commit_ticks(FigSupportPresentation presentation) noexcept {
    return presentation == FigSupportPresentation::single_target ||
                   presentation == FigSupportPresentation::all_targets
               ? 9
               : 0;
}
[[nodiscard]] constexpr FigPlayerEscapeFailurePlacement
fig_player_escape_failure_placement() noexcept {
    return {0x1a, 0x4b, 4, 0x1c, 0x54};
}

// 3c44 reveals a newly composed VGA page in four consecutive 80-byte by
// 50-scanline chunks, with one 70 Hz timer wait after each copy.
[[nodiscard]] constexpr std::array<int, 4>
fig_page_wipe_scanline_ends() noexcept {
    return {50, 100, 150, 200};
}

// Three persistent FIG mediator sprites (MENU 174..176) gate a subset of
// high-level visual/effect handlers. Enemy ability flags summon one when it
// is absent; ability ids 35h/43h/53h dismiss the matching sprite; player
// effects that require an absent mediator enter 58fa's modal failure path.
[[nodiscard]] std::optional<std::size_t> fig_required_medium(
    std::uint16_t effect_code) noexcept;
[[nodiscard]] std::optional<std::size_t> fig_medium_from_target_flags(
    std::uint16_t target_flags) noexcept;
// Player selectors 31h/3bh/3ch directly fly AEh/AFh/B0h into the three
// persistent slots. This is separate from the target-flag prerequisite used
// by 23b1/1048: the selector itself always runs the installation animation.
[[nodiscard]] std::optional<std::size_t> fig_summoned_medium(
    std::uint16_t effect_code) noexcept;
[[nodiscard]] std::optional<std::size_t> fig_player_dismissed_medium(
    std::uint16_t effect_code) noexcept;
[[nodiscard]] std::optional<std::size_t> fig_dismissed_medium(
    std::uint16_t ability_id) noexcept;
[[nodiscard]] FigEffectPlacement fig_medium_placement(
    std::size_t medium) noexcept;

// First SP/ST archive selected by FIG's visual effect dispatcher. The table is
// independent of VGA rendering and can be reused by SDL, console, or mobile
// frontends. Effects without an action archive (status text, escape, etc.)
// return nullopt.
[[nodiscard]] std::optional<std::uint16_t> fig_primary_effect_resource(
    std::uint16_t effect_code) noexcept;

// Exact SP/ST archive load order used by the FIG DS:2bbd dispatcher.  This is
// intentionally a sequence rather than only a root number: several effects
// walk long runs of one-frame archives, and effects 32/34/35/40/41 reload a
// run multiple times.  Empty sequences are non-visual status/flow effects.
[[nodiscard]] std::span<const std::uint16_t> fig_effect_resource_sequence(
    std::uint16_t effect_code) noexcept;

// Numeric SP###.VOC selected by the same dispatcher.  FIG uses SP001 for the
// compact effects 00..30, special shared voices for 31/3b..3f/63, and the
// effect number itself for the remaining presentation handlers.
[[nodiscard]] std::optional<std::uint16_t> fig_effect_voice_resource(
    std::uint16_t effect_code) noexcept;

// FIG 262f has a separate enemy-action voice rule keyed by ability id. Six
// cinematic ids are silent, id 56h uses SP072, compact effects use SP001,
// and the remaining entries use their effect selector directly.
[[nodiscard]] std::optional<std::uint16_t> fig_monster_ability_voice_resource(
    std::uint16_t ability_id, std::uint16_t effect_code) noexcept;

// Exact visual playback schedule for FIG's archive-backed effect handlers
// 32..65.
// An empty result means the handler has no visual frames. Every selector with
// a nonempty resource sequence must have an explicit timeline; the runtime
// deliberately has no generic archive-order fallback. Resource ids and frame
// resets follow the original 49c1/4a82/48da loops, not merely the load order.
[[nodiscard]] std::vector<FigEffectStep> fig_effect_timeline(
    std::uint16_t effect_code);

// Resolve an abstract layer using the values that FIG held in DS:31e3 and in
// the selected monster's DS:3231 runtime slot.  The result is in physical
// 320x200 coordinates suitable for a linear framebuffer.
[[nodiscard]] FigEffectPlacement resolve_fig_effect_placement(
    const FigEffectLayer& layer, int target_mode_x_anchor,
    int target_vertical_center) noexcept;

// Voices emitted by FIG's physical/death/flee paths outside the effect
// dispatcher. Player physical hits select SP003/5/7/9 from fighter identity;
// misses use K1, enemy/ally hits use SP106, party death/capture use SP015,
// and ordinary player/monster/ally flee paths use SV3 (effect 47 does not).
[[nodiscard]] std::vector<FigVoiceCue> fig_non_effect_voice_cues(
    const BattleSessionEvent& event,
    std::optional<std::uint16_t> player_identity = std::nullopt);

}  // namespace swd2
