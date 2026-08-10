#include "swd2/battle_presentation.hpp"

#include "swd2/battle_session.hpp"
#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <list>
#include <stdexcept>

namespace swd2 {

bool fig_same_presented_action(const BattleSessionEvent& left,
                               const BattleSessionEvent& right) noexcept {
    const auto family = [](const BattleSessionEvent& event) {
        switch (event.kind) {
        case BattleEventKind::player_ability: return 1;
        case BattleEventKind::monster_ability: return 2;
        case BattleEventKind::ally_ability: return 3;
        case BattleEventKind::missing_medium:
            return event.source_is_summoned_ally
                       ? 3
                       : (event.source_is_monster ? 2 : 1);
        default: return 0;
        }
    };
    const auto left_family = family(left);
    return left_family != 0 && left_family == family(right) &&
           left.source == right.source &&
           left.monster_generic_path == right.monster_generic_path &&
           left.ability_id == right.ability_id;
}

bool fig_same_effect_phase(const BattleSessionEvent& left,
                           const BattleSessionEvent& right) noexcept {
    return fig_same_presented_action(left, right) &&
           left.kind == right.kind &&
           left.effect_code == right.effect_code &&
           left.kind != BattleEventKind::missing_medium;
}

namespace {

template <std::uint16_t Start, std::size_t Count, std::size_t Repeats = 1>
consteval auto repeated_range() {
    std::array<std::uint16_t, Count * Repeats> result{};
    for (std::size_t repeat = 0; repeat < Repeats; ++repeat) {
        for (std::size_t index = 0; index < Count; ++index) {
            result[repeat * Count + index] =
                static_cast<std::uint16_t>(Start + index);
        }
    }
    return result;
}

template <std::uint16_t First, std::uint16_t Second, std::size_t Repeats>
consteval auto repeated_pair() {
    std::array<std::uint16_t, Repeats * 2> result{};
    for (std::size_t repeat = 0; repeat < Repeats; ++repeat) {
        result[repeat * 2] = First;
        result[repeat * 2 + 1] = Second;
    }
    return result;
}

constexpr auto effect_32 = repeated_range<43, 8, 5>();
constexpr std::array<std::uint16_t, 2> effect_33 = {51, 551};
constexpr auto effect_34 = repeated_pair<52, 552, 5>();
constexpr auto effect_35 = repeated_range<0, 24, 2>();
constexpr std::array<std::uint16_t, 1> effect_36 = {54};
constexpr auto effect_37 = repeated_range<24, 10>();
constexpr std::array<std::uint16_t, 3> effect_38 = {34, 35, 36};
constexpr std::array<std::uint16_t, 2> effect_39 = {37, 38};
constexpr std::array<std::uint16_t, 3> effect_3a = {39, 40, 41};
constexpr auto effect_40 = repeated_range<200, 8, 5>();
constexpr auto effect_41 = repeated_range<208, 12, 2>();
constexpr auto effect_42 = repeated_range<220, 12>();
constexpr auto effect_43 = repeated_range<232, 14>();
constexpr std::array<std::uint16_t, 2> effect_44 = {246, 247};
constexpr auto effect_45 = repeated_range<248, 10>();
constexpr std::array<std::uint16_t, 1> effect_46 = {258};
constexpr auto effect_48 = repeated_range<259, 8>();
constexpr std::array<std::uint16_t, 3> effect_49 = {267, 268, 269};
constexpr std::array<std::uint16_t, 1> effect_4a = {270};
constexpr std::array<std::uint16_t, 2> effect_4b = {271, 272};
constexpr auto effect_4c = repeated_range<273, 10>();
constexpr std::array<std::uint16_t, 1> effect_4d = {283};
constexpr std::array<std::uint16_t, 1> effect_4e = {284};
constexpr std::array<std::uint16_t, 1> effect_4f = {285};
constexpr std::array<std::uint16_t, 1> effect_50 = {286};
constexpr std::array<std::uint16_t, 1> effect_51 = {287};
constexpr std::array<std::uint16_t, 1> effect_52 = {288};
constexpr auto effect_53 = repeated_range<289, 6>();
constexpr std::array<std::uint16_t, 2> effect_54 = {295, 296};
constexpr std::array<std::uint16_t, 3> effect_55 = {297, 298, 299};
constexpr std::array<std::uint16_t, 1> effect_56 = {300};
constexpr std::array<std::uint16_t, 1> effect_57 = {301};
constexpr auto effect_58 = repeated_range<302, 8>();
constexpr std::array<std::uint16_t, 2> effect_59 = {310, 311};
constexpr std::array<std::uint16_t, 3> effect_5a = {312, 313, 314};
constexpr std::array<std::uint16_t, 1> effect_5b = {315};
constexpr std::array<std::uint16_t, 3> effect_5c = {316, 317, 318};
constexpr auto effect_5d = repeated_range<319, 13>();
constexpr std::array<std::uint16_t, 1> effect_5e = {332};
constexpr std::array<std::uint16_t, 2> effect_5f = {333, 334};
constexpr std::array<std::uint16_t, 1> effect_60 = {335};
constexpr std::array<std::uint16_t, 2> effect_61 = {336, 337};
constexpr std::array<std::uint16_t, 1> effect_62 = {338};
constexpr std::array<std::uint16_t, 1> effect_63 = {339};
constexpr std::array<std::uint16_t, 3> effect_64 = {340, 341, 342};
constexpr auto effect_65 = repeated_range<345, 64>();

FigEffectLayer target_layer(std::uint16_t resource, std::size_t frame,
                            int x, int y, bool relative_y = false) {
    return {
        resource,
        frame,
        FigEffectOrigin::target,
        x,
        relative_y ? FigEffectOrigin::target : FigEffectOrigin::screen,
        y,
    };
}

FigEffectLayer screen_layer(std::uint16_t resource, std::size_t frame,
                            int x, int y) {
    return {
        resource, frame, FigEffectOrigin::screen, x,
        FigEffectOrigin::screen, y,
    };
}

void append_frames(std::vector<FigEffectStep>& result, std::uint16_t resource,
                   std::size_t count, int x, int y,
                   bool relative_y = false) {
    for (std::size_t frame = 0; frame < count; ++frame) {
        result.push_back({{{target_layer(resource, frame, x, y, relative_y)}}});
    }
}

void append_screen_frames(std::vector<FigEffectStep>& result,
                          std::uint16_t resource, std::size_t count,
                          int x, int y) {
    for (std::size_t frame = 0; frame < count; ++frame) {
        result.push_back({{{screen_layer(resource, frame, x, y)}}});
    }
}

}  // namespace

std::array<std::uint8_t, 256> fig_palette_translation(
    std::span<const std::uint8_t, 768> palette,
    std::uint8_t table_index) {
    struct Configuration {
        std::uint8_t target;
        int blend;
    };
    static constexpr std::array<Configuration, 5> configurations = {{
        {0, 0}, {0, 40}, {0, 30}, {0, 20}, {106, 35},
    }};
    if (table_index == 0 || table_index >= configurations.size()) {
        throw std::invalid_argument("FIG palette translation table must be 1..4");
    }
    const auto configuration = configurations[table_index];
    return fig_palette_blend_translation(
        palette, configuration.target,
        static_cast<std::uint8_t>(configuration.blend));
}

std::array<std::uint8_t, 256> fig_palette_blend_translation(
    std::span<const std::uint8_t, 768> palette,
    std::uint8_t target_color, std::uint8_t blend) {
    if (blend > 64U) {
        throw std::invalid_argument("FIG palette blend must be 0..64");
    }
    const auto target_offset = static_cast<std::size_t>(target_color) * 3U;

    const auto arithmetic_shift_six = [](int value) {
        // 8086 SAR rounds negative values towards minus infinity, whereas
        // signed C++ division truncates towards zero.
        return value >= 0 ? value / 64 : -((-value + 63) / 64);
    };

    std::array<std::uint8_t, 256> result{};
    for (std::size_t source = 0; source < result.size(); ++source) {
        std::array<int, 3> blended{};
        for (std::size_t component = 0; component < blended.size(); ++component) {
            const auto original = static_cast<int>(palette[source * 3U + component]);
            const auto target = static_cast<int>(palette[target_offset + component]);
            blended[component] = original + arithmetic_shift_six(
                (target - original) * static_cast<int>(blend));
        }

        auto closest = std::uint8_t{};
        auto closest_distance = 0xff;
        for (std::size_t candidate = 0; candidate < result.size(); ++candidate) {
            auto distance = 0;
            for (std::size_t component = 0; component < blended.size(); ++component) {
                distance += std::abs(
                    static_cast<int>(palette[candidate * 3U + component]) -
                    blended[component]);
            }
            // FIG uses CMP distance,DL / JNC, so equal distances keep the
            // lower palette index encountered first.
            if (distance < closest_distance) {
                closest_distance = distance;
                closest = static_cast<std::uint8_t>(candidate);
            }
        }
        result[source] = closest;
    }
    return result;
}

namespace {

const std::array<std::uint8_t, 256>& cached_palette_blend(
    std::span<const std::uint8_t, 768> palette,
    std::uint8_t target_color, std::uint8_t blend) {
    struct Entry {
        std::array<std::uint8_t, 768> palette;
        std::uint8_t target_color{};
        std::uint8_t blend{};
        std::array<std::uint8_t, 256> translation;
    };
    // Battle/map palettes are immutable while their UI is active. A
    // thread-local cache avoids rebuilding the original 256x256 nearest-
    // colour search on every page recomposition.
    static thread_local std::list<Entry> cache;
    const auto found = std::find_if(
        cache.begin(), cache.end(), [&](const Entry& entry) {
            return entry.target_color == target_color &&
                   entry.blend == blend &&
                   std::equal(entry.palette.begin(), entry.palette.end(),
                              palette.begin());
        });
    if (found != cache.end()) return found->translation;
    Entry entry;
    std::copy(palette.begin(), palette.end(), entry.palette.begin());
    entry.target_color = target_color;
    entry.blend = blend;
    entry.translation = fig_palette_blend_translation(
        palette, target_color, blend);
    cache.push_back(std::move(entry));
    return cache.back().translation;
}

void validate_compositor_surface(std::span<std::uint8_t> destination,
                                 int width, int height) {
    if (width <= 0 || height <= 0 ||
        destination.size() != static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height)) {
        throw std::invalid_argument("legacy palette compositor surface is invalid");
    }
}

}  // namespace

