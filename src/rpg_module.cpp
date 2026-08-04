#include "swd2/rpg_module.hpp"

#include "swd2/asset_catalog.hpp"
#include "swd2/battle_presentation.hpp"
#include "swd2/dialogue.hpp"
#include "swd2/event_vm.hpp"
#include "swd2/field_action_system.hpp"
#include "swd2/inventory_system.hpp"
#include "swd2/item_database.hpp"
#include "swd2/map_resource.hpp"
#include "swd2/map_database.hpp"
#include "swd2/map_transition_database.hpp"
#include "swd2/legacy_font.hpp"
#include "swd2/mz_executable.hpp"
#include "swd2/rsk_decoder.hpp"
#include "swd2/planar_sprite_set.hpp"
#include "swd2/rpg_entity_system.hpp"
#include "swd2/rpg_presentation.hpp"
#include "swd2/script_archive.hpp"
#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <iterator>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace swd2 {

namespace {

struct Viewport {
    std::vector<std::uint8_t> pixels;
    std::array<std::uint8_t, 768> palette{};
};

// RPG stores these menu cursors in DATA rather than on the stack. 37fd/37ff
// retain the normal inventory row/window across openings, while 35e6 retains
// the last directional item-action choice for the whole RPG invocation.
struct RpgMenuRuntime {
    std::size_t inventory_selected{};
    std::size_t inventory_first_visible{};
    std::size_t item_action_choice{};
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("RPG module cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void install_map_actor_profile(SharedState& state, bool uses_man1) {
    const auto x_offset = static_cast<std::uint16_t>(uses_man1 ? 1 : 0);
    for (std::size_t actor = 0; actor < 12; ++actor) {
        state.set_u16(0x42 + actor * 2U, x_offset);
    }
    const std::array<std::int16_t, 4> y_offsets = uses_man1
        ? std::array<std::int16_t, 4>{-4, -2, -3, -4}
        : std::array<std::int16_t, 4>{-15, -11, -15, -16};
    for (std::size_t group = 0; group < y_offsets.size(); ++group) {
        for (std::size_t actor = 0; actor < 3; ++actor) {
            state.set_u16(0x5a + group * 6U + actor * 2U,
                          static_cast<std::uint16_t>(y_offsets[group]));
        }
    }
}

SpriteArchive load_default_map_actors(const std::filesystem::path& game_root,
                                      SharedState& state) {
    // RPG:e94 calls 13b4 for area bit 8000h and 136c otherwise.  Besides
    // choosing MAN1/BMAN1, those routines install the twelve horizontal and
    // four groups of vertical sprite offsets used by 506d.
    const auto uses_man1 = (state.u16(0x408) & 0x8000U) != 0U;
    install_map_actor_profile(state, uses_man1);
    return SpriteArchive::parse(decode_rsk_block(read_file(
        game_root / (uses_man1 ? "MAN1.RSK" : "BMAN1.RSK"))).data);
}

std::vector<std::vector<std::uint8_t>> load_rpg_ability_descriptions(
    const std::filesystem::path& path) {
    const auto executable = dos::MzExecutable::load(path);
    const auto file = read_file(path);
    const auto image_start = static_cast<std::size_t>(executable.header_size());
    const auto image_size = static_cast<std::size_t>(executable.load_image_size());
    if (image_start > file.size() || image_size > file.size() - image_start ||
        image_size < 6U) {
        throw std::runtime_error("DATE2.EXE load image is truncated");
    }
    const auto image = std::span<const std::uint8_t>(
        file.data() + image_start, image_size);
    const auto u16 = [&](std::size_t offset) {
        if (offset + 2U > image.size()) {
            throw std::runtime_error("DATE2.EXE pointer is truncated");
        }
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(image[offset]) |
            (static_cast<std::uint16_t>(image[offset + 1U]) << 8U));
    };

    // RPG:2f1a indexes the directory at id*2+4.  The word at +2 is the
    // first description offset, so it also gives the exact player-record
    // count: DATE2 contains descriptions 0..113, not the enemy-only tail of
    // the 151-entry ability table.
    const auto first_description = static_cast<std::size_t>(u16(2));
    if (first_description < 4U || (first_description - 4U) % 2U != 0U ||
        first_description > image.size()) {
        throw std::runtime_error("DATE2.EXE has an invalid description directory");
    }
    const auto count = (first_description - 4U) / 2U;
    std::vector<std::vector<std::uint8_t>> descriptions;
    descriptions.reserve(count);
    for (std::size_t id = 0; id < count; ++id) {
        const auto offset = static_cast<std::size_t>(u16(4U + id * 2U));
        if (offset > image.size()) {
            throw std::runtime_error("DATE2.EXE description points outside the image");
        }
        auto end = offset;
        while (end + 1U < image.size() &&
               !(image[end] == '$' && image[end + 1U] == '$')) {
            ++end;
        }
        if (end + 1U >= image.size()) {
            throw std::runtime_error("DATE2.EXE description lacks its $$ terminator");
        }
        descriptions.emplace_back(image.begin() + static_cast<std::ptrdiff_t>(offset),
                                  image.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return descriptions;
}

Viewport crop_map(const IndexedMapImage& map, std::uint16_t viewport_x,
                  std::uint16_t viewport_y) {
    constexpr std::size_t width = 320;
    constexpr std::size_t height = 200;
    Viewport result;
    result.pixels.assign(width * height, 0);
    result.palette = map.palette;

    const auto max_left = map.width > width ? map.width - width : 0;
    const auto max_top = map.height > height ? map.height - height : 0;
    const auto left = std::min(max_left, static_cast<std::size_t>(viewport_x) * 8);
    const auto top = std::min(max_top, static_cast<std::size_t>(viewport_y) * 8);

    const auto copied_width = std::min(width, map.width);
    const auto copied_height = std::min(height, map.height);
    for (std::size_t row = 0; row < copied_height; ++row) {
        std::copy_n(map.pixels.begin() + static_cast<std::ptrdiff_t>((top + row) * map.width + left),
                    copied_width,
                    result.pixels.begin() + static_cast<std::ptrdiff_t>(row * width));
    }
    return result;
}

void draw_party_actor(Viewport& viewport, const SpriteArchive& actors,
                      const SharedState& state, std::size_t actor) {
    const auto slot = actor * 3U;
    const auto animation = state.u16(0x8a + slot * 2U);
    const auto animation_frame = (animation & 1U) != 0U ? 1U : animation;
    const auto sprite_index = static_cast<std::size_t>(
        state.u16(0x72 + slot * 2U) + state.u16(0xa2 + slot * 2U) +
        animation_frame);
    if (sprite_index >= actors.sprites().size()) return;
    const auto& sprite = actors.sprites()[sprite_index];
    const auto pixels = actors.pixels(sprite_index);
    const auto left = static_cast<int>(
        state.u16(0x12 + slot * 2U) + state.i16(0x42 + slot * 2U)) * 4;
    const auto top = static_cast<int>(state.u16(0x2a + slot * 2U)) +
                     state.i16(0x5a + slot * 2U);
    for (std::size_t row = 0; row < sprite.height; ++row) {
        for (std::size_t column = 0; column < sprite.width; ++column) {
            const auto x = left + static_cast<int>(column);
            const auto y = top + static_cast<int>(row);
            const auto color = pixels[row * sprite.width + column];
            if (x >= 0 && y >= 0 && x < 320 && y < 200 && color != 0xfe) {
                viewport.pixels[static_cast<std::size_t>(y) * 320 +
                                static_cast<std::size_t>(x)] = color;
            }
        }
    }
}

void blit(Viewport& viewport, std::span<const std::uint8_t> pixels, std::size_t width,
          std::size_t height, int left, int top) {
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            const auto x = left + static_cast<int>(column);
            const auto y = top + static_cast<int>(row);
            const auto color = pixels[row * width + column];
            if (x >= 0 && y >= 0 && x < 320 && y < 200 && color != 0xfe) {
                viewport.pixels[static_cast<std::size_t>(y) * 320 +
                                static_cast<std::size_t>(x)] = color;
            }
        }
    }
}

void blit_opaque(Viewport& viewport, std::span<const std::uint8_t> pixels,
                 std::size_t width, std::size_t height, int left, int top) {
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            const auto x = left + static_cast<int>(column);
            const auto y = top + static_cast<int>(row);
            if (x >= 0 && y >= 0 && x < 320 && y < 200) {
                viewport.pixels[static_cast<std::size_t>(y) * 320U +
                                static_cast<std::size_t>(x)] =
                    pixels[row * width + column];
            }
        }
    }
}

void draw_compact_money_overlay(Viewport& viewport, const SpriteArchive& menu_sprites,
                                std::uint16_t money) {
    // RPG.EXE:22cf/2781. Coordinates in the DOS routines are mode-X address
    // bytes, hence the factor of four used here. The frame pieces are copied
    // opaquely by 653a; the currency mark and digits use 6577's 0xfe-keyed
    // transparent blitter.
    const auto opaque_sprite = [&](std::size_t index, int x_byte, int y) {
        if (index >= menu_sprites.sprites().size()) return;
        const auto& sprite = menu_sprites.sprites()[index];
        blit_opaque(viewport, menu_sprites.pixels(index), sprite.width, sprite.height,
                    x_byte * 4, y);
    };
    const auto transparent_sprite = [&](std::size_t index, int x_byte, int y) {
        if (index >= menu_sprites.sprites().size()) return;
        const auto& sprite = menu_sprites.sprites()[index];
        blit(viewport, menu_sprites.pixels(index), sprite.width, sprite.height,
             x_byte * 4, y);
    };

    const auto panel_row = [&](std::size_t left, std::size_t middle,
                               std::size_t right, int y) {
        opaque_sprite(left, 54, y);
        for (int piece = 0; piece < 4; ++piece) {
            opaque_sprite(middle, 60 + piece * 2, y);
        }
        opaque_sprite(right, 68, y);
    };
    panel_row(80, 81, 82, 8);
    // DS:3768 is one during normal event execution, so 2781 emits exactly
    // one sixteen-line middle row before its bottom row.
    panel_row(168, 169, 170, 16);
    panel_row(171, 172, 173, 32);

    transparent_sprite(100, 56, 17);

    std::array<std::uint8_t, 5> reversed{};
    std::size_t digit_count = 0;
    do {
        reversed[digit_count++] = static_cast<std::uint8_t>(money % 10U);
        money = static_cast<std::uint16_t>(money / 10U);
    } while (money != 0 && digit_count < reversed.size());
    auto x_byte = 60;
    while (digit_count != 0) {
        transparent_sprite(101U + reversed[--digit_count], x_byte, 21);
        x_byte += 2;
    }
}

void draw_menu_number(Viewport& viewport, const SpriteArchive& menu_sprites,
                      std::uint16_t value, int x_byte, int top,
                      std::size_t digit_base = 111U) {
    std::array<std::uint8_t, 5> reversed{};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<std::uint8_t>(value % 10U);
        value = static_cast<std::uint16_t>(value / 10U);
    } while (value != 0 && count < reversed.size());
    while (count != 0) {
        const auto frame = digit_base + reversed[--count];
        if (frame < menu_sprites.sprites().size()) {
            const auto& info = menu_sprites.sprites()[frame];
            blit(viewport, menu_sprites.pixels(frame), info.width, info.height,
                 x_byte * 4, top);
        }
        x_byte += 2;
    }
}

std::filesystem::path rpg_item_preview_path(
    const std::filesystem::path& game_root, std::uint16_t sprite) {
    std::ostringstream filename;
    filename << "CD" << std::setw(3) << std::setfill('0') << sprite << ".RSK";
    return game_root / (sprite < 300U ? "CD" : "AD") / filename.str();
}

void fill_rect(Viewport& viewport, int left, int top, int width, int height,
               std::uint8_t color) {
    const auto first_x = std::max(0, left);
    const auto first_y = std::max(0, top);
    const auto last_x = std::min(320, left + width);
    const auto last_y = std::min(200, top + height);
    for (auto y = first_y; y < last_y; ++y) {
        std::fill(viewport.pixels.begin() + y * 320 + first_x,
                  viewport.pixels.begin() + y * 320 + last_x, color);
    }
}

// Draw the original byte stream through a DSK subset.  RPG.EXE's text path
// advances four planar bytes (16 square pixels) per Big5 glyph, treats a
// literal space as a four-pixel indent, and recognizes ## as a line break.
// Height is explicit because ITEM2 descriptions can be longer than the status
// pane. 70a6 itself has no logical right-edge check; horizontal clipping is
// only against the physical VGA page.
void draw_legacy_text(Viewport& viewport, const LegacyFont& font,
                      std::span<const std::uint8_t> text, int left, int top,
                      int width, int height, std::uint8_t color) {
    static_cast<void>(width);
    auto x = 0;
    auto y = 0;
    std::size_t cursor = 0;
    while (cursor < text.size() && y + static_cast<int>(LegacyFont::glyph_height) <= height) {
        if (cursor + 1 < text.size() && text[cursor] == '#' && text[cursor + 1] == '#') {
            x = 0;
            y += 16;
            cursor += 2;
            continue;
        }
        if (cursor + 1 < text.size() && text[cursor] == '%' && text[cursor + 1] == '%') {
            break;
        }
        if (text[cursor] == ' ') {
            x += 4;
            ++cursor;
            continue;
        }
        if (cursor + 1 >= text.size()) break;
        const auto code = static_cast<std::uint16_t>(text[cursor]) << 8U |
                          text[cursor + 1];
        const auto glyph = font.rasterize_or_first(code);
        for (std::size_t row = 0; row < LegacyFont::glyph_height; ++row) {
            for (std::size_t column = 0; column < LegacyFont::glyph_width; ++column) {
                if (glyph[row * LegacyFont::glyph_width + column] == 0) continue;
                const auto target_x = left + x + static_cast<int>(column);
                const auto target_y = top + y + static_cast<int>(row);
                if (target_x >= 0 && target_x < 320 &&
                    target_y >= 0 && target_y < 200) {
                    viewport.pixels[static_cast<std::size_t>(target_y) * 320U +
                                    static_cast<std::size_t>(target_x)] = color;
                }
            }
        }
        x += static_cast<int>(LegacyFont::glyph_width);
        cursor += 2;
    }
}

void draw_item_text(Viewport& viewport, const ItemTextDatabase& texts,
                    const LegacyFont& font, std::uint16_t item_id,
                    int left, int top, int width, int height,
                    std::uint8_t color, bool description = false) {
    if (item_id >= texts.size()) return;
    const auto& text = texts.at(item_id);
    draw_legacy_text(viewport, font,
                     description ? std::span<const std::uint8_t>(text.description)
                                 : std::span<const std::uint8_t>(text.name),
                     left, top, width, height, color);
}

void draw_rpg_actor_card(Viewport& viewport,
                         const SpriteArchive& menu_sprites,
                         SharedState& state,
                         std::size_t actor, int left, int top) {
    const auto bar_height = [](std::uint16_t current, std::uint16_t maximum,
                               std::uint16_t scale) {
        if (current == 0 || maximum == 0) return 0;
        return std::max(1, static_cast<int>(current) * scale / maximum);
    };
    const auto rotate_left = [](std::uint16_t value, unsigned count) {
        count &= 15U;
        if (count == 0U) return value;
        return static_cast<std::uint16_t>((value << count) |
                                          (value >> (16U - count)));
    };

    const auto identity = state.u16(0x72 + actor * 6U);
    const auto portrait = 76U + identity / 12U;
    if (portrait < menu_sprites.sprites().size()) {
        const auto& info = menu_sprites.sprites()[portrait];
        blit_opaque(viewport, menu_sprites.pixels(portrait),
                    info.width, info.height, left, top);
    }

    const auto base = 0x106 + actor * 0x9fU;
    const auto hp_height = bar_height(
        state.u16(base + 0x2d), state.u16(base + 0x2f), 44);
    for (auto step = 0; step < hp_height; ++step) {
        fill_rect(viewport, left + 2, top + 45 - step, 2, 1, 0x6b);
    }
    const auto secondary_height = bar_height(
        state.u16(base + 0x35), state.u16(base + 0x37), 44);
    for (auto step = 0; step < secondary_height; ++step) {
        fill_rect(viewport, left + 44, top + 45 - step, 2, 1, 0x84);
    }

    // The third resource gauge is the literal mode-X plane-mask loop at
    // 2546: two scanlines, complete four-pixel groups in color fe and a
    // rotated partial mask in color 0e.
    const auto third = bar_height(
        state.u16(base + 0x55), state.u16(base + 0x57), 40);
    if (third != 0) {
        const auto complete = third >> 2;
        auto group = 1;
        for (auto index = 0; index < complete; ++index, ++group) {
            fill_rect(viewport, left + group * 4, top + 47, 4, 2, 0xfe);
        }
        if (complete != 0) --group;
        const auto remaining_rotation = static_cast<unsigned>(third - complete);
        const auto mask = static_cast<std::uint8_t>(
            rotate_left(0xf000U, remaining_rotation));
        for (auto plane = 0; plane < 4; ++plane) {
            if ((mask & (1U << plane)) == 0) continue;
            fill_rect(viewport, left + group * 4 + plane,
                      top + 47, 1, 2, 0x0e);
        }
    }

    // RPG 25a8..262b is byte-for-byte the same state overlay family as FIG
    // 2cc9..2d55.  The field/equipment target cards therefore also show
    // death, low HP, and all eleven status icons; they are not bare gauges.
    auto status = state.u16(base + 8);
    if ((status & 0x2000U) != 0) {
        // RPG:25b0 jumps to the death overlay before the routine refreshes
        // the derived low-HP bit, so a dead actor's stored word is untouched.
        if (menu_sprites.sprites().size() > 153U) {
            composite_legacy_masked_sprite(
                viewport.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(viewport.palette),
                menu_sprites, 153, left, top, 0x00);
        }
        return;
    }

    // This is intentionally a state mutation rather than just a rendering
    // decision. RPG:25cf clears bit 1000h in SAVE and 25e0 sets it again when
    // current HP is no greater than one quarter of maximum HP.
    status = static_cast<std::uint16_t>(status & ~0x1000U);
    const auto hit_points = state.u16(base + 0x2d);
    const auto maximum_hit_points = state.u16(base + 0x2f);
    if (hit_points <= (maximum_hit_points >> 2U)) {
        status = static_cast<std::uint16_t>(status | 0x1000U);
    }
    state.set_u16(base + 8, status);
    if ((status & 0x1000U) != 0 && menu_sprites.sprites().size() > 154U) {
        composite_legacy_translucent_sprite(
            viewport.pixels, 320, 200,
            std::span<const std::uint8_t, 768>(viewport.palette),
            menu_sprites, 154, left + 4, top + 2);
    }
    auto displayed = 0;
    auto status_mask = std::uint16_t{0x0800};
    for (std::size_t bit = 0; bit < 11; ++bit, status_mask >>= 1U) {
        if ((status & status_mask) == 0) continue;
        const auto frame = 155U + bit;
        if (frame < menu_sprites.sprites().size()) {
            const auto& info = menu_sprites.sprites()[frame];
            blit(viewport, menu_sprites.pixels(frame),
                 info.width, info.height,
                 left + 8 + (displayed & 1) * 16,
                 top + 1 + (displayed / 2) * 16);
        }
        ++displayed;
    }
}

void draw_rpg_party_target_cards(Viewport& viewport,
                                 const SpriteArchive& menu_sprites,
                                 SharedState& state,
                                 std::size_t party_count,
                                 std::size_t selected_actor) {
    // RPG.EXE:265a/24b8. The four cards intentionally form a directional
    // diamond: Left, Right, Up, Down correspond to actor indices 0..3.
    static constexpr std::array<std::pair<int, int>, 4> positions{{
        {16 * 4, 104}, {40 * 4, 104}, {28 * 4, 80}, {28 * 4, 131},
    }};
    party_count = std::min<std::size_t>(party_count, positions.size());
    for (std::size_t actor = 0; actor < party_count; ++actor) {
        const auto [left, top] = positions[actor];
        draw_rpg_actor_card(viewport, menu_sprites, state, actor, left, top);
    }
    apply_rpg_party_target_highlight(
        viewport.pixels, 320, 200,
        std::span<const std::uint8_t, 768>(viewport.palette),
        selected_actor, party_count);
}

std::filesystem::path de_sprite_path(const std::filesystem::path& game_root,
                                     std::uint16_t resource) {
    std::ostringstream name;
    name << "DE" << std::setw(3) << std::setfill('0') << resource;
    return game_root / "DE" / name.str();
}

PlanarSpriteSet load_de_sprite(const std::filesystem::path& game_root,
                               std::uint16_t resource) {
    const auto layout = de_sprite_path(game_root, resource);
    auto dictionary_resource = resource;
    auto dictionary = layout;
    auto metadata = dictionary;
    metadata.replace_extension(".RSK");
    while (!std::filesystem::is_regular_file(metadata)) {
        if (dictionary_resource == 0) {
            throw std::runtime_error("DE sprite layout has no preceding dictionary: " +
                                     layout.string());
        }
        dictionary = de_sprite_path(game_root, --dictionary_resource);
        metadata = dictionary;
        metadata.replace_extension(".RSK");
    }
    return PlanarSpriteSet::load(dictionary, layout);
}

