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

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("RPG module cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

void draw_actor(Viewport& viewport, const SpriteArchive& actors, const SharedState& state) {
    const auto sprite_index = static_cast<std::size_t>(state.actor_sprite_base() +
                                                       state.actor_direction() +
                                                       (state.actor_animation() & 1U));
    const auto& sprite = actors.sprites().at(sprite_index);
    const auto pixels = actors.pixels(sprite_index);
    const auto left = static_cast<int>(state.actor_screen_x() + state.actor_x_offset()) * 4;
    const auto top = static_cast<int>(state.actor_screen_y()) + state.actor_y_offset();
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
// The bounds are explicit because ITEM2 descriptions can be longer than the
// status pane and the DOS renderer clipped them to its active rectangle.
void draw_legacy_text(Viewport& viewport, const LegacyFont& font,
                      std::span<const std::uint8_t> text, int left, int top,
                      int width, int height, std::uint8_t color) {
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
        if (x + static_cast<int>(LegacyFont::glyph_width) > width) {
            x = 0;
            y += 16;
            if (y + static_cast<int>(LegacyFont::glyph_height) > height) break;
        }
        const auto code = static_cast<std::uint16_t>(text[cursor]) << 8U |
                          text[cursor + 1];
        if (font.contains(code)) {
            const auto glyph = font.rasterize(code);
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

void install_map_location(SharedState& state, MapDatabase& database,
                          std::uint16_t encoded_directory_offset) {
    const auto position_only = (encoded_directory_offset & 0x8000U) != 0;
    const auto directory_offset =
        static_cast<std::uint16_t>(encoded_directory_offset & 0x1fffU);
    const auto& location = database.location_at_directory_offset(directory_offset);
    state.set_u16(0x424, directory_offset);
    state.set_u16(0x40d, location.map_position);
    state.set_viewport_x(location.viewport_x);
    state.set_viewport_y(location.viewport_y);
    state.set_actor_screen_x(location.actor_screen_x);
    state.set_actor_screen_y(location.actor_screen_y);
    for (std::size_t i = 0; i < 12; ++i) {
        state.set_u16(0xa2 + i * 2, location.actor_direction);
    }
    if (position_only) return;

    for (std::size_t i = 0; i < location.big5_name.size(); ++i) {
        state.set_u8(0x3f6 + i, location.big5_name[i]);
    }
    state.set_u16(0x408, location.area.flags);
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
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

void draw_entities(Viewport& viewport, const MapLocationRecord& location,
                   const SharedState& state, const std::filesystem::path& game_root,
                   const SpriteArchive& actors,
                   std::map<std::uint16_t, PlanarSpriteSet>& animation_sets) {
    const auto cell_base = state.u16(0x40f);
    const auto map_width = state.map_width();
    for (std::size_t index = 0; index < location.area.entity_count(); ++index) {
        const auto entity = map_entity(location.area, index);
        if (entity.behavior == 3 || entity.cell_offset < cell_base ||
            ((entity.cell_offset - cell_base) & 1U) != 0) {
            continue;
        }
        const auto cell = static_cast<std::size_t>((entity.cell_offset - cell_base) / 2);
        const auto world_x = cell % map_width;
        const auto world_y = cell / map_width;
        const auto left = (static_cast<int>(world_x) - state.viewport_x()) * 8 +
                          static_cast<int>(entity.render_x_offset) * 4;
        const auto top = (static_cast<int>(world_y) - state.viewport_y()) * 8 - 16 +
                         entity.render_y_offset;
        if (left >= 320 || top >= 200 || left < -160 || top < -200) continue;

        auto frame_index = static_cast<std::size_t>(entity.sprite & 0xffU) +
                           (entity.animation_frame & 1U);
        if (entity.behavior != 4 && entity.behavior != 5 && entity.behavior != 6) {
            frame_index += entity.direction;
        }
        const auto resource = static_cast<std::uint16_t>(entity.sprite >> 8U);
        if (resource == 0) {
            if (frame_index >= actors.sprites().size()) continue;
            const auto& sprite = actors.sprites()[frame_index];
            blit(viewport, actors.pixels(frame_index), sprite.width, sprite.height, left, top);
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
                 std::span<const std::uint8_t> inventory_category_labels,
                 std::span<const std::uint8_t> equipment_slot_labels,
                 std::span<const std::uint8_t> equipment_stat_labels,
                 std::function<Viewport()> scene_provider,
                 MapDatabase* map_database, const SaveSlotWriter* save_slot,
                 FieldActionRuntime& field_action_runtime,
                 SharedState& state, std::filesystem::path game_root,
                 std::filesystem::path* playing_music)
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
          inventory_category_labels_(inventory_category_labels),
          equipment_slot_labels_(equipment_slot_labels),
          equipment_stat_labels_(equipment_stat_labels),
          scene_provider_(std::move(scene_provider)),
          map_database_(map_database), save_slot_(save_slot),
          field_action_runtime_(field_action_runtime), state_(state),
          game_root_(std::move(game_root)), playing_music_(playing_music) {}

    void show_dialogue(std::uint16_t, std::span<const std::uint8_t> text) override {
        std::size_t offset = 0;
        do {
            const auto page = render_dialogue_page(font_, text, offset, 288, 64, 15, &name_font_);
            auto frame = event_scene();
            for (std::size_t y = 0; y < page.height; ++y) {
                for (std::size_t x = 0; x < page.width; ++x) {
                    const auto destination = (128 + y) * 320 + 16 + x;
                    frame.pixels[destination] = page.pixels[y * page.width + x];
                }
            }
            platform_.present({320, 200, frame.pixels,
                               std::span<const std::uint8_t, 768>(frame.palette)});
            InputAction action;
            do {
                action = platform_.wait_for_input();
            } while (action != InputAction::confirm && action != InputAction::cancel &&
                     action != InputAction::quit);
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return;
            }
            offset = page.next_offset;
            if (!page.has_more) return;
        } while (offset < text.size());
    }

    void delay(std::uint16_t ticks) override {
        // RPG.EXE waits on its 70 Hz IRQ word at 5a55. The backend sleep keeps
        // that timing without the original busy loop.
        platform_.delay_for(std::chrono::milliseconds(
            (static_cast<std::uint64_t>(ticks) * 1000U + 69U) / 70U));
    }

    bool present_event_command(std::uint16_t opcode,
                               std::span<const std::uint16_t> arguments) override {
        switch (opcode) {
        case 5:
            fade_out();
            return true;
        case 6:
        case 45:
            fade_in();
            return true;
        case 7:
            palette_dark_ = false;
            present(event_scene());
            return true;
        case 14:
            compact_money_overlay_ = true;
            present_timed(event_scene());
            return true;
        case 22:
        case 30:
        case 31:
        case 32:
        case 33:
        case 36:
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
            present_timed(shifted_scene(event_scene(), arguments[0]));
            return true;
        case 55:
            // RPG:5c35 snapshots DS:5a5c into a private palette table, then
            // 0dbf:0314 rewrites palette indices 10h..1fh on all four VGA
            // planes to an inverse-luminance ramp. Keep the transformed page
            // active for the following positioned text/fade until a new DE
            // background overwrites it.
            monochrome_event_page_ = true;
            present(event_scene());
            return true;
        case 56:
            platform_.stop_audio();
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
        positioned_text_.push_back(
            {x_byte, y, std::vector<std::uint8_t>(text.begin(), text.end())});
        present(event_scene());
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
                             10 * 4, 141, 240, 16, 15);
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
                        41 * 4, 17, 32, 16, 15);
                }
            }

            constexpr std::size_t visible_rows = 5;
            for (std::size_t row = 0; row < visible_rows; ++row) {
                const auto index = first_visible + row;
                if (index >= item_ids.size()) break;
                const auto item_id = item_ids[index];
                const auto top = 43 + static_cast<int>(row) * 16;
                draw_item_text(frame, item_texts_, item_font_, item_id,
                               92, top, 96, 15, 15);
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
            while (!finished_confirmation) {
                auto confirmation = frame;
                draw_bottom_message(confirmation, shop_confirmation_prompt_);
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
        std::size_t selected = 0;
        std::size_t first_visible = 0;
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
                        62 * 4, 17, 32, 16, 15);
                }
            }
            for (std::size_t row = 0; row < visible_rows; ++row) {
                const auto slot = first_visible + row;
                if (slot >= inventory_slot_count) break;
                const auto item_id = inventory.item(slot);
                const auto top = 49 + static_cast<int>(row) * 16;
                draw_item_text(frame, item_texts_, item_font_, item_id,
                               128, top, 96, 15, item_id == 0 ? 8 : 15);
                if (item_id >= 0x44 && item_id <= 0x48) {
                    draw_menu_number(
                        frame, menu_sprites_,
                        state.u16(0x3e6 + (item_id - 0x44) * 2U),
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
            if (action == InputAction::cancel) return InventoryUiResult::cancelled;
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
                    if (!value) continue;
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
                if (definition.equipment_category() == 0) {
                    if (!definition.field_usable()) continue;

                    if (definition.effect_code == 0x6a) {
                        if (map_database_ == nullptr || save_slot_ == nullptr ||
                            !*save_slot_) {
                            continue;
                        }
                        RpgSaveSlotSelector selector;
                        bool save_cancelled = false;
                        while (true) {
                            auto save_frame = scene_provider_();
                            draw_rpg_selector_panel(
                                save_frame.pixels, 320, 200, menu_sprites_,
                                4, 112, 7, 4);
                            draw_legacy_text(save_frame, item_font_, save_slot_prompt_,
                                             10 * 4, 125, 260, 16, 15);
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
                    if (FieldActionSystem::requires_target(definition.effect_code)) {
                        actor = 0;
                        const auto party_count = std::max<std::size_t>(
                            1, std::min<std::size_t>(state.u16(0x10), 4));
                        bool target_cancelled = false;
                        while (true) {
                            auto target_frame = scene_provider_();
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
                                break;
                            }
                        }
                        if (target_cancelled) continue;
                    }

                    const auto field_result = field_actions.apply(definition.effect_code, actor);
                    if (!field_result.dispatched()) continue;

                    std::optional<std::uint8_t> travel_index;
                    if (field_result.status == FieldActionStatus::travel_current) {
                        const auto current = state.u16(0x40a);
                        if (current <= 0xffU) {
                            travel_index = static_cast<std::uint8_t>(current);
                        }
                    } else if (field_result.status == FieldActionStatus::travel_select) {
                        const auto destinations = field_actions.unlocked_travel_indices();
                        if (destinations.empty()) continue;
                        std::size_t destination = 0;
                        std::size_t first_visible = 0;
                        auto scroll_cue = RpgListSelection::ScrollCue::none;
                        bool destination_cancelled = false;
                        while (true) {
                            auto destination_frame = scene_provider_();
                            const auto visible = std::min<std::size_t>(destinations.size(), 10);
                            draw_rpg_selector_panel(
                                destination_frame.pixels, 320, 200, menu_sprites_,
                                14, 16, 5, static_cast<int>(visible));

                            const auto transparent = [&](std::size_t sprite,
                                                         int x_byte, int y) {
                                if (sprite >= menu_sprites_.sprites().size()) return;
                                const auto& info = menu_sprites_.sprites()[sprite];
                                blit(destination_frame, menu_sprites_.pixels(sprite),
                                     info.width, info.height, x_byte * 4, y);
                            };
                            const auto maximum_first = destinations.size() - visible;
                            draw_rpg_selector_scrollbar(
                                destination_frame.pixels, 320, 200,
                                menu_sprites_, 14, 16, 5, visible,
                                maximum_first, first_visible, scroll_cue);
                            scroll_cue = RpgListSelection::ScrollCue::none;

                            for (std::size_t row = 0; row < visible; ++row) {
                                const auto index = first_visible + row;
                                if (index >= destinations.size()) break;
                                const auto label_offset =
                                    static_cast<std::size_t>(destinations[index]) * 8U;
                                if (label_offset + 8U <= travel_labels_.size()) {
                                    draw_legacy_text(
                                        destination_frame, item_font_,
                                        travel_labels_.subspan(label_offset, 8),
                                        22 * 4, 29 + static_cast<int>(row) * 16,
                                        64, 16, 15);
                                }
                            }
                            transparent(1, 20,
                                        25 + static_cast<int>(destination -
                                                              first_visible) * 16);
                            platform_.present({
                                320, 200, destination_frame.pixels,
                                std::span<const std::uint8_t, 768>(destination_frame.palette)});
                            const auto destination_action = platform_.wait_for_input();
                            if (destination_action == InputAction::quit) {
                                quit_requested_ = true;
                                return InventoryUiResult::cancelled;
                            }
                            if (destination_action == InputAction::cancel) {
                                destination_cancelled = true;
                                break;
                            }
                            if (destination_action == InputAction::confirm) {
                                travel_index = destinations[destination];
                                break;
                            } else {
                                const auto selection = rpg_list_selection_input(
                                    {destination, first_visible},
                                    destinations.size(), visible,
                                    destination_action);
                                destination = selection.selected;
                                first_visible = selection.first_visible;
                                scroll_cue = selection.scroll_cue;
                            }
                        }
                        if (destination_cancelled) continue;
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
                    auto actor_frame = scene_provider_();
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
                    continue;
                }
                auto equipment_slot = initial_slot(definition.equipment_category());
                bool back_to_inventory = false;
                while (!back_to_inventory) {
                    auto equipment_frame = scene_provider_();
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
                    draw_item_text(equipment_frame, item_texts_, item_font_, selected_item,
                                   51 * 4, 9, 96, 15, 14);
                    const auto actor_base = 0x106 + actor * 0x9f;
                    draw_legacy_text(equipment_frame, item_font_,
                                     equipment_slot_labels_, 6 * 4, 13,
                                     80, 176, 15);
                    for (std::size_t slot = 0; slot < equipment_slot_count; ++slot) {
                        const auto equipped = state.u16(actor_base + 0x10 + slot * 2);
                        draw_item_text(equipment_frame, item_texts_, item_font_, equipped,
                                       28 * 4, 13 + static_cast<int>(slot) * 16,
                                       96, 15,
                                       static_cast<std::uint8_t>(equipped == 0 ? 8 : 14));
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
                            48 * 4, stat_y, 64, 16, 15);
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
                        static_cast<void>(inventory.exchange_equipment(
                            selected, actor, equipment_slot));
                    }
                }
            }
        }
    }

    [[nodiscard]] bool quit_requested() const noexcept { return quit_requested_; }

private:
    void draw_bottom_message(Viewport& frame,
                             std::span<const std::uint8_t> text) const {
        // 49d0 begins with 2cce: a 7x4 selector panel at Mode-X (4,112),
        // then starts its Big5 stream at (10,125).
        draw_rpg_selector_panel(frame.pixels, 320, 200, menu_sprites_,
                                4, 112, 7, 4);
        draw_legacy_text(frame, item_font_, text,
                         10 * 4, 125, 240, 64, 15);
    }

    bool show_bottom_message(Viewport frame,
                             std::span<const std::uint8_t> text) {
        draw_bottom_message(frame, text);
        platform_.present({
            320, 200, frame.pixels,
            std::span<const std::uint8_t, 768>(frame.palette)});
        while (true) {
            const auto action = platform_.wait_for_input();
            if (action == InputAction::quit) {
                quit_requested_ = true;
                return false;
            }
            if (action == InputAction::confirm ||
                action == InputAction::cancel) {
                return true;
            }
        }
    }

    void present(Viewport scene) {
        if (palette_dark_) scene.palette.fill(0);
        platform_.present({320, 200, scene.pixels,
                           std::span<const std::uint8_t, 768>(scene.palette)});
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
        monochrome_event_page_ = false;
        compact_money_overlay_ = false;
        positioned_text_.clear();
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
    }

    void play_event_music(std::uint16_t number) {
        std::ostringstream name;
        name << "RI" << std::setw(3) << std::setfill('0') << number << ".RIX";
        const auto relative = std::filesystem::path("RX") / name.str();
        const auto path = game_root_ / relative;
        if (std::filesystem::is_regular_file(path)) {
            // RPG DS:3bf5 is RX/RI000.RIX. Handler 5ab5 patches those digits
            // and calls 144b, the normal looping RIX start routine.
            platform_.play_music(read_file(path), true);
            if (playing_music_) *playing_music_ = relative;
        }
    }

    Viewport event_scene() {
        auto frame = scene_provider_();
        if (cutscene_) {
            frame.palette = cutscene_->palette();
            const auto frame_index = static_cast<std::size_t>(state_.u16(0x411)) %
                                     cutscene_->frame_count();
            const auto& source = cutscene_->frame(frame_index);
            const auto left = (320 - static_cast<int>(source.width)) / 2;
            const auto top = (200 - static_cast<int>(source.height)) / 2;
            blit(frame, source.pixels, source.width, source.height, left, top);
        }
        if (compact_money_overlay_) {
            draw_compact_money_overlay(frame, menu_sprites_, state_.u16(0x104));
        }
        for (const auto& layer : positioned_text_) {
            draw_legacy_text(frame, font_, layer.text,
                             static_cast<int>(layer.x_byte) * 4,
                             layer.y, 320 - static_cast<int>(layer.x_byte) * 4,
                             200 - static_cast<int>(layer.y), 15);
        }
        if (monochrome_event_page_) {
            apply_rpg_event_monochrome_filter(
                frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette));
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
    std::span<const std::uint8_t> inventory_category_labels_;
    std::span<const std::uint8_t> equipment_slot_labels_;
    std::span<const std::uint8_t> equipment_stat_labels_;
    std::function<Viewport()> scene_provider_;
    MapDatabase* map_database_{};
    const SaveSlotWriter* save_slot_{};
    FieldActionRuntime& field_action_runtime_;
    SharedState& state_;
    std::filesystem::path game_root_;
    std::filesystem::path* playing_music_{};
    std::optional<PlanarSpriteSet> cutscene_;
    std::map<std::uint16_t, SpriteArchive> item_preview_cache_;
    std::optional<std::uint16_t> cutscene_dictionary_id_;
    std::optional<std::uint16_t> cutscene_id_;
    struct PositionedText {
        std::uint16_t x_byte{};
        std::uint16_t y{};
        std::vector<std::uint8_t> text;
    };
    std::vector<PositionedText> positioned_text_;
    std::uint16_t frame_delay_ticks_{};
    bool compact_money_overlay_{};
    bool palette_dark_{};
    bool monochrome_event_page_{};
    bool quit_requested_{};
};

std::optional<std::size_t> entity_in_front(const MapLocationRecord& location,
                                           const SharedState& state,
                                           const MapResource& map,
                                           bool automatic_only = false) {
    auto x = static_cast<int>(state.world_x());
    auto y = static_cast<int>(state.world_y());
    if (state.actor_direction() == 0) ++y;
    else if (state.actor_direction() == 3) --y;
    else if (state.actor_direction() == 6) --x;
    else if (state.actor_direction() == 9) ++x;
    if (x < 0 || y < 0 || x >= map.layout().width || y >= map.layout().height) {
        return std::nullopt;
    }
    const auto target = static_cast<std::uint16_t>(
        state.u16(0x40f) + (static_cast<std::size_t>(y) * map.layout().width + x) * 2);
    for (std::size_t index = 0; index < location.area.entity_count(); ++index) {
        const auto entity = map_entity(location.area, index);
        if (entity.behavior == 3 || entity.event_directory_offset == 0) continue;
        if (automatic_only && (entity.flags & 0x8000U) == 0) continue;
        if (entity.cell_offset == target || entity.cell_offset + 2U == target ||
            entity.cell_offset + 4U == target) {
            return index;
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
    const auto item_font = LegacyFont::load(context.game_root / "CHAIN.DSK");
    const auto items = ItemDatabase::load(context.game_root / "ITEM.EXE");
    const auto item_texts = ItemTextDatabase::load(context.game_root / "ITEM2.EXE");
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
    FieldActionRuntime field_action_runtime;
    while (true) {
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
    // wandering and most current-entity event writes therefore disappear on
    // a map reload rather than modifying the source MAPZ record.
    auto location = map_database.location_at_directory_offset(
        context.shared_state.map_location_directory_offset());
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
    if (!requested_music.empty() && requested_music != playing_music) {
        context.platform.play_music(read_file(context.game_root / requested_music), true);
        playing_music = requested_music;
    }
    static_cast<void>(event_archive);
    static_cast<void>(event_font);
    static_cast<void>(location);
    auto actor_data = decode_rsk_block(read_file(context.game_root / "MAN1.RSK")).data;
    const auto actors = SpriteArchive::parse(std::move(actor_data));
    std::map<std::uint16_t, PlanarSpriteSet> animation_sets;
    const auto compose_scene = [&]() {
        const auto rendered = map.render(true);
        auto viewport = crop_map(rendered, context.shared_state.viewport_x(),
                                 context.shared_state.viewport_y());
        draw_entities(viewport, location, context.shared_state, context.game_root, actors,
                      animation_sets);
        draw_actor(viewport, actors, context.shared_state);
        return viewport;
    };
    struct EntityEventOutcome {
        Marker marker{Marker::none};
        bool quit{};
        bool map_reload{};
    };
    const auto run_entity_event = [&](std::size_t entity_index) {
        RpgEventHost host(context.platform, event_font, name_font, item_font,
                          items, item_texts, menu_sprites, equipment_art,
                          save_slot_prompt, travel_labels, shop_prompt,
                          shop_sale_prompt, shop_money_error,
                          shop_inventory_error, shop_confirmation_prompt,
                          shop_quantity_error,
                          inventory_category_labels, equipment_slot_labels,
                          equipment_stat_labels,
                          compose_scene,
                          &map_database, &context.save_slot,
                          field_action_runtime,
                          context.shared_state, context.game_root, &playing_music);
        const auto entity = map_entity(location.area, entity_index);
        const auto result = execute_event(
            event_archive, entity.event_directory_offset, context.shared_state,
            &location.area, entity_index, host, 10'000, &map_database);
        return EntityEventOutcome{result.requested_marker,
                                  host.quit_requested(),
                                  result.requested_map_reload};
    };
    while (true) {
        auto viewport = compose_scene();
        context.platform.present({320, 200, viewport.pixels,
                                  std::span<const std::uint8_t, 768>(viewport.palette)});

        auto action = context.platform.wait_for_input();
        if (action == InputAction::quit || action == InputAction::cancel) {
            context.platform.stop_audio();
            return Marker::none;
        }
        if (action == InputAction::confirm) {
            if (const auto entity = entity_in_front(location, context.shared_state, map)) {
                const auto outcome = run_entity_event(*entity);
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
                    // database (including opcode-34 mutations) remains alive.
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
        auto [target_x, target_y] = movement_target(action);

        if (movement_blocked(location, context.shared_state, map,
                             action, target_x, target_y)) {
            // RPG.EXE distinguishes explicit interaction from collision
            // activation. Only entities with field-8 bit 8000h may start an
            // event merely because the player walked into them.
            if (movement_has_special_cell(location, context.shared_state, map,
                                          action, target_x, target_y)) {
              if (const auto entity = entity_in_front(
                      location, context.shared_state, map, true)) {
                const auto outcome = run_entity_event(*entity);
                if (outcome.quit) {
                    context.platform.stop_audio();
                    return Marker::none;
                }
                if (outcome.marker != Marker::none) {
                    context.platform.stop_audio();
                    return outcome.marker;
                }
                if (outcome.map_reload) {
                    pending_map_reload_ = true;
                    break;
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
        context.shared_state.set_actor_animation(
            static_cast<std::uint16_t>((context.shared_state.actor_animation() + 1U) & 3U));
        advance_rpg_entities(location.area, map, context.shared_state,
                             entity_runtime, rpg_load_image);
    }
    }
}

}  // namespace swd2