void composite_legacy_masked_sprite(
    std::span<std::uint8_t> destination, int width, int height,
    std::span<const std::uint8_t, 768> palette,
    const SpriteArchive& archive, std::size_t sprite_index,
    int left, int top, std::uint8_t mask_color) {
    validate_compositor_surface(destination, width, height);
    const auto& sprite = archive.sprites().at(sprite_index);
    const auto source_pixels = archive.pixels(sprite_index);
    const auto& shade = cached_palette_blend(palette, 0, 40);
    for (std::size_t y = 0; y < sprite.height; ++y) {
        for (std::size_t x = 0; x < sprite.width; ++x) {
            const auto destination_x = left + static_cast<int>(x);
            const auto destination_y = top + static_cast<int>(y);
            if (destination_x < 0 || destination_x >= width ||
                destination_y < 0 || destination_y >= height) {
                continue;
            }
            const auto source = source_pixels[y * sprite.width + x];
            if (source == 0xfeU) continue;
            auto& pixel =
                destination[static_cast<std::size_t>(destination_y) *
                                static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(destination_x)];
            pixel = source == mask_color ? shade[pixel] : source;
        }
    }
}

void composite_legacy_translucent_sprite(
    std::span<std::uint8_t> destination, int width, int height,
    std::span<const std::uint8_t, 768> palette,
    const SpriteArchive& archive, std::size_t sprite_index,
    int left, int top) {
    validate_compositor_surface(destination, width, height);
    const auto& sprite = archive.sprites().at(sprite_index);
    const auto source_pixels = archive.pixels(sprite_index);
    std::array<const std::array<std::uint8_t, 256>*, 256> tables{};
    for (std::size_t y = 0; y < sprite.height; ++y) {
        for (std::size_t x = 0; x < sprite.width; ++x) {
            const auto destination_x = left + static_cast<int>(x);
            const auto destination_y = top + static_cast<int>(y);
            if (destination_x < 0 || destination_x >= width ||
                destination_y < 0 || destination_y >= height) {
                continue;
            }
            const auto source = source_pixels[y * sprite.width + x];
            if (source == 0xfeU) continue;
            if (tables[source] == nullptr) {
                tables[source] = &cached_palette_blend(palette, source, 35);
            }
            auto& pixel =
                destination[static_cast<std::size_t>(destination_y) *
                                static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(destination_x)];
            pixel = (*tables[source])[pixel];
        }
    }
}