void draw_world_characters(
    Viewport& viewport, const MapAreaRecord& area, const SharedState& state,
    const std::filesystem::path& game_root, const SpriteArchive& actors,
    std::map<std::uint16_t, PlanarSpriteSet>& animation_sets) {
    const auto cell_base = state.u16(0x40f);
    const auto map_width = state.map_width();
    const auto party_count = std::min<std::size_t>(
        4U, static_cast<std::size_t>(state.u16(0x10) + state.u16(0x102)));

    // RPG:dd6 scans 31 exact eight-pixel baselines from -32 through 208.
    // At each baseline 4f1c draws entities in record order first, followed by
    // active party members in reverse order (slots 9, 6, 3, 0).  Preserving
    // that scan is significant when sprites overlap at the same map depth.
    for (int depth = -32; depth <= 208; depth += 8) {
        for (std::size_t index = 0; index < area.entity_count(); ++index) {
            const auto entity = map_entity(area, index);
            if (entity.behavior == 3 || entity.behavior == 7 ||
                entity.cell_offset < cell_base ||
                ((entity.cell_offset - cell_base) & 1U) != 0) {
                continue;
            }
            const auto cell = static_cast<std::size_t>(
                (entity.cell_offset - cell_base) / 2U);
            const auto world_x = cell % map_width;
            const auto world_y = cell / map_width;
            const auto entity_depth =
                (static_cast<int>(world_y) - state.viewport_y()) * 8 - 16;
            if (entity_depth != depth) continue;
            const auto left_byte =
                (static_cast<int>(world_x) - state.viewport_x()) * 2 +
                static_cast<int>(entity.render_x_offset);
            // 4f52 accepts mode-X byte columns -19..80 inclusive.
            if (left_byte < -19 || left_byte > 80) continue;
            const auto left = left_byte * 4;
            const auto top = entity_depth + entity.render_y_offset;

            const auto animation_frame = (entity.animation_frame & 1U) != 0U
                ? 1U : entity.animation_frame;
            auto frame_index = static_cast<std::size_t>(entity.sprite & 0xffU) +
                               animation_frame;
            if (entity.behavior != 4 && entity.behavior != 5 &&
                entity.behavior != 6) {
                frame_index += entity.direction;
            }
            const auto resource = static_cast<std::uint16_t>(entity.sprite >> 8U);
            if (resource == 0) {
                if (frame_index >= actors.sprites().size()) continue;
                const auto& sprite = actors.sprites()[frame_index];
                blit(viewport, actors.pixels(frame_index), sprite.width,
                     sprite.height, left, top);
            } else {
                auto found = animation_sets.find(resource);
                if (found == animation_sets.end()) {
                    found = animation_sets.emplace(
                        resource, load_de_sprite(game_root, resource)).first;
                }
                if (frame_index >= found->second.frame_count()) continue;
                const auto& frame = found->second.frame(frame_index);
                blit(viewport, frame.pixels, frame.width, frame.height, left, top);
            }
        }
        for (std::size_t actor = party_count; actor != 0; --actor) {
            const auto logical_actor = actor - 1U;
            const auto slot = logical_actor * 3U;
            if (static_cast<std::int16_t>(state.u16(0x2a + slot * 2U)) ==
                depth) {
                draw_party_actor(viewport, actors, state, logical_actor);
            }
        }
    }
}

class RpgEventHost final : public EventVmHost {
public:
    RpgEventHost(PlatformBackend& platform, const LegacyFont& font,
                 const LegacyFont& name_font, const LegacyFont& item_font,
                 const ItemDatabase& items, const ItemTextDatabase& item_texts,
                 const SpriteArchive& menu_sprites,
                 const SpriteArchive& equipment_art,
                 std::span<const std::uint8_t> save_slot_prompt,
                 std::span<const std::uint8_t> travel_labels,
                 std::span<const std::uint8_t> shop_prompt,
                 std::span<const std::uint8_t> shop_sale_prompt,
                 std::span<const std::uint8_t> shop_money_error,
                 std::span<const std::uint8_t> shop_inventory_error,
                 std::span<const std::uint8_t> shop_confirmation_prompt,
                 std::span<const std::uint8_t> shop_quantity_error,
                 std::span<const std::uint8_t> shop_unsellable_error,
                 std::span<const std::uint8_t> item_discard_error,
                 std::span<const std::uint8_t> item_discard_prompt,
                 std::span<const std::uint8_t> item_alchemy_error,
                 std::span<const std::uint8_t> item_alchemy_select_prompt,
                 std::span<const std::uint8_t> item_alchemy_level_error,
                 std::span<const std::uint8_t> item_alchemy_data,
                 std::span<const std::uint8_t> item_effect_labels,
                 std::span<const std::uint8_t> item_alchemy_result_labels,
                 std::span<const std::uint8_t> equipment_actor_error,
                 std::span<const std::uint8_t> equipment_two_hand_error,
                 std::span<const std::uint8_t> equipment_slot_error,
                 std::span<const std::uint8_t> field_action_error,
                 std::span<const std::uint8_t> ability_value_error,
                 std::span<const std::uint8_t> ability_material_error,
                 std::span<const std::uint8_t> ability_dead_error,
                 std::span<const std::uint8_t> ability_inventory_error,
                 std::span<const std::uint8_t> field_ability_records,
                 std::span<const std::uint8_t> ability_resource_labels,
                 const std::vector<std::vector<std::uint8_t>>&
                     ability_descriptions,
                 std::span<const std::uint8_t> system_menu_labels,
                 std::span<const std::uint8_t> system_exit_prompt,
                 std::span<const std::uint8_t> status_menu_labels,
                 std::span<const std::uint8_t> status_value_labels,
                 std::span<const std::uint8_t> inventory_category_labels,
                 std::span<const std::uint8_t> equipment_slot_labels,
                 std::span<const std::uint8_t> equipment_stat_labels,
                 std::function<Viewport()> scene_provider,
                 std::function<void()> scene_palette_advance,
                 MapDatabase* map_database, const SaveSlotWriter* save_slot,
                 const SaveSlotLoader* load_slot,
                 std::shared_ptr<MapDatabase>* live_map_database,
                 FieldActionRuntime& field_action_runtime,
                 SharedState& state, std::filesystem::path game_root,
                 std::filesystem::path* playing_music,
                 bool& music_enabled, bool& sound_enabled,
                 RpgMenuRuntime& menu_runtime)
        : platform_(platform), font_(font), name_font_(name_font),
          item_font_(item_font), items_(items), item_texts_(item_texts),
          menu_sprites_(menu_sprites), equipment_art_(equipment_art),
          save_slot_prompt_(save_slot_prompt), travel_labels_(travel_labels),
          shop_prompt_(shop_prompt),
          shop_sale_prompt_(shop_sale_prompt),
          shop_money_error_(shop_money_error),
          shop_inventory_error_(shop_inventory_error),
          shop_confirmation_prompt_(shop_confirmation_prompt),
          shop_quantity_error_(shop_quantity_error),
          shop_unsellable_error_(shop_unsellable_error),
          item_discard_error_(item_discard_error),
          item_discard_prompt_(item_discard_prompt),
          item_alchemy_error_(item_alchemy_error),
          item_alchemy_select_prompt_(item_alchemy_select_prompt),
          item_alchemy_level_error_(item_alchemy_level_error),
          item_alchemy_data_(item_alchemy_data),
          item_effect_labels_(item_effect_labels),
          item_alchemy_result_labels_(item_alchemy_result_labels),
          equipment_actor_error_(equipment_actor_error),
          equipment_two_hand_error_(equipment_two_hand_error),
          equipment_slot_error_(equipment_slot_error),
          field_action_error_(field_action_error),
          ability_value_error_(ability_value_error),
          ability_material_error_(ability_material_error),
          ability_dead_error_(ability_dead_error),
          ability_inventory_error_(ability_inventory_error),
          field_ability_records_(field_ability_records),
          ability_resource_labels_(ability_resource_labels),
          ability_descriptions_(ability_descriptions),
          system_menu_labels_(system_menu_labels),
          system_exit_prompt_(system_exit_prompt),
          status_menu_labels_(status_menu_labels),
          status_value_labels_(status_value_labels),
          inventory_category_labels_(inventory_category_labels),
          equipment_slot_labels_(equipment_slot_labels),
          equipment_stat_labels_(equipment_stat_labels),
          scene_provider_(std::move(scene_provider)),
          scene_palette_advance_(std::move(scene_palette_advance)),
          map_database_(map_database), save_slot_(save_slot),
          load_slot_(load_slot), live_map_database_(live_map_database),
          field_action_runtime_(field_action_runtime), state_(state),
          game_root_(std::move(game_root)), playing_music_(playing_music),
          music_enabled_(music_enabled), sound_enabled_(sound_enabled),
          menu_runtime_(menu_runtime), frame_delay_ticks_(state.u16(0x406)) {}