void apply_fig_palette_translation(
    std::span<std::uint8_t> pixels, int width, int height,
    std::span<const std::uint8_t, 768> palette,
    int mode_x_column, int top, int mode_x_width, int lines,
    std::uint8_t table_index) {
    if (width <= 0 || height <= 0 ||
        pixels.size() < static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height)) {
        throw std::invalid_argument("FIG palette translation surface is invalid");
    }
    if (mode_x_width <= 0 || lines <= 0) return;
    const auto translation = fig_palette_translation(palette, table_index);
    const auto left = mode_x_column * 4;
    const auto right = left + mode_x_width * 4;
    for (auto y = std::max(top, 0); y < std::min(top + lines, height); ++y) {
        for (auto x = std::max(left, 0); x < std::min(right, width); ++x) {
            auto& pixel = pixels[static_cast<std::size_t>(y) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)];
            pixel = translation[pixel];
        }
    }
}

std::array<FigNumberPlacement, 10> fig_monster_number_timeline(
    int target_mode_x_center, int target_vertical_center,
    std::uint16_t value) noexcept {
    auto digit_count = 1;
    auto remaining = value;
    while (remaining >= 10U && digit_count < 5) {
        remaining = static_cast<std::uint16_t>(remaining / 10U);
        ++digit_count;
    }
    const auto left = target_mode_x_center - (digit_count - 1) * 2;
    std::array<FigNumberPlacement, 10> result{};
    for (std::size_t frame = 0; frame < result.size(); ++frame) {
        result[frame] = {
            left,
            target_vertical_center - 5 - static_cast<int>(frame) * 4,
        };
    }
    return result;
}

std::array<FigNumberPlacement, 10> fig_party_number_timeline(
    std::size_t party_index) noexcept {
    std::array<FigNumberPlacement, 10> result{};
    const auto left = 12 + static_cast<int>(party_index) * 18;
    for (std::size_t frame = 0; frame < result.size(); ++frame) {
        result[frame] = {left, 160 - static_cast<int>(frame) * 2};
    }
    return result;
}