    void show_dialogue(std::uint16_t opcode,
                       std::span<const std::uint8_t> text) override {
        if (quit_requested_) return;
        std::size_t offset = 0;
        do {
            // 2cce installs the 7x4 MENU panel at byte column four. Mode 2
            // (opcode 46) uses y=0; every other dialogue mode uses y=112.
            // Text begins at byte column ten and panel_y+13. 49d0 has no
            // logical right boundary and only resets x for a literal ##, so
            // the host mask extends to the physical 320-pixel page edge.
            // 49d0/70a6 uses the default DATA:6ae5 value 00h for event
            // dialogue. Render a one-bit host mask first because an indexed
            // zero glyph cannot share the page buffer's zero background.
            const auto page = render_dialogue_page(
                font_, text, offset, 280, 64, 1, &name_font_);
            const auto panel_top = opcode == 46 ? 0 : 112;
            const auto text_left = 10 * 4;
            const auto text_top = panel_top + 13;
            const auto source = event_scene();
            const auto compose_page = [&](const DialoguePage& rendered) {
                auto composed = source;
                draw_rpg_selector_panel(composed.pixels, 320, 200, menu_sprites_,
                                        4, panel_top, 7, 4);
                for (std::size_t y = 0; y < rendered.height; ++y) {
                    for (std::size_t x = 0; x < rendered.width; ++x) {
                        const auto color = rendered.pixels[y * rendered.width + x];
                        if (color == 0) continue;
                        const auto destination_y = text_top + static_cast<int>(y);
                        const auto destination_x = text_left + static_cast<int>(x);
                        if (destination_y < 200 && destination_x < 320) {
                            composed.pixels[
                                static_cast<std::size_t>(destination_y) * 320U +
                                static_cast<std::size_t>(destination_x)] = 0;
                        }
                    }
                }
                return composed;
            };
            auto frame = compose_page(page);

            // 49d0 draws one glyph through 70a6, then waits SAVE+3f2 IRQ
            // ticks before reading the keyboard flag. A key consumes that
            // flag and changes the remaining per-glyph delay to zero; the
            // later %%/$$ acknowledgement still needs a separate action.
            // Opcode 20/46 first set DATA:3805 to 1/2: both force two ticks
            // and jump over the keyboard check after every glyph. Mode 2 also
            // selects the top panel above; both still keep their final wait.
            const auto forced_timed_text = opcode == 20U || opcode == 46U;
            auto skipped_delay = false;
            for (const auto glyph_end : page.glyph_end_offsets) {
                if (skipped_delay) break;
                const auto partial = render_dialogue_page(
                    font_, text.first(glyph_end), offset, 280, 64, 1,
                    &name_font_);
                auto shown = compose_page(partial);
                platform_.present_direct_update({
                    320, 200, shown.pixels,
                    std::span<const std::uint8_t, 768>(shown.palette)});
                const auto text_delay = forced_timed_text
                    ? std::uint16_t{2} : state_.u16(0x3f2);
                if (text_delay != 0) {
                    platform_.delay_for(std::chrono::milliseconds(
                        (static_cast<std::uint64_t>(text_delay) * 1000U + 69U) /
                        70U));
                }
                const auto action = forced_timed_text
                    ? InputAction::none : platform_.poll_text_input();
                if (action == InputAction::quit) {
                    direct_event_page_ = std::move(shown);
                    quit_requested_ = true;
                    return;
                }
                skipped_delay = action != InputAction::none;
            }
            if (skipped_delay && !page.glyph_end_offsets.empty()) {
                platform_.present_direct_update({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette)});
            }
            // Opcode 18 enters 5788 with DS:359b=1. The dialogue renderer
            // still pauses at an explicit %% page break, but 4a94 skips the
            // final MENU 149..152 acknowledgement when it reaches $$.
            if (page.page_break) {
                // 4a6b leaves the stream cursor after the last glyph and
                // copies MENU frame 91h there while waiting for the explicit
                // page continuation. Unlike the final marker it is static.
                while (true) {
                    auto shown = frame;
                    if (menu_sprites_.sprites().size() > 0x91U) {
                        const auto& info = menu_sprites_.sprites()[0x91U];
                        blit(shown, menu_sprites_.pixels(0x91U),
                             info.width, info.height,
                             text_left + static_cast<int>(page.cursor_x),
                             text_top + static_cast<int>(page.cursor_y));
                    }
                    platform_.present({
                        320, 200, shown.pixels,
                        std::span<const std::uint8_t, 768>(shown.palette)});
                    const auto action = platform_.poll_input();
                    if (action == InputAction::quit) {
                        direct_event_page_ = std::move(shown);
                        quit_requested_ = true;
                        return;
                    }
                    if (action != InputAction::none) {
                        direct_event_page_ = std::move(shown);
                        break;
                    }
                    platform_.delay_for(std::chrono::milliseconds(20));
                }
            } else if (opcode != 18) {
                // At $$, 4a94 cycles MENU 149..152 at the same cursor until
                // any action arrives. Opcode 18 deliberately bypasses it.
                auto indicator = std::size_t{149};
                while (true) {
                    auto shown = frame;
                    if (indicator < menu_sprites_.sprites().size()) {
                        const auto& info = menu_sprites_.sprites()[indicator];
                        blit(shown, menu_sprites_.pixels(indicator),
                             info.width, info.height,
                             text_left + static_cast<int>(page.cursor_x),
                             text_top + static_cast<int>(page.cursor_y));
                    }
                    platform_.present({
                        320, 200, shown.pixels,
                        std::span<const std::uint8_t, 768>(shown.palette)});
                    const auto action = platform_.poll_input();
                    if (action == InputAction::quit) {
                        direct_event_page_ = std::move(shown);
                        quit_requested_ = true;
                        return;
                    }
                    if (action != InputAction::none) {
                        direct_event_page_ = std::move(shown);
                        break;
                    }
                    platform_.delay_for(std::chrono::milliseconds(20));
                    ++indicator;
                    if (indicator == 153U) indicator = 149U;
                }
            } else {
                platform_.present({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette)});
                direct_event_page_ = std::move(frame);
            }
            offset = page.next_offset;
            if (!page.has_more) return;
        } while (offset < text.size());
    }

    void delay(std::uint16_t ticks) override {
        // RPG.EXE waits on its 70 Hz IRQ word at 5a55. The backend sleep keeps
        // that timing without the original busy loop. A host window close is
        // not a DOS key, so it can interrupt the replacement sleep without
        // consuming any queued story/menu input.
        auto remaining = std::chrono::milliseconds(
            (static_cast<std::uint64_t>(ticks) * 1000U + 69U) / 70U);
        constexpr auto slice = std::chrono::milliseconds(20);
        while (remaining.count() > 0) {
            if (platform_.poll_frontend_quit()) {
                quit_requested_ = true;
                return;
            }
            const auto current = std::min(remaining, slice);
            platform_.delay_for(current);
            remaining -= current;
        }
    }

    void map_relocated(const MapAreaRecord& area) override {
        // RPG:edc/10fd replaces the active graphics/layout and palette. A DE
        // page plus its direct VGA annotations belongs to the old resource
        // set and cannot remain composited over the destination map while the
        // same event continues after opcode 37.
        cutscene_.reset();
        cutscene_id_.reset();
        cutscene_dictionary_id_.reset();
        cutscene_frame_index_ = 0;
        reset_direct_page_layers();
        palette_dark_ = false;
        relocated_area_ = &area;
        auto graphics = normalize_dos_asset_path(state_.area_graphics_path());
        auto layout = normalize_dos_asset_path(state_.area_collision_path());
        graphics.replace_extension();
        layout.replace_extension();
        relocated_map_ = MapResource::load(game_root_ / graphics,
                                           game_root_ / layout);
        relocated_palette_ = relocated_map_->palette();
        relocated_palette_animation_ = relocated_map_->animation_words();
        const auto destination_music =
            normalize_dos_asset_path(state_.music_path());
        if (destination_music.empty()) {
            platform_.stop_music();
            if (playing_music_) playing_music_->clear();
        } else {
            const auto path = game_root_ / destination_music;
            if (music_enabled_ && std::filesystem::is_regular_file(path)) {
                // Opcode 37 clears SAVE+459 before edc specifically to force
                // the loader's comparison unequal. Restart even when the two
                // areas name the same RIX rather than deferring until the
                // event eventually returns to the outer map loop.
                platform_.play_music(read_file(path), true);
            }
            if (playing_music_) *playing_music_ = destination_music;
        }
        relocated_actors_ = load_default_map_actors(game_root_, state_);
        state_.set_u16(0x417, relocated_map_->layout().width);
        state_.set_u16(0x419, relocated_map_->layout().height);
        const auto viewport_cells =
            (static_cast<std::size_t>(state_.viewport_y()) *
                 relocated_map_->layout().width +
             state_.viewport_x()) * 2U;
        if (state_.u16(0x40d) >= viewport_cells) {
            state_.set_u16(0x40f, static_cast<std::uint16_t>(
                                      state_.u16(0x40d) - viewport_cells));
        }
        relocated_animation_sets_.clear();
    }

    bool present_event_command(std::uint16_t opcode,
                               std::span<const std::uint16_t> arguments) override {
        switch (opcode) {
        case 5:
            fade_out();
            return true;
        case 6:
            reset_direct_page_layers();
            fade_in();
            return true;
        case 45:
            fade_in();
            return true;
        case 7:
            reset_direct_page_layers();
            palette_dark_ = false;
            present(event_scene());
            return true;
        case 14:
            // RPG:5596 only calls 22cf. It writes the money card directly and
            // returns; the configurable 5a59 frame interval is not consumed.
            {
                auto frame = event_scene();
                draw_compact_money_overlay(
                    frame, menu_sprites_, state_.u16(0x104));
                direct_event_page_ = frame;
                present(std::move(frame));
            }
            return true;
        case 22:
            reset_direct_page_layers();
            present_timed(event_scene());
            return true;
        case 30:
        case 31:
        case 32:
        case 33:
            advance_event_palette();
            reset_direct_page_layers();
            present_timed(event_scene());
            return true;
        case 39:
            // Unlike opcode 22/36, RPG:5ac7 renders the changed entity to the
            // back page, waits at 5ec0 while the old page is still visible,
            // and only then flips at 6e14. Preserve that pre-flip hold rather
            // than delaying on the newly exposed animation frame.
            if (frame_delay_ticks_ != 0) {
                platform_.delay_for(std::chrono::milliseconds(
                    (static_cast<std::uint64_t>(frame_delay_ticks_) * 1000U + 69U) /
                    70U));
            }
            reset_direct_page_layers();
            present(event_scene());
            return true;
        case 36:
            // The VM advances SAVE+411 after this presentation, matching
            // RPG:5a98. Keep the page that was actually copied to VGA as a
            // separate frontend value: opcode 55 and positioned text operate
            // on that last page, not on the next selector now held in SAVE.
            cutscene_frame_index_ = state_.u16(0x411);
            reset_direct_page_layers();
            present_timed(event_scene());
            return true;
        case 29:
            if (arguments.empty()) return false;
            select_cutscene_dictionary(arguments[0]);
            return true;
        case 35:
            if (arguments.empty()) return false;
            load_cutscene_layout(arguments[0]);
            return true;
        case 38:
            if (arguments.empty()) return false;
            play_event_music(arguments[0]);
            return true;
        case 43:
            if (arguments.empty()) return false;
            fade_out();
            select_cutscene_dictionary(arguments[0]);
            return true;
        case 44:
            if (arguments.empty()) return false;
            frame_delay_ticks_ = arguments[0];
            return true;
        case 49:
            if (arguments.empty()) return false;
            reset_direct_page_layers();
            present_timed(shifted_scene(event_scene(), arguments[0]));
            return true;
        case 55:
            // RPG:5c35 snapshots DS:5a5c into a private palette table, then
            // 0dbf:0314 rewrites palette indices 10h..1fh on all four VGA
            // planes to an inverse-luminance ramp. Keep the transformed page
            // active for the following positioned text/fade until a new DE
            // background overwrites it.
            {
                auto frame = event_scene();
                apply_rpg_event_monochrome_filter(
                    frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette));
                direct_event_page_ = frame;
                present(std::move(frame));
            }
            return true;
        case 56:
            platform_.stop_music();
            if (playing_music_) playing_music_->clear();
            return true;
        case 57:
            if (arguments.empty()) return false;
            play_voice(arguments[0]);
            return true;
        default:
            return false;
        }
    }

    bool show_positioned_text(std::uint16_t x_byte, std::uint16_t y,
                              std::span<const std::uint8_t> text) override {
        auto frame = event_scene();
        // 5c07 temporarily changes DATA:6ae5 to 0fh and writes directly into
        // the currently displayed page; it does not reconstruct the map or
        // discard a preceding dialogue panel.
        draw_legacy_text(frame, font_, text,
                         static_cast<int>(x_byte) * 4, y,
                         320 - static_cast<int>(x_byte) * 4,
                         200 - static_cast<int>(y), 15);
        direct_event_page_ = frame;
        present(std::move(frame));
        // RPG.EXE:5c15 calls its DOS hundredth timer with CL=3.
        platform_.delay_for(std::chrono::milliseconds(30));
        return true;
    }

    bool run_shop(std::span<const std::uint16_t> item_ids,
                  SharedState& state) override {
        if (item_ids.empty()) return true;
        InventorySystem inventory(state, items_);
        std::size_t selected = 0;
        std::size_t first_visible = 0;
        auto scroll_cue = RpgListSelection::ScrollCue::none;
        while (true) {
            auto frame = scene_provider_();
            // RPG.EXE:57d9..580e. The shop composes the normal top selector,
            // money card and a five-row selector rather than a centered box.
            draw_compact_money_overlay(frame, menu_sprites_, state.u16(0x104));
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    4, 112, 7, 4);
            draw_legacy_text(frame, item_font_, shop_prompt_,
                             10 * 4, 141, 240, 16, 0);
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    15, 30, 5, 5);
            draw_rpg_selector_scrollbar(
                frame.pixels, 320, 200, menu_sprites_, 15, 30, 5, 5,
                item_ids.size() > 5U ? item_ids.size() - 5U : 0U,
                first_visible, scroll_cue);
            scroll_cue = RpgListSelection::ScrollCue::none;

            const auto selected_id = item_ids[selected];
            draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                                   38, 8, 1, 1);
            if (selected_id != 0 && selected_id < items_.size()) {
                const auto label =
                    static_cast<std::size_t>(items_.at(selected_id).type) * 4U;
                if (label + 4U <= inventory_category_labels_.size()) {
                    draw_legacy_text(
                        frame, item_font_,
                        inventory_category_labels_.subspan(label, 4),
                        41 * 4, 17, 32, 16, 0);
                }
            }

            constexpr std::size_t visible_rows = 5;
            for (std::size_t row = 0; row < visible_rows; ++row) {
                const auto index = first_visible + row;
                if (index >= item_ids.size()) break;
                const auto item_id = item_ids[index];
                const auto top = 43 + static_cast<int>(row) * 16;
                draw_item_text(frame, item_texts_, item_font_, item_id,
                               92, top, 96, 15, 0);
                if (item_id < items_.size()) {
                    draw_menu_number(frame, menu_sprites_,
                                     items_.at(item_id).price,
                                     47, top + 4, 111);
                }
            }
            if (menu_sprites_.sprites().size() > 1U) {
                const auto& cursor = menu_sprites_.sprites()[1];
                blit(frame, menu_sprites_.pixels(1), cursor.width, cursor.height,
                     21 * 4, 39 + static_cast<int>(selected - first_visible) * 16);
            }
            platform_.present({320, 200, frame.pixels,
                               std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return true;
            }
            if (action == InputAction::cancel) return true;
            const auto selection = rpg_list_selection_input(
                {selected, first_visible}, item_ids.size(), visible_rows,
                action);
            selected = selection.selected;
            first_visible = selection.first_visible;
            scroll_cue = selection.scroll_cue;
            if (action != InputAction::confirm) continue;

            const auto item_id = item_ids[selected];
            if (item_id == 0 || item_id >= items_.size()) continue;
            const auto& definition = items_.at(item_id);
            if (definition.price > state.u16(0x104)) {
                if (!show_bottom_message(frame, shop_money_error_)) return true;
                continue;
            }

            auto needs_inventory_slot = true;
            if (item_id >= 0x44U && item_id <= 0x48U) {
                const auto quantity = state.u16(
                    0x3e6U + (item_id - 0x44U) * 2U);
                if (quantity >= 20U) {
                    if (!show_bottom_message(frame, shop_quantity_error_)) {
                        return true;
                    }
                    continue;
                }
                needs_inventory_slot = quantity == 0;
            }
            // 5874 tests the final physical inventory word. Normal RPG
            // operations keep the 50 slots compact, so this is the original
            // full-list predicate rather than a host-side capacity guess.
            if (needs_inventory_slot && inventory.item(49) != 0) {
                if (!show_bottom_message(frame, shop_inventory_error_)) {
                    return true;
                }
                continue;
            }

            std::uint8_t choice = 0;
            bool finished_confirmation = false;
            auto confirmation_source = frame;
            if (!reveal_bottom_message(
                    confirmation_source, shop_confirmation_prompt_)) {
                return true;
            }
            while (!finished_confirmation) {
                auto confirmation = confirmation_source;
                const auto opaque = [&](std::size_t sprite,
                                        int x_byte, int y) {
                    if (sprite >= menu_sprites_.sprites().size()) return;
                    const auto& info = menu_sprites_.sprites()[sprite];
                    blit_opaque(confirmation, menu_sprites_.pixels(sprite),
                                info.width, info.height, x_byte * 4, y);
                };
                // 54d5/555a uses the same cards as selling: Yes at 44/90,
                // No at 62/90, with the non-selected card mapped by table 3.
                opaque(0, 44, 90);
                opaque(0, 62, 90);
                opaque(29, 50, 99);
                opaque(8, 68, 99);
                apply_rpg_binary_choice_highlight(
                    confirmation.pixels, 320, 200,
                    std::span<const std::uint8_t, 768>(confirmation.palette),
                    44, 62, 90, choice);
                platform_.present({
                    320, 200, confirmation.pixels,
                    std::span<const std::uint8_t, 768>(confirmation.palette)});
                const auto confirmation_action = platform_.wait_for_input();
                if (confirmation_action == InputAction::quit) {
                    quit_requested_ = true;
                    return true;
                }
                if (confirmation_action == InputAction::cancel) {
                    finished_confirmation = true;
                } else if (confirmation_action == InputAction::left) {
                    choice = 0;
                } else if (confirmation_action == InputAction::right) {
                    choice = 1;
                } else if (confirmation_action == InputAction::confirm) {
                    if (choice == 0) {
                        static_cast<void>(inventory.purchase(item_id));
                    }
                    finished_confirmation = true;
                }
            }
        }
    }

    std::optional<InventoryUiResult> run_inventory(
        SharedState& state, InventoryUiMode mode) override {
        InventorySystem inventory(state, items_);
        std::size_t selected = std::min<std::size_t>(
            menu_runtime_.inventory_selected, inventory_slot_count - 1U);
        std::size_t first_visible = std::min<std::size_t>(
            menu_runtime_.inventory_first_visible,
            inventory_slot_count - 8U);
        selected = std::clamp(
            selected, first_visible,
            std::min(inventory_slot_count - 1U, first_visible + 7U));
        // DATA:35e6 is shared by successive entries into RPG:2d0f, so the
        // direction remains where the player last left it even after closing
        // and reopening the field diamond.
        auto& item_action_choice = menu_runtime_.item_action_choice;
        auto scroll_cue = RpgListSelection::ScrollCue::none;
        while (true) {
            auto frame = scene_provider_();
            // RPG.EXE:2ae4/3d1e uses the shared selector frame at mode-X
            // (24,36), not two invented packed-pixel rectangles.
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    24, 36, 5, 8);
            draw_rpg_selector_scrollbar(
                frame.pixels, 320, 200, menu_sprites_,
                24, 36, 5, 8, 42, first_visible, scroll_cue);
            scroll_cue = RpgListSelection::ScrollCue::none;
            if (!menu_sprites_.sprites().empty()) {
                const auto& preview = menu_sprites_.sprites()[0];
                blit_opaque(frame, menu_sprites_.pixels(0),
                            preview.width, preview.height, 58 * 4, 8);
            }

            constexpr std::size_t visible_rows = 8;
            const auto selected_item = inventory.item(selected);
            // RPG.EXE:3d52..3d97 suppresses the CD/AD preview only in the
            // selling path.  Normal inventory uses 2781's (2,10), 4x6 panel,
            // places frame zero at (4+ITEM:+1d,18+ITEM:+1f), and prints the
            // two-glyph type label from DATA:299a on MENU frame zero.
            if (mode != InventoryUiMode::sell && selected_item < items_.size()) {
                const auto& definition = items_.at(selected_item);
                draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                                       2, 10, 4, 6);
                auto found = item_preview_cache_.find(definition.preview_sprite);
                if (found == item_preview_cache_.end()) {
                    const auto path = rpg_item_preview_path(
                        game_root_, definition.preview_sprite);
                    if (std::filesystem::is_regular_file(path)) {
                        auto preview_data =
                            decode_rsk_block(read_file(path)).data;
                        found = item_preview_cache_.emplace(
                            definition.preview_sprite,
                            SpriteArchive::parse(std::move(preview_data))).first;
                    }
                }
                if (found != item_preview_cache_.end() &&
                    !found->second.sprites().empty()) {
                    const auto& preview = found->second.sprites().front();
                    blit(frame, found->second.pixels(0), preview.width,
                         preview.height,
                         (4 + static_cast<int>(definition.preview_x)) * 4,
                         18 + static_cast<int>(definition.preview_y));
                }
                const auto label = static_cast<std::size_t>(definition.type) * 4U;
                if (selected_item != 0 &&
                    label + 4U <= inventory_category_labels_.size()) {
                    draw_legacy_text(
                        frame, item_font_,
                        inventory_category_labels_.subspan(label, 4),
                        62 * 4, 17, 32, 16, 0);
                }
            }
            for (std::size_t row = 0; row < visible_rows; ++row) {
                const auto slot = first_visible + row;
                if (slot >= inventory_slot_count) break;
            const auto item_id = inventory.item(slot);
            const auto top = 49 + static_cast<int>(row) * 16;
            draw_item_text(frame, item_texts_, item_font_, item_id,
                           128, top, 96, 15, 0);
            if (const auto value = inventory_row_value(state, item_id)) {
                draw_menu_number(
                    frame, menu_sprites_, *value,
                    56, top + 3, 111);
            }
            }
            if (menu_sprites_.sprites().size() > 1U) {
                const auto& cursor = menu_sprites_.sprites()[1];
                blit(frame, menu_sprites_.pixels(1), cursor.width, cursor.height,
                     30 * 4, 45 + static_cast<int>(selected - first_visible) * 16);
            }
            platform_.present({320, 200, frame.pixels,
                               std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return InventoryUiResult::cancelled;
            }
            if (action == InputAction::cancel) {
                if (mode != InventoryUiMode::sell) {
                    // 3a82..3a92 is deliberately skipped by the selling path.
                    menu_runtime_.inventory_selected = selected;
                    menu_runtime_.inventory_first_visible = first_visible;
                }
                return InventoryUiResult::cancelled;
            }
            if (action != InputAction::confirm) {
                const auto selection = rpg_list_selection_input(
                    {selected, first_visible}, inventory_slot_count,
                    visible_rows, action);
                selected = selection.selected;
                first_visible = selection.first_visible;
                scroll_cue = selection.scroll_cue;
            } else {
                const auto selected_item = inventory.item(selected);
                if (selected_item == 0) return InventoryUiResult::empty_slot;
                if (mode == InventoryUiMode::sell) {
                    const auto value = inventory.sale_value(selected);
                    if (!value) {
                        // 5617 presents DATA:3c0a when ITEM +05 bit 08 is
                        // clear instead of treating Enter as a no-op.
                        if (!show_bottom_message(frame, shop_unsellable_error_)) {
                            return InventoryUiResult::cancelled;
                        }
                        continue;
                    }
                    std::uint8_t choice = 0;
                    bool rejected = false;
                    while (true) {
                        auto confirmation = frame;
                        draw_bottom_message(confirmation, shop_sale_prompt_);
                        // 565f renders DATA:3c1e without waiting, advances
                        // one Mode-X column/four lines, then 2315 writes the
                        // exact three-quarter sale value with MENU 101..110.
                        draw_menu_number(confirmation, menu_sprites_, *value,
                                         47, 129, 101);
                        const auto opaque = [&](std::size_t sprite,
                                                int x_byte, int y) {
                            if (sprite >= menu_sprites_.sprites().size()) return;
                            const auto& info = menu_sprites_.sprites()[sprite];
                            blit_opaque(confirmation, menu_sprites_.pixels(sprite),
                                        info.width, info.height, x_byte * 4, y);
                        };
                        // RPG.EXE:54d5/555a with DS:3cd7=0.
                        opaque(0, 44, 90);
                        opaque(0, 62, 90);
                        opaque(29, 50, 99);
                        opaque(8, 68, 99);
                        apply_rpg_binary_choice_highlight(
                            confirmation.pixels, 320, 200,
                            std::span<const std::uint8_t, 768>(
                                confirmation.palette),
                            44, 62, 90, choice);
                        platform_.present({
                            320, 200, confirmation.pixels,
                            std::span<const std::uint8_t, 768>(confirmation.palette)});
                        const auto confirmation_action = platform_.wait_for_input();
                        if (confirmation_action == InputAction::quit) {
                            quit_requested_ = true;
                            return InventoryUiResult::cancelled;
                        }
                        if (confirmation_action == InputAction::cancel) {
                            rejected = true;
                            break;
                        }
                        if (confirmation_action == InputAction::left) {
                            choice = 0;
                        } else if (confirmation_action == InputAction::right) {
                            choice = 1;
                        } else if (confirmation_action == InputAction::confirm) {
                            if (choice == 0) {
                                static_cast<void>(inventory.sell(selected));
                                return InventoryUiResult::occupied_slot;
                            }
                            rejected = true;
                            break;
                        }
                    }
                    if (rejected) continue;
                }
                if (selected_item >= items_.size()) return InventoryUiResult::occupied_slot;

                const auto& definition = items_.at(selected_item);
                auto action_frame = frame;
                if (mode == InventoryUiMode::general) {
                    bool return_to_inventory = false;
                    while (true) {
                        const auto action = select_item_action(
                            frame, definition.equipment_category() != 0U,
                            (state.u8(0x3f1U) & 1U) == 0U,
                            item_action_choice);
                        if (quit_requested_) return InventoryUiResult::cancelled;
                        if (!action) {
                            return_to_inventory = true;
                            break;
                        }
                        action_frame = item_action_choice_frame(
                            frame, definition.equipment_category() != 0U,
                            (state.u8(0x3f1U) & 1U) == 0U, *action);
                        if (*action == 0U) break;
                        if (*action == 1U) {
                            // 3c6e loads ITEM2's selected description into
                            // DATA:4a30 and presents it over the copied 2d0f
                            // page.  Empty descriptions simply leave the
                            // action selector active.
                            if (selected_item < item_texts_.size() &&
                                !item_texts_.at(selected_item).description.empty() &&
                                !show_bottom_message(
                                    item_description_frame(
                                        action_frame, definition),
                                    item_texts_.at(selected_item).description)) {
                                return InventoryUiResult::cancelled;
                            }
                            continue;
                        }
                        if (*action == 2U) {
                            if (!definition.discardable()) {
                                if (!show_bottom_message(
                                        action_frame, item_discard_error_)) {
                                    return InventoryUiResult::cancelled;
                                }
                                continue;
                            }
                            const auto confirmed = confirm_item_discard(action_frame);
                            if (quit_requested_) return InventoryUiResult::cancelled;
                            if (!confirmed) continue;
                            state.set_u16(0x382U + selected * 2U, 0U);
                            inventory.compact();
                            return_to_inventory = true;
                            break;
                        }

                        // The fourth card is RPG:4397's alchemy-pot path.
                        // Until that independent state machine is entered, its
                        // exact precondition must still be honoured instead of
                        // silently treating the card as Use.
                        if ((definition.flags & 0xc0U) == 0xc0U) {
                            if (!show_bottom_message(
                                    action_frame, item_alchemy_error_)) {
                                return InventoryUiResult::cancelled;
                            }
                            continue;
                        }
                        if (!show_bottom_message(
                                action_frame, item_alchemy_select_prompt_)) {
                            return InventoryUiResult::cancelled;
                        }

                        // 4397 temporarily removes the first physical word
                        // without compacting, then reuses 39ed to choose the
                        // second ingredient. Cancellation restores that exact
                        // word; success replaces the second word with the
                        // DATA:2a42 product and only then runs 3ced.
                        state.set_u16(0x382U + selected * 2U, 0U);
                        const auto ingredient = select_alchemy_ingredient(
                            state, inventory, selected_item);
                        if (quit_requested_) {
                            state.set_u16(
                                0x382U + selected * 2U, selected_item);
                            return InventoryUiResult::cancelled;
                        }
                        if (!ingredient) {
                            state.set_u16(0x382U + selected * 2U, selected_item);
                            return_to_inventory = true;
                            break;
                        }
                        const auto second_item = inventory.item(*ingredient);
                        const auto product = resolve_item_alchemy_product(
                            items_, item_alchemy_data_, selected_item, second_item);
                        if (!product || *product >= items_.size()) {
                            state.set_u16(0x382U + selected * 2U, selected_item);
                            return_to_inventory = true;
                            break;
                        }
                        const auto product_frame = alchemy_product_frame(*product);
                        if (items_.at(*product).alchemy_required_level >
                            static_cast<unsigned>(state.u16(0x137U)) + 5U) {
                            if (!show_bottom_message(
                                    product_frame, item_alchemy_level_error_)) {
                                state.set_u16(
                                    0x382U + selected * 2U, selected_item);
                                return InventoryUiResult::cancelled;
                            }
                            state.set_u16(0x382U + selected * 2U, selected_item);
                            return_to_inventory = true;
                            break;
                        }
                        if (!confirm_alchemy_product(product_frame)) {
                            if (quit_requested_) {
                                state.set_u16(
                                    0x382U + selected * 2U, selected_item);
                                return InventoryUiResult::cancelled;
                            }
                            state.set_u16(0x382U + selected * 2U, selected_item);
                            return_to_inventory = true;
                            break;
                        }
                        state.set_u16(
                            0x382U + *ingredient * 2U, *product);
                        inventory.compact();
                        return_to_inventory = true;
                        break;
                    }
                    if (return_to_inventory) continue;
                }

                if (definition.equipment_category() == 0) {
                    if (!definition.field_usable()) {
                        // 3b30 enters 4aec with DATA:3620 when ITEM +05 bit
                        // zero is clear; the selection remains in the bag.
                        if (!show_bottom_message(action_frame, field_action_error_)) {
                            return InventoryUiResult::cancelled;
                        }
                        continue;
                    }

                    if (definition.effect_code == 0x6a) {
                        if (map_database_ == nullptr || save_slot_ == nullptr ||
                            !*save_slot_) {
                            continue;
                        }
                        RpgSaveSlotSelector selector;
                        bool save_cancelled = false;
                        while (true) {
                            auto save_frame = action_frame;
                            draw_rpg_selector_panel(
                                save_frame.pixels, 320, 200, menu_sprites_,
                                4, 112, 7, 4);
                            draw_legacy_text(save_frame, item_font_, save_slot_prompt_,
                                             10 * 4, 125, 260, 16, 0);
                            if (menu_sprites_.sprites().size() > 141U) {
                                const auto& cursor = menu_sprites_.sprites()[141];
                                blit(save_frame, menu_sprites_.pixels(141),
                                     cursor.width, cursor.height,
                                     (14 + static_cast<int>(selector.slot()) * 8) * 4,
                                     141);
                            }

                            if (selector.confirming()) {
                                // RPG.EXE:46cc. The two 64x32 frame-zero
                                // cards and MENU labels 29/8 are all copied
                                // through the opaque 653a path.
                                const auto opaque = [&](std::size_t sprite,
                                                        int x_byte, int y) {
                                    if (sprite >= menu_sprites_.sprites().size()) return;
                                    const auto& info = menu_sprites_.sprites()[sprite];
                                    blit_opaque(save_frame,
                                                menu_sprites_.pixels(sprite),
                                                info.width, info.height,
                                                x_byte * 4, y);
                                };
                                opaque(0, 23, 147);
                                opaque(0, 41, 147);
                                opaque(29, 29, 156);
                                opaque(8, 47, 156);
                                apply_rpg_binary_choice_highlight(
                                    save_frame.pixels, 320, 200,
                                    std::span<const std::uint8_t, 768>(
                                        save_frame.palette),
                                    23, 41, 147,
                                    selector.confirmation_choice());
                            }
                            platform_.present({
                                320, 200, save_frame.pixels,
                                std::span<const std::uint8_t, 768>(save_frame.palette)});
                            const auto save_action = platform_.wait_for_input();
                            const auto selector_result = selector.input(save_action);
                            if (selector_result == RpgSaveSelectorResult::quit) {
                                quit_requested_ = true;
                                return InventoryUiResult::cancelled;
                            }
                            if (selector_result == RpgSaveSelectorResult::cancelled) {
                                save_cancelled = true;
                                break;
                            }
                            if (selector_result == RpgSaveSelectorResult::committed) {
                                // 3b75 consumes/compacts the item before
                                // entering 4ce6, so the saved state already
                                // reflects the consumed portable save item.
                                if (definition.consumed_on_use()) {
                                    state.set_u16(0x382 + selected * 2, 0);
                                    inventory.compact();
                                }
                                (*save_slot_)(
                                    static_cast<std::uint8_t>(selector.slot() + 1U),
                                    state, *map_database_);
                                break;
                            }
                        }
                        if (save_cancelled) continue;
                        // FUN_1000_4ce6 returns from the inventory function
                        // immediately after committing the selected pair.
                        return InventoryUiResult::occupied_slot;
                    }

                    FieldActionSystem field_actions(state, &field_action_runtime_);
                    std::optional<std::size_t> actor;
                    std::optional<Viewport> confirmed_target_frame;
                    if (FieldActionSystem::requires_target(definition.effect_code)) {
                        actor = 0;
                        const auto party_count = std::max<std::size_t>(
                            1, std::min<std::size_t>(state.u16(0x10), 4));
                        bool target_cancelled = false;
                        while (true) {
                            auto target_frame = action_frame;
                            draw_rpg_party_target_cards(
                                target_frame, menu_sprites_, state, party_count,
                                *actor);
                            platform_.present({
                                320, 200, target_frame.pixels,
                                std::span<const std::uint8_t, 768>(target_frame.palette)});
                            const auto target_action = platform_.wait_for_input();
                            if (target_action == InputAction::quit) {
                                quit_requested_ = true;
                                return InventoryUiResult::cancelled;
                            }
                            if (target_action == InputAction::cancel) {
                                target_cancelled = true;
                                break;
                            }
                            if (const auto directional_target =
                                    rpg_party_target_for_direction(
                                        target_action, party_count)) {
                                *actor = *directional_target;
                            } else if (target_action == InputAction::confirm) {
                                confirmed_target_frame = std::move(target_frame);
                                break;
                            }
                        }
                        if (target_cancelled) continue;
                    }

                    // ITEM type 10h is the field talisman produced by 333d.
                    // 3857 derives its original ability id from item-8ch and,
                    // after the target has been chosen, charges that target's
                    // +55 resource using DATA:1dce +10h. This cost is in
                    // addition to the ordinary ITEM +05 consumption flag.
                    if (definition.type == 0x10U && actor) {
                        if (selected_item < 0x8cU) {
                            throw std::runtime_error(
                                "RPG field talisman id precedes ability mapping");
                        }
                        const auto ability_id = static_cast<std::size_t>(
                            selected_item - 0x8cU);
                        const auto record_offset = ability_id * 20U;
                        if (record_offset + 18U > field_ability_records_.size()) {
                            throw std::runtime_error(
                                "RPG field talisman ability record is missing");
                        }
                        const auto cost = static_cast<std::uint16_t>(
                            field_ability_records_[record_offset + 16U] |
                            (static_cast<std::uint16_t>(
                                 field_ability_records_[record_offset + 17U])
                             << 8U));
                        const auto resource_offset =
                            0x106U + *actor * 0x9fU + 0x55U;
                        if (state.u16(resource_offset) < cost) {
                            if (!show_bottom_message(
                                    confirmed_target_frame
                                        ? std::move(*confirmed_target_frame)
                                        : action_frame,
                                    ability_value_error_)) {
                                return InventoryUiResult::cancelled;
                            }
                            continue;
                        }
                        state.set_u16(resource_offset, static_cast<std::uint16_t>(
                            state.u16(resource_offset) - cost));
                    }

                    const auto field_result = field_actions.apply(definition.effect_code, actor);
                    if (!field_result.dispatched()) {
                        // Travel selectors 28h/29h branch here when the map's
                        // 4000h/8000h permission bit rejects the action.
                        if (!show_bottom_message(action_frame, field_action_error_)) {
                            return InventoryUiResult::cancelled;
                        }
                        continue;
                    }

                    std::optional<std::uint8_t> travel_index;
                    if (field_result.status == FieldActionStatus::travel_current) {
                        const auto current = state.u16(0x40a);
                        if (current <= 0xffU) {
                            travel_index = static_cast<std::uint8_t>(current);
                        }
                    } else if (field_result.status == FieldActionStatus::travel_select) {
                        travel_index = select_travel_destination(state);
                        if (quit_requested_) {
                            return InventoryUiResult::cancelled;
                        }
                        if (!travel_index) continue;
                    }

                    if (definition.consumed_on_use()) {
                        state.set_u16(0x382 + selected * 2, 0);
                        inventory.compact();
                    }
                    if (travel_index) {
                        const auto directory =
                            FieldActionSystem::travel_directory_offset(*travel_index);
                        if (!directory || map_database_ == nullptr) continue;
                        install_map_location(state, *map_database_, *directory);
                        return InventoryUiResult::map_reload;
                    }
                    continue;
                }

                const auto initial_slot = [](std::uint8_t category) -> std::size_t {
                    switch (category) {
                        case 7: return 0;
                        case 3: return 1;
                        case 8: case 9: return 2;
                        case 2: return 4;
                        case 1: return 5;
                        case 5: return 6;
                        case 6: return 7;
                        case 4: return 9;
                        default: return 0;
                    }
                };
                const auto party_count = std::max<std::size_t>(
                    1, std::min<std::size_t>(state.u16(0x10), 4));
                std::size_t actor = 0;
                bool actor_cancelled = false;
                // RPG.EXE:3f53 selects an equipment recipient through the
                // same 2634/2fa5 directional portrait screen before it opens
                // the equipment layout. The actor is not cycled inside the
                // subsequent slot list.
                while (true) {
                    auto actor_frame = action_frame;
                    draw_rpg_party_target_cards(
                        actor_frame, menu_sprites_, state, party_count, actor);
                    platform_.present({
                        320, 200, actor_frame.pixels,
                        std::span<const std::uint8_t, 768>(actor_frame.palette)});
                    const auto actor_action = platform_.wait_for_input();
                    if (actor_action == InputAction::quit) {
                        quit_requested_ = true;
                        return InventoryUiResult::cancelled;
                    }
                    if (actor_action == InputAction::cancel) {
                        actor_cancelled = true;
                        break;
                    }
                    if (const auto directional_target =
                            rpg_party_target_for_direction(
                                actor_action, party_count)) {
                        actor = *directional_target;
                    } else if (actor_action == InputAction::confirm) {
                        break;
                    }
                }
                if (actor_cancelled) continue;
                if (inventory.character_restricted(definition, actor)) {
                    // 3f7f emits DATA:369a and returns before constructing
                    // the equipment screen for a forbidden identity.
                    auto error_frame = action_frame;
                    draw_rpg_party_target_cards(
                        error_frame, menu_sprites_, state, party_count, actor);
                    if (!show_bottom_message(
                            std::move(error_frame), equipment_actor_error_)) {
                        return InventoryUiResult::cancelled;
                    }
                    continue;
                }
                auto equipment_slot = initial_slot(definition.equipment_category());
                bool back_to_inventory = false;
                while (!back_to_inventory) {
                    // 425b swaps the inventory word and then writes the
                    // returned equipment id back to DATA:3612 before 42c6
                    // jumps to 3feb.  Consequently both the compact title
                    // and subsequent category checks use the newly returned
                    // object, not the item that originally opened this page.
                    const auto equipment_item = inventory.item(selected);
                    auto equipment_frame = action_frame;
                    // RPG.EXE:3fd9/46b1 uses a full 8x11 MENU selector panel.
                    draw_rpg_selector_panel(equipment_frame.pixels, 320, 200,
                                            menu_sprites_, 0, 0, 8, 11);
                    // ME02.RSK is decoded into the adjacent DOS segment and
                    // frame zero is copied opaquely at mode-X (10,40).
                    if (!equipment_art_.sprites().empty()) {
                        const auto& art = equipment_art_.sprites().front();
                        blit_opaque(equipment_frame, equipment_art_.pixels(0),
                                    art.width, art.height, 10 * 4, 40);
                    }
                    // DATA:3768 defaults to one here, so 2781 creates a
                    // single-row item-name card at (48,0), width eight.
                    draw_rpg_compact_panel(equipment_frame.pixels, 320, 200,
                                           menu_sprites_, 48, 0, 8, 1);
                    draw_item_text(equipment_frame, item_texts_, item_font_, equipment_item,
                                   51 * 4, 9, 96, 15, 0);
                    const auto actor_base = 0x106 + actor * 0x9f;
                    draw_legacy_text(equipment_frame, item_font_,
                                     equipment_slot_labels_, 6 * 4, 13,
                                     80, 176, 0);
                    for (std::size_t slot = 0; slot < equipment_slot_count; ++slot) {
                        const auto equipped = state.u16(actor_base + 0x10 + slot * 2);
                        draw_item_text(equipment_frame, item_texts_, item_font_, equipped,
                                       28 * 4, 13 + static_cast<int>(slot) * 16,
                                       96, 15, 0);
                    }
                    draw_rpg_actor_card(equipment_frame, menu_sprites_, state,
                                        actor, 52 * 4, 35);
                    // 4085..40af walks four adjacent $$ strings and the
                    // actor words +0c,+0e,+5d,+65. Text drawing advances X
                    // from byte 48 to 64; the values then use MENU 111..120.
                    static constexpr std::array<std::size_t, 4> stat_offsets{
                        0x0c, 0x0e, 0x5d, 0x65};
                    std::size_t label_cursor = 0;
                    auto stat_y = 90;
                    for (const auto stat_offset : stat_offsets) {
                        auto label_end = label_cursor;
                        while (label_end + 1U < equipment_stat_labels_.size() &&
                               !(equipment_stat_labels_[label_end] == '$' &&
                                 equipment_stat_labels_[label_end + 1U] == '$')) {
                            ++label_end;
                        }
                        if (label_end + 1U >= equipment_stat_labels_.size()) break;
                        draw_legacy_text(
                            equipment_frame, item_font_,
                            equipment_stat_labels_.subspan(
                                label_cursor, label_end - label_cursor),
                            48 * 4, stat_y, 64, 16, 0xbc);
                        draw_menu_number(equipment_frame, menu_sprites_,
                                         state.u16(actor_base + stat_offset),
                                         64, stat_y + 3, 111);
                        label_cursor = label_end + 2U;
                        stat_y += 22;
                    }
                    if (menu_sprites_.sprites().size() > 1U) {
                        const auto& cursor = menu_sprites_.sprites()[1];
                        blit(equipment_frame, menu_sprites_.pixels(1),
                             cursor.width, cursor.height, 4 * 4,
                             9 + static_cast<int>(equipment_slot) * 16);
                    }
                    platform_.present({320, 200, equipment_frame.pixels,
                                       std::span<const std::uint8_t, 768>(equipment_frame.palette)});
                    const auto equipment_action = platform_.wait_for_input();
                    if (equipment_action == InputAction::quit) {
                        quit_requested_ = true;
                        return InventoryUiResult::cancelled;
                    }
                    if (equipment_action == InputAction::cancel) {
                        back_to_inventory = true;
                    } else if (equipment_action == InputAction::up) {
                        if (equipment_slot != 0) --equipment_slot;
                    } else if (equipment_action == InputAction::down) {
                        if (equipment_slot + 1U < equipment_slot_count) {
                            ++equipment_slot;
                        }
                    } else if (equipment_action == InputAction::confirm) {
                        const auto exchange = inventory.exchange_equipment(
                            selected, actor, equipment_slot);
                        if (exchange.status != EquipmentExchangeStatus::exchanged) {
                            auto message = equipment_slot_error_;
                            if (exchange.status ==
                                EquipmentExchangeStatus::two_handed_conflict) {
                                message = equipment_two_hand_error_;
                            } else if (exchange.status ==
                                       EquipmentExchangeStatus::character_restricted) {
                                message = equipment_actor_error_;
                            }
                            // 4241 presents the 370e/3728 stream over the
                            // preserved equipment page, then reconstructs
                            // 3feb after the acknowledgement.
                            if (!show_bottom_message(
                                    std::move(equipment_frame), message)) {
                                return InventoryUiResult::cancelled;
                            }
                        }
                    }
                }
                // 42c9 restores the shared list globals and always calls
                // 42ff before returning to 39ed, even when no swap occurred.
                // That pass recomputes the five maximum equipment traits
                // from all eleven actor slots.
                inventory.recalculate_equipment_traits(actor);
            }
        }
    }

    // RPG.EXE:2e63 is the field menu entered by the second action key.  It is
    // a directional diamond, not a conventional vertical host menu: Magic,
    // Item, System and Status are selected directly by Up/Right/Down/Left.
    // Each branch connects to its reconstructed state machine.  The Magic
    // branch uses RPG's embedded 20-byte records and actor ability slots;
    // uncommon special handlers still remain explicit instead of being
    // silently approximated by a native UI.
    [[nodiscard]] bool run_field_menu() {
        std::size_t selected = 2;  // 2e63 initializes DATA:35e2 to System.
        while (true) {
            auto frame = scene_provider_();
            draw_field_diamond(frame, selected, true);

            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action == InputAction::cancel) return false;
            if (action == InputAction::up) selected = 0;
            else if (action == InputAction::right) selected = 1;
            else if (action == InputAction::down) selected = 2;
            else if (action == InputAction::left) selected = 3;
            else if (action == InputAction::confirm && selected == 0U) {
                if (run_magic_menu()) return true;
                if (quit_requested_) return false;
            } else if (action == InputAction::confirm && selected == 1U) {
                const auto result = run_inventory(
                    state_, InventoryUiMode::general);
                if (quit_requested_) return false;
                if (result == InventoryUiResult::map_reload) return true;
            } else if (action == InputAction::confirm && selected == 2U) {
                if (run_system_menu()) return true;
                if (quit_requested_) return false;
            } else if (action == InputAction::confirm && selected == 3U) {
                run_status_menu();
                if (quit_requested_) return false;
            }
        }
    }

    [[nodiscard]] bool quit_requested() const noexcept { return quit_requested_; }
    [[nodiscard]] bool abort_requested() const override {
        return quit_requested_;
    }

private:
    [[nodiscard]] std::optional<std::uint16_t> inventory_row_value(
        const SharedState& state, std::uint16_t item_id) const {
        if (item_id >= items_.size()) return std::nullopt;

        // RPG 3dc4 checks extended ITEM ids before every other special case.
        // Their +34h word is doubled by the literal SHL before 2315 draws it.
        if (item_id >= 0x13aU) {
            return static_cast<std::uint16_t>(
                items_.at(item_id).alchemy_required_level << 1U);
        }
        if (item_id >= 0x44U && item_id <= 0x48U) {
            return state.u16(0x3e6U + (item_id - 0x44U) * 2U);
        }

        // Type-10 talismans show the same DATA:1dce +10h resource cost that
        // 3857 later charges when the item is used.
        if (items_.at(item_id).type != 0x10U || item_id < 0x8cU) {
            return std::nullopt;
        }
        const auto record_offset =
            static_cast<std::size_t>(item_id - 0x8cU) * 20U;
        if (record_offset + 18U > field_ability_records_.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(
            field_ability_records_[record_offset + 16U] |
            (static_cast<std::uint16_t>(
                 field_ability_records_[record_offset + 17U]) << 8U));
    }

    void draw_alchemy_stats(Viewport& frame,
                            const ItemDefinition& definition,
                            std::span<const std::uint8_t> labels,
                            int label_x_byte, int label_y,
                            int left_x_byte, int right_x_byte,
                            int value_y) const {
        draw_legacy_text(frame, item_font_, labels,
                         label_x_byte * 4, label_y,
                         320 - label_x_byte * 4, 160, 0);
        // 45bc writes level/life/strength/defense down the left column;
        // 45d9 writes wisdom/magic/agility/dodge down the right.
        static constexpr std::array<std::size_t, 4> left{0, 2, 4, 6};
        static constexpr std::array<std::size_t, 4> right{1, 3, 5, 7};
        for (std::size_t row = 0; row < 4U; ++row) {
            draw_menu_number(frame, menu_sprites_,
                             definition.alchemy_stats[left[row]],
                             left_x_byte, value_y + static_cast<int>(row) * 16,
                             111);
            draw_menu_number(frame, menu_sprites_,
                             definition.alchemy_stats[right[row]],
                             right_x_byte, value_y + static_cast<int>(row) * 16,
                             111);
        }
    }

    Viewport item_description_frame(
        const Viewport& source, const ItemDefinition& definition) const {
        auto frame = source;
        if (!definition.shows_effect_panel()) return frame;
        // 3c90..3cd8: extended products display their eight ITEM values on a
        // 5x4 compact card before 49d0 overlays the ITEM2 description.
        draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                               24, 29, 5, 4);
        draw_alchemy_stats(frame, definition, item_effect_labels_,
                           30, 42, 42, 63, 45);
        return frame;
    }

    Viewport alchemy_inventory_frame(
        SharedState& state, const InventorySystem& inventory,
        std::size_t selected, std::size_t first_visible,
        RpgListSelection::ScrollCue scroll_cue) {
        auto frame = scene_provider_();
        draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                24, 36, 5, 8);
        draw_rpg_selector_scrollbar(
            frame.pixels, 320, 200, menu_sprites_,
            24, 36, 5, 8, 42, first_visible, scroll_cue);
        if (!menu_sprites_.sprites().empty()) {
            const auto& preview = menu_sprites_.sprites()[0];
            blit_opaque(frame, menu_sprites_.pixels(0),
                        preview.width, preview.height, 58 * 4, 8);
        }

        const auto selected_item = inventory.item(selected);
        if (selected_item < items_.size()) {
            const auto& definition = items_.at(selected_item);
            draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                                   2, 10, 4, 6);
            auto found = item_preview_cache_.find(definition.preview_sprite);
            if (found == item_preview_cache_.end()) {
                const auto path = rpg_item_preview_path(
                    game_root_, definition.preview_sprite);
                if (std::filesystem::is_regular_file(path)) {
                    auto preview_data = decode_rsk_block(read_file(path)).data;
                    found = item_preview_cache_.emplace(
                        definition.preview_sprite,
                        SpriteArchive::parse(std::move(preview_data))).first;
                }
            }
            if (found != item_preview_cache_.end() &&
                !found->second.sprites().empty()) {
                const auto& preview = found->second.sprites().front();
                blit(frame, found->second.pixels(0), preview.width,
                     preview.height,
                     (4 + static_cast<int>(definition.preview_x)) * 4,
                     18 + static_cast<int>(definition.preview_y));
            }
            const auto label = static_cast<std::size_t>(definition.type) * 4U;
            if (selected_item != 0U &&
                label + 4U <= inventory_category_labels_.size()) {
                draw_legacy_text(
                    frame, item_font_,
                    inventory_category_labels_.subspan(label, 4),
                    62 * 4, 17, 32, 16, 0);
            }
        }
        for (std::size_t row = 0; row < 8U; ++row) {
            const auto slot = first_visible + row;
            if (slot >= inventory_slot_count) break;
            const auto item_id = inventory.item(slot);
            const auto top = 49 + static_cast<int>(row) * 16;
            draw_item_text(frame, item_texts_, item_font_, item_id,
                           128, top, 96, 15, 0);
            if (const auto value = inventory_row_value(state, item_id)) {
                draw_menu_number(
                    frame, menu_sprites_, *value,
                    56, top + 3, 111);
            }
        }
        if (menu_sprites_.sprites().size() > 1U) {
            const auto& cursor = menu_sprites_.sprites()[1];
            blit(frame, menu_sprites_.pixels(1), cursor.width, cursor.height,
                 30 * 4,
                 45 + static_cast<int>(selected - first_visible) * 16);
        }
        return frame;
    }

    std::optional<std::size_t> select_alchemy_ingredient(
        SharedState& state, const InventorySystem& inventory,
        std::uint16_t first_item) {
        std::size_t selected = 0;
        std::size_t first_visible = 0;
        auto scroll_cue = RpgListSelection::ScrollCue::none;
        while (true) {
            auto frame = alchemy_inventory_frame(
                state, inventory, selected, first_visible, scroll_cue);
            scroll_cue = RpgListSelection::ScrollCue::none;
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return std::nullopt;
            }
            if (action == InputAction::cancel) return std::nullopt;
            if (action != InputAction::confirm) {
                const auto choice = rpg_list_selection_input(
                    {selected, first_visible}, inventory_slot_count, 8, action);
                selected = choice.selected;
                first_visible = choice.first_visible;
                scroll_cue = choice.scroll_cue;
                continue;
            }
            const auto item = inventory.item(selected);
            if (item == 0U || item >= items_.size()) continue;
            if ((items_.at(item).flags & 0xc0U) == 0xc0U) {
                if (!show_bottom_message(frame, item_alchemy_error_)) {
                    return std::nullopt;
                }
                continue;
            }
            auto pair = frame;
            draw_rpg_compact_panel(pair.pixels, 320, 200, menu_sprites_,
                                   4, 112, 7, 4);
            draw_item_text(pair, item_texts_, item_font_, first_item,
                           8 * 4, 125, 112, 16, 0);
            draw_item_text(pair, item_texts_, item_font_, item,
                           40 * 4, 125, 112, 16, 0);
            if (confirm_alchemy_product(pair)) return selected;
            if (quit_requested_) return std::nullopt;
            return std::nullopt;
        }
    }

    Viewport alchemy_product_frame(std::uint16_t product) {
        auto frame = scene_provider_();
        draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                0, 0, 8, 11);
        draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                               4, 12, 5, 4);
        draw_item_text(frame, item_texts_, item_font_, product,
                       6 * 4, 21, 144, 16, 0);
        if (product < items_.size()) {
            const auto& definition = items_.at(product);
            auto found = item_preview_cache_.find(definition.preview_sprite);
            if (found == item_preview_cache_.end()) {
                const auto path = rpg_item_preview_path(
                    game_root_, definition.preview_sprite);
                if (std::filesystem::is_regular_file(path)) {
                    auto preview_data = decode_rsk_block(read_file(path)).data;
                    found = item_preview_cache_.emplace(
                        definition.preview_sprite,
                        SpriteArchive::parse(std::move(preview_data))).first;
                }
            }
            if (found != item_preview_cache_.end() &&
                !found->second.sprites().empty()) {
                const auto& preview = found->second.sprites().front();
                blit(frame, found->second.pixels(0), preview.width,
                     preview.height, 30 * 4, 40);
            }
            draw_alchemy_stats(frame, definition,
                               item_alchemy_result_labels_,
                               8, 50, 20, 64, 53);
        }
        return frame;
    }

    bool confirm_alchemy_product(const Viewport& source) {
        std::size_t choice = 0;
        while (true) {
            auto frame = source;
            const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
                if (sprite >= menu_sprites_.sprites().size()) return;
                const auto& info = menu_sprites_.sprites()[sprite];
                blit_opaque(frame, menu_sprites_.pixels(sprite),
                            info.width, info.height, x_byte * 4, y);
            };
            opaque(0, 23, 147);
            opaque(0, 41, 147);
            opaque(29, 29, 156);
            opaque(8, 47, 156);
            apply_rpg_binary_choice_highlight(
                frame.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(frame.palette),
                23, 41, 147, choice);
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action == InputAction::cancel) return false;
            if (action == InputAction::left) choice = 0;
            else if (action == InputAction::right) choice = 1;
            else if (action == InputAction::confirm) return choice == 0U;
        }
    }

    Viewport item_action_choice_frame(const Viewport& source,
                                      bool equipment,
                                      bool has_alchemy,
                                      std::size_t selected) const {
        auto frame = source;
        const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
            if (sprite >= menu_sprites_.sprites().size()) return;
            const auto& info = menu_sprites_.sprites()[sprite];
            blit_opaque(frame, menu_sprites_.pixels(sprite),
                        info.width, info.height, x_byte * 4, y);
        };

        // RPG:2d0f.  Index order follows DATA:35ca/35d2: left, right,
        // upper, lower. All frames and glyphs use 653a's opaque copy path.
        opaque(0, 12, 106);
        opaque(0, 2, 138);
        opaque(0, 22, 138);
        if (equipment) {
            opaque(66, 6, 147);   // 裝
            opaque(67, 10, 147);  // 備
        } else {
            opaque(70, 6, 147);   // 使
            opaque(27, 10, 147);  // 用
        }
        opaque(28, 26, 147);      // 說
        opaque(9, 30, 147);       // 明
        opaque(2, 16, 115);       // 丟
        opaque(3, 20, 115);       // 棄
        if (has_alchemy) {
            opaque(0, 12, 170);
            opaque(47, 14, 179);  // 煉妖壺
            opaque(48, 18, 179);
            opaque(49, 22, 179);
        }

        static constexpr std::array<std::pair<int, int>, 4> positions{{
            {2, 138}, {22, 138}, {12, 106}, {12, 170},
        }};
        const auto count = has_alchemy ? std::size_t{4} : std::size_t{3};
        selected = std::min(selected, count - 1U);
        for (std::size_t choice = 0; choice < count; ++choice) {
            if (choice == selected) continue;
            apply_fig_palette_translation(
                frame.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(frame.palette),
                positions[choice].first, positions[choice].second,
                0x10, 0x20, 3);
        }
        return frame;
    }

    std::optional<std::size_t> select_item_action(
        const Viewport& source, bool equipment, bool has_alchemy,
        std::size_t& selected) {
        const auto count = has_alchemy ? std::size_t{4} : std::size_t{3};
        if (selected >= count) selected = 0;
        while (true) {
            auto frame = item_action_choice_frame(
                source, equipment, has_alchemy, selected);
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return std::nullopt;
            }
            if (action == InputAction::cancel) return std::nullopt;
            if (action == InputAction::left) selected = 0;
            else if (action == InputAction::right) selected = 1;
            else if (action == InputAction::up) selected = 2;
            else if (action == InputAction::down && has_alchemy) selected = 3;
            else if (action == InputAction::confirm) return selected;
        }
    }

    bool confirm_item_discard(const Viewport& source) {
        std::size_t choice = 0;
        while (true) {
            auto frame = source;
            draw_bottom_message(frame, item_discard_prompt_);
            const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
                if (sprite >= menu_sprites_.sprites().size()) return;
                const auto& info = menu_sprites_.sprites()[sprite];
                blit_opaque(frame, menu_sprites_.pixels(sprite),
                            info.width, info.height, x_byte * 4, y);
            };
            // 46cc: Yes/No cards inside 2cce's bottom panel.
            opaque(0, 23, 147);
            opaque(0, 41, 147);
            opaque(29, 29, 156);
            opaque(8, 47, 156);
            apply_rpg_binary_choice_highlight(
                frame.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(frame.palette),
                23, 41, 147, choice);
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action == InputAction::cancel) return false;
            if (action == InputAction::left) choice = 0;
            else if (action == InputAction::right) choice = 1;
            else if (action == InputAction::confirm) return choice == 0U;
        }
    }

    void draw_field_diamond(Viewport& frame, std::size_t selected,
                            bool draw_party_row) {
        draw_compact_money_overlay(
            frame, menu_sprites_, state_.u16(0x104));
        const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
            if (sprite >= menu_sprites_.sprites().size()) return;
            const auto& info = menu_sprites_.sprites()[sprite];
            blit_opaque(frame, menu_sprites_.pixels(sprite),
                        info.width, info.height, x_byte * 4, y);
        };
        const auto transparent = [&](std::size_t sprite, int x_byte, int y) {
            if (sprite >= menu_sprites_.sprites().size()) return;
            const auto& info = menu_sprites_.sprites()[sprite];
            blit(frame, menu_sprites_.pixels(sprite),
                 info.width, info.height, x_byte * 4, y);
        };

        opaque(0, 16, 8);
        opaque(0, 16, 72);
        opaque(0, 6, 40);
        opaque(0, 26, 40);
        transparent(6, 20, 17);
        transparent(7, 24, 17);
        transparent(35, 20, 81);
        transparent(36, 24, 81);
        transparent(58, 10, 49);
        transparent(62, 14, 49);
        transparent(56, 30, 49);
        transparent(57, 34, 49);

        static constexpr std::array<std::pair<int, int>, 4> card_positions{{
            {16, 8}, {26, 40}, {16, 72}, {6, 40},
        }};
        for (std::size_t choice = 0; choice < card_positions.size(); ++choice) {
            if (choice == selected) continue;
            apply_fig_palette_translation(
                frame.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(frame.palette),
                card_positions[choice].first,
                card_positions[choice].second,
                0x10, 0x20, 3);
        }
        if (!draw_party_row) return;

        // 2448 is only part of the top-level diamond.  The 2fb7/2f64 actor
        // pages instead retain 211a underneath and overlay 2634's directional
        // cards, so callers can explicitly omit this horizontal row.
        const auto party_count = std::max<std::size_t>(
            1, std::min<std::size_t>(state_.u16(0x10), 4));
        for (std::size_t actor = 0; actor < party_count; ++actor) {
            draw_rpg_actor_card(frame, menu_sprites_, state_, actor,
                                (8 + static_cast<int>(actor) * 18) * 4, 136);
        }
    }

    void draw_magic_ability_info(Viewport& frame, std::size_t actor,
                                 std::span<const std::uint8_t> record) const {
        const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
            if (sprite >= menu_sprites_.sprites().size()) return;
            const auto& info = menu_sprites_.sprites()[sprite];
            blit_opaque(frame, menu_sprites_.pixels(sprite),
                        info.width, info.height, x_byte * 4, y);
        };

        // 2fed always places the selected-entry frame at (56,8).  2b03
        // fills it with the two resource glyphs for this record type and
        // builds a one-row compact value card at (4,104).
        opaque(0, 56, 8);
        draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                               4, 104, 4, 1);
        const auto type = static_cast<std::uint8_t>(record[13] & 0x0fU);
        if (type == 0U || type > 5U) return;
        std::pair<std::size_t, std::size_t> icons;
        if (type == 4U) icons = {59U, 60U};
        else if (type == 2U || type == 3U) icons = {45U, 46U};
        else if (type == 1U) icons = {20U, 74U};
        else icons = {61U, 62U};
        opaque(icons.first, 60, 17);
        opaque(icons.second, 64, 17);

        const auto label_offset = static_cast<std::size_t>(type - 1U) * 4U;
        if (label_offset + 4U <= ability_resource_labels_.size()) {
            draw_legacy_text(
                frame, item_font_,
                ability_resource_labels_.subspan(label_offset, 4U),
                6 * 4, 113, 32, 16, 0);
        }
        if (type != 5U) {
            const auto actor_base = 0x106U + actor * 0x9fU;
            const auto value = type == 2U || type == 3U
                                   ? state_.u16(actor_base + 0x35U)
                                   : state_.u16(actor_base + 0x55U);
            draw_menu_number(frame, menu_sprites_, value, 15, 116, 111);
        }
    }

    Viewport magic_action_choice_frame(const Viewport& source,
                                       bool can_refine,
                                       std::size_t selected) const {
        auto frame = source;
        const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
            if (sprite >= menu_sprites_.sprites().size()) return;
            const auto& info = menu_sprites_.sprites()[sprite];
            blit_opaque(frame, menu_sprites_.pixels(sprite),
                        info.width, info.height, x_byte * 4, y);
        };

        // RPG:2c0b uses the same 64x32 opaque MENU cards and table-3
        // highlight as the other binary selectors.  The two bottom cards are
        // MENU 70/27 (Use) and 28/9 (Explain). Type four adds the upper
        // MENU 71/59 (Refine talisman) card.
        opaque(0, 4, 138);
        opaque(0, 20, 138);
        opaque(70, 8, 147);
        opaque(27, 12, 147);
        opaque(28, 24, 147);
        opaque(9, 28, 147);
        if (can_refine) {
            opaque(0, 12, 106);
            opaque(71, 16, 115);
            opaque(59, 20, 115);
        }

        static constexpr std::array<std::pair<int, int>, 3> positions{{
            {4, 138}, {20, 138}, {12, 106},
        }};
        const auto count = can_refine ? 3U : 2U;
        for (std::size_t choice = 0; choice < count; ++choice) {
            if (choice == selected) continue;
            apply_fig_palette_translation(
                frame.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(frame.palette),
                positions[choice].first, positions[choice].second,
                0x10, 0x20, 3);
        }
        return frame;
    }

    std::optional<std::size_t> select_magic_action(
        const Viewport& source, bool can_refine, std::uint8_t ability_id,
        std::size_t& selected) {
        while (true) {
            auto frame = magic_action_choice_frame(source, can_refine, selected);
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return std::nullopt;
            }
            if (action == InputAction::cancel) return std::nullopt;
            if (action == InputAction::left) selected = 0;
            else if (action == InputAction::right) selected = 1;
            else if (action == InputAction::up && can_refine) selected = 2;
            else if (action == InputAction::down && selected == 2U) selected = 0;
            else if (action == InputAction::confirm) {
                if (selected != 1U) return selected;
                if (ability_id < ability_descriptions_.size() &&
                    !ability_descriptions_[ability_id].empty()) {
                    // 2f1a copies the DATE2 record to DATA:4a30 and 4aec
                    // displays it through the ordinary 49d0 bottom message.
                    if (!show_bottom_message(
                            std::move(frame), ability_descriptions_[ability_id])) {
                        return std::nullopt;
                    }
                }
            }
        }
    }

    std::optional<std::uint8_t> select_travel_destination(
        SharedState& state) {
        FieldActionSystem field_actions(state, &field_action_runtime_);
        const auto destinations = field_actions.unlocked_travel_indices();
        if (destinations.empty()) return std::nullopt;
        std::size_t selected = 0;
        std::size_t first_visible = 0;
        auto scroll_cue = RpgListSelection::ScrollCue::none;
        while (true) {
            auto frame = scene_provider_();
            const auto visible = std::min<std::size_t>(destinations.size(), 10U);
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    14, 16, 5, static_cast<int>(visible));
            draw_rpg_selector_scrollbar(
                frame.pixels, 320, 200, menu_sprites_,
                14, 16, 5, static_cast<int>(visible),
                destinations.size() - visible, first_visible, scroll_cue);
            scroll_cue = RpgListSelection::ScrollCue::none;
            for (std::size_t row = 0; row < visible; ++row) {
                const auto index = first_visible + row;
                const auto label = static_cast<std::size_t>(destinations[index]) * 8U;
                if (label + 8U <= travel_labels_.size()) {
                    draw_legacy_text(
                        frame, item_font_, travel_labels_.subspan(label, 8),
                        22 * 4, 29 + static_cast<int>(row) * 16,
                        64, 16, 0);
                }
            }
            if (menu_sprites_.sprites().size() > 1U) {
                const auto& cursor = menu_sprites_.sprites()[1];
                blit(frame, menu_sprites_.pixels(1),
                     cursor.width, cursor.height, 20 * 4,
                     25 + static_cast<int>(selected - first_visible) * 16);
            }
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return std::nullopt;
            }
            if (action == InputAction::cancel) return std::nullopt;
            if (action == InputAction::confirm) return destinations[selected];
            const auto selection = rpg_list_selection_input(
                {selected, first_visible}, destinations.size(), visible, action);
            selected = selection.selected;
            first_visible = selection.first_visible;
            scroll_cue = selection.scroll_cue;
        }
    }

    [[nodiscard]] bool run_magic_menu() {
        const auto party_count = std::max<std::size_t>(
            1, std::min<std::size_t>(state_.u16(0x10), 4));
        std::size_t actor = 0;
        while (true) {
            while (true) {
                auto actor_frame = scene_provider_();
                draw_field_diamond(actor_frame, 0U, false);
                draw_rpg_party_target_cards(
                    actor_frame, menu_sprites_, state_, party_count, actor);
                platform_.present({
                    320, 200, actor_frame.pixels,
                    std::span<const std::uint8_t, 768>(actor_frame.palette)});
                const auto action = platform_.wait_for_input();
                if (action == InputAction::quit) {
                    quit_requested_ = true;
                    return false;
                }
                if (action == InputAction::cancel) return false;
                if (const auto target = rpg_party_target_for_direction(
                        action, party_count)) {
                    actor = *target;
                } else if (action == InputAction::confirm) {
                    break;
                }
            }

            std::size_t selected = 0;
            std::size_t first_visible = 0;
            auto scroll_cue = RpgListSelection::ScrollCue::none;
            bool back_to_actor = false;
            while (!back_to_actor) {
                auto frame = scene_provider_();
                const auto actor_base = 0x106U + actor * 0x9fU;
                const auto selected_id =
                    state_.u8(actor_base + 0x6dU + selected);
                const auto selected_record_offset =
                    static_cast<std::size_t>(selected_id) * 20U;
                draw_rpg_actor_card(frame, menu_sprites_, state_, actor,
                                    8 * 4, 21);
                if (selected_record_offset + 20U <=
                    field_ability_records_.size()) {
                    draw_magic_ability_info(
                        frame, actor,
                        field_ability_records_.subspan(
                            selected_record_offset, 20U));
                }
                draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                        24, 36, 5, 8);
                draw_rpg_selector_scrollbar(
                    frame.pixels, 320, 200, menu_sprites_,
                    24, 36, 5, 8, 42, first_visible, scroll_cue);
                scroll_cue = RpgListSelection::ScrollCue::none;
                for (std::size_t row = 0; row < 8U; ++row) {
                    const auto slot = first_visible + row;
                    const auto id = state_.u8(actor_base + 0x6dU + slot);
                    const auto record_offset = static_cast<std::size_t>(id) * 20U;
                    if (record_offset + 20U > field_ability_records_.size()) continue;
                    const auto record = field_ability_records_.subspan(record_offset, 20);
                    const auto color = static_cast<std::uint8_t>(
                        (record[13] & 0x80U) != 0U ? 0x6bU : 0U);
                    const auto top = 49 + static_cast<int>(row) * 16;
                    draw_legacy_text(frame, item_font_, record.first(12),
                                     32 * 4, top, 96, 16, color);
                    const auto type = static_cast<std::uint8_t>(record[13] & 0x0fU);
                    const auto parameter = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(record[16]) |
                        (static_cast<std::uint16_t>(record[17]) << 8U));
                    if (type != 0U && type != 5U) {
                        draw_menu_number(frame, menu_sprites_, parameter,
                                         54, top + 3, 111);
                    } else if (type == 5U) {
                        static constexpr std::array<std::array<std::uint8_t, 2>, 5>
                            elements{{{{0xaa, 0xf7}}, {{0xa4, 0xec}}, {{0xa4, 0xf4}},
                                      {{0xa4, 0xf5}}, {{0xa4, 0x67}}}};
                        auto x_byte = 54;
                        auto bit = std::uint8_t{0x10};
                        for (const auto& element : elements) {
                            if ((parameter & bit) != 0U) {
                                draw_legacy_text(frame, item_font_, element,
                                                 x_byte * 4, top, 16, 16, 1);
                                x_byte += 4;
                            }
                            bit >>= 1U;
                        }
                    }
                }
                if (menu_sprites_.sprites().size() > 1U) {
                    const auto& cursor = menu_sprites_.sprites()[1];
                    blit(frame, menu_sprites_.pixels(1),
                         cursor.width, cursor.height, 30 * 4,
                         45 + static_cast<int>(selected - first_visible) * 16);
                }
                platform_.present({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette)});
                const auto action = platform_.wait_for_input();
                if (action == InputAction::quit) {
                    quit_requested_ = true;
                    return false;
                }
                if (action == InputAction::cancel) {
                    back_to_actor = true;
                    continue;
                }
                if (action != InputAction::confirm) {
                    const auto selection = rpg_list_selection_input(
                        {selected, first_visible}, 50, 8, action);
                    selected = selection.selected;
                    first_visible = selection.first_visible;
                    scroll_cue = selection.scroll_cue;
                    continue;
                }

                const auto id = state_.u8(actor_base + 0x6dU + selected);
                const auto record_offset = static_cast<std::size_t>(id) * 20U;
                if (id == 0U || record_offset + 20U > field_ability_records_.size()) {
                    continue;
                }
                const auto record = field_ability_records_.subspan(record_offset, 20);
                const auto effect = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(record[14]) |
                    (static_cast<std::uint16_t>(record[15]) << 8U));
                const auto cost = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(record[16]) |
                    (static_cast<std::uint16_t>(record[17]) << 8U));
                const auto type = static_cast<std::uint8_t>(record[13] & 0x0fU);
                if (effect == 0U) continue;

                bool action_cancelled = false;
                std::size_t action_choice = 0;
                while (true) {
                    const auto selected_action = select_magic_action(
                        frame, type == 4U, id, action_choice);
                    if (quit_requested_) return false;
                    if (!selected_action) {
                        action_cancelled = true;
                        break;
                    }
                    if (*selected_action == 0U) break;

                    // The third 2c0b option is not another field effect.
                    // RPG:333d..337a creates the type-10 battle item
                    // ability_id+8ch in physical inventory slot 49, compacts
                    // it forward, and deliberately charges no ability points.
                    if ((state_.u16(actor_base + 8U) & 0xe000U) != 0U) {
                        auto action_frame = magic_action_choice_frame(
                            frame, true, *selected_action);
                        if (!show_bottom_message(
                                std::move(action_frame), ability_dead_error_)) {
                            return false;
                        }
                        continue;
                    }
                    if (state_.u16(0x3e4U) != 0U) {
                        auto action_frame = magic_action_choice_frame(
                            frame, true, *selected_action);
                        if (!show_bottom_message(
                                std::move(action_frame), ability_inventory_error_)) {
                            return false;
                        }
                        continue;
                    }
                    state_.set_u16(
                        0x3e4U, static_cast<std::uint16_t>(id + 0x8cU));
                    InventorySystem(state_, items_).compact();
                    platform_.delay_for(std::chrono::milliseconds(
                        (13U * 1000U + 69U) / 70U));
                }
                if (action_cancelled) continue;

                // Choice zero remains selected on the copied source page
                // while 3857 overlays the directional target cards or 4aec
                // overlays a validation message.
                const auto action_frame = magic_action_choice_frame(
                    frame, type == 4U, 0U);

                if ((state_.u16(actor_base + 8U) & 0xe000U) != 0U) {
                    if (!show_bottom_message(
                            action_frame, ability_dead_error_)) return false;
                    continue;
                }
                if ((record[13] & 0x80U) != 0U || effect > 0x29U) {
                    if (!show_bottom_message(
                            action_frame, field_action_error_)) return false;
                    continue;
                }

                std::optional<std::size_t> resource_offset;
                if (type == 1U || type == 4U) resource_offset = actor_base + 0x55U;
                else if (type == 2U || type == 3U) resource_offset = actor_base + 0x35U;
                if (resource_offset && state_.u16(*resource_offset) < cost) {
                    if (!show_bottom_message(
                            action_frame, ability_value_error_)) return false;
                    continue;
                }
                if (type == 5U) {
                    auto bit = std::uint8_t{0x10};
                    bool missing = false;
                    for (std::size_t material = 0; material < 5U; ++material) {
                        if ((cost & bit) != 0U &&
                            state_.u16(0x3e6U + material * 2U) == 0U) {
                            missing = true;
                        }
                        bit >>= 1U;
                    }
                    if (missing) {
                        if (!show_bottom_message(
                                action_frame, ability_material_error_)) return false;
                        continue;
                    }
                }

                std::optional<std::size_t> target_actor;
                if (FieldActionSystem::requires_target(effect)) {
                    target_actor = 0;
                    bool target_cancelled = false;
                    while (true) {
                        auto target_frame = action_frame;
                        draw_rpg_party_target_cards(
                            target_frame, menu_sprites_, state_, party_count,
                            *target_actor);
                        platform_.present({
                            320, 200, target_frame.pixels,
                            std::span<const std::uint8_t, 768>(target_frame.palette)});
                        const auto target_action = platform_.wait_for_input();
                        if (target_action == InputAction::quit) {
                            quit_requested_ = true;
                            return false;
                        }
                        if (target_action == InputAction::cancel) {
                            target_cancelled = true;
                            break;
                        }
                        if (const auto target = rpg_party_target_for_direction(
                                target_action, party_count)) {
                            *target_actor = *target;
                        } else if (target_action == InputAction::confirm) {
                            break;
                        }
                    }
                    if (target_cancelled) continue;
                }

                FieldActionSystem field_actions(
                    state_, &field_action_runtime_, actor);
                const auto result = field_actions.apply(effect, target_actor);
                if (!result.dispatched()) {
                    if (!show_bottom_message(
                            action_frame, field_action_error_)) return false;
                    continue;
                }

                std::optional<std::uint8_t> travel_index;
                if (result.status == FieldActionStatus::travel_current) {
                    const auto current = state_.u16(0x40a);
                    if (current <= 0xffU) {
                        travel_index = static_cast<std::uint8_t>(current);
                    }
                } else if (result.status == FieldActionStatus::travel_select) {
                    travel_index = select_travel_destination(state_);
                    if (quit_requested_) return false;
                    if (!travel_index) continue;
                }

                if (resource_offset) {
                    state_.set_u16(
                        *resource_offset,
                        static_cast<std::uint16_t>(
                            state_.u16(*resource_offset) - cost));
                } else if (type == 5U) {
                    InventorySystem inventory(state_, items_);
                    auto bit = std::uint8_t{0x10};
                    for (std::size_t material = 0; material < 5U; ++material) {
                        if ((cost & bit) != 0U) {
                            const auto quantity_offset = 0x3e6U + material * 2U;
                            const auto quantity = static_cast<std::uint16_t>(
                                state_.u16(quantity_offset) - 1U);
                            state_.set_u16(quantity_offset, quantity);
                            if (quantity == 0U) {
                                const auto item = static_cast<std::uint16_t>(0x44U + material);
                                for (std::size_t slot = 0; slot < inventory_slot_count; ++slot) {
                                    if (inventory.item(slot) == item) {
                                        state_.set_u16(0x382U + slot * 2U, 0);
                                        inventory.compact();
                                        break;
                                    }
                                }
                            }
                        }
                        bit >>= 1U;
                    }
                }

                if (travel_index) {
                    if (const auto directory =
                            FieldActionSystem::travel_directory_offset(*travel_index);
                        directory && map_database_ != nullptr) {
                        install_map_location(state_, *map_database_, *directory);
                        return true;
                    }
                }
            }
        }
    }

    void run_status_menu() {
        const auto party_count = std::max<std::size_t>(
            1, std::min<std::size_t>(state_.u16(0x10), 4));
        std::size_t actor = 0;
        while (true) {
            // 2f64 uses the common 2634 directional actor selector before
            // entering 26f3. A cancellation here returns to the field diamond.
            while (true) {
                auto actor_frame = scene_provider_();
                draw_field_diamond(actor_frame, 3U, false);
                draw_rpg_party_target_cards(
                    actor_frame, menu_sprites_, state_, party_count, actor);
                platform_.present({
                    320, 200, actor_frame.pixels,
                    std::span<const std::uint8_t, 768>(actor_frame.palette)});
                const auto action = platform_.wait_for_input();
                if (action == InputAction::quit) {
                    quit_requested_ = true;
                    return;
                }
                if (action == InputAction::cancel) return;
                if (const auto target = rpg_party_target_for_direction(
                        action, party_count)) {
                    actor = *target;
                } else if (action == InputAction::confirm) {
                    break;
                }
            }

            std::size_t selected = 0;
            std::size_t first_visible = 0;
            auto scroll_cue = RpgListSelection::ScrollCue::none;
            bool back_to_actor = false;
            while (!back_to_actor) {
                auto frame = scene_provider_();
                // 33a9/26f3 combines the selected actor card with the shared
                // eight-row selector. DATA:3806 contains 28 fixed 8-byte rows;
                // 37f5=20 is therefore the maximum first visible row.
                draw_rpg_actor_card(frame, menu_sprites_, state_, actor,
                                    8 * 4, 21);
                draw_rpg_compact_panel(frame.pixels, 320, 200, menu_sprites_,
                                       4, 72, 4, 1);
                draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                        24, 36, 5, 8);
                draw_rpg_selector_scrollbar(
                    frame.pixels, 320, 200, menu_sprites_,
                    24, 36, 5, 8, 20, first_visible, scroll_cue);
                scroll_cue = RpgListSelection::ScrollCue::none;

                const auto actor_base = 0x106U + actor * 0x9fU;
                const auto draw_pair = [&](std::uint16_t current,
                                           std::uint16_t maximum, int top) {
                    // 47f8 right-aligns the current value against x=52 and
                    // changes its MENU digit family at zero/quarter health.
                    // The maximum begins at x=56; MENU 38 is the slash at
                    // x=54,y+1. All coordinates are mode-X byte columns.
                    auto digits = std::size_t{1};
                    for (auto value = current; value >= 10U; value /= 10U) {
                        ++digits;
                    }
                    const auto current_base = current == 0U
                        ? 131U
                        : current <= static_cast<std::uint16_t>(maximum >> 2U)
                            ? 121U
                            : 111U;
                    draw_menu_number(
                        frame, menu_sprites_, current,
                        52 - static_cast<int>((digits - 1U) * 2U),
                        top + 3, current_base);
                    draw_menu_number(frame, menu_sprites_, maximum,
                                     56, top + 3, 111);
                    if (menu_sprites_.sprites().size() > 38U) {
                        const auto& slash = menu_sprites_.sprites()[38U];
                        blit(frame, menu_sprites_.pixels(38U),
                             slash.width, slash.height, 54 * 4, top + 1);
                    }
                };
                // RPG DATA:376a dispatches the post-status rows as strength,
                // wisdom, agility, magic, reaction, battle power, defence,
                // level and experience.  The status handler itself consumes
                // four visual rows (the three blank labels at 3..5), so these
                // offsets correspond to labels 7..15 rather than adjacent
                // entries in the function-pointer table.
                static constexpr std::array<std::size_t, 9> scalar_offsets{
                    0x3d, 0x45, 0x4d, 0x55, 0x5d,
                    0x0c, 0x0e, 0x31, 0x39};
                for (std::size_t row = 0; row < 8U; ++row) {
                    const auto index = first_visible + row;
                    const auto top = 49 + static_cast<int>(row) * 16;
                    const auto label = index * 8U;
                    if (label + 8U <= status_menu_labels_.size()) {
                        draw_legacy_text(
                            frame, item_font_,
                            status_menu_labels_.subspan(label, 8),
                            32 * 4, top, 48, 16, 0);
                    }
                    if (index == 0U) {
                        draw_pair(state_.u16(actor_base + 0x2d),
                                  state_.u16(actor_base + 0x2f), top);
                    } else if (index == 1U) {
                        draw_pair(state_.u16(actor_base + 0x35),
                                  state_.u16(actor_base + 0x37), top);
                    } else if (index == 2U &&
                               status_value_labels_.size() >= 15U * 4U) {
                        // RPG:4877/4960 expands the status word below the
                        // label. Zero, death and near-death are exclusive;
                        // otherwise set bits 1000h..0002h are listed in
                        // descending priority. 499a advances one row for each
                        // name and clips only outside y=49..161, so up to six
                        // simultaneous conditions can remain visible beside
                        // the later money/strength rows.
                        const auto bits = state_.u16(actor_base + 8U);
                        auto status_top = top;
                        const auto draw_status = [&](std::size_t entry) {
                            if (status_top >= 49 && status_top <= 161) {
                                draw_legacy_text(
                                    frame, item_font_,
                                    status_value_labels_.subspan(entry * 4U, 4U),
                                    42 * 4, status_top, 32, 16, 0);
                            }
                            status_top += 16;
                        };
                        for (const auto entry :
                             rpg_status_label_indices(bits)) {
                            draw_status(entry);
                        }
                    } else if (index == 6U) {
                        // 488b uses MENU 100 as the currency mark at x=46,
                        // then the 101..110 digit family from x=50. This is
                        // distinct from the 111..120 small numbers used by
                        // ordinary attributes and inventory quantities.
                        if (menu_sprites_.sprites().size() > 100U) {
                            const auto& currency =
                                menu_sprites_.sprites()[100U];
                            blit(frame, menu_sprites_.pixels(100U),
                                 currency.width, currency.height,
                                 46 * 4, top);
                        }
                        draw_menu_number(frame, menu_sprites_,
                                         state_.u16(0x104),
                                         50, top + 3, 101);
                    } else if (index >= 7U && index <= 15U) {
                        const auto offset = scalar_offsets[index - 7U];
                        if (index == 10U || index == 15U) {
                            draw_pair(state_.u16(actor_base + offset),
                                      state_.u16(actor_base + offset + 2U), top);
                        } else {
                            draw_menu_number(
                                frame, menu_sprites_,
                                state_.u16(actor_base + offset),
                                52, top + 3, 111);
                        }
                    } else if (index >= 17U && index <= 27U) {
                        const auto item = state_.u16(
                            actor_base + 0x10U + (index - 17U) * 2U);
                        // 490f subtracts ten mode-X columns from the common
                        // x=56 value and draws exactly six ITEM2 glyphs.
                        draw_item_text(frame, item_texts_, item_font_, item,
                                       46 * 4, top, 96, 16, 0);
                    }
                }
                if (menu_sprites_.sprites().size() > 1U) {
                    const auto& cursor = menu_sprites_.sprites()[1];
                    blit(frame, menu_sprites_.pixels(1),
                         cursor.width, cursor.height,
                         30 * 4,
                         45 + static_cast<int>(selected - first_visible) * 16);
                }
                platform_.present({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette)});
                const auto action = platform_.wait_for_input();
                if (action == InputAction::quit) {
                    quit_requested_ = true;
                    return;
                }
                if (action == InputAction::cancel) {
                    back_to_actor = true;
                    continue;
                }
                const auto selection = rpg_list_selection_input(
                    {selected, first_visible}, 28, 8, action);
                selected = selection.selected;
                first_visible = selection.first_visible;
                scroll_cue = selection.scroll_cue;
            }
        }
    }

    bool confirm_binary_choice(Viewport frame,
                               bool cancel_uses_current_choice = false) {
        std::uint8_t choice = 0;
        while (true) {
            auto shown = frame;
            const auto opaque = [&](std::size_t sprite, int x_byte, int y) {
                if (sprite >= menu_sprites_.sprites().size()) return;
                const auto& info = menu_sprites_.sprites()[sprite];
                blit_opaque(shown, menu_sprites_.pixels(sprite),
                            info.width, info.height, x_byte * 4, y);
            };
            // RPG 46cc: the shared Yes/No cards used by system exit and by
            // every five-value load/speed selection.
            opaque(0, 23, 147);
            opaque(0, 41, 147);
            opaque(29, 29, 156);
            opaque(8, 47, 156);
            apply_rpg_binary_choice_highlight(
                shown.pixels, 320, 200,
                std::span<const std::uint8_t, 768>(shown.palette),
                23, 41, 147, choice);
            platform_.present({
                320, 200, shown.pixels,
                std::span<const std::uint8_t, 768>(shown.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action == InputAction::cancel) {
                // 4e4b forgets to inspect DATA:3801 after nested 46cc, so
                // Escape accepts the current choice there. Direct callers
                // such as system exit do inspect it and therefore cancel.
                return cancel_uses_current_choice && choice == 0;
            }
            if (action == InputAction::left) choice = 0;
            else if (action == InputAction::right) choice = 1;
            else if (action == InputAction::confirm) return choice == 0;
        }
    }

    std::optional<std::size_t> select_system_value(Viewport base) {
        // 4e4b always writes DATA:60d5=0eh before its first MENU 141 cursor.
        // The current value is shown on the seven-row system page, but it is
        // not carried into this five-choice popup.
        std::size_t selected = 0;
        while (true) {
            auto frame = base;
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    4, 112, 7, 4);
            draw_legacy_text(frame, item_font_, save_slot_prompt_,
                             10 * 4, 125, 260, 16, 0);
            if (menu_sprites_.sprites().size() > 141U) {
                const auto& cursor = menu_sprites_.sprites()[141];
                blit(frame, menu_sprites_.pixels(141),
                     cursor.width, cursor.height,
                     (14 + static_cast<int>(selected) * 8) * 4, 141);
            }
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return std::nullopt;
            }
            if (action == InputAction::cancel) return std::nullopt;
            // 4e9d..4ead has the same asymmetric edge rule as the save-slot
            // selector: Left from the first value jumps to the fifth, while
            // Right at the fifth value remains clamped.
            if (action == InputAction::left) {
                selected = selected == 0U ? 4U : selected - 1U;
            }
            else if (action == InputAction::right && selected != 4U) ++selected;
            else if (action == InputAction::confirm) {
                // 4ef7 preserves AX across 46cc and commits it only when the
                // default-left Yes choice leaves DATA:35e8 at zero.
                if (confirm_binary_choice(std::move(frame), true)) {
                    return selected;
                }
                return std::nullopt;
            }
        }
    }

    bool confirm_system_exit(Viewport frame) {
        if (!reveal_bottom_message(frame, system_exit_prompt_)) return false;
        return confirm_binary_choice(std::move(frame));
    }

    void run_system_save(Viewport base) {
        if ((state_.u16(0x408) & 0x2000U) == 0U) {
            // RPG 4cc3..4cd4 does not silently ignore Record on a map which
            // forbids saving. It routes DATA:3620 through the same 49d0
            // bottom-message renderer used by restricted field actions.
            static_cast<void>(show_bottom_message(
                std::move(base), field_action_error_));
            return;
        }
        if (map_database_ == nullptr || save_slot_ == nullptr ||
            !*save_slot_) {
            return;
        }
        RpgSaveSlotSelector selector;
        while (true) {
            auto frame = base;
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    4, 112, 7, 4);
            draw_legacy_text(frame, item_font_, save_slot_prompt_,
                             10 * 4, 125, 260, 16, 0);
            if (menu_sprites_.sprites().size() > 141U) {
                const auto& cursor = menu_sprites_.sprites()[141];
                blit(frame, menu_sprites_.pixels(141),
                     cursor.width, cursor.height,
                     (14 + static_cast<int>(selector.slot()) * 8) * 4, 141);
            }
            if (selector.confirming()) {
                const auto opaque = [&](std::size_t sprite,
                                        int x_byte, int y) {
                    if (sprite >= menu_sprites_.sprites().size()) return;
                    const auto& info = menu_sprites_.sprites()[sprite];
                    blit_opaque(frame, menu_sprites_.pixels(sprite),
                                info.width, info.height, x_byte * 4, y);
                };
                opaque(0, 23, 147);
                opaque(0, 41, 147);
                opaque(29, 29, 156);
                opaque(8, 47, 156);
                apply_rpg_binary_choice_highlight(
                    frame.pixels, 320, 200,
                    std::span<const std::uint8_t, 768>(frame.palette),
                    23, 41, 147, selector.confirmation_choice());
            }
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto result = selector.input(platform_.wait_for_input());
            if (result == RpgSaveSelectorResult::quit) {
                quit_requested_ = true;
                return;
            }
            if (result == RpgSaveSelectorResult::cancelled) return;
            if (result == RpgSaveSelectorResult::committed) {
                (*save_slot_)(
                    static_cast<std::uint8_t>(selector.slot() + 1U),
                    state_, *map_database_);
                return;
            }
        }
    }

    [[nodiscard]] bool run_system_menu() {
        std::size_t selected = 0;
        while (true) {
            auto frame = scene_provider_();
            // 4b76/4dbd: seven rows at mode-X (20,0), four middle pieces.
            draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                    20, 0, 4, 7);
            draw_legacy_text(frame, item_font_, system_menu_labels_,
                             28 * 4, 13, 128, 112, 0);
            static constexpr std::array<std::uint8_t, 2> enabled_label{
                0xb6, 0x7d};  // 開
            static constexpr std::array<std::uint8_t, 2> disabled_label{
                0xc3, 0xf6};  // 關
            draw_legacy_text(
                frame, item_font_,
                music_enabled_ ? std::span<const std::uint8_t>(enabled_label)
                               : std::span<const std::uint8_t>(disabled_label),
                40 * 4, 13, 16, 16, 0x85);
            draw_legacy_text(
                frame, item_font_,
                sound_enabled_ ? std::span<const std::uint8_t>(enabled_label)
                               : std::span<const std::uint8_t>(disabled_label),
                40 * 4, 29, 16, 16, 0x85);
            draw_menu_number(frame, menu_sprites_,
                             static_cast<std::uint16_t>(state_.u16(0x3f2) + 1U),
                             49, 80, 111);
            draw_menu_number(frame, menu_sprites_, state_.u16(0x406),
                             49, 96, 111);
            if (menu_sprites_.sprites().size() > 1U) {
                const auto& cursor = menu_sprites_.sprites()[1];
                blit(frame, menu_sprites_.pixels(1),
                     cursor.width, cursor.height, 23 * 4,
                     9 + static_cast<int>(selected) * 16);
            }
            platform_.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action == InputAction::cancel) return false;
            if (action == InputAction::up && selected != 0U) {
                --selected;
                continue;
            }
            if (action == InputAction::down && selected != 6U) {
                ++selected;
                continue;
            }
            if (action != InputAction::confirm) continue;

            if (selected == 0U) {
                music_enabled_ = !music_enabled_;
                state_.set_u8(0x3f4, music_enabled_ ? 0U : 1U);
                if (!music_enabled_) {
                    platform_.stop_music();
                } else if (playing_music_ != nullptr &&
                           !playing_music_->empty()) {
                    const auto path = game_root_ / *playing_music_;
                    if (std::filesystem::is_regular_file(path)) {
                        platform_.play_music(read_file(path), true);
                    }
                }
            } else if (selected == 1U) {
                sound_enabled_ = !sound_enabled_;
                state_.set_u8(0x3f5, sound_enabled_ ? 0U : 1U);
            } else if (selected == 2U) {
                const auto slot = select_system_value(frame);
                if (quit_requested_) return false;
                if (slot && load_slot_ != nullptr && *load_slot_ &&
                    live_map_database_ != nullptr) {
                    auto loaded = (*load_slot_)(
                        static_cast<std::uint8_t>(*slot + 1U));
                    if (!loaded.map_database) {
                        throw std::runtime_error(
                            "RPG system load returned no MAPZ database");
                    }
                    state_ = std::move(loaded.state);
                    *live_map_database_ = std::move(loaded.map_database);
                    const auto loaded_music_enabled = state_.u8(0x3f4) == 0U;
                    sound_enabled_ = state_.u8(0x3f5) == 0U;
                    if (music_enabled_ && !loaded_music_enabled) {
                        platform_.stop_music();
                    } else if (!music_enabled_ && loaded_music_enabled &&
                               playing_music_ != nullptr &&
                               !playing_music_->empty()) {
                        const auto path = game_root_ / *playing_music_;
                        if (std::filesystem::is_regular_file(path)) {
                            platform_.play_music(read_file(path), true);
                        }
                    }
                    music_enabled_ = loaded_music_enabled;
                    return true;
                }
            } else if (selected == 3U) {
                run_system_save(frame);
                if (quit_requested_) return false;
            } else if (selected == 4U) {
                const auto value = select_system_value(frame);
                if (quit_requested_) return false;
                if (value) state_.set_u16(0x3f2, static_cast<std::uint16_t>(*value));
            } else if (selected == 5U) {
                const auto value = select_system_value(frame);
                if (quit_requested_) return false;
                if (value) state_.set_u16(
                    0x406, static_cast<std::uint16_t>(*value + 1U));
            } else if (selected == 6U && confirm_system_exit(frame)) {
                quit_requested_ = true;
                return false;
            }
        }
    }

    void draw_bottom_message(Viewport& frame,
                             std::span<const std::uint8_t> text) const {
        // 49d0 begins with 2cce: a 7x4 selector panel at Mode-X (4,112),
        // then starts its Big5 stream at (10,125).
        draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                4, 112, 7, 4);
        draw_legacy_text(frame, item_font_, text,
                         10 * 4, 125, 240, 64, 0);
    }

    bool reveal_bottom_message(Viewport& frame,
                               std::span<const std::uint8_t> text) {
        auto completed = frame;
        draw_bottom_message(completed, text);
        std::vector<std::size_t> glyph_end_offsets;
        for (std::size_t offset = 0; offset < text.size();) {
            if (text[offset] == ' ') {
                ++offset;
            } else if (offset + 1U < text.size() &&
                       ((text[offset] == '#' && text[offset + 1U] == '#') ||
                        (text[offset] == '%' && text[offset + 1U] == '%'))) {
                offset += 2U;
            } else {
                offset += std::min<std::size_t>(2U, text.size() - offset);
                glyph_end_offsets.push_back(offset);
            }
        }

        auto skipped_delay = false;
        for (const auto glyph_end : glyph_end_offsets) {
            if (skipped_delay) break;
            auto shown = frame;
            draw_bottom_message(shown, text.first(glyph_end));
            platform_.present_direct_update({
                320, 200, shown.pixels,
                std::span<const std::uint8_t, 768>(shown.palette)});
            const auto text_delay = state_.u16(0x3f2);
            if (text_delay != 0) {
                platform_.delay_for(std::chrono::milliseconds(
                    (static_cast<std::uint64_t>(text_delay) * 1000U + 69U) /
                    70U));
            }
            const auto action = platform_.poll_text_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            skipped_delay = action != InputAction::none;
        }
        if (skipped_delay && !glyph_end_offsets.empty()) {
            platform_.present_direct_update({
                320, 200, completed.pixels,
                std::span<const std::uint8_t, 768>(completed.palette)});
        }
        frame = std::move(completed);
        return true;
    }

    bool show_bottom_message(Viewport frame,
                             std::span<const std::uint8_t> text) {
        auto page_start = std::size_t{0};
        auto final_cursor_x_byte = 10;
        auto final_cursor_y = 125;
        while (true) {
            auto page_end = page_start;
            while (page_end + 1U < text.size() &&
                   !(text[page_end] == '%' && text[page_end + 1U] == '%')) {
                ++page_end;
            }
            if (page_end + 1U >= text.size()) page_end = text.size();

            const auto page_text = text.subspan(page_start, page_end - page_start);
            auto page = frame;
            if (!reveal_bottom_message(page, page_text)) return false;
            auto cursor_x_byte = 10;
            auto cursor_y = 125;
            for (std::size_t offset = 0; offset < page_text.size();) {
                if (page_text[offset] == ' ') {
                    ++cursor_x_byte;
                    ++offset;
                } else if (offset + 1U < page_text.size() &&
                           page_text[offset] == '#' &&
                           page_text[offset + 1U] == '#') {
                    cursor_x_byte = 10;
                    cursor_y += 16;
                    offset += 2U;
                } else {
                    cursor_x_byte += 4;
                    offset += std::min<std::size_t>(
                        2U, page_text.size() - offset);
                }
            }

            if (page_end == text.size()) {
                final_cursor_x_byte = cursor_x_byte;
                final_cursor_y = cursor_y;
                frame = std::move(page);
                break;
            }

            // 49d0's %% branch leaves the text cursor in place, draws MENU
            // frame 91h, waits for an action, and then re-enters 49d0 after
            // the separator.  Each continuation therefore rebuilds 2cce's
            // panel over the same saved source page instead of appending to
            // the previous four lines.
            while (true) {
                auto shown = page;
                if (menu_sprites_.sprites().size() > 0x91U) {
                    const auto& info = menu_sprites_.sprites()[0x91U];
                    blit(shown, menu_sprites_.pixels(0x91U),
                         info.width, info.height,
                         cursor_x_byte * 4, cursor_y);
                }
                platform_.present({
                    320, 200, shown.pixels,
                    std::span<const std::uint8_t, 768>(shown.palette)});
                const auto action = platform_.poll_input();
                if (action == InputAction::quit) {
                    quit_requested_ = true;
                    return false;
                }
                if (action != InputAction::none) break;
                platform_.delay_for(std::chrono::milliseconds(20));
            }
            page_start = page_end + 2U;
        }

        auto indicator = std::size_t{149};
        while (true) {
            auto shown = frame;
            if (indicator < menu_sprites_.sprites().size()) {
                const auto& info = menu_sprites_.sprites()[indicator];
                blit(shown, menu_sprites_.pixels(indicator),
                     info.width, info.height,
                     final_cursor_x_byte * 4, final_cursor_y);
            }
            platform_.present({
                320, 200, shown.pixels,
                std::span<const std::uint8_t, 768>(shown.palette)});
            const auto action = platform_.poll_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action != InputAction::none) {
                return true;
            }
            platform_.delay_for(std::chrono::milliseconds(20));
            ++indicator;
            if (indicator == 153U) indicator = 149U;
        }
    }

    void present(Viewport scene) {
        if (palette_dark_) scene.palette.fill(0);
        platform_.present({320, 200, scene.pixels,
                           std::span<const std::uint8_t, 768>(scene.palette)});
    }

    void reset_direct_page_layers() {
        // Dialogue and opcodes 14/53/55 modify the already displayed VGA
        // page. Any later dd6/219 full-page rebuild overwrites those pixels;
        // retaining the snapshot beyond that boundary makes panels, money
        // cards, captions or monochrome pages leak into unrelated frames.
        direct_event_page_.reset();
    }

    void advance_event_palette() {
        if (relocated_map_) {
            advance_map_palette(relocated_palette_,
                                relocated_palette_animation_);
        } else if (scene_palette_advance_) {
            scene_palette_advance_();
        }
    }

    void present_timed(Viewport scene) {
        present(std::move(scene));
        if (frame_delay_ticks_ != 0) {
            platform_.delay_for(std::chrono::milliseconds(
                (static_cast<std::uint64_t>(frame_delay_ticks_) * 1000U + 69U) / 70U));
        }
    }

    void fade_out() {
        auto scene = event_scene();
        for (std::uint16_t amount = 3; amount <= 63; amount += 3) {
            auto frame = scene;
            for (std::size_t i = 0; i < frame.palette.size(); ++i) {
                frame.palette[i] = static_cast<std::uint8_t>(
                    frame.palette[i] > amount ? frame.palette[i] - amount : 0);
            }
            platform_.present({320, 200, frame.pixels,
                               std::span<const std::uint8_t, 768>(frame.palette)});
            platform_.delay_for(std::chrono::milliseconds(15));
        }
        palette_dark_ = true;
    }

    void fade_in() {
        const auto scene = event_scene();
        for (std::uint16_t ceiling = 3; ceiling <= 63; ceiling += 3) {
            auto frame = scene;
            for (auto& component : frame.palette) {
                component = static_cast<std::uint8_t>(
                    std::min<std::uint16_t>(component, ceiling));
            }
            platform_.present({320, 200, frame.pixels,
                               std::span<const std::uint8_t, 768>(frame.palette)});
            platform_.delay_for(std::chrono::milliseconds(15));
        }
        palette_dark_ = false;
    }

    void select_cutscene_dictionary(std::uint16_t number) {
        // RPG handlers 59bc (opcode 29) and 5b57 (opcode 43) load a DE tile
        // dictionary. They do not select the RAP frame layout; following
        // opcode-35 calls may deliberately reuse this dictionary for several
        // numerically non-adjacent RAP files (notably DE063 -> DE066/067).
        cutscene_dictionary_id_ = number;
        // The shared loader installs the RSK target palette after opcode 43's
        // fade-to-black (5f81), so subsequent RAP frames are visible without
        // a separate opcode-6/45 fade-in.
        palette_dark_ = false;
    }

    void load_cutscene_layout(std::uint16_t number) {
        // RPG:5a5e (opcode 35) installs a new full-page RAP layout using the
        // dictionary most recently chosen by opcode 29/43. This overwrites
        // the in-place opcode-55 conversion and any text drawn on the old page.
        direct_event_page_.reset();
        auto layout = de_sprite_path(game_root_, number);
        auto rap = layout;
        rap.replace_extension(".RAP");
        if (!std::filesystem::is_regular_file(rap)) return;
        const auto dictionary = cutscene_dictionary_id_.value_or(number);
        cutscene_ = PlanarSpriteSet::load(
            de_sprite_path(game_root_, dictionary), layout);
        cutscene_id_ = number;
        // The RAP loader at 0d84 resets SAVE+411 before rendering frame zero.
        state_.set_u16(0x411, 0);
        cutscene_frame_index_ = 0;
    }

    void play_event_music(std::uint16_t number) {
        std::ostringstream name;
        name << "RI" << std::setw(3) << std::setfill('0') << number << ".RIX";
        const auto relative = std::filesystem::path("RX") / name.str();
        const auto path = game_root_ / relative;
        if (std::filesystem::is_regular_file(path)) {
            // RPG DS:3bf5 is RX/RI000.RIX. Handler 5ab5 patches those digits
            // and calls 144b, the normal looping RIX start routine.
            if (playing_music_) *playing_music_ = relative;
            if (music_enabled_) platform_.play_music(read_file(path), true);
        }
    }

    Viewport event_scene() {
        if (direct_event_page_) return *direct_event_page_;
        auto frame = [&]() {
            if (!relocated_map_ || relocated_area_ == nullptr ||
                !relocated_actors_) {
                return scene_provider_();
            }
            auto viewport = crop_map(relocated_map_->render(true),
                                     state_.viewport_x(), state_.viewport_y());
            viewport.palette = relocated_palette_;
            draw_world_characters(viewport, *relocated_area_, state_, game_root_,
                                  *relocated_actors_, relocated_animation_sets_);
            return viewport;
        }();
        if (cutscene_) {
            frame.palette = cutscene_->palette();
            const auto frame_index = cutscene_frame_index_ % cutscene_->frame_count();
            const auto& source = cutscene_->frame(frame_index);
            const auto left = (320 - static_cast<int>(source.width)) / 2;
            const auto top = (200 - static_cast<int>(source.height)) / 2;
            blit(frame, source.pixels, source.width, source.height, left, top);
        }
        return frame;
    }

    static Viewport shifted_scene(const Viewport& source, std::uint16_t address_bytes) {
        Viewport shifted = source;
        // RPG.EXE:5bbb adds the argument directly to 60e5, the VGA byte
        // address used by every scene blitter. CHNA4 alternates 81/161 with
        // zero: one/two 80-byte scanlines plus one four-pixel mode-X group.
        shifted.pixels = composite_mode_x_address_offset(
            source.pixels, source.pixels, address_bytes);
        return shifted;
    }

    void play_voice(std::uint16_t number) {
        if (!sound_enabled_) return;
        std::ostringstream name;
        name << "SP" << std::setw(3) << std::setfill('0') << number << ".VOC";
        const auto path = game_root_ / "VC" / name.str();
        if (std::filesystem::is_regular_file(path)) {
            platform_.play_voice(read_file(path));
        }
    }

    PlatformBackend& platform_;
    const LegacyFont& font_;
    const LegacyFont& name_font_;
    const LegacyFont& item_font_;
    const ItemDatabase& items_;
    const ItemTextDatabase& item_texts_;
    const SpriteArchive& menu_sprites_;
    const SpriteArchive& equipment_art_;
    std::span<const std::uint8_t> save_slot_prompt_;
    std::span<const std::uint8_t> travel_labels_;
    std::span<const std::uint8_t> shop_prompt_;
    std::span<const std::uint8_t> shop_sale_prompt_;
    std::span<const std::uint8_t> shop_money_error_;
    std::span<const std::uint8_t> shop_inventory_error_;
    std::span<const std::uint8_t> shop_confirmation_prompt_;
    std::span<const std::uint8_t> shop_quantity_error_;
    std::span<const std::uint8_t> shop_unsellable_error_;
    std::span<const std::uint8_t> item_discard_error_;
    std::span<const std::uint8_t> item_discard_prompt_;
    std::span<const std::uint8_t> item_alchemy_error_;
    std::span<const std::uint8_t> item_alchemy_select_prompt_;
    std::span<const std::uint8_t> item_alchemy_level_error_;
    std::span<const std::uint8_t> item_alchemy_data_;
    std::span<const std::uint8_t> item_effect_labels_;
    std::span<const std::uint8_t> item_alchemy_result_labels_;
    std::span<const std::uint8_t> equipment_actor_error_;
    std::span<const std::uint8_t> equipment_two_hand_error_;
    std::span<const std::uint8_t> equipment_slot_error_;
    std::span<const std::uint8_t> field_action_error_;
    std::span<const std::uint8_t> ability_value_error_;
    std::span<const std::uint8_t> ability_material_error_;
    std::span<const std::uint8_t> ability_dead_error_;
    std::span<const std::uint8_t> ability_inventory_error_;
    std::span<const std::uint8_t> field_ability_records_;
    std::span<const std::uint8_t> ability_resource_labels_;
    const std::vector<std::vector<std::uint8_t>>& ability_descriptions_;
    std::span<const std::uint8_t> system_menu_labels_;
    std::span<const std::uint8_t> system_exit_prompt_;
    std::span<const std::uint8_t> status_menu_labels_;
    std::span<const std::uint8_t> status_value_labels_;
    std::span<const std::uint8_t> inventory_category_labels_;
    std::span<const std::uint8_t> equipment_slot_labels_;
    std::span<const std::uint8_t> equipment_stat_labels_;
    std::function<Viewport()> scene_provider_;
    std::function<void()> scene_palette_advance_;
    MapDatabase* map_database_{};
    const SaveSlotWriter* save_slot_{};
    const SaveSlotLoader* load_slot_{};
    std::shared_ptr<MapDatabase>* live_map_database_{};
    FieldActionRuntime& field_action_runtime_;
    SharedState& state_;
    std::filesystem::path game_root_;
    std::filesystem::path* playing_music_{};
    bool& music_enabled_;
    bool& sound_enabled_;
    RpgMenuRuntime& menu_runtime_;
    std::optional<PlanarSpriteSet> cutscene_;
    std::map<std::uint16_t, SpriteArchive> item_preview_cache_;
    std::optional<std::uint16_t> cutscene_dictionary_id_;
    std::optional<std::uint16_t> cutscene_id_;
    std::size_t cutscene_frame_index_{};
    std::optional<Viewport> direct_event_page_;
    std::uint16_t frame_delay_ticks_{};
    bool palette_dark_{};
    bool quit_requested_{};
    const MapAreaRecord* relocated_area_{};
    std::optional<MapResource> relocated_map_;
    std::array<std::uint8_t, 768> relocated_palette_{};
    std::array<std::uint16_t, 24> relocated_palette_animation_{};
    std::optional<SpriteArchive> relocated_actors_;
    std::map<std::uint16_t, PlanarSpriteSet> relocated_animation_sets_;
};