FigSummonedActionCardPlacement fig_summoned_action_card_placement(
    std::size_t summon_slot) noexcept {
    // The first summon is installed with AX=2 and the second with AX=16h.
    // Both are Mode-X columns, while the portable renderer uses pixels.
    const auto mode_x_column = summon_slot == 0 ? 2 : 0x16;
    return {
        mode_x_column * 4,
        15,
        (mode_x_column + 2) * 4,
        24,
    };
}

FigSummonedNameCardPlacement fig_summoned_name_card_placement(
    std::size_t summon_slot) noexcept {
    const auto column = summon_slot == 0 ? 0 : 0x14;
    return {column, 0, 4, 1, (column + 2) * 4, 9};
}

std::uint16_t fig_monster_status_icon_frame(
    std::size_t status_slot) noexcept {
    // 2deb checks DS:33fd/3411/3425/3439/344d in this order and passes the
    // following literal MENU frame ids to 2eb3.
    static constexpr std::array<std::uint16_t, 5> frames = {
        0xa1, 0xa4, 0x9e, 0xa5, 0x9f,
    };
    return status_slot < frames.size() ? frames[status_slot] : 0;
}

FigMonsterStatusIconPlacement fig_monster_status_icon_placement(
    int monster_mode_x_left, std::size_t active_ordinal) noexcept {
    return {
        (monster_mode_x_left + 4) * 4,
        0,
        (monster_mode_x_left + 8) * 4,
        4 + static_cast<int>(active_ordinal) * 8,
    };
}

FigMonsterReactionPhase fig_monster_reaction_phase(
    const BattleSessionEvent& event) noexcept {
    if (!event.target_is_monster) return FigMonsterReactionPhase::none;
    if (event.kind == BattleEventKind::player_attack) {
        return event.damage != 0 ? FigMonsterReactionPhase::weapon_frame
                                 : FigMonsterReactionPhase::none;
    }
    if (event.kind == BattleEventKind::status_damage) {
        // FIG sets +3114 before drawing random(first_actor_level*2), even when
        // that draw is zero and 144e therefore floats a literal zero.
        return FigMonsterReactionPhase::first_result_frame;
    }
    return event.damage != 0 ? FigMonsterReactionPhase::first_result_frame
                             : FigMonsterReactionPhase::none;
}

void apply_fig_monster_result_palette(
    std::array<std::uint8_t, 768>& palette,
    std::uint16_t canonical_target_flags,
    std::size_t page_index) noexcept {
    static constexpr std::array<std::uint8_t, 15> fire = {
        63, 26, 1, 59, 28, 17, 63, 50, 35,
        59, 35, 12, 60, 16, 7,
    };
    static constexpr std::array<std::uint8_t, 15> water = {
        63, 63, 63, 33, 52, 59, 23, 39, 53,
        14, 25, 48, 7, 12, 43,
    };
    static constexpr std::array<std::uint8_t, 15> neutral = {
        44, 44, 44, 51, 51, 51, 58, 58, 58,
        44, 44, 44, 35, 35, 35,
    };
    const auto selector = canonical_target_flags & 7U;
    const auto& ramp = selector == 1U
                           ? fire
                           : (selector == 2U || selector == 5U ? water
                                                               : neutral);
    constexpr auto colors = std::size_t{5};
    // Runtime word 42b2 starts at interval two.  144e calls 5ed8 after every
    // flip, so the visible stable pages are baseline, shift 1 twice, shift 2
    // twice, ... and finally baseline again on page ten.
    const auto shift = ((page_index + 1U) / 2U) % colors;
    constexpr auto first_color = std::size_t{0xe0};
    for (std::size_t destination = 0; destination < colors; ++destination) {
        const auto source = (destination + colors - shift) % colors;
        std::copy_n(ramp.begin() + static_cast<std::ptrdiff_t>(source * 3U),
                    3, palette.begin() + static_cast<std::ptrdiff_t>(
                           (first_color + destination) * 3U));
    }
}

std::optional<std::uint16_t> fig_player_status_bit(
    std::uint16_t effect_code) noexcept {
    switch (effect_code) {
    case 0x5e: return 0x0020;
    case 0x64: return 0x0002;
    case 0x5f: return 0x0004;
    case 0x65: return 0x0080;
    default: return std::nullopt;
    }
}

std::optional<std::size_t> fig_required_medium(
    std::uint16_t effect_code) noexcept {
    switch (effect_code) {
    case 0x32:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x40:
        return 0;
    case 0x37:
    case 0x43:
        return 1;
    case 0x45:
    case 0x48:
        return 2;
    default:
        return std::nullopt;
    }
}

std::optional<std::size_t> fig_medium_from_target_flags(
    std::uint16_t target_flags) noexcept {
    // 23b1 tests the low record byte in strict 80h,40h,20h priority order.
    if ((target_flags & 0x0080U) != 0) return 0;
    if ((target_flags & 0x0040U) != 0) return 1;
    if ((target_flags & 0x0020U) != 0) return 2;
    return std::nullopt;
}