std::optional<std::size_t> interaction_entity(const MapLocationRecord& location,
                                              const SharedState& state,
                                              const MapResource& map) {
    const auto occupied_entity = [&](std::uint16_t target) {
        for (std::size_t index = 0; index < location.area.entity_count(); ++index) {
            const auto entity = map_entity(location.area, index);
            if (entity.behavior == 3) continue;
            if (entity.cell_offset == target || entity.cell_offset + 2U == target ||
                entity.cell_offset + 4U == target) {
                return std::optional<std::size_t>{index};
            }
        }
        return std::optional<std::size_t>{};
    };
    const auto probe = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= map.layout().width ||
            y >= map.layout().height) {
            return std::pair{false, std::optional<std::size_t>{}};
        }
        const auto cell = static_cast<std::size_t>(y) * map.layout().width +
                          static_cast<std::size_t>(x);
        const auto target = static_cast<std::uint16_t>(
            state.u16(0x40f) + cell * 2U);
        const auto entity = occupied_entity(target);
        const auto special = (map.cells()[cell] & 0x0800U) != 0U ||
                             entity.has_value();
        return std::pair{special, entity};
    };

    // RPG:523d/52fd scans three parallel forward rays, each at most four
    // cells long. After an empty centre ray DATA:3cbf offsets the next ray
    // one cell to either side before 52fd repeats the actor direction.
    std::array<std::pair<int, int>, 3> ray_offsets{};
    if (state.actor_direction() == 0U || state.actor_direction() == 3U) {
        ray_offsets = {{{0, 0}, {-1, 0}, {1, 0}}};
    } else {
        ray_offsets = {{{0, 0}, {0, 1}, {0, -1}}};
    }
    const auto center_x = static_cast<int>(state.world_x());
    const auto center_y = static_cast<int>(state.world_y());
    for (const auto [offset_x, offset_y] : ray_offsets) {
        auto x = center_x + offset_x;
        auto y = center_y + offset_y;
        for (std::size_t distance = 0; distance < 4U; ++distance) {
            if (state.actor_direction() == 0U) ++y;
            else if (state.actor_direction() == 3U) --y;
            else if (state.actor_direction() == 6U) --x;
            else ++x;
            const auto [special, entity] = probe(x, y);
            if (!special) continue;
            // A static 0800h word without an entity ends the DOS search too;
            // it does not fall through to another ray.
            return entity;
        }
    }
    return std::nullopt;
}

bool entity_occupies_cell(const MapLocationRecord& location,
                          const SharedState& state, const MapResource& map,
                          int x, int y) {
    if (x < 0 || y < 0 || x >= map.layout().width || y >= map.layout().height) {
        return true;
    }
    const auto target = static_cast<std::uint16_t>(
        state.u16(0x40f) +
        (static_cast<std::size_t>(y) * map.layout().width +
         static_cast<std::size_t>(x)) * 2U);
    for (std::size_t index = 0; index < location.area.entity_count(); ++index) {
        const auto entity = map_entity(location.area, index);
        // RPG.EXE:4ff4 marks three consecutive cells with 8800h for every
        // entity except behavior three. Those bits are transient map state,
        // not stored in RAP, so reproduce the occupancy layer here.
        if (entity.behavior == 3) continue;
        if (target == entity.cell_offset || target == entity.cell_offset + 2U ||
            target == entity.cell_offset + 4U) {
            return true;
        }
    }
    return false;
}

bool static_cell_has(const MapResource& map, int x, int y, std::uint16_t mask) {
    if (x < 0 || y < 0 || x >= map.layout().width || y >= map.layout().height) {
        return true;
    }
    return (map.cells()[static_cast<std::size_t>(y) * map.layout().width +
                        static_cast<std::size_t>(x)] & mask) != 0;
}