std::optional<std::size_t> fig_dismissed_medium(
    std::uint16_t ability_id) noexcept {
    switch (ability_id) {
    case 0x35: return 0;
    case 0x43: return 1;
    case 0x53: return 2;
    default: return std::nullopt;
    }
}

FigEffectPlacement fig_medium_placement(std::size_t medium) noexcept {
    static constexpr std::array<int, 3> columns = {0x4a, 0x44, 0x3e};
    const auto index = std::min<std::size_t>(medium, columns.size() - 1U);
    return {columns[index] * 4, 1};
}

std::span<const std::uint16_t> fig_effect_resource_sequence(
    std::uint16_t effect) noexcept {
    switch (effect) {
    case 0x32: return effect_32;
    case 0x33: return effect_33;
    case 0x34: return effect_34;
    case 0x35: return effect_35;
    case 0x36: return effect_36;
    case 0x37: return effect_37;
    case 0x38: return effect_38;
    case 0x39: return effect_39;
    case 0x3a: return effect_3a;
    case 0x40: return effect_40;
    case 0x41: return effect_41;
    case 0x42: return effect_42;
    case 0x43: return effect_43;
    case 0x44: return effect_44;
    case 0x45: return effect_45;
    case 0x46: return effect_46;
    case 0x48: return effect_48;
    case 0x49: return effect_49;
    case 0x4a: return effect_4a;
    case 0x4b: return effect_4b;
    case 0x4c: return effect_4c;
    case 0x4d: return effect_4d;
    case 0x4e: return effect_4e;
    case 0x4f: return effect_4f;
    case 0x50: return effect_50;
    case 0x51: return effect_51;
    case 0x52: return effect_52;
    case 0x53: return effect_53;
    case 0x54: return effect_54;
    case 0x55: return effect_55;
    case 0x56: return effect_56;
    case 0x57: return effect_57;
    case 0x58: return effect_58;
    case 0x59: return effect_59;
    case 0x5a: return effect_5a;
    case 0x5b: return effect_5b;
    case 0x5c: return effect_5c;
    case 0x5d: return effect_5d;
    case 0x5e: return effect_5e;
    case 0x5f: return effect_5f;
    case 0x60: return effect_60;
    case 0x61: return effect_61;
    case 0x62: return effect_62;
    case 0x63: return effect_63;
    case 0x64: return effect_64;
    case 0x65: return effect_65;
    default: return {};
    }
}

std::optional<std::uint16_t> fig_primary_effect_resource(
    std::uint16_t effect) noexcept {
    const auto sequence = fig_effect_resource_sequence(effect);
    if (sequence.empty()) return std::nullopt;
    return sequence.front();
}

std::optional<std::uint16_t> fig_effect_voice_resource(
    std::uint16_t effect) noexcept {
    if (effect <= 0x30) return 1;
    if (effect == 0x31 || effect == 0x3b || effect == 0x3c) return 49;
    if (effect == 0x3d || effect == 0x3e || effect == 0x3f) return 61;
    if (effect == 0x63) return 72;
    if (effect <= 0x69) return effect;
    return std::nullopt;
}

std::optional<std::uint16_t> fig_monster_ability_voice_resource(
    std::uint16_t ability_id, std::uint16_t effect_code) noexcept {
    switch (ability_id) {
    case 0x33:
    case 0x35:
    case 0x42:
    case 0x43:
    case 0x52:
    case 0x53:
        return std::nullopt;
    case 0x56:
        return 0x48;
    default:
        return effect_code <= 0x30 ? std::optional<std::uint16_t>{1}
                                   : std::optional<std::uint16_t>{effect_code};
    }
}