bool movement_blocked(const MapLocationRecord& location, const SharedState& state,
                      const MapResource& map, InputAction action,
                      int target_x, int target_y) {
    const auto blocked = [&](int x, int y) {
        return static_cell_has(map, x, y, 0x8000U) ||
               entity_occupies_cell(location, state, map, x, y);
    };
    // The actor occupies three horizontal RAP words.  Horizontal movement
    // tests only the new leading edge; north/south movement scans all three,
    // exactly matching RPG.EXE:1a70/1b69 and 1f0c.
    if (action == InputAction::left) return blocked(target_x - 1, target_y);
    if (action == InputAction::right) return blocked(target_x + 1, target_y);
    return blocked(target_x - 1, target_y) || blocked(target_x, target_y) ||
           blocked(target_x + 1, target_y);
}

bool movement_has_special_cell(const MapLocationRecord& location,
                               const SharedState& state, const MapResource& map,
                               InputAction action, int target_x, int target_y) {
    const auto special = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= map.layout().width || y >= map.layout().height) {
            return false;
        }
        return static_cell_has(map, x, y, 0x0800U) ||
               entity_occupies_cell(location, state, map, x, y);
    };
    if (action == InputAction::left) return special(target_x - 1, target_y);
    if (action == InputAction::right) return special(target_x + 1, target_y);
    return special(target_x - 1, target_y) || special(target_x, target_y) ||
           special(target_x + 1, target_y);
}

std::optional<InputAction> corner_slide(const MapLocationRecord& location,
                                        const SharedState& state,
                                        const MapResource& map,
                                        InputAction requested) {
    const auto x = static_cast<int>(state.world_x());
    const auto y = static_cast<int>(state.world_y());
    const auto clear = [&](int cell_x, int cell_y) {
        return !static_cell_has(map, cell_x, cell_y, 0x8000U) &&
               !entity_occupies_cell(location, state, map, cell_x, cell_y);
    };
    const auto footprint_clear = [&](int center_x, int center_y) {
        return clear(center_x - 1, center_y) && clear(center_x, center_y) &&
               clear(center_x + 1, center_y);
    };

    if (requested == InputAction::right || requested == InputAction::left) {
        const auto sign = requested == InputAction::right ? 1 : -1;
        const auto inner_x = x + sign;
        const auto edge_x = x + sign * 2;
        // RPG.EXE:1a8f/1b88 prefers sliding south, then north. The second
        // edge probe may extend two rows around a diagonal corner.
        if (clear(inner_x, y + 1) &&
            (clear(edge_x, y + 1) || clear(edge_x, y + 2))) {
            return InputAction::down;
        }
        if (clear(inner_x, y - 1) &&
            (clear(edge_x, y - 1) || clear(edge_x, y - 2))) {
            return InputAction::up;
        }
        return std::nullopt;
    }

    if (requested == InputAction::down || requested == InputAction::up) {
        const auto sign = requested == InputAction::down ? 1 : -1;
        const auto edge_y = y + sign;
        // RPG.EXE:1c8b/1d8a prefers east, then west and probes complete
        // three-word actor footprints for its vertical-to-horizontal slide.
        if (footprint_clear(x + 1, y) &&
            (footprint_clear(x + 1, edge_y) ||
             footprint_clear(x + 2, edge_y))) {
            return InputAction::right;
        }
        if (footprint_clear(x - 1, y) &&
            (footprint_clear(x - 1, edge_y) ||
             footprint_clear(x - 2, edge_y))) {
            return InputAction::left;
        }
    }
    return std::nullopt;
}

}  // namespace

Marker RpgModule::run(GameContext& context, Marker) {
    // A map-changing event reloads resources by restarting this outer loop.
    // Keeping the transition iterative is important: the DOS game could cross
    // maps indefinitely, whereas recursive re-entry would eventually exhaust
    // the native stack on a long portable session.
    std::filesystem::path playing_music;
    // RPG:4c38..4c42 restores the two driver-disable bytes from SAVE before
    // loading map music. Zero means enabled; the system menu writes the same
    // bytes back, so audio preferences survive save/load and FIG round trips.
    music_enabled_ = context.shared_state.u8(0x3f4) == 0U;
    sound_enabled_ = context.shared_state.u8(0x3f5) == 0U;
    const auto item_font = LegacyFont::load(context.game_root / "CHAIN.DSK");
    const auto items = ItemDatabase::load(context.game_root / "ITEM.EXE");
    const auto item_texts = ItemTextDatabase::load(context.game_root / "ITEM2.EXE");
    const auto map_transitions =
        MapTransitionDatabase::load(context.game_root / "MAP0.EXE");
    auto menu_data = decode_rsk_block(read_file(context.game_root / "MENU.RSK")).data;
    const auto menu_sprites = SpriteArchive::parse(std::move(menu_data));
    auto equipment_data =
        decode_rsk_block(read_file(context.game_root / "ME02.RSK")).data;
    const auto equipment_art = SpriteArchive::parse(std::move(equipment_data));
    const auto rpg_path = context.game_root / "RPG.EXE";
    const auto rpg_mz = dos::MzExecutable::load(rpg_path);
    const auto rpg_file = read_file(rpg_path);
    const auto image_start = static_cast<std::size_t>(rpg_mz.header_size());
    const auto image_size = static_cast<std::size_t>(rpg_mz.load_image_size());
    if (image_start > rpg_file.size() || image_size > rpg_file.size() - image_start) {
        throw std::runtime_error("RPG.EXE load image is truncated");
    }
    const auto rpg_load_image = std::span<const std::uint8_t>(
        rpg_file.data() + image_start, image_size);
    const auto rpg_entry_offset =
        static_cast<std::size_t>(rpg_mz.header().initial_cs) * 16U +
        rpg_mz.header().initial_ip;
    // RPG.EXE:4e57 points at DATA:3a4a (" one two three four five " in
    // Big5). Read it from the original executable rather than substituting a
    // translated or host-encoded string.
    const auto save_slot_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3a4a);
    // RPG.EXE:37c3 indexes 34 fixed four-glyph destination labels at
    // DATA:3ace. Disabled SAVE+51e entries simply skip their eight bytes.
    const auto travel_labels = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x3ace, 34U * 8U);
    const auto shop_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c32);
    // 56xx/58xx shop feedback is not host UI: every prompt is an embedded
    // Big5 stream consumed by 49d0/4aec before the shared 555a Yes/No page.
    const auto shop_sale_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c1e);
    const auto shop_money_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c44);
    const auto shop_inventory_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c54);
    const auto shop_confirmation_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c6c);
    const auto shop_quantity_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c80);
    const auto shop_unsellable_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3c0a);
    const auto item_discard_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3686);
    const auto item_alchemy_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x36b0);
    const auto item_discard_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3754);
    const auto item_alchemy_select_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x36c8);
    const auto item_alchemy_level_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x36f8);
    const auto item_alchemy_data = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x2a42, 0x2f08U - 0x2a42U);
    const auto item_effect_labels = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x399a);
    const auto item_alchemy_result_labels = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3922);
    const auto equipment_actor_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x369a);
    const auto equipment_two_hand_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x370e);
    const auto equipment_slot_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3728);
    const auto field_action_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3620);
    const auto ability_value_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3630);
    const auto ability_material_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x364a);
    const auto ability_dead_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3678);
    const auto ability_inventory_error = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x36e0);
    // RPG:1ffb shows this normal 49d0 dialogue every time an overworld
    // poison pulse reduces a party member to zero HP.
    const auto poison_defeat_text = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x373c);
    const auto field_ability_records = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x1dce, 151U * 20U);
    const auto ability_resource_labels = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x3664, 5U * 4U);
    const auto ability_descriptions = load_rpg_ability_descriptions(
        context.game_root / "DATE2.EXE");
    const auto system_menu_labels = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x39e6);
    const auto system_exit_prompt = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x3a36);
    const auto status_menu_labels = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x3806, 28U * 8U);
    // RPG:4960 indexes fifteen fixed two-glyph condition names at 38e6:
    // healthy/dead/near-death, then status bits 1000h down through 0002h.
    const auto status_value_labels = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x38e6, 15U * 4U);
    // RPG.EXE:3d7e indexes forty-two fixed two-glyph type names. Equipment
    // uses one eleven-line label string and four consecutive $$-terminated
    // statistic labels rather than host-language UI text.
    const auto inventory_category_labels = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x299a, 42U * 4U);
    const auto equipment_slot_labels = extract_rpg_embedded_text(
        rpg_load_image, rpg_entry_offset, 0x388e);
    const auto equipment_stat_labels = extract_rpg_embedded_data(
        rpg_load_image, rpg_entry_offset, 0x3a60, 40U);
    RpgEntityRuntime entity_runtime;
    RpgWorldStepRuntime world_step_runtime;
    FieldActionRuntime field_action_runtime;
    RpgMenuRuntime menu_runtime;
    std::optional<MapAreaRecord> relocated_transient_area;
    while (true) {
    // RPG:10fd installs a fresh transient entity array for every area load;
    // the behavior-stream cursor, delays and roam counters beside it are BSS
    // scratch, not save data. Reusing a runtime merely because two successive
    // areas happen to have the same entity count carries movement state across
    // opcode 37/52 and travel boundaries.
    entity_runtime = {};
    auto graphics_relative = normalize_dos_asset_path(context.shared_state.area_graphics_path());
    auto layout_relative = normalize_dos_asset_path(context.shared_state.area_collision_path());
    graphics_relative.replace_extension();
    layout_relative.replace_extension();
    const auto map = MapResource::load(context.game_root / graphics_relative,
                                       context.game_root / layout_relative);
    MapDatabase* active_map_database = context.map_database.get();
    if (active_map_database == nullptr) {
        const auto map_database_path = context.game_root / "MAPZ.DA1";
        if (!map_database_ || map_database_path_ != map_database_path) {
            map_database_ = MapDatabase::load(map_database_path);
            map_database_path_ = map_database_path;
        }
        active_map_database = &*map_database_;
    }
    auto& map_database = *active_map_database;
    // RPG.EXE copies the selected MAPZ area into transient BSS arrays. Entity
    // wandering and opcodes 1/2/23..27/39 therefore disappear on a map reload.
    // Opcode 3 is different: it reopens and rewrites MAPZ after changing the
    // current entity, while opcode 34 patches an explicitly selected area.
    auto location = map_database.location_at_directory_offset(
        context.shared_state.map_location_directory_offset());
    if (relocated_transient_area) {
        location.area = std::move(*relocated_transient_area);
        relocated_transient_area.reset();
    }
    context.shared_state.set_u16(0x417, map.layout().width);
    context.shared_state.set_u16(0x419, map.layout().height);
    if (pending_map_reload_) {
        const auto viewport_cells =
            (static_cast<std::size_t>(context.shared_state.viewport_y()) * map.layout().width +
             context.shared_state.viewport_x()) * 2U;
        if (location.map_position < viewport_cells) {
            throw std::runtime_error(
                "MAPZ location viewport precedes its RAP cell base (position=" +
                std::to_string(location.map_position) + ", viewport cells=" +
                std::to_string(viewport_cells) + ", location=" +
                std::to_string(location.directory_offset) + ")");
        }
        context.shared_state.set_u16(
            0x40f, static_cast<std::uint16_t>(location.map_position - viewport_cells));
        context.shared_state.set_u16(0x40d, location.map_position);
        pending_map_reload_ = false;
    }
    const auto event_archive = ScriptArchive::load(
        context.game_root / normalize_dos_asset_path(context.shared_state.event_executable_path()));
    const auto event_font = LegacyFont::load(
        context.game_root / normalize_dos_asset_path(context.shared_state.event_data_path()));
    const auto name_font = LegacyFont::load(context.game_root / "NAME.DSK");
    const auto requested_music =
        normalize_dos_asset_path(context.shared_state.music_path());
    if (requested_music != playing_music) {
        if (requested_music.empty()) {
            context.platform.stop_music();
        } else if (music_enabled_) {
            context.platform.play_music(
                read_file(context.game_root / requested_music), true);
        }
        playing_music = requested_music;
    }
    static_cast<void>(event_archive);
    static_cast<void>(event_font);
    static_cast<void>(location);
    auto actors = load_default_map_actors(context.game_root,
                                          context.shared_state);
    std::uint8_t actor_resource_variant = 0;
    std::map<std::uint16_t, PlanarSpriteSet> animation_sets;
    auto map_palette = map.palette();
    auto map_palette_animation = map.animation_words();
    const auto advance_scene_palette = [&]() {
        advance_map_palette(map_palette, map_palette_animation);
    };
    const auto compose_scene = [&]() {
        const auto rendered = map.render(true);
        auto viewport = crop_map(rendered, context.shared_state.viewport_x(),
                                 context.shared_state.viewport_y());
        viewport.palette = map_palette;
        draw_world_characters(viewport, location.area, context.shared_state,
                              context.game_root, actors, animation_sets);
        return viewport;
    };
    struct EntityEventOutcome {
        Marker marker{Marker::none};
        bool quit{};
        bool map_reload{};
        std::optional<MapAreaRecord> relocated_area;
    };
    const auto make_event_host = [&](const LegacyFont& dialogue_font) {
        return RpgEventHost(
            context.platform, dialogue_font, name_font, item_font,
            items, item_texts, menu_sprites, equipment_art,
            save_slot_prompt, travel_labels, shop_prompt,
            shop_sale_prompt, shop_money_error,
            shop_inventory_error, shop_confirmation_prompt,
            shop_quantity_error, shop_unsellable_error,
            item_discard_error, item_discard_prompt,
            item_alchemy_error, item_alchemy_select_prompt,
            item_alchemy_level_error, item_alchemy_data,
            item_effect_labels, item_alchemy_result_labels,
            equipment_actor_error, equipment_two_hand_error,
            equipment_slot_error, field_action_error,
            ability_value_error, ability_material_error,
            ability_dead_error, ability_inventory_error,
            field_ability_records, ability_resource_labels,
            ability_descriptions,
            system_menu_labels, system_exit_prompt,
            status_menu_labels, status_value_labels,
            inventory_category_labels, equipment_slot_labels,
            equipment_stat_labels,
            compose_scene, advance_scene_palette,
            &map_database, &context.save_slot,
            &context.load_slot, &context.map_database,
            field_action_runtime,
            context.shared_state, context.game_root, &playing_music,
            music_enabled_, sound_enabled_, menu_runtime);
    };
    const auto run_entity_event = [&](std::size_t entity_index) {
        // RPG:52b4 saves the entity's current facing, turns it toward the
        // leader, redraws/flips the world once, runs the CHNA record, and
        // finally restores the saved facing.  The faced frame is observable
        // before even the first dialogue opcode, so it cannot be folded into
        // the event host's later presentations.
        const auto old_direction =
            location.area.entity_fields[1][entity_index];
        const auto actor_direction = context.shared_state.actor_direction();
        location.area.entity_fields[1][entity_index] =
            actor_direction == 0U ? 3U :
            actor_direction == 9U ? 6U :
            actor_direction == 6U ? 9U : 0U;
        auto faced_viewport = compose_scene();
        context.platform.present(
            {320, 200, faced_viewport.pixels,
             std::span<const std::uint8_t, 768>(faced_viewport.palette)});

        auto host = make_event_host(event_font);
        const auto entity = map_entity(location.area, entity_index);
        const auto result = execute_event(
            event_archive, entity.event_directory_offset, context.shared_state,
            &location.area, entity_index, host, 10'000, &map_database);
        location.area.entity_fields[1][entity_index] = old_direction;
        return EntityEventOutcome{result.requested_marker,
                                  host.quit_requested(),
                                  result.requested_map_reload,
                                  std::move(result.relocated_area)};
    };
    while (true) {
        // RPG's main loop calls 5e16 after composing pixels and before the
        // page flip. Apply one fixed-point palette-cycle step to the palette
        // carried by this submitted frame.
        advance_scene_palette();
        auto viewport = compose_scene();
        context.platform.present({320, 200, viewport.pixels,
                                  std::span<const std::uint8_t, 768>(viewport.palette)});

        const auto frame_ticks = context.shared_state.u16(0x406);
        if (frame_ticks != 0) {
            context.platform.delay_for(std::chrono::milliseconds(
                (static_cast<std::uint64_t>(frame_ticks) * 1000U + 69U) / 70U));
        }

        // RPG:1a26 probes the actor's centre RAP word after the world frame
        // and dispatches MAP0.EXE through e94 whenever bit 1000h is present.
        // Normal records install another MAPZ location before the next input
        // sample.  The packed record's high row-count byte unlocks one of the
        // 34 travel destinations when action bit 2000h is set.
        const auto actor_x = context.shared_state.world_x();
        const auto actor_y = context.shared_state.world_y();
        if (actor_x < map.layout().width && actor_y < map.layout().height) {
            const auto cell_index =
                static_cast<std::size_t>(actor_y) * map.layout().width + actor_x;
            if ((map.cells()[cell_index] & 0x1000U) != 0U) {
                const auto actor_cell = static_cast<std::uint16_t>(
                    context.shared_state.u16(0x40f) + cell_index * 2U);
                if (const auto transition = map_transitions.match(
                        context.shared_state.u16(0x408), actor_cell,
                        map.layout().width)) {
                    if (!transition->is_special()) {
                        if (transition->sets_travel_flag()) {
                            context.shared_state.set_u16(
                                0x40a, transition->flag_index);
                            context.shared_state.set_u8(
                                0x51eU + transition->flag_index, 1U);
                        }
                        if (transition->uses_relative_placement()) {
                            relocated_transient_area = location.area;
                        }
                        install_map_location(context.shared_state, map_database,
                                             transition->action);
                        pending_map_reload_ = true;
                        break;
                    }

                    // Actions 5..9/11 replace the BMAN archive in the same
                    // buffer used by 506d.  Action 11 deliberately gives the
                    // runtime selector value five while reloading BMAN5, just
                    // as the shipped 0ff2h branch does (BMAN6 is not named).
                    std::optional<std::pair<std::uint8_t, std::uint8_t>>
                        actor_resource;
                    switch (transition->special_action()) {
                    case 5: actor_resource = {{0, 1}}; break;
                    case 6: actor_resource = {{1, 2}}; break;
                    case 7: actor_resource = {{2, 3}}; break;
                    case 8: actor_resource = {{3, 4}}; break;
                    case 9: actor_resource = {{4, 5}}; break;
                    case 11: actor_resource = {{5, 5}}; break;
                    default: break;
                    }
                    if (actor_resource &&
                        actor_resource_variant != actor_resource->first) {
                        actor_resource_variant = actor_resource->first;
                        const auto path = context.game_root /
                            ("BMAN" +
                             std::to_string(actor_resource->second) + ".RSK");
                        actors = SpriteArchive::parse(
                            decode_rsk_block(read_file(path)).data);
                    }

                    // f19's non-location actions 1..4/10/12..21 call 52b4
                    // for a fixed transient entity.  The once-only variants
                    // first set a bit in SAVE+51a; an already-set bit makes
                    // the trigger a no-op. They all enter the same 52b4 event
                    // path as an ordinary explicit/automatic interaction.
                    std::optional<std::size_t> event_entity;
                    std::uint16_t once_flag = 0;
                    switch (transition->special_action()) {
                    case 1: case 3: case 12: case 16: event_entity = 0; break;
                    case 2: event_entity = 3; break;
                    case 4: event_entity = 1; break;
                    case 10: event_entity = 0; once_flag = 0x0001; break;
                    case 13: event_entity = 1; once_flag = 0x0004; break;
                    case 14: event_entity = 0; once_flag = 0x0002; break;
                    case 15: event_entity = 0; once_flag = 0x0008; break;
                    case 17: event_entity = 1; once_flag = 0x0010; break;
                    case 18: event_entity = 0; once_flag = 0x0020; break;
                    case 19: event_entity = 0; once_flag = 0x0040; break;
                    case 20: event_entity = 0; once_flag = 0x0080; break;
                    case 21: event_entity = 0; once_flag = 0x0100; break;
                    default: break;
                    }
                    if (event_entity && once_flag != 0U) {
                        const auto flags = context.shared_state.u16(0x51a);
                        if ((flags & once_flag) != 0U) {
                            event_entity.reset();
                        } else {
                            context.shared_state.set_u16(
                                0x51a,
                                static_cast<std::uint16_t>(flags | once_flag));
                        }
                    }
                    if (event_entity &&
                        *event_entity < location.area.entity_count()) {
                        auto outcome = run_entity_event(*event_entity);
                        if (outcome.quit) {
                            context.platform.stop_audio();
                            return Marker::none;
                        }
                        if (outcome.marker != Marker::none) {
                            context.platform.stop_audio();
                            return outcome.marker;
                        }
                        if (outcome.map_reload) {
                            relocated_transient_area =
                                std::move(outcome.relocated_area);
                            pending_map_reload_ = true;
                            break;
                        }
                    }
                }
            }
        }

        // RPG:0129..01c6 keeps composing world frames and samples the
        // keyboard without blocking. Menus/dialogue still use wait_for_input,
        // but blocking here would freeze autonomous entities and RSK palette
        // cycles whenever the player releases every key.
        auto action = context.platform.poll_input();
        if (action == InputAction::quit) {
            context.platform.stop_audio();
            return Marker::none;
        }
        if (action == InputAction::cancel) {
            auto host = make_event_host(event_font);
            const auto map_reload = host.run_field_menu();
            if (host.quit_requested()) {
                context.platform.stop_audio();
                return Marker::none;
            }
            if (map_reload) {
                pending_map_reload_ = true;
                break;
            }
            continue;
        }
        if (action == InputAction::confirm) {
            if (const auto entity = interaction_entity(
                    location, context.shared_state, map)) {
                auto outcome = run_entity_event(*entity);
                if (outcome.quit) {
                    context.platform.stop_audio();
                    return Marker::none;
                }
                if (outcome.marker != Marker::none) {
                    context.platform.stop_audio();
                    return outcome.marker;
                }
                if (outcome.map_reload) {
                    // Restart the outer resource-loading loop. The cached MAPZ
                    // database (including opcode-3/34 mutations) remains alive.
                    relocated_transient_area = std::move(outcome.relocated_area);
                    pending_map_reload_ = true;
                    break;
                }
            }
            advance_rpg_entities(location.area, map, context.shared_state,
                                 entity_runtime, rpg_load_image);
            continue;
        }

        auto screen_x = context.shared_state.actor_screen_x();
        auto screen_y = context.shared_state.actor_screen_y();
        auto viewport_x = context.shared_state.viewport_x();
        auto viewport_y = context.shared_state.viewport_y();
        const auto previous_viewport_x = viewport_x;
        const auto previous_viewport_y = viewport_y;

        const auto movement_target = [&](InputAction direction) {
            auto target_x = static_cast<int>(context.shared_state.world_x());
            auto target_y = static_cast<int>(context.shared_state.world_y());
            if (direction == InputAction::left) --target_x;
            else if (direction == InputAction::right) ++target_x;
            else if (direction == InputAction::up) --target_y;
            else if (direction == InputAction::down) ++target_y;
            return std::pair{target_x, target_y};
        };
        if (action != InputAction::left && action != InputAction::right &&
            action != InputAction::up && action != InputAction::down) {
            advance_rpg_entities(location.area, map, context.shared_state,
                                 entity_runtime, rpg_load_image);
            continue;
        }

        const auto face = [&](InputAction direction) {
            if (direction == InputAction::left) context.shared_state.set_actor_direction(6);
            else if (direction == InputAction::right) context.shared_state.set_actor_direction(9);
            else if (direction == InputAction::up) context.shared_state.set_actor_direction(3);
            else context.shared_state.set_actor_direction(0);
        };
        face(action);
        advance_rpg_party_animation(context.shared_state);
        auto [target_x, target_y] = movement_target(action);

        if (movement_blocked(location, context.shared_state, map,
                             action, target_x, target_y)) {
            // RPG.EXE distinguishes explicit interaction from collision
            // activation. Only entities with field-8 bit 8000h may start an
            // event merely because the player walked into them.
            if (movement_has_special_cell(location, context.shared_state, map,
                                          action, target_x, target_y)) {
              if (const auto entity = interaction_entity(
                      location, context.shared_state, map)) {
                const auto encountered = map_entity(location.area, *entity);
                if (encountered.behavior == 6U) {
                    // RPG:5298 handles behavior six before checking the
                    // automatic-event flag: collision removes this transient
                    // entity and returns without running its event.
                    location.area.entity_fields[3][*entity] = 3U;
                } else if ((encountered.flags & 0x8000U) != 0U) {
                    auto outcome = run_entity_event(*entity);
                    if (outcome.quit) {
                        context.platform.stop_audio();
                        return Marker::none;
                    }
                    if (outcome.marker != Marker::none) {
                        context.platform.stop_audio();
                        return outcome.marker;
                    }
                    if (outcome.map_reload) {
                        relocated_transient_area = std::move(outcome.relocated_area);
                        pending_map_reload_ = true;
                        break;
                    }
                }
              }
              advance_rpg_entities(location.area, map, context.shared_state,
                                   entity_runtime, rpg_load_image);
              continue;
            }
            const auto slide = corner_slide(location, context.shared_state, map, action);
            if (!slide) {
                advance_rpg_entities(location.area, map, context.shared_state,
                                     entity_runtime, rpg_load_image);
                continue;
            }
            action = *slide;
            face(action);
            std::tie(target_x, target_y) = movement_target(action);
            if (movement_blocked(location, context.shared_state, map,
                                 action, target_x, target_y)) {
                advance_rpg_entities(location.area, map, context.shared_state,
                                     entity_runtime, rpg_load_image);
                continue;
            }
        }

        if (action == InputAction::left) {
            if (screen_x == 0x26 && viewport_x > 0) --viewport_x;
            else if (screen_x >= 2) screen_x -= 2;
        } else if (action == InputAction::right) {
            if (screen_x == 0x26 &&
                viewport_x + context.shared_state.viewport_columns() < map.layout().width) {
                ++viewport_x;
            } else if (screen_x < 0x4a) {
                screen_x += 2;
            }
        } else if (action == InputAction::up) {
            if (screen_y == 0x50 && viewport_y > 0) --viewport_y;
            else if (screen_y >= 8) screen_y -= 8;
        } else if (action == InputAction::down) {
            if (screen_y == 0x50 &&
                viewport_y + context.shared_state.viewport_rows() < map.layout().height) {
                ++viewport_y;
            } else if (screen_y < 0xb0) {
                screen_y += 8;
            }
        }
        context.shared_state.set_actor_screen_x(screen_x);
        context.shared_state.set_actor_screen_y(screen_y);
        context.shared_state.set_viewport_x(viewport_x);
        context.shared_state.set_viewport_y(viewport_y);
        context.shared_state.set_u16(
            0x40d, static_cast<std::uint16_t>(context.shared_state.u16(0x40f) +
                (static_cast<std::size_t>(viewport_y) * map.layout().width +
                 viewport_x) * 2U));
        advance_rpg_party_formation(
            context.shared_state,
            static_cast<std::int16_t>(
                (static_cast<int>(previous_viewport_x) -
                 static_cast<int>(viewport_x)) * 2),
            static_cast<std::int16_t>(
                (static_cast<int>(previous_viewport_y) -
                 static_cast<int>(viewport_y)) * 8));

        const auto world_step = advance_rpg_world_step(
            context.shared_state, world_step_runtime, rpg_load_image,
            location.area.auxiliary != 0U);
        if (!world_step.defeated_party_members.empty()) {
            // The main-loop warning is rendered through CHAIN.DSK, not the
            // current area's CHNA font; its two uncommon glyphs exist only in
            // that shared field/menu subset.
            auto host = make_event_host(item_font);
            for (const auto actor : world_step.defeated_party_members) {
                static_cast<void>(actor);
                host.show_dialogue(20U, poison_defeat_text);
                if (host.quit_requested()) {
                    context.platform.stop_audio();
                    return Marker::none;
                }
            }
        }
        if (world_step.poison_flash) {
            auto poison_frame = compose_scene();
            std::fill(poison_frame.pixels.begin(), poison_frame.pixels.end(), 0x6bU);
            context.platform.present({
                320, 200, poison_frame.pixels,
                std::span<const std::uint8_t, 768>(poison_frame.palette)});
            context.platform.delay_for(std::chrono::milliseconds(15));
            auto restored_frame = compose_scene();
            context.platform.present({
                320, 200, restored_frame.pixels,
                std::span<const std::uint8_t, 768>(restored_frame.palette)});
            context.platform.delay_for(std::chrono::milliseconds(15));
        }
        if (world_step.random_encounter) {
            context.platform.stop_audio();
            return Marker::open_figure;
        }
        advance_rpg_entities(location.area, map, context.shared_state,
                             entity_runtime, rpg_load_image);
    }
    }
}

}  // namespace swd2