std::vector<FigEffectStep> fig_effect_timeline(std::uint16_t effect) {
    std::vector<FigEffectStep> result;
    switch (effect) {
    case 0x32:
        // Helper 48da draws the two halves 100 scan lines apart before one
        // page flip; the whole eight-archive run is reloaded five times.
        for (int repeat = 0; repeat < 5; ++repeat) {
            for (std::uint16_t resource = 43; resource <= 50; resource += 2) {
                result.push_back({{
                    screen_layer(resource, 0, 0, 0),
                    screen_layer(static_cast<std::uint16_t>(resource + 1),
                                 0, 0, 100),
                }});
            }
        }
        break;
    case 0x33: {
        // Signed cumulative paths from FIG DS:300c/3026 and 3016/3030.
        static constexpr std::array<int, 5> first_x = {-5, -1, -3, 4, -2};
        static constexpr std::array<int, 5> first_y = {-40, -12, 0, 10, -8};
        static constexpr std::array<int, 8> second_x = {5, 2, 1, 1, -6, 0, 1, 4};
        static constexpr std::array<int, 8> second_y = {
            -16, -5, -2, -1, -16, -6, -29, -16,
        };
        auto x = 0;
        auto y = 150;
        for (std::size_t frame = 0; frame < first_x.size(); ++frame) {
            x += first_x[frame];
            y += first_y[frame];
            result.push_back({{{target_layer(51, frame, x, y)}}});
        }
        for (std::size_t frame = 0; frame < second_x.size(); ++frame) {
            x += second_x[frame];
            y += second_y[frame];
            result.push_back({{{target_layer(551, frame, x, y)}}});
        }
        break;
    }
    case 0x34:
        for (int repeat = 0; repeat < 5; ++repeat) {
            append_frames(result, 52, 2, -7, 0);
            append_frames(result, 552, 2, -7, 0);
        }
        break;
    case 0x35:
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (std::uint16_t resource = 0; resource <= 23; resource += 2) {
                result.push_back({{
                    screen_layer(resource, 0, 0, 0),
                    screen_layer(static_cast<std::uint16_t>(resource + 1),
                                 0, 0, 100),
                }});
            }
        }
        break;
    case 0x36: {
        static constexpr std::array<int, 4> delta_x = {0, -1, -2, -3};
        static constexpr std::array<int, 4> delta_y = {0, 31, 37, 44};
        auto x = 0;
        auto y = 0;
        for (std::size_t frame = 0; frame < delta_x.size(); ++frame) {
            x += delta_x[frame];
            y += delta_y[frame];
            result.push_back({{{target_layer(54, frame, x, y)}}});
        }
        break;
    }
    case 0x37:
        for (std::uint16_t resource = 24; resource <= 29; ++resource) {
            append_screen_frames(result, resource, 1, 2, 50);
        }
        for (std::uint16_t resource = 30; resource <= 33; ++resource) {
            append_screen_frames(result, resource, 1, 0, 50);
        }
        break;
    case 0x38:
        append_frames(result, 34, 4, -3, 10);
        append_frames(result, 35, 4, -3, 10);
        append_frames(result, 36, 1, -3, 10);
        break;
    case 0x39:
        append_frames(result, 37, 6, -3, 10);
        append_frames(result, 38, 5, -3, 10);
        break;
    case 0x3a: {
        append_frames(result, 39, 2, -3, 10);
        append_frames(result, 40, 2, -3, 10);
        static constexpr std::array<int, 3> delta_x = {-2, -5, -2};
        static constexpr std::array<int, 3> delta_y = {0, -21, -11};
        auto x = -3;
        auto y = 10;
        for (std::size_t frame = 0; frame < delta_x.size(); ++frame) {
            x += delta_x[frame];
            y += delta_y[frame];
            result.push_back({{{target_layer(41, frame, x, y)}}});
        }
        break;
    }
    case 0x40:
        for (int repeat = 0; repeat < 5; ++repeat) {
            for (std::uint16_t resource = 200; resource <= 207; resource += 2) {
                result.push_back({{
                    screen_layer(resource, 0, 4, 0),
                    screen_layer(static_cast<std::uint16_t>(resource + 1),
                                 0, 4, 100),
                }});
            }
        }
        break;
    case 0x41:
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (std::uint16_t resource = 208; resource <= 219; resource += 2) {
                result.push_back({{
                    target_layer(resource, 0, -38, -100, true),
                    target_layer(static_cast<std::uint16_t>(resource + 1),
                                 0, -38, 0, true),
                }});
            }
        }
        break;
    case 0x42:
        // Resources 220..227 are a travelling chain.  The final four
        // archives each draw both frames before flipping the VGA page.
        for (const auto& layer : std::array<FigEffectLayer, 12>{
                 screen_layer(220, 0, 16, 10),
                 screen_layer(220, 1, 20, 56),
                 screen_layer(220, 2, 26, 88),
                 screen_layer(220, 3, 36, 72),
                 screen_layer(221, 0, 33, 96),
                 screen_layer(221, 1, 27, 92),
                 screen_layer(221, 2, 21, 74),
                 screen_layer(222, 0, 21, 57),
                 screen_layer(222, 1, 18, 58),
                 screen_layer(223, 0, 13, 51),
                 screen_layer(224, 0, 8, 54),
                 screen_layer(225, 0, 3, 57),
             }) {
            result.push_back({{{layer}}});
        }
        result.push_back({{
            screen_layer(228, 0, 4, 53), screen_layer(228, 1, 44, 58),
        }});
        result.push_back({{
            screen_layer(229, 0, 4, 52), screen_layer(229, 1, 44, 55),
        }});
        result.push_back({{
            screen_layer(230, 0, 3, 59), screen_layer(230, 1, 52, 59),
        }});
        result.push_back({{
            screen_layer(231, 0, 5, 62), screen_layer(231, 1, 60, 62),
        }});
        break;
    case 0x43: {
        static constexpr std::array<int, 14> delta_x = {
            0, 12, -3, 3, 3, 2, -3, -6, -5, -6, -11, 0, -1, 0,
        };
        static constexpr std::array<int, 14> delta_y = {
            0, 25, 34, 17, 2, -10, 7, -16, -13, 13, -16, -13, 0, 0,
        };
        auto x = 19;
        auto y = 11;
        for (std::size_t index = 0; index < delta_x.size(); ++index) {
            x += delta_x[index];
            y += delta_y[index];
            result.push_back({{{screen_layer(
                static_cast<std::uint16_t>(232 + index), 0, x, y)}}});
        }
        break;
    }
    case 0x44: {
        auto y = 2;
        static constexpr std::array<int, 5> initial_delta_y = {0, 13, 13, 13, 13};
        for (const auto delta : initial_delta_y) {
            y += delta;
            result.push_back({{{target_layer(246, 0, -6, y)}}});
        }
        for (std::size_t frame = 1; frame <= 3; ++frame) {
            result.push_back({{{target_layer(246, frame, -6, y)}}});
        }
        result.push_back({{{target_layer(247, 0, -6, y)}}});
        result.push_back({{{target_layer(247, 1, -6, y)}}});
        break;
    }
    case 0x45:
        for (std::uint16_t resource = 248; resource <= 257; ++resource) {
            append_screen_frames(result, resource, 1, 0, 70);
        }
        break;
    case 0x46:
        append_frames(result, 258, 10, -6, -30, true);
        append_frames(result, 258, 10, -6, -30, true);
        break;
    case 0x48:
        for (std::uint16_t resource = 259; resource <= 266; ++resource) {
            append_frames(result, resource, 1, -13, 20);
        }
        break;
    case 0x49:
        append_frames(result, 267, 4, -10, 20);
        append_frames(result, 268, 3, -10, 20);
        append_frames(result, 269, 3, -10, 20);
        break;
    case 0x4a:
        append_frames(result, 270, 4, -7, 20);
        append_frames(result, 270, 4, -7, 20);
        break;
    case 0x4b:
        append_frames(result, 271, 5, -2, 0);
        append_frames(result, 272, 2, -2, 0);
        break;
    case 0x4c:
        for (std::uint16_t resource = 273; resource <= 282; ++resource) {
            append_frames(result, resource, 1, -18, 0);
        }
        break;
    case 0x4d:
        append_frames(result, 283, 3, -3, -20, true);
        append_frames(result, 283, 3, -3, -20, true);
        break;
    case 0x4e:
        append_frames(result, 284, 4, -6, -40, true);
        append_frames(result, 284, 4, -6, -40, true);
        break;
    case 0x4f:
        append_frames(result, 285, 4, -10, -40, true);
        append_frames(result, 285, 4, -10, -40, true);
        break;
    case 0x50:
        for (int repeat = 0; repeat < 4; ++repeat) {
            append_frames(result, 286, 2, -8, -50, true);
        }
        break;
    case 0x51:
        append_frames(result, 287, 5, -4, -40, true);
        append_frames(result, 287, 5, -4, -40, true);
        break;
    case 0x52:
        append_frames(result, 288, 5, -8, -40, true);
        append_frames(result, 288, 5, -8, -40, true);
        break;
    case 0x53:
        for (std::uint16_t resource = 289; resource <= 294; ++resource) {
            append_frames(result, resource, 1, -17, 0);
        }
        break;
    case 0x54:
        append_frames(result, 295, 5, -12, -40, true);
        append_frames(result, 296, 3, -12, -40, true);
        break;
    case 0x55:
        append_frames(result, 297, 2, -17, 0);
        append_frames(result, 298, 2, -17, 0);
        append_frames(result, 299, 2, -17, 0);
        break;
    case 0x56:
        append_frames(result, 300, 5, -7, -40, true);
        append_frames(result, 300, 5, -7, -40, true);
        break;
    case 0x57:
        append_frames(result, 301, 5, -7, -40, true);
        append_frames(result, 301, 5, -7, -40, true);
        break;
    case 0x58:
        for (std::uint16_t resource = 302; resource <= 309; ++resource) {
            append_frames(result, resource, 1, -17, 0);
        }
        break;
    case 0x59:
        append_frames(result, 310, 6, -5, 0);
        append_frames(result, 311, 6, -5, 0);
        break;
    case 0x5a:
        append_frames(result, 312, 3, -8, 75);
        append_frames(result, 313, 3, -4, 0);
        append_frames(result, 314, 2, -4, 0);
        break;
    case 0x5b: {
        // FIG DS:30bc/30c6 are signed cumulative x/y deltas consumed by
        // helper 494f before each of SP315's five frames.
        static constexpr std::array<int, 5> delta_x = {0, -3, -4, -8, -24};
        static constexpr std::array<int, 5> delta_y = {0, 14, 42, 36, 2};
        auto x = 38;
        auto y = 0;
        for (std::size_t frame = 0; frame < delta_x.size(); ++frame) {
            x += delta_x[frame];
            y += delta_y[frame];
            append_screen_frames(result, 315, 1, x, y);
            result.back().layers.front().sprite_frame = frame;
        }
        break;
    }
    case 0x5c:
        append_frames(result, 316, 3, -6, 75);
        append_frames(result, 317, 5, -2, 0);
        append_frames(result, 318, 2, -2, 0);
        break;
    case 0x5d:
        // SP319's first three frames are drawn onto the same back page and
        // become visible together after one present call.
        result.push_back({{
            target_layer(319, 0, -25, 4),
            target_layer(319, 1, -25, 94),
            target_layer(319, 2, -25, 139),
        }});
        for (std::uint16_t resource = 320; resource <= 331; ++resource) {
            append_frames(result, resource, 1, -21, 20);
        }
        break;
    case 0x5e:
        append_frames(result, 332, 8, -8, -10, true);
        break;
    case 0x5f:
        append_frames(result, 333, 5, -4, -80, true);
        append_frames(result, 334, 5, -4, -80, true);
        break;
    case 0x60:
        append_frames(result, 335, 12, -4, -40, true);
        break;
    case 0x61:
        append_frames(result, 336, 5, -7, -40, true);
        append_frames(result, 337, 3, -7, -40, true);
        break;
    case 0x62:
        append_frames(result, 338, 4, 2, 130);
        break;
    case 0x63:
        append_frames(result, 339, 4, -2, 185);
        append_frames(result, 339, 4, -2, 185);
        break;
    case 0x64:
        append_frames(result, 340, 6, -2, -70, true);
        append_frames(result, 341, 11, -2, -70, true);
        append_frames(result, 342, 3, -2, -70, true);
        break;
    case 0x65:
        for (std::uint16_t resource = 345; resource <= 408; resource += 2) {
            result.push_back({{
                target_layer(resource, 0, -35, 0),
                target_layer(static_cast<std::uint16_t>(resource + 1),
                             0, -35, 100),
            }});
        }
        break;
    default:
        break;
    }
    return result;
}

FigEffectPlacement resolve_fig_effect_placement(
    const FigEffectLayer& layer, int target_mode_x_anchor,
    int target_vertical_center) noexcept {
    const auto mode_x = layer.horizontal +
                        (layer.horizontal_origin == FigEffectOrigin::target
                             ? target_mode_x_anchor
                             : 0);
    const auto top = layer.vertical +
                     (layer.vertical_origin == FigEffectOrigin::target
                          ? target_vertical_center
                          : 0);
    return {mode_x * 4, top};
}

std::vector<FigVoiceCue> fig_non_effect_voice_cues(
    const BattleSessionEvent& event,
    std::optional<std::uint16_t> player_identity) {
    std::vector<FigVoiceCue> result;
    switch (event.kind) {
    case BattleEventKind::player_attack: {
        // 13e8 plays the actor's identity voice before checking immunity,
        // evasion, or final damage. A critical adds SP004 after the pose.
        const auto identity = player_identity.value_or(0);
        result.push_back({
            FigVoiceFile::sp,
            static_cast<std::uint16_t>((identity >> 1U) / 3U + 3U),
            FigVoiceTiming::after_first_pose,
        });
        if (event.critical) {
            result.push_back({FigVoiceFile::sp, 4,
                              FigVoiceTiming::after_pose});
        }
        // Only 12d5's physical-immunity/insufficient-attack path calls the
        // K1 direct sample. 130d's successful evasion card does not.
        if (event.damage == 0 && !event.evaded) {
            result.push_back({FigVoiceFile::k1, 0,
                              FigVoiceTiming::after_action});
        }
        break;
    }
    case BattleEventKind::monster_attack:
        if (event.damage != 0) {
            result.push_back({FigVoiceFile::sp, 106,
                              FigVoiceTiming::before_action});
        }
        break;
    case BattleEventKind::ally_attack:
        // Captured ally 0fce calls the SP106 path before rolling attack,
        // defense, or evasion. Enemy 2a65 reaches it only after a hit.
        result.push_back({FigVoiceFile::sp, 106,
                          FigVoiceTiming::before_action});
        break;
    case BattleEventKind::monster_fled:
    case BattleEventKind::ally_fled:
        result.push_back({FigVoiceFile::sv3, 0,
                          FigVoiceTiming::before_action});
        break;
    case BattleEventKind::player_escaped:
        // Only 09b8's ordinary command path calls the direct SV3 sample.
        // Dispatcher selector 47 instead calls 5b37(47), i.e. SP071.VOC,
        // after the common actor-pose setup and then unwinds the stack.
        if (event.ability_id == 0) {
            result.push_back({FigVoiceFile::sv3, 0,
                              FigVoiceTiming::after_pose});
        }
        break;
    case BattleEventKind::monster_captured:
    case BattleEventKind::capture_failed:
        // 0ded always announces the pot with SP016 before its DATA:2e5d
        // card. A successful capture sets HP to zero and later reaches the
        // common defeated-target SP015 cue below.
        result.push_back({FigVoiceFile::sp, 16,
                          FigVoiceTiming::before_action});
        break;
    default:
        break;
    }
    // SP015 belongs to party-member death handlers 25cb/2aa1, plus the
    // successful capture path 0e84. Ordinary monster death after 144e does
    // not call 5b37(15).
    if (event.defeated &&
        (!event.target_is_monster ||
         event.kind == BattleEventKind::monster_captured)) {
        result.push_back({FigVoiceFile::sp, 15,
                          FigVoiceTiming::after_action});
    }
    return result;
}

}  // namespace swd2
