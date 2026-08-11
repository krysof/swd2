#include "swd2/battle_module.hpp"

#include "swd2/asset_catalog.hpp"
#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_command_menu.hpp"
#include "swd2/battle_database.hpp"
#include "swd2/battle_random.hpp"
#include "swd2/battle_presentation.hpp"
#include "swd2/battle_session.hpp"
#include "swd2/dialogue.hpp"
#include "swd2/fig_timing.hpp"
#include "swd2/legacy_font.hpp"
#include "swd2/monster_definition.hpp"
#include "swd2/rsk_decoder.hpp"
#include "swd2/script_archive.hpp"
#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace swd2 {

namespace {

struct BattleSurface {
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(320 * 200, 0xfe);
    std::array<std::uint8_t, 768> palette{};
};

struct BattleRewards {
    std::uint16_t experience{};
    std::uint16_t money{};
};

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> callback)
        : callback_(std::move(callback)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() {
        if (callback_) callback_();
    }

private:
    std::function<void()> callback_;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("FIG module cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool delay_for_or_frontend_quit(
    PlatformBackend& platform, std::chrono::milliseconds duration);

std::uint16_t span_word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2U > bytes.size()) return 0;
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::filesystem::path monster_path(const std::filesystem::path& root,
                                   std::uint16_t sprite_number);

SpriteArchive load_sprites(const std::filesystem::path& path) {
    return SpriteArchive::parse(decode_rsk_block(read_file(path)).data);
}

void blit(BattleSurface& surface, const SpriteArchive& archive, std::size_t index,
          int left, int top, bool transparent = true) {
    const auto& sprite = archive.sprites().at(index);
    const auto pixels = archive.pixels(index);
    for (std::size_t y = 0; y < sprite.height; ++y) {
        for (std::size_t x = 0; x < sprite.width; ++x) {
            const auto destination_x = left + static_cast<int>(x);
            const auto destination_y = top + static_cast<int>(y);
            const auto color = pixels[y * sprite.width + x];
            if (destination_x >= 0 && destination_x < 320 && destination_y >= 0 &&
                destination_y < 200 && (!transparent || color != 0xfe)) {
                surface.pixels[static_cast<std::size_t>(destination_y) * 320 +
                               static_cast<std::size_t>(destination_x)] = color;
            }
        }
    }
}

void blit_mode0_stencil(BattleSurface& surface,
                        const SpriteArchive& archive, std::size_t index,
                        int left, int top) {
    const auto& sprite = archive.sprites().at(index);
    const auto source = archive.pixels(index);
    // SpriteArchive::pixels has already de-interleaved the four Mode-X planes
    // into display order.  65f1 -> 684f uses each resulting source byte only
    // as an FEh transparency mask and writes colour 0fh for every covered
    // pixel.  Treating this span as plane-major a second time creates striped
    // columns; the captured-ally 3eh dismissal proves the released renderer
    // instead produces one solid 21x48 AF silhouette at x=273..293.
    for (std::size_t y = 0; y < sprite.height; ++y) {
        for (std::size_t x = 0; x < sprite.width; ++x) {
            if (source[y * sprite.width + x] == 0xfeU) continue;
            const auto destination_x = left + static_cast<int>(x);
            const auto destination_y = top + static_cast<int>(y);
            if (destination_x >= 0 && destination_x < 320 &&
                destination_y >= 0 && destination_y < 200) {
                surface.pixels[static_cast<std::size_t>(destination_y) * 320U +
                               static_cast<std::size_t>(destination_x)] = 0x0f;
            }
        }
    }
}

void blit_mirrored(BattleSurface& surface, const SpriteArchive& archive,
                   std::size_t index, int left, int top,
                   bool transparent = true) {
    const auto& sprite = archive.sprites().at(index);
    const auto pixels = archive.pixels(index);
    for (std::size_t y = 0; y < sprite.height; ++y) {
        for (std::size_t x = 0; x < sprite.width; ++x) {
            const auto destination_x =
                left + static_cast<int>(sprite.width - 1U - x);
            const auto destination_y = top + static_cast<int>(y);
            const auto color = pixels[y * sprite.width + x];
            if (destination_x >= 0 && destination_x < 320 &&
                destination_y >= 0 && destination_y < 200 &&
                (!transparent || color != 0xfe)) {
                surface.pixels[static_cast<std::size_t>(destination_y) * 320U +
                               static_cast<std::size_t>(destination_x)] = color;
            }
        }
    }
}

void draw_selector_panel(BattleSurface& surface,
                         const SpriteArchive& menu_sprites,
                         int left, int top, int columns, int rows);
void transform_menu_tiles(
    BattleSurface& surface,
    std::span<const std::pair<int, int>> tiles,
    std::size_t selected, std::uint8_t base_table);

BattleSurface compose_introduction(
    const BattleSurface& scene, const DialoguePage& page,
    const SpriteArchive& menu_sprites,
    std::optional<std::size_t> prompt_selection = std::nullopt) {
    auto result = scene;
    // FIG 3de8 configures the generic 38e6 selector at Mode-X x=4/y=112,
    // with seven middle columns and four text rows. Text starts at x=10/y=125.
    // DATA:4d35 is zero; DialoguePage uses one only as a host-side mask so
    // that the original black glyph can be distinguished from empty pixels.
    draw_selector_panel(result, menu_sprites, 4, 112, 7, 4);
    for (std::size_t y = 0; y < page.height && 125 + y < 200; ++y) {
        for (std::size_t x = 0; x < page.width && 40 + x < 320; ++x) {
            const auto mask = page.pixels[y * page.width + x];
            if (mask != 0) result.pixels[(125 + y) * 320 + 40 + x] = 0;
        }
    }
    if (prompt_selection) {
        // FIG 5c98: two frame-0 tiles at x=44/62,y=90, with frame 29 and 8
        // spelling the original choices. Selection uses the same 1e91 colour
        // translation as the battle command menu, not a synthetic rectangle.
        blit(result, menu_sprites, 0, 44 * 4, 90);
        blit(result, menu_sprites, 0, 62 * 4, 90);
        blit(result, menu_sprites, 29, 50 * 4, 99);
        blit(result, menu_sprites, 8, 68 * 4, 99);
        static constexpr std::array<std::pair<int, int>, 2> tiles = {{
            {44, 90}, {62, 90},
        }};
        transform_menu_tiles(result, tiles, *prompt_selection, 3);
    }
    return result;
}

void fill_rect(BattleSurface& surface, int left, int top, int width, int height,
               std::uint8_t color) {
    const auto x0 = std::clamp(left, 0, 320);
    const auto y0 = std::clamp(top, 0, 200);
    const auto x1 = std::clamp(left + width, 0, 320);
    const auto y1 = std::clamp(top + height, 0, 200);
    for (auto y = y0; y < y1; ++y) {
        std::fill(surface.pixels.begin() + static_cast<std::ptrdiff_t>(y * 320 + x0),
                  surface.pixels.begin() + static_cast<std::ptrdiff_t>(y * 320 + x1),
                  color);
    }
}

void darken_fig_dispatcher_palette(BattleSurface& surface) {
    // 43f9 walks 60h DAC bytes from DS:3eec, i.e. palette colours C0h..DFh,
    // and subtracts three with saturation. 43ce invokes it five times before
    // entering every >30h player effect handler.
    constexpr auto first = 0xc0U * 3U;
    constexpr auto finish = 0xe0U * 3U;
    for (auto offset = first; offset < finish; ++offset) {
        surface.palette[offset] = static_cast<std::uint8_t>(
            surface.palette[offset] <= 3U ? 0U : surface.palette[offset] - 3U);
    }
}

void brighten_fig_dispatcher_palette(
    BattleSurface& surface,
    const std::array<std::uint8_t, 768>& original_palette) {
    // 4456 reverses one fade step by adding three and clamping to the saved
    // BA palette at DS:420c.
    constexpr auto first = 0xc0U * 3U;
    constexpr auto finish = 0xe0U * 3U;
    for (auto offset = first; offset < finish; ++offset) {
        surface.palette[offset] = static_cast<std::uint8_t>(std::min<unsigned>(
            static_cast<unsigned>(surface.palette[offset]) + 3U,
            original_palette[offset]));
    }
}

void rotate_fig_existing_result_palette(
    std::array<std::uint8_t, 768>& palette,
    std::optional<std::size_t> page_index,
    std::size_t entering_shift = 0) {
    // The autonomous slot-two path enters 5ed8 without calling 14a3 first.
    // Rotate the five E0h..E4h colours already installed by the preceding
    // action instead of replacing them with a canonical ability ramp.
    constexpr auto colors = std::size_t{5};
    constexpr auto first = std::size_t{0xe0} * 3U;
    std::array<std::uint8_t, colors * 3U> source{};
    std::copy_n(palette.begin() + static_cast<std::ptrdiff_t>(first),
                source.size(), source.begin());
    const auto shift =
        (entering_shift +
         (page_index ? (*page_index + 1U) / 2U : 0U)) % colors;
    for (std::size_t destination = 0; destination < colors; ++destination) {
        const auto source_color =
            (destination + colors - shift) % colors;
        std::copy_n(
            source.begin() + static_cast<std::ptrdiff_t>(source_color * 3U),
            3, palette.begin() + static_cast<std::ptrdiff_t>(
                   first + destination * 3U));
    }
}

void draw_big5(BattleSurface& surface, const LegacyFont& font,
               const LegacyFont& fallback, std::span<const std::uint8_t> text,
               int left, int top, std::uint8_t color) {
    for (std::size_t offset = 0; offset + 1 < text.size(); offset += 2) {
        if (text[offset] == 0 && text[offset + 1] == 0) break;
        const auto code = static_cast<std::uint16_t>(text[offset]) << 8U |
                          text[offset + 1];
        const auto glyph = !font.contains(code) && fallback.contains(code)
                               ? fallback.rasterize(code)
                               : font.rasterize_or_first(code);
        for (std::size_t y = 0; y < LegacyFont::glyph_height; ++y) {
            for (std::size_t x = 0; x < LegacyFont::glyph_width; ++x) {
                const auto destination_x = left + static_cast<int>(x);
                const auto destination_y = top + static_cast<int>(y);
                if (glyph[y * LegacyFont::glyph_width + x] != 0 &&
                    destination_x >= 0 && destination_x < 320 &&
                    destination_y >= 0 && destination_y < 200) {
                    surface.pixels[static_cast<std::size_t>(destination_y) * 320 +
                                   static_cast<std::size_t>(destination_x)] = color;
                }
            }
        }
        left += static_cast<int>(LegacyFont::glyph_width);
    }
}

int fig_card_bar_extent(std::uint16_t current, std::uint16_t maximum,
                        std::uint16_t scale) {
    if (current == 0 || maximum == 0) return 0;
    return std::max(1, static_cast<int>(current) * scale / maximum);
}

std::uint16_t rotate_left_word(std::uint16_t value, unsigned count) {
    count &= 15U;
    if (count == 0U) return value;
    return static_cast<std::uint16_t>((value << count) |
                                      (value >> (16U - count)));
}

void draw_fig_party_card(BattleSurface& surface,
                         const SpriteArchive& menu_sprites,
                         const BattlePartyMember& member,
                         bool action_background = false,
                         std::optional<int> action_mode_x_anchor = std::nullopt) {
    const auto left = action_background && action_mode_x_anchor
                          ? (*action_mode_x_anchor - 4) * 4
                          : (8 + static_cast<int>(member.party_index) * 18) * 4;
    constexpr int top = 150;
    const auto portrait = action_background
                              ? std::size_t{180}
                              : 76U + member.identity / 12U;
    if (portrait < menu_sprites.sprites().size()) {
        // Normal portraits use 65fc's opaque copy. During an actor action,
        // 137a installs frame 180 through the masked path instead: its FE
        // interior must retain the BA page, not become palette colour FE.
        blit(surface, menu_sprites, portrait, left, top, action_background);
    }

    // FIG 2bd9 draws the same three direct Mode-X gauges as RPG 24b8.
    const auto hp = fig_card_bar_extent(
        member.hit_points, member.maximum_hit_points, 44);
    for (auto step = 0; step < hp; ++step) {
        fill_rect(surface, left + 2, top + 45 - step, 2, 1, 0x6b);
    }
    const auto secondary = fig_card_bar_extent(
        member.secondary_points, member.maximum_secondary_points, 44);
    for (auto step = 0; step < secondary; ++step) {
        fill_rect(surface, left + 44, top + 45 - step, 2, 1, 0x84);
    }
    const auto ability = fig_card_bar_extent(
        member.ability_points, member.maximum_ability_points, 40);
    if (ability != 0) {
        const auto complete = ability >> 2;
        auto group = 1;
        for (auto index = 0; index < complete; ++index, ++group) {
            fill_rect(surface, left + group * 4, top + 47, 4, 2, 0xfe);
        }
        if (complete != 0) --group;
        const auto mask = static_cast<std::uint8_t>(rotate_left_word(
            0xf000U, static_cast<unsigned>(ability - complete)));
        for (auto plane = 0; plane < 4; ++plane) {
            if ((mask & (1U << plane)) != 0) {
                fill_rect(surface, left + group * 4 + plane,
                          top + 47, 1, 2, 0x0e);
            }
        }
    }

    auto status = static_cast<std::uint16_t>(member.status_bits & ~0x1000U);
    if (member.maximum_hit_points != 0 &&
        member.hit_points <= (member.maximum_hit_points >> 2U)) {
        status = static_cast<std::uint16_t>(status | 0x1000U);
    }
    if ((status & 0x2000U) != 0) {
        if (menu_sprites.sprites().size() > 153U) {
            // 2cd1 calls 798b with CS:76f2 reset to zero. Zero pixels in the
            // death overlay therefore shade the portrait through table 1.
            composite_legacy_masked_sprite(
                surface.pixels, 320, 200, surface.palette,
                menu_sprites, 153, left, top, 0x00);
        }
        return;
    }
    if ((status & 0x1000U) != 0 && menu_sprites.sprites().size() > 154U) {
        // 2cf1 calls the normal 799b compositor after 7900/7960 have assigned
        // every frame-154 colour its own 35/64 palette blend table.
        composite_legacy_translucent_sprite(
            surface.pixels, 320, 200, surface.palette,
            menu_sprites, 154, left + 4, top + 2);
    }
    auto displayed = 0;
    auto mask = std::uint16_t{0x0800};
    for (std::size_t bit = 0; bit < 11; ++bit, mask >>= 1U) {
        if ((status & mask) == 0) continue;
        const auto frame = 155U + bit;
        if (frame < menu_sprites.sprites().size()) {
            blit(surface, menu_sprites, frame,
                 left + 8 + (displayed & 1) * 16,
                 top + 1 + (displayed / 2) * 16);
        }
        ++displayed;
    }
}

void draw_fig_party_cards(BattleSurface& surface,
                          const SpriteArchive& menu_sprites,
                          std::span<const BattlePartyMember> party,
                          std::optional<std::size_t> action_actor = std::nullopt,
                          std::optional<int> action_mode_x_anchor = std::nullopt) {
    for (const auto& member : party) {
        const auto is_action_actor = action_actor.has_value() &&
                                     *action_actor == member.party_index;
        if (is_action_actor) {
            draw_fig_party_card(surface, menu_sprites, member, true,
                                action_mode_x_anchor);
        } else {
            draw_fig_party_card(surface, menu_sprites, member, false,
                                std::nullopt);
        }
    }
}

void transform_menu_tiles(
    BattleSurface& surface,
    std::span<const std::pair<int, int>> tiles,
    std::size_t selected, std::uint8_t base_table) {
    // FIG 1e91 does not draw a cursor border. It palette-translates each
    // complete 16-byte x 32-line Mode-X tile through 77ef. The active tile
    // receives the next brighter table; at level three it remains untouched.
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        auto table = base_table;
        if (index == selected) {
            if (table == 3U) continue;
            ++table;
        }
        if (tiles[index].first == 0x50) continue;
        apply_fig_palette_translation(
            surface.pixels, 320, 200, surface.palette,
            tiles[index].first, tiles[index].second, 16, 32, table);
    }
}

void draw_main_command_panel(BattleSurface& surface,
                             const SpriteArchive& menu_sprites,
                             std::size_t selected,
                             std::uint8_t base_table = 3) {
    // FIG 1dc9. Coordinates are the original Mode-X byte columns and pixels.
    static constexpr std::array<std::pair<int, int>, 4> tiles = {{
        {6, 40},   // abilities
        {26, 40},  // items
        {16, 8},   // attack
        {16, 72},  // tactics
    }};
    for (const auto& [left, top] : tiles) {
        blit(surface, menu_sprites, 0, left * 4, top);
    }
    blit(surface, menu_sprites, 63, 20 * 4, 17);
    blit(surface, menu_sprites, 64, 24 * 4, 17);
    blit(surface, menu_sprites, 33, 20 * 4, 81);
    blit(surface, menu_sprites, 34, 24 * 4, 81);
    blit(surface, menu_sprites, 58, 10 * 4, 49);
    blit(surface, menu_sprites, 62, 14 * 4, 49);
    blit(surface, menu_sprites, 56, 30 * 4, 49);
    blit(surface, menu_sprites, 57, 34 * 4, 49);
    transform_menu_tiles(surface, tiles, selected, base_table);
}

void draw_attack_mode_panel(BattleSurface& surface,
                            const SpriteArchive& menu_sprites,
                            std::size_t selected,
                            std::uint8_t base_table = 3) {
    // FIG 1f82.
    blit(surface, menu_sprites, 0, 10 * 4, 52);
    blit(surface, menu_sprites, 0, 30 * 4, 52);
    blit(surface, menu_sprites, 63, 14 * 4, 61);
    blit(surface, menu_sprites, 64, 18 * 4, 61);
    blit(surface, menu_sprites, 12, 34 * 4, 61);
    blit(surface, menu_sprites, 13, 38 * 4, 61);
    static constexpr std::array<std::pair<int, int>, 2> tiles = {{
        {10, 52}, {30, 52},
    }};
    transform_menu_tiles(surface, tiles, selected, base_table);
}

void draw_tactics_panel(BattleSurface& surface,
                        const SpriteArchive& menu_sprites,
                        std::size_t selected, bool capture_visible) {
    // FIG 2003.
    blit(surface, menu_sprites, 0, 10 * 4, 84);
    blit(surface, menu_sprites, 0, 30 * 4, 84);
    blit(surface, menu_sprites, 42, 14 * 4, 93);
    blit(surface, menu_sprites, 43, 18 * 4, 93);
    blit(surface, menu_sprites, 47, 34 * 4, 93);
    blit(surface, menu_sprites, 48, 38 * 4, 93);
    if (capture_visible) {
        blit(surface, menu_sprites, 0, 20 * 4, 54);
        blit(surface, menu_sprites, 71, 22 * 4, 63);
        blit(surface, menu_sprites, 72, 26 * 4, 63);
        blit(surface, menu_sprites, 73, 30 * 4, 63);
    }
    static constexpr std::array<std::pair<int, int>, 3> tiles = {{
        {10, 84}, {30, 84}, {20, 54},
    }};
    transform_menu_tiles(
        surface,
        std::span<const std::pair<int, int>>(tiles).first(capture_visible ? 3U : 2U),
        selected, 3);
}

void draw_selector_panel(BattleSurface& surface,
                         const SpriteArchive& menu_sprites,
                         int left, int top, int columns, int rows) {
    // Literal translation of FIG 38e6. `left` is a Mode-X byte column;
    // horizontal body pieces advance eight bytes (32 physical pixels), while
    // rows advance sixteen scanlines.
    auto sprite = [&](std::size_t frame, int x, int y) {
        blit(surface, menu_sprites, frame, x * 4, y);
    };

    sprite(92, left, top);
    sprite(83, left, top + 4);
    auto y = top + 12;
    for (int row = 0; row < rows; ++row, y += 16) {
        sprite(84, left, y);
    }
    sprite(85, left, y);
    sprite(93, left, y + 8);

    auto x = left + 8;
    for (int column = 0; column < columns; ++column, x += 8) {
        sprite(86, x, top + 4);
        y = top + 12;
        for (int row = 0; row < rows; ++row, y += 16) {
            sprite(87, x, y);
        }
        sprite(88, x, y);
    }

    sprite(92, x + 6, top);
    sprite(89, x, top + 4);
    y = top + 12;
    for (int row = 0; row < rows; ++row, y += 16) {
        sprite(90, x, y);
    }
    sprite(91, x, y);
    sprite(93, x + 6, y + 8);
}

void draw_selector_scrollbar(BattleSurface& surface,
                             const SpriteArchive& menu_sprites,
                             std::size_t first) {
    // FIG 39d0 for the 50-slot, eight-visible-row selectors configured by
    // 3bab: x=24, y=0, five middle columns, max first row 42. Frames 94/96/97
    // form the track and frame 99 is its original six-pixel thumb.
    constexpr int mode_x_column = 74;
    blit(surface, menu_sprites, 94, mode_x_column * 4, 4);
    auto y = 20;
    for (int row = 0; row < 7; ++row, y += 16) {
        blit(surface, menu_sprites, 96, mode_x_column * 4, y);
    }
    blit(surface, menu_sprites, 97, mode_x_column * 4, y);

    first = std::min<std::size_t>(first, 42U);
    // 106 / 42 = 2 remainder 22, exactly the DIV result stored in
    // DS:3539/353b by 3a56.
    const auto thumb_y = 20 + static_cast<int>(first * 2U +
                                               std::min(first, std::size_t{22}));
    blit(surface, menu_sprites, 99, (mode_x_column + 1) * 4, thumb_y);
}

void draw_message_panel(BattleSurface& surface,
                        const SpriteArchive& menu_sprites,
                        int left, int top, int columns, int rows = 1) {
    // FIG 384a, used by the compact resource/count information cards.
    auto sprite = [&](std::size_t frame, int x, int y) {
        blit(surface, menu_sprites, frame, x * 4, y);
    };
    sprite(80, left, top);
    auto x = left + 6;
    for (int column = 0; column < columns; ++column, x += 2) {
        sprite(81, x, top);
    }
    sprite(82, x, top);

    auto y = top + 8;
    for (int row = 0; row < rows; ++row, y += 16) {
        sprite(168, left, y);
        x = left + 6;
        for (int column = 0; column < columns; ++column, x += 2) {
            sprite(169, x, y);
        }
        sprite(170, x, y);
    }

    sprite(171, left, y);
    x = left + 6;
    for (int column = 0; column < columns; ++column, x += 2) {
        sprite(172, x, y);
    }
    sprite(173, x, y);
}

void draw_battle_notice(BattleSurface& surface,
                        const SpriteArchive& menu_sprites,
                        const LegacyFont& font, const LegacyFont& fallback,
                        std::span<const std::uint8_t> text,
                        std::optional<std::size_t> marker_frame) {
    // FIG 3e19 begins every selector failure with 3de8's 7x4 panel and text
    // origin. The shipped messages used by 184f/4043 are single-line Big5,
    // but preserve the renderer's byte-space and ## newline rules here.
    draw_selector_panel(surface, menu_sprites, 4, 112, 7, 4);
    auto column = 10;
    auto top = 125;
    const auto line_start = column;
    for (std::size_t offset = 0; offset < text.size();) {
        if (text[offset] == ' ') {
            ++column;
            ++offset;
            continue;
        }
        if (offset + 1U >= text.size()) break;
        if (text[offset] == '#' && text[offset + 1U] == '#') {
            column = line_start;
            top += 16;
            offset += 2U;
            continue;
        }
        draw_big5(surface, font, fallback, text.subspan(offset, 2U),
                  column * 4, top, 0x00);
        column += 4;
        offset += 2U;
    }
    if (marker_frame) {
        blit(surface, menu_sprites, *marker_frame, column * 4, top);
    }
}

void draw_fig_text(BattleSurface& surface, const LegacyFont& font,
                   const LegacyFont& fallback,
                   std::span<const std::uint8_t> text,
                   int start_column, int start_top,
                   std::uint8_t color = 0x00) {
    // Linear equivalent of 7284: ASCII space advances one Mode-X column,
    // ## restores the initial column and advances one 16-line text row.
    auto column = start_column;
    auto top = start_top;
    for (std::size_t offset = 0; offset < text.size();) {
        if (text[offset] == ' ') {
            ++column;
            ++offset;
            continue;
        }
        if (offset + 1U >= text.size()) break;
        if (text[offset] == '#' && text[offset + 1U] == '#') {
            column = start_column;
            top += 16;
            offset += 2U;
            continue;
        }
        draw_big5(surface, font, fallback, text.subspan(offset, 2U),
                  column * 4, top, color);
        column += 4;
        offset += 2U;
    }
}

void draw_menu_number(BattleSurface& surface,
                      const SpriteArchive& menu_sprites,
                      std::uint16_t value, int mode_x_column, int top,
                      std::size_t digit_base = 111U) {
    std::array<std::uint8_t, 5> digits{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<std::uint8_t>(value % 10U);
        value = static_cast<std::uint16_t>(value / 10U);
    } while (value != 0 && count < digits.size());
    while (count != 0) {
        --count;
        blit(surface, menu_sprites, digit_base + digits[count],
             mode_x_column * 4, top);
        mode_x_column += 2;
    }
}

void draw_menu_number_right(BattleSurface& surface,
                            const SpriteArchive& menu_sprites,
                            std::uint16_t value, int mode_x_column, int top,
                            std::size_t digit_base = 111U) {
    // FIG 3d6c treats 4325 as the one-digit origin and moves it two Mode-X
    // columns left for every additional decimal digit before entering 3c77.
    // This is used by the two level-up columns; the victory totals use 3c77
    // directly and therefore remain left-aligned.
    auto remaining = value;
    while (remaining >= 10U) {
        mode_x_column -= 2;
        remaining = static_cast<std::uint16_t>(remaining / 10U);
    }
    draw_menu_number(surface, menu_sprites, value, mode_x_column, top,
                     digit_base);
}

void draw_fighter_pose(BattleSurface& surface, const SpriteArchive& fighters,
                       const BattlePartyMember& member, std::size_t pose,
                       std::optional<int> mode_x_anchor = std::nullopt);

BattleSurface compose_command_frame(
    const BattleSurface& scene, const BattleSession& session,
    const BattleCommandMenu& menu, const BattleEncounter& encounter,
    const BattleAbilityDatabase& abilities, const LegacyFont& font,
    const LegacyFont& fallback, const SpriteArchive& menu_sprites,
    const SpriteArchive& fighters, const ScriptArchive& items,
    const std::filesystem::path& game_root,
    std::optional<std::size_t> notice_text_bytes = std::nullopt,
    std::optional<std::size_t> notice_marker = std::size_t{149}) {
    auto result = scene;
    const auto active_page = menu.page();
    const auto selecting_target =
        active_page == BattleCommandMenuPage::monster_target ||
        active_page == BattleCommandMenuPage::party_target;
    const auto page = selecting_target ? menu.target_return_page() : active_page;
    const auto action_actor = page == BattleCommandMenuPage::summon_replace &&
                                      menu.actor() < session.party_count()
                                  ? std::optional<std::size_t>{menu.actor()}
                                  : std::nullopt;
    draw_fig_party_cards(
        result, menu_sprites,
        std::span<const BattlePartyMember>(session.party()).first(
            session.party_count()),
        action_actor);
    const auto cursor = selecting_target ? menu.target_return_cursor() : menu.cursor();
    const auto entries = selecting_target ? menu.target_return_entries()
                                          : menu.entries();
    const auto target_entries = menu.entries();
    const auto target_cursor = menu.cursor();
    constexpr std::uint8_t enabled_text_color = 0x00;
    constexpr std::uint8_t disabled_text_color = 0x6b;

    const auto draw_target_overlay = [&] {
        if (active_page == BattleCommandMenuPage::monster_target) {
            // FIG 178c/2d75 draws this list over the still-visible command,
            // ability or item page. A one-monster encounter bypasses it.
            const auto rows = static_cast<int>(target_entries.size());
            draw_selector_panel(result, menu_sprites, 18, 70, 4,
                                std::max(1, rows));
            for (std::size_t row = 0; row < target_entries.size(); ++row) {
                const auto target =
                    static_cast<std::size_t>(target_entries[row].value);
                if (target >= encounter.definition_slots.size()) continue;
                const auto definition = encounter.monster_definition_ids[
                    encounter.definition_slots[target]];
                if (definition >= BattleAbilityDatabase::item_name_count) continue;
                draw_big5(result, font, fallback, abilities.item_name(definition),
                          25 * 4, 83 + static_cast<int>(row) * 16,
                          enabled_text_color);
            }
            // FIG 178c uses the same text-then-frame-1 order as 41a1/1ac2.
            // The bar's bottom two scanlines intentionally cover the next
            // monster name when the first row is selected.
            if (!target_entries.empty()) {
                blit(result, menu_sprites, 1, 21 * 4,
                     79 + static_cast<int>(target_cursor) * 16);
            }
        } else if (active_page == BattleCommandMenuPage::party_target &&
                   !target_entries.empty()) {
            // FIG 19a8 overlays MENU 143 on the selected bottom fighter card.
            const auto target = target_entries[target_cursor].value;
            blit(result, menu_sprites, 143,
                 (static_cast<int>(target) * 18 + 4) * 4, 180);
        }
    };
    const auto finish_frame = [&]() -> BattleSurface {
        if (menu.notice() != BattleCommandNotice::none) {
            auto text = abilities.notice_text(menu.notice());
            if (notice_text_bytes) {
                text = text.first(std::min(*notice_text_bytes, text.size()));
            }
            draw_battle_notice(result, menu_sprites, font, fallback,
                               text, notice_marker);
        }
        return result;
    };

    // FIG 176f uses MENU frame 178 as the current-actor marker.
    if (page != BattleCommandMenuPage::summon_replace &&
        menu.actor() < session.party_count()) {
        blit(result, menu_sprites, 178,
             (static_cast<int>(menu.actor()) * 18 + 13) * 4, 137);
    }

    if (page == BattleCommandMenuPage::commands) {
        draw_main_command_panel(result, menu_sprites, cursor);
        draw_target_overlay();
        return finish_frame();
    }

    if (page == BattleCommandMenuPage::attack_modes) {
        draw_main_command_panel(result, menu_sprites, 2,
                                selecting_target ? 1 : 2);
        draw_attack_mode_panel(result, menu_sprites, cursor,
                               selecting_target ? 2 : 3);
        draw_target_overlay();
        return finish_frame();
    }

    if (page == BattleCommandMenuPage::tactics) {
        draw_main_command_panel(result, menu_sprites, 3, 2);
        draw_tactics_panel(result, menu_sprites, cursor, entries.size() == 3);
        draw_target_overlay();
        return finish_frame();
    }

    if (page == BattleCommandMenuPage::summon_replace) {
        // Original 111b3/5d24 asks this during resolution; the portable
        // command collector records the deterministic slot choice up front so
        // BattleSession remains input-free. Geometry and two top-card arrows
        // are still the exact FIG composition.
        draw_message_panel(result, menu_sprites, 0x1a, 0x4b, 0x0a);
        draw_fig_text(result, font, fallback,
                      abilities.summon_replacement_text(), 0x1c, 0x54);
        blit(result, menu_sprites, 143,
             static_cast<int>(cursor) * 0x14 * 4, 10);
        // 111b3 is entered by 1138 after the acting fighter's base pose has
        // replaced the command portrait.  Although the portable collector
        // asks for the slot before resolution, preserve that observable page.
        if (menu.actor() < session.party_count()) {
            draw_fighter_pose(result, fighters, session.party()[menu.actor()], 0);
        }
        return finish_frame();
    }

    // Inventory and ability selectors are handled below; both are entered
    // from an already-visible main panel in the original FIG page.
    if (page == BattleCommandMenuPage::abilities) {
        draw_main_command_panel(result, menu_sprites, 0, 2);
    } else if (page == BattleCommandMenuPage::items) {
        draw_main_command_panel(result, menu_sprites, 1, 2);
    }

    if (page == BattleCommandMenuPage::abilities) {
        // FIG 41a1: selected-ability card at byte column 10/y18, followed by
        // an eight-row sliding list at byte column 32/y13.
        const auto first = std::min<std::size_t>(
            cursor < 8U ? 0U : cursor - 7U,
            entries.size() > 8U ? entries.size() - 8U : 0U);
        draw_selector_panel(result, menu_sprites, 24, 0, 5, 8);
        draw_selector_scrollbar(result, menu_sprites, first);
        blit(result, menu_sprites, 0, 10 * 4, 18);
        const auto selected_id = static_cast<std::size_t>(
            entries.empty() ? 0U : entries[cursor].value);
        if (selected_id < abilities.abilities().size()) {
            const auto& selected_ability = abilities.ability(selected_id);
            const auto resource_class = static_cast<std::uint8_t>(
                (selected_ability.target_flags >> 8U) & 0x0fU);
            if (resource_class != 0) {
                std::pair<std::size_t, std::size_t> icon_frames;
                if (resource_class == 4) icon_frames = {59, 60};
                else if (resource_class == 2 || resource_class == 3) {
                    icon_frames = {45, 46};
                } else if (resource_class == 1) {
                    icon_frames = {20, 74};
                } else {
                    icon_frames = {61, 62};
                }
                blit(result, menu_sprites, icon_frames.first, 14 * 4, 27);
                blit(result, menu_sprites, icon_frames.second, 18 * 4, 27);
            }
            draw_message_panel(result, menu_sprites, 8, 50, 4);
            if (const auto resource_label =
                    abilities.ability_resource_label(resource_class);
                !resource_label.empty()) {
                draw_big5(result, font, fallback,
                          resource_label,
                          10 * 4, 59, enabled_text_color);
                if (resource_class != 5) {
                    const auto& actor = session.party()[menu.actor()];
                    const auto value = resource_class == 2 || resource_class == 3
                                           ? actor.secondary_points
                                           : actor.ability_points;
                    draw_menu_number(result, menu_sprites, value, 19, 62);
                }
            }
        }
        const auto selected_row = cursor - first;
        for (std::size_t visible = 0; visible < 8 && first + visible < entries.size();
             ++visible) {
            const auto index = first + visible;
            const auto id = static_cast<std::size_t>(entries[index].value);
            if (id >= abilities.abilities().size()) continue;
            const auto& ability = abilities.ability(id);
            draw_big5(result, font, fallback, ability.name_big5, 32 * 4,
                      13 + static_cast<int>(visible) * 16,
                      (ability.target_flags & 0x4000U) == 0
                          ? enabled_text_color
                          : disabled_text_color);
            const auto resource_class = static_cast<std::uint8_t>(
                (ability.target_flags >> 8U) & 0x0fU);
            if (resource_class == 5) {
                // FIG 41a1 draws one Big5 element glyph for every set bit in
                // cost, in 10/8/4/2/1 order, using palette index one.
                static constexpr std::array<std::array<std::uint8_t, 2>, 5>
                    element_glyphs = {{
                        {{0xaa, 0xf7}}, {{0xa4, 0xec}}, {{0xa4, 0xf4}},
                        {{0xa4, 0xf5}}, {{0xa4, 0x67}},
                    }};
                static constexpr std::array<std::uint8_t, 5> masks = {
                    0x10, 0x08, 0x04, 0x02, 0x01,
                };
                auto column = 54;
                for (std::size_t glyph = 0; glyph < masks.size(); ++glyph) {
                    if ((static_cast<std::uint8_t>(ability.cost) & masks[glyph]) == 0) {
                        continue;
                    }
                    draw_big5(result, font, fallback, element_glyphs[glyph],
                              column * 4,
                              13 + static_cast<int>(visible) * 16, 0x01);
                    column += 4;
                }
            } else if (resource_class != 0) {
                // Six name glyphs advance x=32 to x=56; FIG subtracts two
                // Mode-X columns and draws MENU 111..120 three scanlines down.
                draw_menu_number(result, menu_sprites, ability.cost, 54,
                                 16 + static_cast<int>(visible) * 16);
            }
        }
        // FIG 41a1 draws all visible names/costs first, then overlays the
        // 168x22 MENU frame-1 selection bar.  This matters at the boundary
        // with the following row: the lower two bar scanlines cover two black
        // pixels in record zero's first glyph.  Reversing these calls leaves a
        // two-pixel artifact that is absent in the original VGA page.
        blit(result, menu_sprites, 1, 30 * 4,
             9 + static_cast<int>(selected_row) * 16);
        draw_target_overlay();
        return finish_frame();
    }

    if (page == BattleCommandMenuPage::items) {
        // FIG 1ac2 uses the same eight-row selector and MENU frame 1.  Its
        // selected-item information card is frame 0 at byte column 5/y116.
        const auto first = std::min<std::size_t>(
            cursor < 8U ? 0U : cursor - 7U,
            entries.size() > 8U ? entries.size() - 8U : 0U);
        draw_selector_panel(result, menu_sprites, 24, 0, 5, 8);
        draw_selector_scrollbar(result, menu_sprites, first);
        blit(result, menu_sprites, 0, 5 * 4, 116);
        if (!entries.empty()) {
            const auto selected_slot = entries[cursor].value;
            const auto selected_id = session.inventory()[selected_slot];
            const auto record_index = static_cast<std::size_t>(selected_id) + 2U;
            if (record_index < items.entry_count()) {
                const auto record = items.entry(record_index);
                const auto preview_sprite = span_word(record, 2);
                const auto preview_x = static_cast<std::int16_t>(
                    span_word(record, 0x1d));
                const auto preview_y = static_cast<std::int16_t>(
                    span_word(record, 0x1f));
                draw_message_panel(result, menu_sprites, 3, 3, 4, 6);
                const auto preview = load_sprites(
                    monster_path(game_root, preview_sprite));
                blit(result, preview, 0,
                     (5 + static_cast<int>(preview_x)) * 4,
                     11 + static_cast<int>(preview_y));
                const auto category = static_cast<std::size_t>(span_word(record, 0));
                if (selected_id != 0 &&
                    category < BattleAbilityDatabase::item_category_count) {
                    draw_big5(result, font, fallback,
                              abilities.item_category_label(category),
                              9 * 4, 125, enabled_text_color);
                }
            }
        }
        const auto selected_row = cursor - first;
        for (std::size_t visible = 0; visible < 8 && first + visible < entries.size();
             ++visible) {
            const auto index = first + visible;
            const auto id = session.inventory()[entries[index].value];
            if (id >= BattleAbilityDatabase::item_name_count) continue;
            draw_big5(result, font, fallback, abilities.item_name(id), 32 * 4,
                      13 + static_cast<int>(visible) * 16,
                      menu.item_reserved(entries[index].value)
                          ? disabled_text_color
                          : enabled_text_color);
            std::optional<std::uint16_t> displayed_cost;
            const auto record_index = static_cast<std::size_t>(id) + 2U;
            if (record_index < items.entry_count()) {
                const auto record = items.entry(record_index);
                if (id >= 0x13aU) {
                    displayed_cost = static_cast<std::uint16_t>(
                        MonsterDefinition::parse(id, record).level * 2U);
                } else if (id >= 0x44U && id <= 0x48U) {
                    displayed_cost = session.special_item_counts()[id - 0x44U];
                } else if (record.size() >= 9) {
                    const auto definition = BattleItemDefinition::parse(id, record);
                    if (definition.type == 0x10 && id >= 0x8cU) {
                        const auto ability_id = static_cast<std::size_t>(id - 0x8cU);
                        if (ability_id < abilities.abilities().size()) {
                            displayed_cost = abilities.ability(ability_id).cost;
                        }
                    }
                }
            }
            if (displayed_cost) {
                draw_menu_number(result, menu_sprites, *displayed_cost, 56,
                                 16 + static_cast<int>(visible) * 16);
            }
        }
        // FIG 1ac2 shares 41a1's ordering: item names/counts precede the
        // frame-1 selector overlay.  Drawing the bar first lets record zero's
        // first glyph punch two black pixels through its lower edge.
        blit(result, menu_sprites, 1, 30 * 4,
             9 + static_cast<int>(selected_row) * 16);
    }
    draw_target_overlay();
    return finish_frame();
}

std::filesystem::path monster_path(const std::filesystem::path& root,
                                   std::uint16_t sprite_number) {
    std::ostringstream filename;
    filename << "CD" << std::setw(3) << std::setfill('0') << sprite_number << ".RSK";
    return root / (sprite_number < 300 ? "CD" : "AD") / filename.str();
}

void draw_enemies(BattleSurface& surface, const BattleEncounter& encounter,
                  const ScriptArchive& items,
                  const std::filesystem::path& game_root,
                  const SpriteArchive& menu_sprites,
                  std::span<const MonsterBattleState> states = {},
                  std::optional<std::size_t> reacting_monster = std::nullopt,
                  std::uint16_t encounter_directory_offset = 0xffffU) {
    std::map<std::uint16_t, SpriteArchive> archives;
    for (std::size_t index = 0; index < encounter.definition_slots.size(); ++index) {
        if (!states.empty() && index < states.size() &&
            states[index].hit_points == 0) {
            continue;
        }
        const auto definition =
            encounter.monster_definition_ids[encounter.definition_slots[index]];
        // FIG.EXE's FUN_1cfc indexes ITEM.EXE as definition*2+4, hence the two
        // metadata directory words before logical item zero.
        const auto record_index = static_cast<std::size_t>(definition) + 2;
        if (record_index >= items.entry_count()) {
            throw std::runtime_error("ORC.EXE references a missing ITEM.EXE definition");
        }
        const auto monster = MonsterDefinition::parse(definition, items.entry(record_index));
        // FIG 32aa preloads AD/CD523 for the directory-42 story boss; 2e30
        // substitutes that full-screen archive while the target's one-shot
        // +3114 hit-reaction flag is set, instead of CD522 frame one.
        const auto sprite_number = reacting_monster == index &&
                                           encounter_directory_offset == 0x42
                                       ? std::uint16_t{523}
                                       : monster.sprite_number;
        auto found = archives.find(sprite_number);
        if (found == archives.end()) {
            found = archives.emplace(
                sprite_number,
                load_sprites(monster_path(game_root, sprite_number))).first;
        }
        // FIG keeps horizontal coordinates in Mode-X byte columns. The modern
        // surface is linear, so convert the formation value to physical pixels.
        const auto left = static_cast<int>(encounter.horizontal_positions[index]) * 4;
        const auto frame = reacting_monster == index &&
                                   encounter_directory_offset != 0x42 &&
                                   found->second.sprites().size() > 1
                               ? 1U
                               : 0U;
        // 2deb sets CS:76f2=EF before every 798b monster draw. EF pixels are
        // not opaque shadow paint: they darken the BA page through table 1.
        composite_legacy_masked_sprite(
            surface.pixels, 320, 200, surface.palette,
            found->second, frame, left,
            static_cast<int>(monster.vertical_position), 0xef);

        if (states.empty() || index >= states.size()) continue;
        auto active_ordinal = std::size_t{};
        const auto has_status = std::any_of(
            states[index].status_turns.begin(), states[index].status_turns.end(),
            [](std::uint16_t turns) { return turns != 0; });
        if (has_status) {
            const auto placement = fig_monster_status_icon_placement(
                static_cast<int>(encounter.horizontal_positions[index]), 0);
            // 2eb3 draws MENU B1 once before the first icon. Later icon calls
            // see the prior frame id (>0ah) and only extend the vertical stack.
            blit(surface, menu_sprites, 0xb1,
                 placement.backing_left, placement.backing_top);
        }
        for (std::size_t slot = 0; slot < states[index].status_turns.size();
             ++slot) {
            if (states[index].status_turns[slot] == 0) continue;
            const auto placement = fig_monster_status_icon_placement(
                static_cast<int>(encounter.horizontal_positions[index]),
                active_ordinal++);
            const auto status_frame = fig_monster_status_icon_frame(slot);
            if (status_frame < menu_sprites.sprites().size()) {
                blit(surface, menu_sprites, status_frame,
                     placement.icon_left, placement.icon_top);
            }
        }
    }
}

void draw_battle_media(BattleSurface& surface,
                       const SpriteArchive& menu_sprites,
                       const std::array<bool, 3>& media) {
    for (std::size_t medium = 0; medium < media.size(); ++medium) {
        if (!media[medium]) continue;
        const auto frame = 174U + medium;  // MENU AEh/AFh/B0h
        if (frame >= menu_sprites.sprites().size()) continue;
        const auto placement = fig_medium_placement(medium);
        blit(surface, menu_sprites, frame, placement.left, placement.top);
    }
}

bool present_story_battle_setup(GameContext& context,
                                const BattleSurface& background,
                                std::uint16_t encounter_directory_offset) {
    if (encounter_directory_offset == 0x42) {
        // FIG 32aa..32c9 decodes AD/CD523 into its dedicated +4000 segment
        // before the first command round.  It is not displayed yet: 2e30
        // substitutes it only while the story boss hit-reaction flag is one.
        const auto action_archive = load_sprites(
            monster_path(context.game_root, 523));
        if (action_archive.sprites().empty()) {
            throw std::runtime_error(
                "FIG directory-42 story action archive is empty");
        }
        return true;
    }
    if (encounter_directory_offset != 0x30) return true;
    struct Step {
        std::uint16_t sprite;
        std::size_t frame;
        int mode_x_column;
        int top;
    };
    // FIG 32d4..3385: CD348, the four CD521 frames moving two byte
    // columns left each time, then CD352. 3386 presents each composition for
    // exactly three FIG INT-08h timer ticks and restores the decoded background.
    static constexpr std::array<Step, 6> steps = {{
        {348, 0, 30, 21},
        {521, 0, 28, 21},
        {521, 1, 26, 21},
        {521, 2, 24, 21},
        {521, 3, 22, 21},
        {352, 0, 20, 23},
    }};
    for (const auto& step : steps) {
        auto frame = background;
        const auto archive = load_sprites(
            monster_path(context.game_root, step.sprite));
        if (step.frame >= archive.sprites().size()) {
            throw std::runtime_error("FIG story setup references a missing CD frame");
        }
        blit(frame, archive, step.frame, step.mode_x_column * 4, step.top);
        context.platform.present({
            320, 200, frame.pixels,
            std::span<const std::uint8_t, 768>(frame.palette),
        });
        if (!delay_for_or_frontend_quit(
                context.platform, fig_timer_ticks(3))) {
            return false;
        }
    }
    return true;
}

void draw_fighter_pose(BattleSurface& surface, const SpriteArchive& fighters,
                       const BattlePartyMember& member, std::size_t pose,
                       std::optional<int> mode_x_anchor) {
    auto placement = fig_fighter_placement(
        member.identity, member.party_index, pose, fighters.sprites().size());
    if (placement.frame >= fighters.sprites().size()) return;
    if (mode_x_anchor) {
        const auto normal_anchor =
            12 + static_cast<int>(member.party_index) * 18;
        placement.left += (*mode_x_anchor - normal_anchor) * 4;
    }
    blit(surface, fighters, placement.frame, placement.left, placement.top);
}

std::filesystem::path numbered_action_path(const std::filesystem::path& root,
                                            std::uint16_t resource) {
    std::ostringstream filename;
    filename << "SP" << std::setw(3) << std::setfill('0') << resource << ".RSK";
    return root / (resource < 300 ? "SP" : "ST") / filename.str();
}

std::filesystem::path weapon_path(const std::filesystem::path& root,
                                  std::uint16_t item) {
    std::ostringstream filename;
    filename << "SW" << std::setw(3) << std::setfill('0') << item << ".RSK";
    return root / "SW" / filename.str();
}

std::filesystem::path effect_voice_path(const std::filesystem::path& root,
                                        std::uint16_t resource) {
    std::ostringstream filename;
    filename << "SP" << std::setw(3) << std::setfill('0') << resource << ".VOC";
    return root / "VC" / filename.str();
}

std::filesystem::path non_effect_voice_path(const std::filesystem::path& root,
                                            const FigVoiceCue& cue) {
    if (cue.file == FigVoiceFile::k1) return root / "VC" / "K1.VOC";
    if (cue.file == FigVoiceFile::sv3) return root / "VC" / "SV3.VOC";
    return effect_voice_path(root, cue.sp_number);
}

void play_voice_cue(GameContext& context, const FigVoiceCue& cue) {
    if (context.shared_state.u8(0x3f5) != 0U) return;
    const auto path = non_effect_voice_path(context.game_root, cue);
    if (std::filesystem::exists(path)) {
        context.platform.play_voice(read_file(path));
    }
}

void play_battle_voice(GameContext& context,
                       const std::filesystem::path& path) {
    if (context.shared_state.u8(0x3f5) == 0U &&
        std::filesystem::exists(path)) {
        context.platform.play_voice(read_file(path));
    }
}

void play_battle_music(GameContext& context,
                       const std::filesystem::path& path, bool loop) {
    if (context.shared_state.u8(0x3f4) == 0U) {
        context.platform.play_music(read_file(path), loop);
    }
}

struct BattleVisualState {
    std::array<BattlePartyMember, 4> party{};
    std::size_t party_count{};
    std::vector<MonsterBattleState> monsters;
    std::array<bool, 3> media{};
    std::array<bool, 2> summoned_ally_present{};
    std::array<std::array<std::uint8_t, 12>, 2> summoned_ally_names{};
};

BattleVisualState capture_visual_state(
    const BattleSession& session, const BattleAbilityDatabase& abilities) {
    BattleVisualState result;
    result.party_count = session.party_count();
    std::copy_n(session.party().begin(), result.party_count, result.party.begin());
    result.monsters.assign(session.monsters().begin(), session.monsters().end());
    result.media = session.battle_media();
    for (const auto& ally : session.summoned_allies()) {
        if (ally.summon_slot >= result.summoned_ally_present.size() ||
            ally.item_id >= BattleAbilityDatabase::item_name_count) {
            continue;
        }
        result.summoned_ally_present[ally.summon_slot] = true;
        result.summoned_ally_names[ally.summon_slot] =
            abilities.item_name(ally.item_id);
    }
    return result;
}

void apply_visual_event(BattleVisualState& state,
                        const BattleSessionEvent& event,
                        const BattleAbilityDatabase& abilities) {
    if (event.kind == BattleEventKind::ally_summoned) {
        if (event.target < state.summoned_ally_present.size() &&
            event.ability_id < BattleAbilityDatabase::item_name_count) {
            state.summoned_ally_present[event.target] = true;
            state.summoned_ally_names[event.target] =
                abilities.item_name(event.ability_id);
        }
        return;
    }
    if (event.kind == BattleEventKind::ally_fled) {
        if (event.source == 0) {
            state.summoned_ally_present[0] =
                state.summoned_ally_present[1];
            state.summoned_ally_names[0] = state.summoned_ally_names[1];
            state.summoned_ally_present[1] = false;
            state.summoned_ally_names[1] = {};
        } else if (event.source < state.summoned_ally_present.size()) {
            state.summoned_ally_present[event.source] = false;
            state.summoned_ally_names[event.source] = {};
        }
        return;
    }
    if (event.kind == BattleEventKind::medium_summoned ||
        event.kind == BattleEventKind::medium_dismissed) {
        if (event.target < state.media.size()) {
            state.media[event.target] =
                event.kind == BattleEventKind::medium_summoned;
        }
        return;
    }
    if (event.kind == BattleEventKind::monster_fled) {
        if (event.source < state.monsters.size()) {
            state.monsters[event.source].hit_points = 0;
        }
        return;
    }
    if (event.kind == BattleEventKind::monster_captured) {
        if (event.target < state.monsters.size()) {
            state.monsters[event.target].hit_points = 0;
        }
        return;
    }
    if (event.target_is_monster) {
        if (event.target >= state.monsters.size()) return;
        auto& target = state.monsters[event.target];
        for (std::size_t slot = 0; slot < target.status_turns.size(); ++slot) {
            if ((event.expired_monster_status_mask & (1U << slot)) != 0) {
                target.status_turns[slot] = 0;
            }
        }
        if (event.status_duration != 0) {
            std::optional<std::size_t> status_slot;
            switch (event.effect_code) {
            case 0x5e: status_slot = 0; break;
            case 0x5f: status_slot = 1; break;
            case 0x60: status_slot = 2; break;
            case 0x64: status_slot = 3; break;
            case 0x65: status_slot = 4; break;
            default: break;
            }
            if (status_slot) {
                target.status_turns[*status_slot] = event.status_duration;
            }
        }
        target.hit_points = static_cast<std::uint16_t>(
            event.damage >= target.hit_points ? 0 : target.hit_points - event.damage);
        target.hit_points = static_cast<std::uint16_t>(std::min<unsigned>(
            target.maximum_hit_points,
            static_cast<unsigned>(target.hit_points) + event.healing));
        if (event.defeated) target.hit_points = 0;
        return;
    }
    if (event.target >= state.party_count) return;
    auto& target = state.party[event.target];
    if (event.resulting_player_support_state) {
        // 58a9 redraws the target card only after the support handler has
        // committed every field. Assign the resolved snapshot atomically;
        // applying `healing` as a second delta would double the HP change.
        target.apply_support_target(*event.resulting_player_support_state);
        return;
    }
    if (event.kind == BattleEventKind::status_expired) {
        if ((event.expired_player_buff_mask & 0x01U) != 0) {
            target.speed = target.base_speed;
        }
        if ((event.expired_player_buff_mask & 0x02U) != 0) {
            target.physical_defense = target.base_physical_defense;
        }
        if ((event.expired_player_buff_mask & 0x04U) != 0) {
            target.physical_attack = target.base_physical_attack;
        }
        if ((event.expired_player_buff_mask & 0x08U) != 0) {
            target.evasion = target.base_evasion;
        }
        static constexpr std::array<std::uint16_t, 4> recovered_bits = {
            0x0020, 0x0002, 0x0004, 0x0080,
        };
        for (std::size_t slot = 0; slot < recovered_bits.size(); ++slot) {
            if ((event.recovered_player_status_mask & (1U << slot)) != 0) {
                target.status_bits = static_cast<std::uint16_t>(
                    target.status_bits & ~recovered_bits[slot]);
            }
        }
    }
    if (event.status_duration != 0) {
        if (const auto bit = fig_player_status_bit(event.effect_code)) {
            target.status_bits = static_cast<std::uint16_t>(
                target.status_bits | *bit);
        }
    }
    target.hit_points = static_cast<std::uint16_t>(
        event.damage >= target.hit_points ? 0 : target.hit_points - event.damage);
    target.hit_points = static_cast<std::uint16_t>(std::min<unsigned>(
        target.maximum_hit_points,
        static_cast<unsigned>(target.hit_points) + event.healing));
    if (event.defeated) {
        target.hit_points = 0;
        target.status_bits = static_cast<std::uint16_t>(target.status_bits | 0x2000U);
    }
}

void draw_summoned_ally_name_cards(
    BattleSurface& surface, const SpriteArchive& menu_sprites,
    const LegacyFont& font, const LegacyFont& fallback,
    const BattleVisualState& visual) {
    for (std::size_t slot = 0;
         slot < visual.summoned_ally_present.size(); ++slot) {
        if (!visual.summoned_ally_present[slot]) continue;
        // 2f1f/2f4c: four-column, one-row cards at x=0/14h,y=0;
        // six ITEM-name glyphs begin two Mode-X columns in and nine lines down.
        const auto placement = fig_summoned_name_card_placement(slot);
        draw_message_panel(surface, menu_sprites,
                           placement.mode_x_column, placement.top,
                           placement.columns, placement.rows);
        draw_big5(surface, font, fallback,
                  visual.summoned_ally_names[slot],
                  placement.text_left, placement.text_top, 0x00);
    }
}

std::pair<int, int> monster_visual_center(
    const BattleEncounter& encounter, const ScriptArchive& items,
    const std::filesystem::path& game_root, std::size_t index) {
    if (index >= encounter.definition_slots.size()) return {160, 80};
    const auto definition =
        encounter.monster_definition_ids[encounter.definition_slots[index]];
    const auto record_index = static_cast<std::size_t>(definition) + 2U;
    if (record_index >= items.entry_count()) return {160, 80};
    const auto monster = MonsterDefinition::parse(definition, items.entry(record_index));
    const auto archive = load_sprites(monster_path(game_root, monster.sprite_number));
    const auto& sprite = archive.sprites().front();
    return {
        static_cast<int>(encounter.horizontal_positions[index]) * 4 +
            static_cast<int>(sprite.width) / 2,
        static_cast<int>(monster.vertical_position) +
            static_cast<int>(sprite.height) / 2,
    };
}

std::pair<int, int> event_target_center(
    const BattleEncounter& encounter, const ScriptArchive& items,
    const std::filesystem::path& game_root, const BattleVisualState& state,
    const BattleSessionEvent& event, const SpriteArchive& fighters) {
    if (event.target_is_monster) {
        return monster_visual_center(encounter, items, game_root, event.target);
    }
    if (event.target < state.party_count) {
        const auto& member = state.party[event.target];
        const auto placement = fig_fighter_placement(
            member.identity, member.party_index, 0, fighters.sprites().size());
        if (placement.frame < fighters.sprites().size()) {
            const auto& sprite = fighters.sprites()[placement.frame];
            return {placement.left + static_cast<int>(sprite.width) / 2,
                    placement.top + static_cast<int>(sprite.height) / 2};
        }
    }
    return {160, 150};
}

bool player_actor_event(BattleEventKind kind) {
    switch (kind) {
    case BattleEventKind::player_attack:
    case BattleEventKind::player_ability:
    case BattleEventKind::monster_captured:
    case BattleEventKind::capture_failed:
    case BattleEventKind::player_escaped:
    case BattleEventKind::escape_failed:
    case BattleEventKind::ally_summoned:
        return true;
    default:
        return false;
    }
}

void draw_weapon_overlay(BattleSurface& surface,
                         const FigWeaponAnimation& animation,
                         const std::filesystem::path& game_root,
                         int target_x, int target_y) {
    const auto path = weapon_path(game_root, animation.item_id);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(
            "FIG weapon animation resource is missing: " + path.string());
    }
    const auto archive = load_sprites(path);
    if (archive.sprites().empty()) {
        throw std::runtime_error("FIG weapon animation archive is empty");
    }
    const auto placement = fig_weapon_placement(target_x, target_y);
    if (animation.mirrored) {
        blit_mirrored(surface, archive, 0, placement.left, placement.top);
    } else {
        blit(surface, archive, 0, placement.left, placement.top);
    }
}

std::optional<std::uint16_t> event_effect_code(
    const BattleSessionEvent& event,
    const BattleAbilityDatabase& abilities) noexcept {
    switch (event.kind) {
    case BattleEventKind::player_ability:
    case BattleEventKind::monster_ability:
    case BattleEventKind::ally_ability:
        // These event producers always preserve the exact selector, including
        // selector zero. Physical events also use zero as their default and
        // must not accidentally inherit ability record zero / SP001.
        return event.effect_code;
    case BattleEventKind::monster_heal:
        if (event.ability_id < abilities.abilities().size()) {
            return abilities.ability(event.ability_id).effect_code;
        }
        return std::nullopt;
    case BattleEventKind::player_escaped:
    case BattleEventKind::escape_failed:
        // Normal command escape has no dispatcher selector. Learned ability
        // 7 and ITEM 237 both preserve the exact selector explicitly.
        return event.ability_id != 0 && event.effect_code != 0
                   ? std::optional<std::uint16_t>{event.effect_code}
                   : std::nullopt;
    default:
        return std::nullopt;
    }
}

struct LoadedBattleEffectLayer {
    const SpriteArchive* archive{};
    std::size_t frame{};
    std::optional<FigEffectLayer> exact;
};

struct LoadedBattleEffectStep {
    std::vector<LoadedBattleEffectLayer> layers;
};

std::vector<LoadedBattleEffectStep> load_event_effect_frames(
    const BattleSessionEvent& event, const BattleAbilityDatabase& abilities,
    const std::filesystem::path& game_root,
    std::map<std::uint16_t, SpriteArchive>& cache) {
    const auto effect = event_effect_code(event, abilities);
    if (!effect) return {};
    const auto resources = fig_effect_resource_sequence(*effect);
    const auto exact_timeline = fig_effect_timeline(*effect);
    if (!resources.empty() && exact_timeline.empty()) {
        throw std::runtime_error(
            "FIG archive-backed effect has no exact presentation timeline");
    }
    std::vector<LoadedBattleEffectStep> frames;
    for (const auto& step : exact_timeline) {
        LoadedBattleEffectStep loaded;
        for (const auto& layer : step.layers) {
            auto found = cache.find(layer.resource);
            if (found == cache.end()) {
                const auto path = numbered_action_path(game_root, layer.resource);
                if (!std::filesystem::exists(path)) {
                    throw std::runtime_error(
                        "FIG exact effect timeline resource is missing: " +
                        path.string());
                }
                found = cache.emplace(layer.resource, load_sprites(path)).first;
            }
            if (layer.sprite_frame >= found->second.sprites().size()) {
                throw std::runtime_error(
                    "FIG exact effect timeline frame is out of range");
            }
            loaded.layers.push_back({&found->second, layer.sprite_frame, layer});
        }
        if (!loaded.layers.empty()) frames.push_back(std::move(loaded));
    }
    return frames;
}

BattleSurface compose_event_frame(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& fighters, const SpriteArchive& menu_sprites,
    const LegacyFont& font, const LegacyFont& fallback,
    const BattleVisualState& visual,
    const BattleSessionEvent& event, std::optional<std::size_t> fighter_pose,
    std::span<const LoadedBattleEffectLayer> effect_layers,
    std::optional<FigNumberPlacement> result_number,
    std::uint16_t encounter_directory_offset,
    std::optional<FigWeaponAnimation> weapon_animation = std::nullopt,
    bool force_monster_reaction = false,
    bool include_party_cards = true) {
    auto frame = base_surface;
    const auto [target_x, target_y] = event_target_center(
        encounter, items, context.game_root, visual, event, fighters);
    std::optional<std::size_t> reacting_monster;
    if (event.target_is_monster) {
        // +3114 belongs to the monster being hit, never to the monster taking
        // its own turn. 2deb consumes it on exactly one recomposition: during
        // the physical weapon frame, or the first 144e number frame for magic
        // and the persistent slot-two status tick (including a rolled zero).
        const auto reaction_phase = fig_monster_reaction_phase(event);
        const auto first_number_frame =
            reaction_phase == FigMonsterReactionPhase::first_result_frame &&
            result_number && result_number->top == target_y - 5;
        if (force_monster_reaction || first_number_frame) {
            reacting_monster = event.target;
        }
    }
    draw_battle_media(frame, menu_sprites, visual.media);
    draw_summoned_ally_name_cards(
        frame, menu_sprites, font, fallback, visual);
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 visual.monsters,
                 reacting_monster, encounter_directory_offset);
    const auto action_actor = fighter_pose && player_actor_event(event.kind) &&
                                      event.source < visual.party_count
                                  ? std::optional<std::size_t>(event.source)
                                  : std::nullopt;
    const auto action_mode_x_anchor = action_actor
        ? std::optional<int>{
              event.action_anchor_is_target
                  ? (event.target_is_monster
                         ? target_x / 4 - 3
                         : 12 + static_cast<int>(event.target) * 18)
                  : 12 + static_cast<int>(event.source) * 18}
        : std::nullopt;
    if (weapon_animation) {
        // 152a/155c install the SW page before 137a redraws the action card.
        // Their overlap is observable with tall SW122: the first two weapon
        // rows inside y=150 are covered again by the party card.
        draw_weapon_overlay(frame, *weapon_animation, context.game_root,
                            target_x, target_y);
    }
    // 1048 enters the player-side selector table from the captured-ally
    // dispatcher without ever calling 2bb5.  Its effect, status/result and
    // retained-handler pages therefore keep the bare ally-action bottom
    // strip even when the packed ally index happens to equal a party index.
    const auto draw_party_cards =
        include_party_cards && !event.source_is_summoned_ally;
    if (draw_party_cards) {
        draw_fig_party_cards(
            frame, menu_sprites,
            std::span<const BattlePartyMember>(visual.party).first(
                visual.party_count),
            action_actor, action_mode_x_anchor);
    }
    if (draw_party_cards && fighter_pose && player_actor_event(event.kind) &&
        event.source < visual.party_count) {
        draw_fighter_pose(frame, fighters, visual.party[event.source],
                          *fighter_pose, action_mode_x_anchor);
    }
    const auto target_mode_x_anchor = !event.action_anchor_is_target
                                          ? 12 + static_cast<int>(event.source) * 18
                                          : event.target_is_monster
                                                ? target_x / 4 - 3
                                                : 12 + static_cast<int>(
                                                           event.target) * 18;
    for (const auto& layer : effect_layers) {
        if (layer.archive == nullptr || layer.archive->sprites().empty()) continue;
        const auto effect_frame = std::min(
            layer.frame, layer.archive->sprites().size() - 1U);
        const auto& sprite = layer.archive->sprites()[effect_frame];
        auto left = sprite.width >= 300
                        ? 0
                        : target_x - static_cast<int>(sprite.width) / 2;
        auto top = sprite.height >= 180
                       ? 0
                       : target_y - static_cast<int>(sprite.height) / 2;
        if (layer.exact) {
            const auto placement = resolve_fig_effect_placement(
                *layer.exact, target_mode_x_anchor, target_y);
            left = placement.left;
            top = placement.top;
        }
        blit(frame, *layer.archive, effect_frame, left, top);
    }
    const auto resistance_zero =
        event.block_reason == AbilityBlockReason::resistance &&
        !event.target_is_monster;
    const auto physical_zero =
        (event.kind == BattleEventKind::player_attack ||
         event.kind == BattleEventKind::ally_attack) &&
        event.damage == 0 && !event.evaded;
    const auto periodic_zero =
        event.kind == BattleEventKind::status_damage &&
        event.target_is_monster && event.damage == 0;
    if (result_number &&
        (event.damage != 0 || event.healing != 0 || resistance_zero ||
         physical_zero || periodic_zero)) {
        const auto value = event.damage != 0 ? event.damage : event.healing;
        draw_menu_number(frame, menu_sprites, value,
                         result_number->mode_x_column,
                         result_number->top,
                         event.damage != 0 || resistance_zero || physical_zero ||
                                 periodic_zero
                             ? 121U
                             : 111U);
    }
    return frame;
}

void present_battle_surface(GameContext& context,
                            const BattleSurface& frame) {
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_event_frame(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& fighters, const SpriteArchive& menu_sprites,
    const LegacyFont& font, const LegacyFont& fallback,
    const BattleVisualState& visual,
    const BattleSessionEvent& event, std::optional<std::size_t> fighter_pose,
    std::span<const LoadedBattleEffectLayer> effect_layers,
    std::optional<FigNumberPlacement> result_number,
    std::uint16_t encounter_directory_offset,
    std::optional<FigWeaponAnimation> weapon_animation = std::nullopt,
    bool force_monster_reaction = false,
    bool include_party_cards = true) {
    const auto frame = compose_event_frame(
        context, base_surface, encounter, items, fighters, menu_sprites,
        font, fallback, visual, event, fighter_pose, effect_layers,
        result_number, encounter_directory_offset, weapon_animation,
        force_monster_reaction, include_party_cards);
    present_battle_surface(context, frame);
}

bool present_fig_page_wipe(GameContext& context,
                           const BattleSurface& previous,
                           const BattleSurface& next,
                           std::chrono::milliseconds tick) {
    auto frame = previous;
    frame.palette = next.palette;
    auto first_line = 0;
    for (const auto line_end : fig_page_wipe_scanline_ends()) {
        const auto first = static_cast<std::size_t>(first_line) * 320U;
        const auto last = static_cast<std::size_t>(line_end) * 320U;
        std::copy(next.pixels.begin() + static_cast<std::ptrdiff_t>(first),
                  next.pixels.begin() + static_cast<std::ptrdiff_t>(last),
                  frame.pixels.begin() + static_cast<std::ptrdiff_t>(first));
        present_battle_surface(context, frame);
        if (!delay_for_or_frontend_quit(context.platform, tick)) return false;
        first_line = line_end;
    }
    return true;
}

void present_player_status_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleVisualState& visual,
    const BattleSessionEvent& event, std::span<const std::uint8_t> text,
    std::uint16_t encounter_directory_offset, int columns = 4,
    std::uint8_t color = 0x6b, int panel_left_offset = 6,
    int text_left_offset = 8, const SpriteArchive* fighters = nullptr,
    std::optional<std::size_t> fighter_pose = std::nullopt,
    const std::array<std::uint8_t, 768>* palette_override = nullptr,
    BattleSurface* captured_surface = nullptr) {
    if (event.target_is_monster || event.target >= visual.party_count || text.empty()) {
        return;
    }
    auto frame = base_surface;
    draw_battle_media(frame, menu_sprites, visual.media);
    draw_summoned_ally_name_cards(
        frame, menu_sprites, font, fallback, visual);
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 visual.monsters,
                 std::nullopt, encounter_directory_offset);
    const auto actor_column = static_cast<int>(event.target) * 18;
    const auto action_status_card =
        fighters != nullptr && fighter_pose && event.source < visual.party_count;
    if (!action_status_card) {
        draw_fig_party_cards(
            frame, menu_sprites,
            std::span<const BattlePartyMember>(visual.party).first(
                visual.party_count));
    }

    // FIG 57d6 -> 2338: after 2bb5 leaves x=party*18+8, the compact
    // four-column card starts two Mode-X columns to the left at y=120. The
    // four-glyph status label itself starts at x=party*18+8/y=129 in colour 6b.
    draw_message_panel(frame, menu_sprites,
                       actor_column + panel_left_offset, 120, columns);
    draw_big5(frame, font, fallback, text,
              (actor_column + text_left_offset) * 4, 129, color);
    if (action_status_card) {
        // 57d6's compact panel is already on the page when 2bb5 redraws the
        // acting card; the card's top two rows therefore cover the panel's
        // overlapping bottom edge.  Finally 137a restores pose4 over that
        // paid-resource card.
        draw_fig_party_card(frame, menu_sprites, visual.party[event.source], false);
        draw_fighter_pose(frame, *fighters, visual.party[event.source],
                          *fighter_pose);
    }
    if (palette_override != nullptr) {
        frame.palette = *palette_override;
    }
    if (captured_surface != nullptr) {
        *captured_surface = frame;
    }
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_monster_compact_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleVisualState& visual,
    const BattleSessionEvent& event, std::span<const std::uint8_t> text,
    std::uint16_t encounter_directory_offset, int columns = 4,
    std::uint8_t color = 0x00, int center_offset = 10,
    const BattleSurface* retained_surface = nullptr,
    bool include_party_cards = true,
    BattleSurface* captured_surface = nullptr) {
    if (!event.target_is_monster || event.target >= visual.monsters.size() ||
        text.empty()) {
        return;
    }
    auto frame = retained_surface != nullptr ? *retained_surface : base_surface;
    if (retained_surface == nullptr) {
        draw_battle_media(frame, menu_sprites, visual.media);
        draw_summoned_ally_name_cards(
            frame, menu_sprites, font, fallback, visual);
        draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                     visual.monsters,
                     std::nullopt, encounter_directory_offset);
        if (include_party_cards) {
            draw_fig_party_cards(
                frame, menu_sprites,
                std::span<const BattlePartyMember>(visual.party).first(
                    visual.party_count));
        }
    }

    // FIG 2375 uses the selected monster's runtime x centre (+321d), moves
    // ten Mode-X columns left, and opens a four-column panel at scanline 50.
    // 55e4 prints the four-glyph removal label at left+2/y=59, then keeps it
    // visible for 18 timer ticks before reporting the next removed state.
    const auto [center_x, center_y] = monster_visual_center(
        encounter, items, context.game_root, event.target);
    (void)center_y;
    const auto left = center_x / 4 - center_offset;
    draw_message_panel(frame, menu_sprites, left, 50, columns);
    draw_big5(frame, font, fallback, text, (left + 2) * 4, 59, color);
    if (captured_surface != nullptr) *captured_surface = frame;
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_monster_attack_failure_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleVisualState& visual,
    const BattleSessionEvent& event,
    std::span<const std::uint8_t> attack_text,
    std::span<const std::uint8_t> failure_text,
    std::uint16_t encounter_directory_offset) {
    if (!event.target_is_monster || event.target >= visual.monsters.size()) return;
    auto frame = base_surface;
    draw_battle_media(frame, menu_sprites, visual.media);
    draw_summoned_ally_name_cards(
        frame, menu_sprites, font, fallback, visual);
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 visual.monsters,
                 std::nullopt, encounter_directory_offset);
    draw_fig_party_cards(
        frame, menu_sprites,
        std::span<const BattlePartyMember>(visual.party).first(
            visual.party_count));
    const auto [center_x, center_y] = monster_visual_center(
        encounter, items, context.game_root, event.target);
    (void)center_y;
    const auto attack_left = center_x / 4 - 8;
    draw_message_panel(frame, menu_sprites, attack_left, 50, 2);
    draw_big5(frame, font, fallback, attack_text,
              (attack_left + 2) * 4, 59, 0x00);
    const auto failure_left = center_x / 4 - 4;
    draw_message_panel(frame, menu_sprites, failure_left, 65, 2);
    draw_big5(frame, font, fallback, failure_text,
              (failure_left + 2) * 4, 74, 0x6b);
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_monster_ability_name_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleVisualState& visual,
    const BattleSessionEvent& event, const BattleAbilityDatabase& abilities,
    std::uint16_t encounter_directory_offset,
    bool include_party_cards = true,
    BattleSurface* captured_surface = nullptr) {
    if (event.source >= visual.monsters.size() ||
        event.ability_id >= abilities.abilities().size()) {
        return;
    }
    const auto& name = abilities.ability(event.ability_id).name_big5;
    // FIG 262f starts with two middle columns, then inspects only glyphs
    // 4..6. Each non-space glyph adds two columns; the first A140 terminates
    // the scan. 72bf still writes all six name glyphs into that panel.
    auto columns = 2;
    for (std::size_t offset = 6; offset + 1U < name.size(); offset += 2U) {
        if (name[offset] == 0xa1 && name[offset + 1U] == 0x40) break;
        columns += 2;
    }
    auto source_event = event;
    source_event.target_is_monster = true;
    source_event.target = event.source;
    present_monster_compact_card(
        context, base_surface, encounter, items, menu_sprites,
        font, fallback, visual, source_event, name,
        encounter_directory_offset, columns, 0x00, 10, nullptr,
        include_party_cards, captured_surface);
}

void present_summoned_ally_action_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleVisualState& visual,
    const BattleSessionEvent& event, std::span<const std::uint8_t> text,
    std::uint16_t encounter_directory_offset, std::uint8_t color = 0x00,
    BattleSurface* captured_surface = nullptr) {
    if (event.source > 1U || text.empty() || menu_sprites.sprites().empty()) return;
    auto frame = base_surface;
    draw_battle_media(frame, menu_sprites, visual.media);
    draw_summoned_ally_name_cards(
        frame, menu_sprites, font, fallback, visual);
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 visual.monsters,
                 std::nullopt, encounter_directory_offset);

    const auto placement = fig_summoned_action_card_placement(event.source);
    // FIG 10fc calls 65fc with MENU index zero and its opaque copy mode.
    blit(frame, menu_sprites, 0, placement.left, placement.top, false);
    draw_big5(frame, font, fallback, text,
              placement.text_left, placement.text_top, color);
    if (captured_surface != nullptr) *captured_surface = frame;
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_capture_action_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& fighters, const SpriteArchive& menu_sprites,
    const LegacyFont& font, const LegacyFont& fallback,
    const BattleVisualState& visual, const BattleSessionEvent& event,
    const BattleAbilityDatabase& abilities,
    std::uint16_t encounter_directory_offset, bool failed) {
    if (event.source >= visual.party_count) return;
    // 0ded calls 137a with DS:31e3 still anchored on the selected monster.
    // Thus both the actor's base pose/card and 0da7's pot card move under the
    // target; they do not return to actor*18 before the capture resolves.
    auto frame = compose_event_frame(
        context, base_surface, encounter, items, fighters, menu_sprites,
        font, fallback, visual, event, 0, {}, std::nullopt,
        encounter_directory_offset);
    const auto [target_x, target_y] = event_target_center(
        encounter, items, context.game_root, visual, event, fighters);
    static_cast<void>(target_y);
    const auto action_column = target_x / 4 - 3;
    auto panel_column = action_column - 8;
    // 0dbb uses an unsigned comparison after subtracting eight, so both an
    // underflow and a value at/above 64h reset the panel to column zero.
    if (panel_column < 0 || panel_column >= 100) panel_column = 0;
    draw_message_panel(frame, menu_sprites, panel_column, 120, 4);
    draw_big5(frame, font, fallback, abilities.capture_action_text(),
              (panel_column + 2) * 4, 129, 0x00);
    if (failed) {
        // 0e42 offsets the preserved 0da7 rectangle by +4 columns/+15 lines,
        // then prints DATA:2c95 at another +4 columns in colour 6b.
        draw_message_panel(frame, menu_sprites, panel_column + 4, 135, 4);
        draw_big5(frame, font, fallback, abilities.physical_failure_text(),
                  (panel_column + 8) * 4, 144, 0x6b);
    }
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_player_escape_failure_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& fighters, const SpriteArchive& menu_sprites,
    const LegacyFont& font, const LegacyFont& fallback,
    const BattleVisualState& visual, const BattleSessionEvent& event,
    const BattleAbilityDatabase& abilities,
    std::uint16_t encounter_directory_offset) {
    auto frame = base_surface;
    draw_battle_media(frame, menu_sprites, visual.media);
    draw_summoned_ally_name_cards(
        frame, menu_sprites, font, fallback, visual);
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 visual.monsters, std::nullopt, encounter_directory_offset);
    const auto actor = event.source < visual.party_count
                           ? std::optional<std::size_t>(event.source)
                           : std::nullopt;
    draw_fig_party_cards(
        frame, menu_sprites,
        std::span<const BattlePartyMember>(visual.party).first(
            visual.party_count),
        actor);
    if (actor) {
        draw_fighter_pose(frame, fighters, visual.party[*actor], 5);
    }
    const auto placement = fig_player_escape_failure_placement();
    draw_message_panel(frame, menu_sprites, placement.mode_x_column,
                       placement.top, placement.columns);
    draw_big5(frame, font, fallback, abilities.monster_escape_text(false),
              placement.text_mode_x_column * 4, placement.text_top, 0x6b);
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

void present_missing_medium_card(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& fighters, const SpriteArchive& menu_sprites,
    const LegacyFont& font, const LegacyFont& fallback,
    const BattleVisualState& visual, const BattleSessionEvent& event,
    const BattleAbilityDatabase& abilities,
    std::uint16_t encounter_directory_offset,
    std::optional<std::size_t> player_pose = std::nullopt,
    const std::array<std::uint8_t, 768>* palette_override = nullptr,
    BattleSurface* captured_surface = nullptr) {
    auto frame = base_surface;
    draw_battle_media(frame, menu_sprites, visual.media);
    draw_summoned_ally_name_cards(
        frame, menu_sprites, font, fallback, visual);
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 visual.monsters,
                 std::nullopt, encounter_directory_offset);
    const auto action_actor = !event.source_is_summoned_ally &&
                                      event.source < visual.party_count
                                  ? std::optional<std::size_t>(event.source)
                                  : std::nullopt;
    draw_fig_party_cards(
        frame, menu_sprites,
        std::span<const BattlePartyMember>(visual.party).first(
            visual.party_count),
        action_actor);
    if (action_actor) {
        // Learned abilities reach 58fa after 4338 has left pose4 visible.
        // Direct items can instead enter it from 1138 with pose0 retained.
        draw_fighter_pose(
            frame, fighters, visual.party[*action_actor],
            player_pose.value_or(fig_player_ability_poses()[1]));
    }

    // 58fa programs 384a with x=10h/y=4bh/18 columns, then 7284 writes
    // DATA:2d1f at x=12h/y=54h. Its stack adjustment skips the visual effect.
    draw_message_panel(frame, menu_sprites, 0x10, 0x4b, 0x12);
    draw_fig_text(frame, font, fallback, abilities.missing_medium_text(),
                  0x12, 0x54);
    if (palette_override != nullptr) frame.palette = *palette_override;
    if (captured_surface != nullptr) *captured_surface = frame;
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
}

bool present_medium_summon_animation(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleVisualState& visual,
    const BattleSessionEvent& event,
    std::uint16_t encounter_directory_offset,
    BattleSurface* retained_action_surface = nullptr) {
    const auto summoned_source = event.source_is_summoned_ally;
    const auto player_source = !event.source_is_monster && !summoned_source;
    if ((event.source_is_monster && event.source >= visual.monsters.size()) ||
        (summoned_source && event.source >= visual.summoned_ally_present.size()) ||
        (player_source && (event.source >= visual.party_count ||
                           retained_action_surface == nullptr)) ||
        event.target >= visual.media.size()) {
        return true;
    }
    const auto sprite_frame = 174U + event.target;
    if (sprite_frame >= menu_sprites.sprites().size()) return true;

    const auto destination = fig_medium_placement(event.target);
    auto current_x = std::uint16_t{};
    auto current_y = std::uint16_t{};
    if (summoned_source) {
        // 1048 reads the packed ally slot's +31f5 value (2/16h), adds
        // 0fh Mode-X columns and fixes y to 0fh before calling 5b41.
        const auto action = fig_summoned_action_card_placement(event.source);
        current_x = static_cast<std::uint16_t>(action.left / 4 + 0x0f);
        current_y = 0x0f;
    } else if (player_source) {
        // 4792/47c6/47fa copy DS:31e3 and fix y=160 before 5b41.
        current_x = static_cast<std::uint16_t>(12U + event.source * 18U);
        current_y = 0xa0;
    } else {
        const auto [source_left, source_top] = monster_visual_center(
            encounter, items, context.game_root, event.source);
        current_x = static_cast<std::uint16_t>(source_left / 4);
        current_y = static_cast<std::uint16_t>(source_top);
    }
    const auto target_x = static_cast<std::uint16_t>(destination.left / 4);
    const auto target_y = static_cast<std::uint16_t>(destination.top);

    // Literal unsigned arithmetic from 5b41. A wrapped/backwards delta is
    // capped to 20 before division; vertical motion is always eight lines.
    auto horizontal_step = static_cast<std::uint16_t>(target_x - current_x);
    if (horizontal_step > 0x50U) horizontal_step = 0x14U;
    horizontal_step = static_cast<std::uint16_t>(horizontal_step / 0x14U);
    if (horizontal_step == 0) horizontal_step = 1;
    // 5b41 copies the already visible action page to its scratch background
    // before moving the mediator.  For a player this is the dark pose page;
    // for a monster it is 262f's fixed mediator-name card.  Rebuilding from
    // base_surface here both discarded the monster card and selected the
    // summoner's blue reaction frame for the whole flight.
    const auto action_background = retained_action_surface != nullptr
                                       ? std::optional<BattleSurface>{
                                             *retained_action_surface}
                                       : std::nullopt;

    for (std::size_t guard = 0; guard < 512U; ++guard) {
        auto frame = action_background ? *action_background : base_surface;
        if (!action_background) {
            draw_battle_media(frame, menu_sprites, visual.media);
            draw_summoned_ally_name_cards(
                frame, menu_sprites, font, fallback, visual);
            draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                         visual.monsters,
                         summoned_source
                             ? std::nullopt
                             : std::optional<std::size_t>(event.source),
                         encounter_directory_offset);
        }
        blit(frame, menu_sprites, sprite_frame,
             static_cast<int>(current_x) * 4, static_cast<int>(current_y));
        // 1048 flies a requested mediator over the card-free ally-action
        // page.  The persistent captured-ally name card remains at the top,
        // but 2bb5's bottom party cards are not part of this path.
        context.platform.present({
            320, 200, frame.pixels,
            std::span<const std::uint8_t, 768>(frame.palette),
        });
        // Player callers restore from the final 5b41 page.  The monster
        // caller instead keeps 262f's other Mode-X page untouched for a
        // possible following 2731 dismissal in the same round.
        if (player_source && retained_action_surface != nullptr) {
            *retained_action_surface = frame;
        }
        if (!delay_for_or_frontend_quit(
                context.platform, fig_timer_ticks(1))) {
            return false;
        }

        if (current_x < target_x) {
            const auto next = static_cast<std::uint16_t>(
                current_x + horizontal_step);
            current_x = next < target_x ? next : target_x;
        } else if (current_x > target_x) {
            const auto next = static_cast<std::uint16_t>(
                current_x - horizontal_step);
            current_x = next > target_x ? next : target_x;
        }
        if (current_y < target_y) {
            const auto next = static_cast<std::uint16_t>(current_y + 8U);
            current_y = next < target_y ? next : target_y;
        } else if (current_y > target_y) {
            const auto next = static_cast<std::uint16_t>(current_y - 8U);
            current_y = next > 60000U || next <= target_y ? target_y : next;
        }
        if (current_x == target_x && current_y == target_y) return true;
    }
    throw std::runtime_error("FIG mediator summon animation did not converge");
}

bool present_round_events(
    GameContext& context, const BattleSurface& base_surface,
    const BattleSurface& previous_page,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const BattleAbilityDatabase& abilities, const LegacyFont& font,
    const LegacyFont& fallback, const SpriteArchive& fighters,
    const SpriteArchive& menu_sprites,
    BattleVisualState& visual, const BattleRoundResult& result,
    std::uint16_t encounter_directory_offset,
    const std::array<PlayerBattleCommand, 4>& commands,
    std::size_t& existing_result_palette_shift) {
    constexpr auto action_delay = fig_timer_ticks(3);
    constexpr auto effect_delay = fig_timer_ticks(1);
    constexpr auto status_card_delay = fig_timer_ticks(18);
    constexpr auto ward_card_delay = fig_timer_ticks(5);
    constexpr auto immunity_card_delay = fig_timer_ticks(8);
    constexpr auto monster_action_card_delay = fig_timer_ticks(4);
    constexpr auto summoned_action_card_delay = fig_timer_ticks(9);
    constexpr auto capture_action_card_delay = fig_timer_ticks(18);
    constexpr auto summon_install_delay = fig_timer_ticks(10);
    std::map<std::uint16_t, SpriteArchive> effect_cache;
    std::array<bool, 4> player_resource_cost_presented{};
    const auto present_player_resource_cost = [&](const BattleSessionEvent& event) {
        if (event.source >= visual.party_count ||
            event.source >= commands.size() ||
            player_resource_cost_presented[event.source]) {
            return;
        }
        const auto& command = commands[event.source];
        if (event.kind != BattleEventKind::player_ability &&
            event.kind != BattleEventKind::missing_medium &&
            event.kind != BattleEventKind::medium_summoned &&
            event.kind != BattleEventKind::medium_dismissed &&
            event.kind != BattleEventKind::medium_dismissal_empty) {
            return;
        }
        auto cost = std::uint16_t{};
        auto resource_class = std::uint8_t{};
        if (command.kind == PlayerCommandKind::ability &&
            command.ability_id < abilities.abilities().size()) {
            const auto& ability = abilities.ability(command.ability_id);
            if (ability.effect_code == 0x47U) return;
            cost = ability.cost;
            resource_class = static_cast<std::uint8_t>(
                (ability.target_flags >> 8U) & 0x0fU);
        } else if (command.kind == PlayerCommandKind::item &&
                   (event.resulting_player_support_state ||
                    event.kind == BattleEventKind::medium_summoned ||
                    event.kind == BattleEventKind::medium_dismissed ||
                    event.kind == BattleEventKind::medium_dismissal_empty) &&
                   event.ability_id >= 0x8cU &&
                   static_cast<std::size_t>(event.ability_id) + 2U <
                       items.entry_count()) {
            // The 447b..478f support handlers are the proven direct-item
            // path that rejoins 585e here. Other item selectors own distinct
            // restoration epilogues and remain locked by their own captures.
            const auto item = BattleItemDefinition::parse(
                event.ability_id,
                items.entry(static_cast<std::size_t>(event.ability_id) + 2U));
            const auto embedded_id = static_cast<std::size_t>(
                event.ability_id - 0x8cU);
            if (item.type != 0x10U || item.effect_code == 0x47U ||
                embedded_id >= abilities.abilities().size()) {
                return;
            }
            const auto& ability = abilities.ability(embedded_id);
            cost = ability.cost;
            // 184f and the direct-item return both hard-wire actor +55 AP.
            // item_id-8c selects the embedded cost/effect, but its learned-
            // ability resource nibble does not redirect an ITEM payment.
            resource_class = 4U;
        } else {
            return;
        }
        auto& member = visual.party[event.source];
        if (resource_class == 1U || resource_class == 4U) {
            member.ability_points = static_cast<std::uint16_t>(
                cost >= member.ability_points
                    ? 0U
                    : member.ability_points - cost);
        } else if (resource_class == 2U || resource_class == 3U) {
            member.secondary_points = static_cast<std::uint16_t>(
                cost >= member.secondary_points
                    ? 0U
                    : member.secondary_points - cost);
        }
        player_resource_cost_presented[event.source] = true;
    };
    auto frontend_abort = false;
    auto alternate_page = previous_page;
    const auto delay = [&](std::chrono::milliseconds duration) {
        if (delay_for_or_frontend_quit(context.platform, duration)) return true;
        frontend_abort = true;
        return false;
    };
    for (std::size_t event_index = 0; event_index < result.events.size();
         ++event_index) {
        // A resolved round can contain a long multi-target presentation. Its
        // rules are already committed atomically, but a closed frontend need
        // not render every remaining DOS animation before the process exits.
        if (frontend_abort || context.platform.poll_frontend_quit()) return false;
        const auto& event = result.events[event_index];
        const auto same_player_expiry_follows = [&] {
            return event_index + 1U < result.events.size() &&
                result.events[event_index + 1U].kind ==
                    BattleEventKind::status_expired &&
                !result.events[event_index + 1U].target_is_monster &&
                result.events[event_index + 1U].source == event.source;
        };
        const auto action_first =
            event_index == 0 ||
            !fig_same_presented_action(result.events[event_index - 1U], event);
        if (action_first && event.source_is_monster) {
            // 22e0 returns monster actions through a fresh 2db8 battlefield;
            // the directory-64h zero-hit capture proves the following
            // autonomous slot-two action observes the BA ramp rather than a
            // stale player weapon-wipe phase.
            existing_result_palette_shift = 0;
        }
        const auto effect_first =
            event_index == 0 ||
            !fig_same_effect_phase(result.events[event_index - 1U], event);
        const auto monster_action_event = event.source_is_monster &&
            (event.kind == BattleEventKind::monster_attack ||
             event.kind == BattleEventKind::monster_ability ||
             event.kind == BattleEventKind::monster_heal ||
             event.kind == BattleEventKind::medium_summoned ||
             event.kind == BattleEventKind::medium_dismissed);
        const auto next_continues_monster_action =
            event_index + 1U < result.events.size() &&
            fig_same_presented_action(
                event, result.events[event_index + 1U]);
        const auto next_is_death_reaction =
            event_index + 1U < result.events.size() &&
            result.events[event_index + 1U].kind ==
                BattleEventKind::death_reaction;
        const auto finish_monster_action_here =
            monster_action_event && !next_continues_monster_action &&
            !next_is_death_reaction;
        const auto present_monster_turn_tail =
            [&](const BattleSessionEvent& shown) -> bool {
                // Every normal enemy action rejoins 22e0: retain the handler's
                // last page for five ticks, redraw the clean battlefield, then
                // retain that page for three. Death-reaction cards reach this
                // same tail after their own five-tick wait.
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, shown,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset);
                return delay(action_delay);
            };
        if (event.monster_special_silent_return) {
            // 20e7 exposes a bare 2db8 page before action selection. Effects
            // without an explicit 26af branch jump directly to 2938, so no
            // 262f name card, voice, selector visual or seven-tick name hold
            // occurs. The outer 22e0 tail still waits five ticks, redraws the
            // same bare page and waits three more ticks.
            if (action_first) {
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
            }
            if (finish_monster_action_here) {
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        // Captured allies return from physical resolution or the player-side
        // effect table to the same 0fb9/1039 epilogue: rebuild the clean
        // battlefield and hold it for five ticks.  A scope guard is used
        // because individual selectors have several faithful early-return
        // branches of their own.  Multi-target events receive this epilogue
        // only after the last event in the presented action.
        const auto captured_ally_dispatch =
            (event.kind == BattleEventKind::ally_attack && !event.evaded) ||
            event.kind == BattleEventKind::ally_ability ||
            (event.kind == BattleEventKind::missing_medium &&
             event.source_is_summoned_ally);
        ScopeExit captured_ally_tail([&] {
            if (frontend_abort || !captured_ally_dispatch) return;
            if (event_index + 1U < result.events.size() &&
                fig_same_presented_action(
                    event, result.events[event_index + 1U])) {
                return;
            }
            // 1039/0fb9 call bare 2db8.  That compositor redraws the media,
            // captured-ally name cards and monsters, but never 2bb5's bottom
            // party cards.
            const auto clean = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset, std::nullopt,
                false, false);
            present_battle_surface(context, clean);
            static_cast<void>(delay(ward_card_delay));
        });
        if (event.kind == BattleEventKind::skipped) {
            // 0694/0c11/0c1f/0c2d jump straight back to the initiative loop.
            // A skipped turn is useful to deterministic frontends, but FIG
            // does not present an extra clean page or insert an action delay.
            continue;
        }
        if (event.kind == BattleEventKind::missing_medium) {
            if (event.source_is_summoned_ally) {
                present_summoned_ally_action_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event,
                    abilities.summoned_ally_ability_text(),
                    encounter_directory_offset);
                if (!delay(summoned_action_card_delay)) return false;
            } else if (!event.source_is_monster &&
                       event.source < visual.party_count) {
                const auto direct_item =
                    event.source < commands.size() &&
                    commands[event.source].kind == PlayerCommandKind::item;
                auto pose_event = event;
                pose_event.kind = BattleEventKind::player_ability;
                const auto pose_count = direct_item ? 1U : 2U;
                const auto poses = fig_player_ability_poses();
                for (std::size_t phase = 0; phase < pose_count; ++phase) {
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, pose_event,
                        poses[phase],
                        {}, std::nullopt, encounter_directory_offset);
                    if (!delay(action_delay)) return false;
                }
                if (direct_item) {
                    // 1138 dispatches target-flag failures through the same
                    // >30h table as a successful tactical item: retain pose0,
                    // run 43ce's five darkening ticks, let 58fa hold the dark
                    // panel for nine ticks, then preserve it through 4417's
                    // five restoration ticks before 0d98's bare tail.
                    auto faded = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, pose_event,
                        poses[0], {}, std::nullopt,
                        encounter_directory_offset);
                    for (auto step = 0; step < 5; ++step) {
                        darken_fig_dispatcher_palette(faded);
                        present_battle_surface(context, faded);
                        if (!delay(effect_delay)) return false;
                    }
                    BattleSurface missing;
                    present_missing_medium_card(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event, abilities,
                        encounter_directory_offset, poses[0], &faded.palette,
                        &missing);
                    play_voice_cue(context, {FigVoiceFile::sp, 2,
                                             FigVoiceTiming::before_action});
                    if (!delay(summoned_action_card_delay)) return false;
                    for (auto step = 0; step < 5; ++step) {
                        brighten_fig_dispatcher_palette(
                            missing, base_surface.palette);
                        present_battle_surface(context, missing);
                        if (!delay(effect_delay)) return false;
                    }
                    const auto expires_here =
                        event_index + 1U < result.events.size() &&
                        result.events[event_index + 1U].kind ==
                            BattleEventKind::status_expired &&
                        !result.events[event_index + 1U].target_is_monster &&
                        result.events[event_index + 1U].source == event.source;
                    if (!expires_here) {
                        const auto clean = compose_event_frame(
                            context, base_surface, encounter, items, fighters,
                            menu_sprites, font, fallback, visual, event,
                            std::nullopt, {}, std::nullopt,
                            encounter_directory_offset, std::nullopt, false,
                            false);
                        present_battle_surface(context, clean);
                        if (!delay(ward_card_delay)) return false;
                    }
                    continue;
                }
                // 58fa is entered after the ordinary learned-ability debit;
                // its missing-medium page therefore sees the paid gauge even
                // though the selected effect body never runs.
                present_player_resource_cost(event);
            }
            present_missing_medium_card(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, abilities,
                encounter_directory_offset);
            // 58fa presents the panel first, then starts SP002 and holds it for
            // exactly nine timer ticks before returning past the effect body.
            play_voice_cue(context, {FigVoiceFile::sp, 2,
                                     FigVoiceTiming::before_action});
            if (!delay(summoned_action_card_delay)) return false;
            if (!event.source_is_monster &&
                !event.source_is_summoned_ally &&
                event.source < visual.party_count) {
                // 58fa returns through the ordinary player-action caller and
                // then 0c41. Even without an expiring status, 0d98 exposes a
                // bare 2db8 page for five ticks before initiative advances.
                const auto expires_here =
                    event_index + 1U < result.events.size() &&
                    result.events[event_index + 1U].kind ==
                        BattleEventKind::status_expired &&
                    !result.events[event_index + 1U].target_is_monster &&
                    result.events[event_index + 1U].source == event.source;
                if (!expires_here) {
                    const auto clean = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false,
                        false);
                    present_battle_surface(context, clean);
                    if (!delay(ward_card_delay)) return false;
                }
            }
            continue;
        }
        if (event.kind == BattleEventKind::medium_summoned) {
            if (!event.source_is_monster &&
                !event.source_is_summoned_ally &&
                event.source < visual.party_count) {
                // Player selectors 31h/3bh/3ch share one 4338/1138 caller
                // envelope even when 57f2 dispatches two of them. Compose
                // the pose and 43ce darkening once, run each 5b41 flight on
                // the retained dark page, then perform the single 585e/4417
                // payment/restoration and 0d98 clean tail.
                auto group_end = event_index;
                while (group_end + 1U < result.events.size()) {
                    const auto& next = result.events[group_end + 1U];
                    if (next.kind != BattleEventKind::medium_summoned ||
                        next.source_is_monster ||
                        next.source_is_summoned_ally ||
                        !fig_same_presented_action(event, next)) {
                        break;
                    }
                    ++group_end;
                }

                const auto direct_item =
                    event.source < commands.size() &&
                    commands[event.source].kind == PlayerCommandKind::item;
                auto pose_event = event;
                pose_event.kind = BattleEventKind::player_ability;
                pose_event.target_is_monster = false;
                pose_event.target = event.source;
                pose_event.action_anchor_is_target = false;
                const auto ability_poses = fig_player_ability_poses();
                const auto pose_count = direct_item ? 1U : 2U;
                BattleSurface retained;
                for (std::size_t phase = 0; phase < pose_count; ++phase) {
                    retained = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, pose_event,
                        ability_poses[phase], {}, std::nullopt,
                        encounter_directory_offset);
                    present_battle_surface(context, retained);
                    if (!delay(action_delay)) return false;
                }
                for (auto step = 0; step < 5; ++step) {
                    darken_fig_dispatcher_palette(retained);
                    present_battle_surface(context, retained);
                    if (!delay(effect_delay)) return false;
                }
                for (auto index = event_index; index <= group_end; ++index) {
                    const auto& medium_event = result.events[index];
                    play_voice_cue(context, {FigVoiceFile::sp, 0x31,
                                             FigVoiceTiming::before_action});
                    if (!present_medium_summon_animation(
                            context, base_surface, encounter, items,
                            menu_sprites, font, fallback, visual,
                            medium_event, encounter_directory_offset,
                            &retained)) {
                        frontend_abort = true;
                        return false;
                    }
                    apply_visual_event(visual, medium_event, abilities);
                }

                present_player_resource_cost(event);
                for (auto step = 0; step < 5; ++step) {
                    brighten_fig_dispatcher_palette(
                        retained, base_surface.palette);
                    present_battle_surface(context, retained);
                    if (!delay(effect_delay)) return false;
                }
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
                if (!delay(ward_card_delay)) return false;
                event_index = group_end;
                continue;
            }
            if (event.source_is_summoned_ally) {
                // 0fa7 has already selected the ability and 10fc displays the
                // fixed “奇術” ally card for nine ticks.  1048 then starts
                // SP049 and flies the mediator from slot x+0fh/y=0fh; it does
                // not show the enemy 262f ability-name card.
                present_summoned_ally_action_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event,
                    abilities.summoned_ally_ability_text(),
                    encounter_directory_offset);
                if (!delay(summoned_action_card_delay)) return false;
            } else {
                auto summon_name_event = event;
                static constexpr std::array<std::uint16_t, 3>
                    summon_name_ids = {0x33, 0x42, 0x52};
                if (event.target < summon_name_ids.size()) {
                    // 23b1 substitutes the fixed mediator-install record; it
                    // does not display the generic ability that requested the
                    // missing mediator.
                    summon_name_event.ability_id =
                        summon_name_ids[event.target];
                }
                if (action_first) {
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false, false);
                }
                present_monster_ability_name_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, summon_name_event, abilities,
                    encounter_directory_offset, false, &alternate_page);
                if (!delay(fig_timer_ticks(7))) return false;
            }
            play_voice_cue(context, {FigVoiceFile::sp, 0x31,
                                     FigVoiceTiming::before_action});
            if (!present_medium_summon_animation(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event,
                    encounter_directory_offset,
                    event.source_is_monster ? &alternate_page : nullptr)) {
                frontend_abort = true;
                return false;
            }
            apply_visual_event(visual, event, abilities);
            if (event.source_is_summoned_ally) {
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, std::nullopt,
                    {}, std::nullopt, encounter_directory_offset);
                // The captured-ally caller 0fb9 redraws the newly persistent
                // mediator and holds that clean page for five ticks.
                if (!delay(ward_card_delay)) return false;
            } else if (finish_monster_action_here) {
                // 2439 returns straight to 22e0. The flight's destination page
                // remains visible for five ticks, then 2db8 replaces it with a
                // bare page containing the newly persistent mediator.
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        if (event.kind == BattleEventKind::medium_dismissed ||
            event.kind == BattleEventKind::medium_dismissal_empty) {
            if (event.source_is_summoned_ally) {
                // 1048 has already entered the captured-ally action envelope,
                // so 3dh/3eh/3fh retain the fixed “奇術” card instead of
                // calling the monster-only 262f name panel.  3c15 backs up
                // the preceding bare 2db8 page; the selector then removes the
                // persistent medium and alternates that page with the action
                // page after 65f1 has stencilled the old sprite over it.
                auto bare_medium = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                BattleSurface action_page;
                present_summoned_ally_action_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event,
                    abilities.summoned_ally_ability_text(),
                    encounter_directory_offset, 0x00, &action_page);
                if (!delay(summoned_action_card_delay)) return false;
                play_voice_cue(context, {FigVoiceFile::sp, 0x3d,
                                         FigVoiceTiming::before_action});

                if (event.kind == BattleEventKind::medium_dismissed) {
                    apply_visual_event(visual, event, abilities);
                    const auto medium_frame = 174U + event.target;
                    if (medium_frame < menu_sprites.sprites().size()) {
                        const auto placement = fig_medium_placement(event.target);
                        blit_mode0_stencil(
                            action_page, menu_sprites, medium_frame,
                            placement.left, placement.top);
                    }
                    present_battle_surface(context, bare_medium);
                    for (std::size_t flip = 0; flip < 8U; ++flip) {
                        present_battle_surface(
                            context, (flip & 1U) == 0U
                                         ? action_page
                                         : bare_medium);
                        if (!delay(effect_delay)) return false;
                    }
                } else {
                    // 4844 sees the empty 50h x sentinel and waits five ticks
                    // without creating a backup/flip pair.
                    if (!delay(ward_card_delay)) return false;
                }
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
                if (!delay(ward_card_delay)) return false;
                continue;
            }
            if (!event.source_is_monster &&
                !event.source_is_summoned_ally &&
                event.source < visual.party_count) {
                const auto direct_item =
                    event.source < commands.size() &&
                    commands[event.source].kind == PlayerCommandKind::item;
                auto pose_event = event;
                pose_event.kind = BattleEventKind::player_ability;
                pose_event.target_is_monster = false;
                pose_event.target = event.source;
                pose_event.action_anchor_is_target = false;
                const auto ability_poses = fig_player_ability_poses();
                const auto pose_count = direct_item ? 1U : 2U;
                BattleSurface retained;
                for (std::size_t phase = 0; phase < pose_count; ++phase) {
                    retained = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, pose_event,
                        ability_poses[phase], {}, std::nullopt,
                        encounter_directory_offset);
                    present_battle_surface(context, retained);
                    if (!delay(action_delay)) return false;
                }
                for (auto step = 0; step < 5; ++step) {
                    darken_fig_dispatcher_palette(retained);
                    present_battle_surface(context, retained);
                    if (!delay(effect_delay)) return false;
                }
                play_voice_cue(context, {FigVoiceFile::sp, 0x3d,
                                         FigVoiceTiming::before_action});

                // A present medium retains the old sprite on the page saved
                // by 3c15 while 482e/4886/4894 clear its persistent x and
                // flip that pair eight times. Its disappearance is visible
                // only on the ordinary 0d98 recomposition after 585e/4417.
                if (event.kind == BattleEventKind::medium_dismissed) {
                    apply_visual_event(visual, event, abilities);
                    for (std::size_t flip = 0; flip < 8U; ++flip) {
                        present_battle_surface(context, retained);
                        if (!delay(effect_delay)) return false;
                    }
                } else {
                    // 4844 observes the empty slot's x sentinel (50h), waits
                    // five ticks and returns without 3c15/65f1 page flips.
                    if (!delay(ward_card_delay)) return false;
                }
                present_player_resource_cost(event);
                for (auto step = 0; step < 5; ++step) {
                    brighten_fig_dispatcher_palette(
                        retained, base_surface.palette);
                    present_battle_surface(context, retained);
                    if (!delay(effect_delay)) return false;
                }
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
                if (!delay(ward_card_delay)) return false;
                continue;
            }
            // 20e7 first exposes a bare 2db8 page. 2731 is reached before the
            // ordinary named-action dispatcher below, so this branch must
            // reproduce its own 262f setup rather than inheriting the party
            // cards from the command page.
            if (action_first) {
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
            }
            BattleSurface dismissal_name_page;
            present_monster_ability_name_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, abilities,
                encounter_directory_offset, false, &dismissal_name_page);
            // 262f suppresses the normal ability voice for 35h/43h/53h but
            // still retains the name card for seven INT-08h timer ticks.
            if (!delay(fig_timer_ticks(7))) return false;
            play_voice_cue(context, {FigVoiceFile::sp, 0x3d,
                                     FigVoiceTiming::before_action});

            // 275b..278c backs up both Mode-X pages, removes the mediator from
            // battle state, redraws its old AEh/AFh/B0h sprite on the name-card
            // page, and flips eight times. Consequently the visible result is
            // four one-tick alternations between the retained 262f name page
            // and the last command/target page; the mediator remains visible
            // on both. Only 22e0's later clean redraw makes it disappear.
            apply_visual_event(visual, event, abilities);
            const auto medium_frame = 174U + event.target;
            if (medium_frame < menu_sprites.sprites().size()) {
                const auto placement = fig_medium_placement(event.target);
                blit_mode0_stencil(
                    dismissal_name_page, menu_sprites, medium_frame,
                    placement.left, placement.top);
            }
            present_battle_surface(context, alternate_page);
            for (std::size_t flip = 0; flip < 8U; ++flip) {
                if ((flip & 1U) == 0) {
                    present_battle_surface(context, dismissal_name_page);
                } else {
                    present_battle_surface(context, alternate_page);
                }
                if (!delay(effect_delay)) return false;
            }
            if (finish_monster_action_here) {
                // 22e0's 2db8 cleanup is bare: unlike the generic event
                // compositor it does not restore 2bb5's bottom party cards.
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        if (event.kind == BattleEventKind::monster_fled ||
            event.kind == BattleEventKind::monster_escape_failed) {
            const auto succeeded = event.kind == BattleEventKind::monster_fled;
            present_monster_compact_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event,
                abilities.monster_escape_text(succeeded),
                encounter_directory_offset, 4, 0x6b, 10, nullptr, false);
            if (succeeded) {
                play_voice_cue(context, {FigVoiceFile::sv3, 0,
                                         FigVoiceTiming::before_action});
            }
            // 212a/214c hold the card for three ticks, then flow through
            // 22e0's additional five-tick pause before the clean redraw.
            if (!delay(immunity_card_delay)) return false;
            if (succeeded) apply_visual_event(visual, event, abilities);
            present_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt, {}, std::nullopt,
                encounter_directory_offset, std::nullopt, false, false);
            if (!delay(action_delay)) return false;
            continue;
        }
        if (event.kind == BattleEventKind::death_reaction) {
            present_player_status_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event,
                abilities.death_reaction_text(),
                encounter_directory_offset, 2, 0x6b);
            if (!delay(ward_card_delay)) return false;
            if (!present_monster_turn_tail(event)) return false;
            continue;
        }
        if (event.kind == BattleEventKind::monster_captured ||
            event.kind == BattleEventKind::capture_failed) {
            // 0ded first presents the actor's base FMAN pose, then starts
            // SP016 and enters 0da7's 18-tick "炼妖壶" card.
            present_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, 0, {}, std::nullopt,
                encounter_directory_offset);
            const auto cues = fig_non_effect_voice_cues(event);
            for (const auto& cue : cues) {
                if (cue.timing == FigVoiceTiming::before_action) {
                    play_voice_cue(context, cue);
                }
            }
            present_capture_action_card(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, abilities,
                encounter_directory_offset, false);
            if (!delay(capture_action_card_delay)) return false;
            if (event.kind == BattleEventKind::capture_failed) {
                present_capture_action_card(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, abilities,
                    encounter_directory_offset, true);
                if (!delay(ward_card_delay)) return false;

                // 0e83 returns to the same 0c41 player-action tail as a
                // failed ordinary escape. With no expiring status, 0d98
                // still flips a bare 2db8 battlefield for five ticks before
                // the next initiative entry. The capture-failure card must
                // not remain underneath the following monster action.
                const auto expires_here =
                    event_index + 1U < result.events.size() &&
                    result.events[event_index + 1U].kind ==
                        BattleEventKind::status_expired &&
                    !result.events[event_index + 1U].target_is_monster &&
                    result.events[event_index + 1U].source == event.source;
                if (!expires_here) {
                    const auto clean = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false,
                        false);
                    present_battle_surface(context, clean);
                    if (!delay(ward_card_delay)) return false;
                }
                continue;
            }
            apply_visual_event(visual, event, abilities);
            for (const auto& cue : cues) {
                if (cue.timing == FigVoiceTiming::after_action) {
                    play_voice_cue(context, cue);
                }
            }
            present_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, 0, {}, std::nullopt,
                encounter_directory_offset);
            if (!delay(summoned_action_card_delay)) return false;
            continue;
        }
        if (event.kind == BattleEventKind::ally_summoned) {
            // 5de4 stores the new slot before calling 2f84, so its four-column
            // ITEM-name card is already visible behind the summoner's base
            // pose. The install uses SP061 and ten ticks; it does not decode an
            // ST### archive named after the captured-monster item id.
            apply_visual_event(visual, event, abilities);
            present_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, 0, {},
                std::nullopt, encounter_directory_offset);
            play_voice_cue(context, {FigVoiceFile::sp, 0x3d,
                                     FigVoiceTiming::before_action});
            if (!delay(summon_install_delay)) return false;
            if (event.source < visual.party_count &&
                static_cast<std::size_t>(event.ability_id) + 2U <
                    items.entry_count()) {
                // The resolved session already owns the post-state, but the
                // presentation snapshot is the pre-round state.  Original
                // capture shows 2f84 holding the old +35 gauge for all ten
                // ticks; the level*2 debit becomes visible only when the next
                // action redraws the party card.
                const auto summoned = MonsterDefinition::parse(
                    event.ability_id,
                    items.entry(static_cast<std::size_t>(event.ability_id) + 2U));
                const auto cost = static_cast<std::uint16_t>(
                    static_cast<unsigned>(summoned.level) * 2U);
                auto& member = visual.party[event.source];
                member.secondary_points = static_cast<std::uint16_t>(
                    cost >= member.secondary_points
                        ? 0U
                        : member.secondary_points - cost);
            }
            continue;
        }
        if (event.kind == BattleEventKind::status_expired) {
            // 0af5 silently expires the five 2deb monster-status icons before
            // any buff-expiry cards or the following action are recomposed.
            if (event.target_is_monster) {
                apply_visual_event(visual, event, abilities);
                // FIG 0b00 checks special ward, evasion and attack in this
                // exact order and reuses 2375/55e4 for every timer reaching 0.
                static constexpr std::array<std::size_t, 3> order = {0, 2, 1};
                for (const auto slot : order) {
                    if ((event.expired_monster_buff_mask & (1U << slot)) == 0) {
                        continue;
                    }
                    present_monster_compact_card(
                        context, base_surface, encounter, items, menu_sprites,
                        font, fallback, visual, event,
                        abilities.monster_removed_buff_text(slot),
                        encounter_directory_offset, 4, 0x00, 10, nullptr,
                        false);
                    if (!delay(status_card_delay)) return false;
                }
            } else {
                // FIG 0c41..0d98 reports five expiring party buffs followed
                // by four recovered status bits, all through 0da7's neutral
                // four-column bottom card rather than 2338's colour-6b card.
                //
                // 0da7 does not reset the physical-action globals before it
                // calls 2db8/137a.  When this event immediately follows a
                // player attack, 31e1/31e3 therefore still describe the
                // attack's final fighter pose at the selected monster.  The
                // compact panel is placed eight Mode-X columns left of the
                // fighter sprite and is drawn *after* 137a, covering the top
                // of that relocated action card.  Reusing the ordinary
                // party-slot status helper instead moves the panel to x=0,
                // replaces the fighter with a portrait and cannot match the
                // original 0c41 path.
                std::optional<BattleSessionEvent> retained_action;
                std::optional<std::size_t> retained_pose;
                if (event_index != 0) {
                    const auto& previous = result.events[event_index - 1U];
                    if (previous.kind == BattleEventKind::player_attack &&
                        !previous.source_is_monster &&
                        previous.source == event.source &&
                        previous.source < visual.party_count) {
                        retained_action = previous;
                        retained_pose = previous.critical ? 3U : 2U;
                    } else if (previous.kind ==
                                   BattleEventKind::player_ability &&
                               !previous.source_is_monster &&
                               previous.source == event.source &&
                               previous.source < visual.party_count) {
                        // Learned abilities leave 31dd at the common 4338
                        // pose-four phase, while 1138's direct-item entry
                        // exposes only pose zero.  Both retain 31e1/31e3's
                        // selected action anchor.  If 0c41 expires a party
                        // buff as the dispatcher returns, 0da7 recomposes
                        // that exact final pose at the target before
                        // overlaying its compact card.  Clearing to the
                        // ordinary party portrait (or forcing every caller
                        // to pose four) is observably wrong.
                        retained_action = previous;
                        retained_pose =
                            previous.source < commands.size() &&
                                    commands[previous.source].kind ==
                                        PlayerCommandKind::item
                                ? 0U
                                : 4U;
                    } else if (previous.kind == BattleEventKind::skipped &&
                               !previous.source_is_monster &&
                               previous.source == event.source &&
                               previous.source < visual.party_count) {
                        // 0694 sets 31e1 to the incapacitated actor before it
                        // skips command dispatch.  31e3 remains that actor's
                        // own 137a anchor and 31dd is pose zero, so 0da7 still
                        // draws a full fighter action card rather than the
                        // ordinary portrait used by 2bb5.
                        retained_action = previous;
                        retained_action->kind =
                            BattleEventKind::player_attack;
                        retained_action->target_is_monster = false;
                        retained_action->target = event.source;
                        retained_action->action_anchor_is_target = false;
                        retained_pose = 0U;
                    } else if (previous.kind ==
                                   BattleEventKind::escape_failed &&
                               !previous.source_is_monster &&
                               previous.source == event.source &&
                               previous.source < visual.party_count) {
                        // 09b8's failed ordinary escape leaves 31dd on the
                        // second escape pose (frame five) when it rejoins
                        // 0c41.  A timer expiring on that turn is therefore
                        // drawn by 0da7 over the same back-facing fighter,
                        // not over the ordinary bottom portrait.
                        retained_action = previous;
                        retained_pose = fig_player_escape_poses()[1];
                    } else if (previous.kind ==
                                   BattleEventKind::capture_failed &&
                               !previous.source_is_monster &&
                               previous.source == event.source &&
                               previous.source < visual.party_count) {
                        // 0e83 returns from a failed capture with 31dd still
                        // on pose zero and 31e3 still anchored to the selected
                        // monster. The following 0da7 expiry must retain that
                        // exact capture page until 0d98 runs.
                        retained_action = previous;
                        retained_pose = 0U;
                    } else if (previous.kind ==
                                   BattleEventKind::monster_captured &&
                               !previous.source_is_monster &&
                               previous.source == event.source &&
                               previous.source < visual.party_count) {
                        // Successful 0ded returns through 0e84 with the same
                        // target-anchored pose zero, after removing the
                        // monster and holding that clean action page for nine
                        // ticks. 0c41/0da7 retains it for a same-turn expiry.
                        retained_action = previous;
                        retained_pose = 0U;
                    } else if (previous.kind ==
                                   BattleEventKind::missing_medium &&
                               !previous.source_is_monster &&
                               !previous.source_is_summoned_ally &&
                               previous.source == event.source &&
                               previous.source < visual.party_count) {
                        // 58fa returns without changing the common player
                        // action globals. Learned callers therefore retain
                        // pose four, while 1138's direct-item caller retains
                        // pose zero, at the same selected action anchor.
                        retained_action = previous;
                        retained_action->kind =
                            BattleEventKind::player_ability;
                        retained_pose =
                            previous.source < commands.size() &&
                                    commands[previous.source].kind ==
                                        PlayerCommandKind::item
                                ? 0U
                                : 4U;
                    }
                }
                const auto present_player_expiry_card = [&](std::span<const std::uint8_t> text) {
                    if (retained_action && retained_pose) {
                        auto frame = compose_event_frame(
                            context, base_surface, encounter, items, fighters,
                            menu_sprites, font, fallback, visual,
                            *retained_action, *retained_pose, {}, std::nullopt,
                            encounter_directory_offset);
                        const auto [target_x, target_y] = event_target_center(
                            encounter, items, context.game_root, visual,
                            *retained_action, fighters);
                        static_cast<void>(target_y);
                        const auto action_anchor =
                            retained_action->action_anchor_is_target
                                ? (retained_action->target_is_monster
                                       ? target_x / 4 - 3
                                       : 12 + static_cast<int>(
                                                  retained_action->target) *
                                                  18)
                                : 12 + static_cast<int>(
                                           retained_action->source) * 18;
                        auto placement = fig_fighter_placement(
                            visual.party[retained_action->source].identity,
                            visual.party[retained_action->source].party_index,
                            *retained_pose, fighters.sprites().size());
                        const auto normal_anchor =
                            12 + static_cast<int>(
                                     visual.party[retained_action->source]
                                         .party_index) *
                                     18;
                        placement.left +=
                            (action_anchor - normal_anchor) * 4;
                        auto panel_column = placement.left / 4 - 8;
                        // 0dbb performs an unsigned bound check after the
                        // subtraction, folding both underflow and >=100 back
                        // to column zero.
                        if (panel_column < 0 || panel_column >= 100) {
                            panel_column = 0;
                        }
                        draw_message_panel(
                            frame, menu_sprites, panel_column, 120, 4);
                        draw_big5(frame, font, fallback, text,
                                  (panel_column + 2) * 4, 129, 0x00);
                        present_battle_surface(context, frame);
                        return;
                    }
                    present_player_status_card(
                        context, base_surface, encounter, items, menu_sprites,
                        font, fallback, visual, event, text,
                        encounter_directory_offset, 4, 0x00, 0, 2);
                };
                for (std::size_t slot = 0; slot < 5U; ++slot) {
                    if ((event.expired_player_buff_mask & (1U << slot)) == 0) {
                        continue;
                    }
                    auto visual_expiry = event;
                    visual_expiry.expired_player_buff_mask =
                        static_cast<std::uint8_t>(1U << slot);
                    visual_expiry.recovered_player_status_mask = 0;
                    apply_visual_event(visual, visual_expiry, abilities);
                    present_player_expiry_card(
                        abilities.player_removed_buff_text(slot));
                    if (!delay(status_card_delay)) return false;
                }
                for (std::size_t slot = 0; slot < 4U; ++slot) {
                    if ((event.recovered_player_status_mask & (1U << slot)) == 0) {
                        continue;
                    }
                    auto visual_recovery = event;
                    visual_recovery.expired_player_buff_mask = 0;
                    visual_recovery.recovered_player_status_mask =
                        static_cast<std::uint8_t>(1U << slot);
                    apply_visual_event(visual, visual_recovery, abilities);
                    present_player_expiry_card(
                        abilities.recovered_player_status_text(slot));
                    if (!delay(status_card_delay)) return false;
                }
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(ward_card_delay)) return false;
            }
            continue;
        }
        if (event.kind == BattleEventKind::monster_attack) {
            // FIG 296a begins every physical enemy action with 2b08's
            // two-column "攻 擊" card at x=center-8/y=50. The initiative
            // loop's preceding bare page is owned by the path that rejoins
            // this action rather than by 296a itself.
            auto attacker_event = event;
            attacker_event.target_is_monster = true;
            attacker_event.target = event.source;
            present_monster_compact_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, attacker_event,
                abilities.monster_attack_text(),
                encounter_directory_offset, 2, 0x00, 8);
            if (!delay(monster_action_card_delay)) return false;
            if (event.damage == 0 && !event.evaded) {
                // Physical immunity or the retry-zero branch redraws that
                // card and overlays a second colour-6b "失 敗" card below.
                present_monster_attack_failure_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, attacker_event,
                    abilities.monster_attack_text(),
                    abilities.physical_failure_text(),
                    encounter_directory_offset);
                if (!delay(monster_action_card_delay)) return false;
                if (finish_monster_action_here) {
                    if (!present_monster_turn_tail(event)) return false;
                }
                continue;
            }
            if (event.evaded) {
                // The target's successful 12-evasion roll enters the bottom
                // two-column "閃 躲" card and never reaches damage animation.
                present_player_status_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event, abilities.evasion_text(),
                    encounter_directory_offset, 2, 0x6b);
                if (!delay(immunity_card_delay)) return false;
                if (finish_monster_action_here) {
                    if (!present_monster_turn_tail(event)) return false;
                }
                continue;
            }
        }
        if (action_first &&
            (event.kind == BattleEventKind::ally_attack ||
             event.kind == BattleEventKind::ally_ability ||
             event.kind == BattleEventKind::ally_fled)) {
            if (event.kind == BattleEventKind::ally_attack) {
                // The physical path enters through the initiative loop's
                // bare 2db8 page before 10fc installs the fixed ally action
                // card.  Keeping this page after the card reverses two
                // observable flips in the original zero-damage sequence.
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
            }
            auto text = abilities.monster_attack_text();
            auto color = std::uint8_t{0x00};
            if (event.kind == BattleEventKind::ally_ability) {
                text = abilities.summoned_ally_ability_text();
            } else if (event.kind == BattleEventKind::ally_fled) {
                text = abilities.summoned_ally_flee_text();
                color = 0x6b;
            }
            present_summoned_ally_action_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, text,
                encounter_directory_offset, color);
            if (!delay(summoned_action_card_delay)) return false;
        }
        if (event.kind == BattleEventKind::ally_fled) {
            // 0eef..0f18 starts SV3 immediately after the nine-tick colour-6b
            // card, removes/packs the ally, then enters 2db8's bare battlefield
            // redraw before waiting four ticks.  It neither leaves the player
            // cards on that page nor inserts the generic three-tick pre-removal
            // page or attack/ability five-tick epilogue.
            for (const auto& cue : fig_non_effect_voice_cues(event)) {
                if (cue.timing == FigVoiceTiming::before_action) {
                    play_voice_cue(context, cue);
                }
            }
            apply_visual_event(visual, event, abilities);
            present_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset,
                std::nullopt, false, false);
            if (!delay(monster_action_card_delay)) return false;
            continue;
        }
        if (event.kind == BattleEventKind::ally_attack && event.evaded) {
            // After 0fce's unconditional SP106, the successful evasion roll
            // tail-jumps to 2a28. That routine leaves the two-column colour-6b
            // “閃躲” card visible for eight ticks and RETs from the entire
            // ally action, bypassing 1039's clean/five-tick epilogue. 2a28
            // recomposes from bare 2db8 and never calls 2bb5, so this page also
            // omits the bottom commandable-party cards.
            for (const auto& cue : fig_non_effect_voice_cues(event)) {
                if (cue.timing == FigVoiceTiming::before_action) {
                    play_voice_cue(context, cue);
                }
            }
            present_monster_compact_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, abilities.evasion_text(),
                encounter_directory_offset, 2, 0x6b, 8, nullptr, false);
            if (!delay(immunity_card_delay)) return false;
            continue;
        }
        const auto monster_named_action = action_first &&
            (event.kind == BattleEventKind::monster_ability ||
             event.kind == BattleEventKind::monster_heal);
        const auto monster_generic_single_target =
            monster_named_action && event.monster_generic_path &&
            event.ability_id < abilities.abilities().size() &&
            (abilities.ability(event.ability_id).target_flags & 0x2000U) != 0;
        const auto monster_special_player_status =
            monster_named_action &&
            event.kind == BattleEventKind::monster_ability &&
            !event.monster_generic_path && !event.target_is_monster &&
            event.status_duration != 0;
        const auto monster_special_player_resistance =
            monster_named_action &&
            event.kind == BattleEventKind::monster_ability &&
            !event.monster_generic_path && !event.target_is_monster &&
            event.resisted &&
            (event.effect_code == 0x5e || event.effect_code == 0x64);
        const auto monster_special_self_buff =
            monster_named_action &&
            event.kind == BattleEventKind::monster_ability &&
            !event.monster_generic_path &&
            (event.effect_code == 0x67 || event.effect_code == 0x68 ||
             event.effect_code == 0x69);
        if (monster_named_action) {
            if (event.kind == BattleEventKind::monster_heal ||
                monster_generic_single_target ||
                monster_special_player_status ||
                monster_special_player_resistance ||
                monster_special_self_buff) {
                // 20e7 begins every enemy turn by flipping a bare 2db8 page
                // and copying it to scratch before either the low-HP decision
                // or either single-target dispatcher. 2485's all-party path
                // has already installed the four cards at this point.
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
            }
            if (monster_special_player_status ||
                monster_special_player_resistance) {
                // 26f3 (successful status) or 2721 (resisted status) switches
                // to the other page, runs 2bb5 for the selected party card,
                // and switches back. On success 262f immediately overlays the
                // ability name; on resistance the card remains by itself.
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset);
            }
            if (!monster_special_player_resistance) {
                present_monster_ability_name_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event, abilities,
                    encounter_directory_offset,
                    event.kind != BattleEventKind::monster_heal &&
                        !monster_generic_single_target &&
                        !monster_special_self_buff);
            }
        }
        const auto player_identity =
            event.kind == BattleEventKind::player_attack &&
                    event.source < visual.party_count
                ? std::optional<std::uint16_t>(visual.party[event.source].identity)
                : std::nullopt;
        const auto non_effect_voices =
            fig_non_effect_voice_cues(event, player_identity);
        for (const auto& cue : non_effect_voices) {
            if (cue.timing == FigVoiceTiming::before_action) {
                play_voice_cue(context, cue);
            }
        }
        const auto effect_code = event_effect_code(event, abilities);
        const auto dispatcher_escape_action =
            action_first && event.ability_id != 0 && effect_code == 0x47 &&
            (event.kind == BattleEventKind::player_escaped ||
             event.kind == BattleEventKind::escape_failed);
        const auto player_dispatcher_action =
            effect_first && effect_code.has_value() &&
            (event.kind == BattleEventKind::player_ability ||
             dispatcher_escape_action);
        const auto item_dispatcher_action =
            player_dispatcher_action && event.source < commands.size() &&
            commands[event.source].kind == PlayerCommandKind::item;
        const auto dispatcher_effect_pose =
            item_dispatcher_action ? std::size_t{0} : std::size_t{4};
        if (effect_code && effect_first &&
            !monster_special_player_resistance) {
            const auto voice = monster_named_action
                                   ? fig_monster_ability_voice_resource(
                                         event.ability_id, *effect_code)
                                   : fig_effect_voice_resource(*effect_code);
            if (voice && !dispatcher_escape_action &&
                !player_dispatcher_action) {
                const auto path = effect_voice_path(context.game_root, *voice);
                if (std::filesystem::exists(path)) {
                    // FIG 5b37 edits the SP000 template and starts VOC playback
                    // immediately before entering the selected visual handler.
                    play_battle_voice(context, path);
                }
            }
        }
        if (monster_named_action && !monster_special_player_resistance) {
            if (!delay(fig_timer_ticks(7))) return false;
            if (event.monster_generic_path) {
                // 2464 first flips the bare scratch page that 262f restored;
                // the single-target path then composes its selected card page.
                // 2485's all-target path already has all cards and exposes no
                // corresponding bare flip.
                if (monster_generic_single_target) {
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false,
                        false);
                }
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, std::nullopt,
                    {}, std::nullopt, encounter_directory_offset);
            }
        }
        if (event.kind == BattleEventKind::monster_heal) {
            // 26af's self-heal branch commits HP immediately after 262f and
            // returns through 2938. It never enters 2ac7/144e, so there is no
            // floating healing number. Both the name card and 22e0 cleanup
            // inherit the bare 2db8 battlefield; party cards reappear only
            // when the next player action composes 137a.
            apply_visual_event(visual, event, abilities);
            if (finish_monster_action_here) {
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        if (monster_special_player_resistance) {
            // Effects 5eh/64h compare the selected actor's third resistance
            // before entering 26f3. On immunity they jump directly to 2721,
            // which exposes only the selected-party card prepared by 2bb5;
            // 262f's name card, voice, status write and seven-tick hold are all
            // skipped. The common 22e0 tail retains that card for five ticks,
            // then replaces it with a bare battlefield for three ticks.
            if (finish_monster_action_here) {
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        if (monster_special_self_buff) {
            // 27ee/281a's successful 67h..69h branches call 262f directly on
            // the bare page installed by 20e7, update only the monster's
            // hidden runtime counter/stat, and RET without another redraw.
            // The common 22e0 tail therefore retains the bare name card for
            // five ticks, then exposes a genuinely bare battlefield for three.
            if (finish_monster_action_here) {
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        if (monster_special_player_status) {
            // After 262f's seven ticks, 2707..272a commit the timer/status bit
            // and call 2bb5 on the opposite page without erasing the ability
            // panel. Thus the status icon appears while the name card remains
            // visible. 22e0 then holds that page five ticks and replaces it
            // with a genuinely bare 2db8 page for the final three ticks.
            apply_visual_event(visual, event, abilities);
            present_monster_ability_name_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, abilities,
                encounter_directory_offset, true);
            if (finish_monster_action_here) {
                if (!delay(ward_card_delay)) return false;
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                if (!delay(action_delay)) return false;
            }
            continue;
        }
        const auto monster_side_dispatcher = event.source_is_monster &&
            (event.kind == BattleEventKind::monster_ability ||
             event.kind == BattleEventKind::monster_heal);
        const auto effect_frames = effect_first && !monster_side_dispatcher
                                       ? load_event_effect_frames(
                                             event, abilities, context.game_root,
                                             effect_cache)
                                       : std::vector<LoadedBattleEffectStep>{};
        const auto dispatcher_event = effect_code.has_value() &&
            (event.kind == BattleEventKind::player_ability ||
             event.kind == BattleEventKind::monster_ability ||
             event.kind == BattleEventKind::monster_heal ||
             event.kind == BattleEventKind::ally_ability);
        const auto monster_all_target_action =
            event.monster_generic_path &&
            event.kind == BattleEventKind::monster_ability &&
            !event.target_is_monster &&
            event.ability_id < abilities.abilities().size() &&
            (abilities.ability(event.ability_id).target_flags & 0x2000U) == 0;
        const auto delay_monster_all_target_slots = [&]() -> bool {
            if (!monster_all_target_action) return true;
            std::optional<std::size_t> next_target;
            if (event_index + 1U < result.events.size() &&
                fig_same_effect_phase(event, result.events[event_index + 1U])) {
                next_target = result.events[event_index + 1U].target;
            }
            const auto slots = fig_all_target_slot_span(
                event.target, next_target, visual.party_count);
            return delay(action_delay * static_cast<int>(slots));
        };
        const auto dispatcher_immediate_return =
            effect_first && dispatcher_event &&
            fig_selector_is_immediate_return(*effect_code) &&
            event.kind != BattleEventKind::monster_heal;
        std::array<std::size_t, 3> poses{};
        std::size_t pose_count = 0;
        std::optional<BattleSurface> retained_dispatcher_pose_surface;
        const auto normal_escape_action =
            action_first && event.ability_id == 0 &&
            (event.kind == BattleEventKind::player_escaped ||
             event.kind == BattleEventKind::escape_failed);
        if (action_first && event.kind == BattleEventKind::player_attack) {
            poses = {0, 1, event.critical ? 3U : 2U};
            pose_count = 3;
        } else if (normal_escape_action) {
            const auto escape_poses = fig_player_escape_poses();
            poses = {escape_poses[0], escape_poses[1], escape_poses[1]};
            pose_count = 2;
        } else if (action_first && player_actor_event(event.kind)) {
            const auto ability_poses = fig_player_ability_poses();
            poses = {ability_poses[0], ability_poses[1], ability_poses[1]};
            // 1138's direct-item path calls 2db8/137a exactly once, saves
            // that pose-0 page with 3c15 and waits three ticks before the
            // selector. Learned abilities instead enter 4338 and expose the
            // separate pose0(3) -> pose4(3) pair.
            pose_count = item_dispatcher_action ? 1U : 2U;
        }
        std::size_t effect_cursor = 0;
        if (pose_count == 0 && effect_frames.empty() && !dispatcher_event) {
            if (event.kind == BattleEventKind::status_damage &&
                event.target_is_monster) {
                // 0b94 enters the periodic slot-two handler through bare
                // 2db8. Unlike ordinary actions, this clean preparation page
                // does not reinstall 2bb5's bottom party cards.
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
            } else if (event.kind == BattleEventKind::ally_attack) {
                // The captured-ally physical path already exposed its bare
                // 2db8 preparation immediately before the 10fc action card.
            } else {
                present_event_frame(context, base_surface, encounter, items,
                                    fighters, menu_sprites, font, fallback,
                                    visual, event, std::nullopt, {},
                                    std::nullopt,
                                    encounter_directory_offset);
            }
            if (event.kind != BattleEventKind::monster_attack &&
                event.kind != BattleEventKind::ally_attack &&
                event.kind != BattleEventKind::status_damage) {
                if (!delay(action_delay)) return false;
            }
        } else {
            for (std::size_t phase = 0; phase < pose_count; ++phase) {
                // 4338 finishes both player fighter poses, then 436a starts
                // the voice and only then jumps through DS:2bbd. Never spend
                // the first two effect frames underneath the pose setup.
                const auto layers = !player_dispatcher_action &&
                                            effect_cursor < effect_frames.size()
                                        ? std::span<const LoadedBattleEffectLayer>(
                                              effect_frames[effect_cursor].layers)
                                        : std::span<const LoadedBattleEffectLayer>{};
                auto pose_surface = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual,
                    event, poses[phase], layers, std::nullopt,
                    encounter_directory_offset);
                present_battle_surface(context, pose_surface);
                if (player_dispatcher_action && phase + 1U == pose_count) {
                    retained_dispatcher_pose_surface = std::move(pose_surface);
                }
                if (!layers.empty()) ++effect_cursor;
                if (phase == 0) {
                    for (const auto& cue : non_effect_voices) {
                        if (cue.timing == FigVoiceTiming::after_first_pose) {
                            play_voice_cue(context, cue);
                        }
                    }
                }
                auto pose_delay = action_delay;
                if (event.kind == BattleEventKind::player_attack && phase != 0) {
                    pose_delay = effect_delay;
                }
                if (!delay(pose_delay)) return false;
            }
        }
        for (const auto& cue : non_effect_voices) {
            if (cue.timing == FigVoiceTiming::after_pose) {
                play_voice_cue(context, cue);
            }
        }
        const auto retained_immunity_handler =
            player_dispatcher_action &&
            event.block_reason == AbilityBlockReason::resistance &&
            event.target_is_monster && effect_code && *effect_code > 0x30;
        const auto retained_monster_status_handler =
            player_dispatcher_action &&
            event.block_reason == AbilityBlockReason::none &&
            event.target_is_monster && event.status_duration != 0 &&
            effect_code && *effect_code > 0x30;
        const auto retained_monster_damage_handler =
            player_dispatcher_action &&
            event.block_reason == AbilityBlockReason::none &&
            event.target_is_monster && event.status_duration == 0 &&
            (event.damage != 0 || event.healing != 0) &&
            effect_code && *effect_code > 0x30;
        const auto retained_ally_ability_damage_handler =
            event.kind == BattleEventKind::ally_ability &&
            event.block_reason == AbilityBlockReason::none &&
            event.target_is_monster && event.status_duration == 0 &&
            (event.damage != 0 || event.healing != 0) &&
            effect_code && *effect_code > 0x30;
        std::optional<std::uint16_t> retained_damage_target_flags;
        if (retained_monster_damage_handler ||
            retained_ally_ability_damage_handler) {
            if (const auto canonical =
                    fig_player_effect_canonical_ability(event.effect_code);
                canonical && *canonical < abilities.abilities().size()) {
                retained_damage_target_flags =
                    abilities.ability(*canonical).target_flags;
            }
        }
        const auto status_text = abilities.player_status_text(event.effect_code);
        const auto retained_player_status_handler =
            player_dispatcher_action &&
            event.block_reason == AbilityBlockReason::none &&
            !event.target_is_monster && !status_text.empty() &&
            fig_effect_leaves_player_status_card(event.effect_code) &&
            effect_code && *effect_code > 0x30;
        const auto retained_player_barrier_handler =
            player_dispatcher_action &&
            event.block_reason == AbilityBlockReason::none &&
            !event.target_is_monster && effect_code && *effect_code == 0x62;
        const auto retained_player_dispel_handler =
            player_dispatcher_action &&
            event.block_reason == AbilityBlockReason::none &&
            event.target_is_monster && effect_code && *effect_code == 0x61;
        const auto retained_player_support_handler =
            player_dispatcher_action && event.resulting_player_support_state &&
            effect_code && *effect_code <= 0x30;
        const auto retained_fading_handler =
            retained_immunity_handler || retained_monster_status_handler ||
            retained_player_status_handler || retained_player_barrier_handler ||
            retained_player_dispel_handler || retained_monster_damage_handler;
        if (action_first && !retained_fading_handler &&
            !retained_player_support_handler) {
            // Non-fading learned handlers expose the paid pool after the
            // common pose0/pose4 pages. The verified 43ce status/immunity
            // handlers and 447b..478f support selectors instead retain their
            // pre-debit page through the handler and are charged explicitly
            // after it below.
            present_player_resource_cost(event);
        }
        if (player_dispatcher_action) {
            if (const auto voice = fig_effect_voice_resource(*effect_code)) {
                const auto path = effect_voice_path(context.game_root, *voice);
                if (std::filesystem::exists(path)) {
                    // 436a selects SP001/special/effect voice only after the
                    // common base->pose4 pair and immediately before jumping
                    // through DS:2bbd to the selected effect handler.
                    play_battle_voice(context, path);
                }
            }
        }
        std::optional<std::array<std::uint8_t, 768>>
            dispatcher_palette_override;
        if (retained_fading_handler && retained_dispatcher_pose_surface) {
            auto faded = *retained_dispatcher_pose_surface;
            for (auto step = 0; step < 5; ++step) {
                darken_fig_dispatcher_palette(faded);
                present_battle_surface(context, faded);
                if (!delay(effect_delay)) return false;
            }
            dispatcher_palette_override = faded.palette;
        }
        if (dispatcher_escape_action) {
            // 4ef3 is just the common actor setup, SP071, five ticks, and a
            // four-byte stack discard into the battle-exit/turn-loop path.
            // It has no SV3 sample, effect archive, result frame, or failure
            // panel of its own.
            if (!delay(ward_card_delay)) return false;
            continue;
        }
        if (normal_escape_action) {
            if (event.kind == BattleEventKind::escape_failed) {
                present_player_escape_failure_card(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, abilities,
                    encounter_directory_offset);
                if (!delay(ward_card_delay)) return false; // five ticks

                // 09b8's failure branch rejoins 0c41 rather than returning
                // from FIG. With no expiring player status, 0d98 still
                // executes a bare 2db8 page flip and holds it for five ticks
                // before the initiative loop chooses the next actor. This is
                // observable between the failure card and the following
                // monster action; do not jump directly from one card to the
                // other or retain the bottom party cards on the boundary.
                const auto expires_here =
                    event_index + 1U < result.events.size() &&
                    result.events[event_index + 1U].kind ==
                        BattleEventKind::status_expired &&
                    !result.events[event_index + 1U].target_is_monster &&
                    result.events[event_index + 1U].source == event.source;
                if (!expires_here) {
                    const auto clean = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false,
                        false);
                    present_battle_surface(context, clean);
                    if (!delay(ward_card_delay)) return false;
                }
            }
            continue;
        }
        if (event.monster_generic_path && effect_first &&
            event.ability_id < abilities.abilities().size()) {
            // 25ee fills the non-visible page with one resistance-class colour
            // and flips eight times without redrawing either page. The result
            // is solid/clean alternating at one tick per step, starting solid
            // and ending clean. Generic monster magic never runs the player's
            // DS:2bbd SP/ST selector timeline.
            const auto clean_frame = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset);
            auto solid_frame = clean_frame;
            std::fill(solid_frame.pixels.begin(), solid_frame.pixels.end(),
                      fig_monster_ability_flash_color(
                          abilities.ability(event.ability_id).target_flags));
            for (std::uint16_t step = 0;
                 step < fig_monster_ability_flash_steps(); ++step) {
                present_battle_surface(
                    context, (step & 1U) == 0 ? solid_frame : clean_frame);
                if (!delay(effect_delay)) return false;
            }
        }
        std::optional<BattleSurface> retained_player_attack_surface;
        if (action_first && event.kind == BattleEventKind::player_attack &&
            event.source < visual.party_count && pose_count != 0) {
            // 14b1 follows the three 13e8 fighter poses. Each 152a/155c hand
            // pass first composes SW### (left hand mirrored), then rebuilds a
            // clean page after assigning +3114=31db so a successful hit shows
            // exactly one target reaction frame per weapon. Both calls use
            // 3c44's four 50-line, one-tick top-down page-copy steps rather
            // than a static three-tick hold. SW000 is the real unarmed archive.
            // 13e8 has just called 14a3 with DATA:2f6a, replacing any older
            // E0h..E4h order before the first weapon pass begins.
            existing_result_palette_shift = 0;
            const auto weapon_pose = poses[pose_count - 1U];
            auto prior_weapon_frame = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, weapon_pose, {},
                std::nullopt, encounter_directory_offset);
            for (const auto& animation :
                 fig_weapon_animations(visual.party[event.source])) {
                const auto overlay_frame = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, weapon_pose,
                    {}, std::nullopt, encounter_directory_offset, animation,
                    false);
                if (!present_fig_page_wipe(
                        context, prior_weapon_frame, overlay_frame,
                        effect_delay)) {
                    frontend_abort = true;
                    return false;
                }
                // 3c44 calls 5ed8 after each of its four 50-line copies.
                // Descriptor zero has interval two, so one complete wipe
                // advances the existing E0h..E4h ramp by two colours.
                existing_result_palette_shift =
                    (existing_result_palette_shift + 2U) % 5U;
                const auto reaction_frame = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event, weapon_pose,
                    {}, std::nullopt, encounter_directory_offset, std::nullopt,
                    event.damage != 0 && !event.evaded);
                if (!present_fig_page_wipe(
                        context, overlay_frame, reaction_frame,
                        effect_delay)) {
                    frontend_abort = true;
                    return false;
                }
                existing_result_palette_shift =
                    (existing_result_palette_shift + 2U) % 5U;
                prior_weapon_frame = reaction_frame;
            }
            retained_player_attack_surface = std::move(prior_weapon_frame);
        }
        std::optional<BattleSurface> retained_effect_surface;
        if (retained_player_barrier_handler) {
            // 55fd does not leave the acting fighter in pose 4 underneath
            // SP338.  After 43ce has darkened C0h..DFh it copies the current
            // page to A800, calls 5c4c (which temporarily selects the party
            // target and runs ordinary 2bb5), flips that target-card page,
            // and only then starts the four-frame barrier archive.  This
            // target page is observable before frame zero and is also the
            // scratch background restored between 49c1 iterations.
            retained_effect_surface = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset);
            if (dispatcher_palette_override) {
                retained_effect_surface->palette =
                    *dispatcher_palette_override;
            }
            present_battle_surface(context, *retained_effect_surface);
        }
        while (effect_cursor < effect_frames.size()) {
            const auto& effect = effect_frames[effect_cursor++];
            // 4338/436a leave a learned caster in pose 4 while DS:2bbd
            // renders the selected effect; direct-item entry 1138 instead
            // leaves the same anchor in pose 0. Rebuilding these pages with
            // the ordinary portrait loses both frame 180's action backing and
            // FMAN, and also gives 59a1 the wrong page to retain.
            auto retained_pose = std::optional<std::size_t>{};
            if (player_dispatcher_action &&
                !retained_player_barrier_handler) {
                retained_pose = dispatcher_effect_pose;
            }
            retained_effect_surface = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, retained_pose,
                effect.layers, std::nullopt, encounter_directory_offset,
                std::nullopt, false,
                event.kind != BattleEventKind::ally_ability);
            if (dispatcher_palette_override) {
                retained_effect_surface->palette =
                    *dispatcher_palette_override;
            }
            present_battle_surface(context, *retained_effect_surface);
            if (!delay(effect_delay)) return false;
            if (retained_player_dispel_handler && effect_cursor == 5U) {
                // 55ca runs the five SP336 pages first, restores 3c15's
                // dark pose-4 scratch once, and only then loads SP337 for
                // its final three pages.  The intervening page flip has no
                // explicit delay but is visible in the 70 Hz original
                // capture between SP336 frame 4 and SP337 frame 0.
                auto between_archives = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::optional<std::size_t>{dispatcher_effect_pose}, {},
                    std::nullopt,
                    encounter_directory_offset);
                if (dispatcher_palette_override) {
                    between_archives.palette = *dispatcher_palette_override;
                }
                present_battle_surface(context, between_archives);
            }
        }
        if (retained_player_dispel_handler) {
            // 55ca rebuilds one clean dark pose-4 page after SP336/SP337 and
            // before examining +3461/+3475/+3489.  Each non-zero counter is
            // then cleared and reported by 55e4 on a fresh copy of that page;
            // with no counters set this clean page is the handler's last one.
            auto dispel_surface = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::optional<std::size_t>{dispatcher_effect_pose}, {},
                std::nullopt,
                encounter_directory_offset);
            if (dispatcher_palette_override) {
                dispel_surface.palette = *dispatcher_palette_override;
            }
            if (event.removed_monster_buff_mask == 0) {
                // With no reportable counter, 55ca flips the prepared clean
                // page itself.  Otherwise 55e4 adds the first compact card
                // before that page is exposed; presenting the clean scratch
                // page here would invent an extra visible frame.
                present_battle_surface(context, dispel_surface);
            }

            for (std::size_t slot = 0; slot < 3U; ++slot) {
                if ((event.removed_monster_buff_mask & (1U << slot)) == 0) {
                    continue;
                }
                present_monster_compact_card(
                    context, base_surface, encounter, items, menu_sprites,
                    font, fallback, visual, event,
                    abilities.monster_removed_buff_text(slot),
                    encounter_directory_offset, 4, 0x00, 10,
                    &dispel_surface, true, &dispel_surface);
                if (!delay(status_card_delay)) return false;
            }

            // The selected handler returns with either its final 55e4 card
            // or the clean page still visible.  Only then does 585e debit the
            // pool.  4417 draws pose 4 on the other VGA page, so the visible
            // handler page survives all five C0h..DFh palette restoration
            // steps.  The ordinary player caller finally exposes 0d98's bare
            // battlefield for five ticks before initiative continues.
            apply_visual_event(visual, event, abilities);
            present_player_resource_cost(event);
            for (auto step = 0; step < 5; ++step) {
                brighten_fig_dispatcher_palette(
                    dispel_surface, base_surface.palette);
                present_battle_surface(context, dispel_surface);
                if (!delay(effect_delay)) return false;
            }
            const auto clean = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::nullopt, {}, std::nullopt,
                encounter_directory_offset, std::nullopt, false, false);
            present_battle_surface(context, clean);
            if (!delay(ward_card_delay)) return false;
            continue;
        }
        if (retained_player_status_handler) {
            // 57d6 first draws 2338's compact card on the dark handler page
            // with the ordinary party portrait and holds it for 18 ticks.
            // Once it returns, 585e debits the pool; 4417 calls 137a with
            // DS:31e1 unchanged and restores C0h..DFh in five steps. 4338's
            // learned-ability entry advanced that anchor to pose 4, whereas
            // 1138's direct-item entry deliberately leaves it at pose 0.
            present_player_status_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, status_text,
                encounter_directory_offset, 4, 0x6b, 6, 8, nullptr,
                std::nullopt,
                dispatcher_palette_override
                    ? &*dispatcher_palette_override
                    : nullptr);
            if (!delay(status_card_delay)) return false;
            apply_visual_event(visual, event, abilities);
            present_player_resource_cost(event);
            BattleSurface restored;
            present_player_status_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, status_text,
                encounter_directory_offset, 4, 0x6b, 6, 8, &fighters,
                dispatcher_effect_pose,
                dispatcher_palette_override
                    ? &*dispatcher_palette_override
                    : nullptr,
                &restored);
            for (auto step = 0; step < 5; ++step) {
                brighten_fig_dispatcher_palette(
                    restored, base_surface.palette);
                present_battle_surface(context, restored);
                if (!delay(effect_delay)) return false;
            }

            // 57d6/4417 return to the ordinary player caller, which always
            // runs 0c41. In the no-expiry case, 0d98 replaces the restored
            // pose/status page with a bare 2db8 battlefield and holds it for
            // five ticks. If 0c41 finds a natural expiry, however, 0da7 first
            // overlays that card on this still-live dispatcher pose; 0d98 is
            // reached only after the expiry's eighteen-tick hold.
            const auto expires_here =
                event_index + 1U < result.events.size() &&
                result.events[event_index + 1U].kind ==
                    BattleEventKind::status_expired &&
                !result.events[event_index + 1U].target_is_monster &&
                result.events[event_index + 1U].source == event.source;
            if (!expires_here) {
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
                if (!delay(ward_card_delay)) return false;
            }
            continue;
        }
        if (retained_monster_status_handler) {
            // 5a91 commits the new monster timer before 5b06 rebuilds the
            // action page, so 2deb's persistent icon is already visible on
            // the first clean page after the final SP frame. 585e then debits
            // the resource and 4417 restores C0h..DFh in five palette steps
            // while the same dispatcher-anchor/status page remains selected.
            // 4338's learned-ability entry advances that anchor to pose 4;
            // direct ITEM entry 1138 leaves DS:31e1 at pose 0 instead.
            apply_visual_event(visual, event, abilities);
            auto restored = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::optional<std::size_t>{dispatcher_effect_pose}, {},
                std::nullopt,
                encounter_directory_offset);
            if (dispatcher_palette_override) {
                restored.palette = *dispatcher_palette_override;
            }
            present_battle_surface(context, restored);
            present_player_resource_cost(event);
            restored = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::optional<std::size_t>{dispatcher_effect_pose}, {},
                std::nullopt,
                encounter_directory_offset);
            if (dispatcher_palette_override) {
                restored.palette = *dispatcher_palette_override;
            }
            present_battle_surface(context, restored);
            for (auto step = 0; step < 5; ++step) {
                brighten_fig_dispatcher_palette(
                    restored, base_surface.palette);
                present_battle_surface(context, restored);
                if (!delay(effect_delay)) return false;
            }
            // The direct/learned caller's existing round tail owns the
            // subsequent 0d98 page. Do not add a second bare hold here.
            continue;
        }
        if (event.kind == BattleEventKind::monster_attack &&
            event.damage != 0 && !event.evaded) {
            // 2939 copies 197 scanlines from the displayed clean page at
            // source +0xf0 (three Mode-X rows) into the other page, then flips
            // shifted/clean eight times. The destination page's untouched
            // bottom three rows are the zeroed off-screen tail, not pixels
            // copied from the clean page.
            const auto clean_frame = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset);
            auto shifted_frame = clean_frame;
            constexpr auto shift = fig_monster_attack_shake_scanlines();
            for (auto y = 0; y < 200 - shift; ++y) {
                std::copy_n(
                    clean_frame.pixels.begin() +
                        static_cast<std::ptrdiff_t>((y + shift) * 320),
                    320,
                    shifted_frame.pixels.begin() +
                        static_cast<std::ptrdiff_t>(y * 320));
            }
            std::fill(
                shifted_frame.pixels.begin() +
                    static_cast<std::ptrdiff_t>((200 - shift) * 320),
                shifted_frame.pixels.end(), 0);
            for (std::uint16_t step = 0;
                 step < fig_monster_attack_shake_steps(); ++step) {
                present_battle_surface(
                    context, (step & 1U) == 0 ? shifted_frame : clean_frame);
                if (!delay(effect_delay)) return false;
            }
        }
        if (monster_all_target_action && effect_first && event.target != 0) {
            // 24ca still waits three ticks for each dead slot skipped before
            // the first emitted living-target event.
            if (!delay(action_delay * static_cast<int>(event.target))) {
                return false;
            }
        }
        if (dispatcher_immediate_return &&
            !event.resulting_player_support_state) {
            // Selector zero reaches 4497 immediately after the common actor
            // setup/SP001 path. Entries 28/29 are handled below when their
            // unchanged support snapshots are present.
            if (finish_monster_action_here) {
                if (!present_monster_turn_tail(event)) return false;
            }
            continue;
        }
        if (event.resulting_player_support_state && effect_code &&
            *effect_code <= 0x30) {
            const auto presentation = fig_support_presentation(*effect_code);
            const auto source_ability_points =
                event.source < visual.party_count
                    ? visual.party[event.source].ability_points
                    : 0;
            const auto source_secondary_points =
                event.source < visual.party_count
                    ? visual.party[event.source].secondary_points
                    : 0;
            auto resource_class = std::uint8_t{};
            if (event.source < commands.size()) {
                const auto& command = commands[event.source];
                if (command.kind == PlayerCommandKind::ability &&
                    command.ability_id < abilities.abilities().size()) {
                    resource_class = static_cast<std::uint8_t>(
                        (abilities.ability(command.ability_id).target_flags >>
                         8U) & 0x0fU);
                } else if (command.kind == PlayerCommandKind::item &&
                           event.ability_id >= 0x8cU &&
                           static_cast<std::size_t>(event.ability_id) + 2U <
                               items.entry_count()) {
                    // 1138 turns a type-10 ITEM wrapper into ability
                    // item_id-8c before entering the same 447b..478f
                    // support handler used by a learned ability. Recover the
                    // embedded resource class so its result snapshot stays
                    // on the pre-debit page until the shared 585e return; the
                    // command's ability_id field is the inventory slot here.
                    const auto item = BattleItemDefinition::parse(
                        event.ability_id,
                        items.entry(static_cast<std::size_t>(
                                        event.ability_id) + 2U));
                    const auto embedded_id = static_cast<std::size_t>(
                        event.ability_id - 0x8cU);
                    if (item.type == 0x10U &&
                        embedded_id < abilities.abilities().size()) {
                        // The direct-item caller always pays actor +55 AP,
                        // even when the embedded learned record is class 5.
                        resource_class = 4U;
                    }
                }
            }
            const auto apply_support_visual = [&](const auto& shown) {
                apply_visual_event(visual, shown, abilities);
                if (event.source >= visual.party_count) return;
                // The support result snapshot is captured inside the handler,
                // before 585e debits the selected pool. Keep that charged
                // resource at its pre-action value through the final 58a9
                // page while preserving recovery of every other pool.
                if (resource_class == 1U || resource_class == 4U) {
                    visual.party[event.source].ability_points =
                        source_ability_points;
                } else if (resource_class == 2U || resource_class == 3U) {
                    visual.party[event.source].secondary_points =
                        source_secondary_points;
                }
            };
            const auto present_support_frame =
                [&](const BattleSessionEvent& shown) {
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, shown,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset);
                };
            const auto present_player_action_tail = [&]() -> bool {
                // Every 447b..478f support selector returns through the
                // common 585e debit and then the ordinary player caller's
                // 0c41. With no expiring status, 0d98 flips one bare 2db8
                // battlefield and holds it for five ticks. A same-turn 0da7
                // report instead retains the support caller's action pose and
                // reaches that cleanup only after its eighteen-tick card.
                present_player_resource_cost(event);
                if (same_player_expiry_follows()) return true;
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
                return delay(ward_card_delay);
            };

            if (presentation == FigSupportPresentation::none) {
                // Entries 28/29 point to literal RET instructions. The
                // snapshot is unchanged, but consume it without inventing a
                // 5841 target preview or a result redraw.
                apply_support_visual(event);
                if (!present_player_action_tail()) return false;
                continue;
            }

            present_support_frame(event);
            if (!delay(
                    presentation == FigSupportPresentation::all_targets
                        ? summoned_action_card_delay
                        : action_delay)) {
                return false;
            }

            if (presentation == FigSupportPresentation::all_targets) {
                // 452a/45c7 redraw every party card, wait nine ticks, mutate
                // the whole party in a tight loop, redraw the whole set, and
                // wait nine more ticks. Apply every contiguous target event
                // before presenting the post-state so partial updates cannot
                // leak through an event-driven frontend.
                auto group_end = event_index;
                while (group_end + 1U < result.events.size() &&
                       result.events[group_end + 1U]
                           .resulting_player_support_state &&
                       fig_same_effect_phase(
                           event, result.events[group_end + 1U])) {
                    ++group_end;
                }
                for (auto index = event_index; index <= group_end; ++index) {
                    apply_support_visual(result.events[index]);
                }
                present_support_frame(event);
                if (!delay(summoned_action_card_delay)) return false;
                event_index = group_end;
                if (!present_player_action_tail()) return false;
                continue;
            }

            apply_support_visual(event);
            if (presentation ==
                FigSupportPresentation::commit_without_redraw) {
                // 4728..4765 return immediately after the permanent +3
                // write. The next normal composition observes the new state;
                // this handler itself does not call 58a9 or wait nine ticks.
                if (!present_player_action_tail()) return false;
                continue;
            }
            present_support_frame(event);
            if (!delay(summoned_action_card_delay)) return false;
            if (!present_player_action_tail()) return false;
            continue;
        }
        if (effect_code && fig_effect_tail_hold_ticks(*effect_code) != 0 &&
            event.block_reason == AbilityBlockReason::none &&
            !event.source_is_monster) {
            // 55fd increments +3164 before the visual body, presents four
            // SP338 frames over 5c4c's ordinary target card, and holds the
            // last for four ticks.  The common 585e debit and 4417 palette
            // restoration happen only after that handler returns.  4417
            // draws pose 4 on the opposite page, so the visible page during
            // its five colour steps remains the final barrier frame.  The
            // ordinary player caller then reaches 0c41/0d98 and exposes a
            // bare battlefield for five ticks before initiative advances.
            apply_visual_event(visual, event, abilities);
            if (!delay(monster_action_card_delay)) return false;
            present_player_resource_cost(event);
            if (retained_effect_surface) {
                auto restored = *retained_effect_surface;
                for (auto step = 0; step < 5; ++step) {
                    brighten_fig_dispatcher_palette(
                        restored, base_surface.palette);
                    present_battle_surface(context, restored);
                    if (!delay(effect_delay)) return false;
                }
            }
            const auto clean = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::nullopt, {}, std::nullopt,
                encounter_directory_offset, std::nullopt, false, false);
            present_battle_surface(context, clean);
            if (!delay(ward_card_delay)) return false;
            continue;
        }
        if (event.kind == BattleEventKind::player_attack && event.evaded) {
            // FIG 130d shares 2a28 with the enemy-attack dodge path, but
            // anchors the two-column colour-6b card at monster center-8/y=50.
            present_monster_compact_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, abilities.evasion_text(),
                encounter_directory_offset, 2, 0x6b, 8,
                retained_player_attack_surface
                    ? &*retained_player_attack_surface
                    : nullptr);
            if (!delay(immunity_card_delay)) return false;
            // 130d tail-jumps into 2a28; that card's RET returns from the
            // entire physical handler. There is no zero number or additional
            // clean action frame after a successful monster dodge.
            continue;
        }
        if (event.block_reason == AbilityBlockReason::magic_shield) {
            // FIG 24ed consumes one +3164 charge only after the incoming
            // ability sequence, then plays voice/effect selector 62 (SP338).
            const auto voice = fig_effect_voice_resource(0x62);
            if (voice) {
                const auto path = effect_voice_path(context.game_root, *voice);
                if (std::filesystem::exists(path)) {
                    play_battle_voice(context, path);
                }
            }
            auto shield_event = event;
            shield_event.effect_code = 0x62;
            const auto shield_frames = load_event_effect_frames(
                shield_event, abilities, context.game_root, effect_cache);
            for (const auto& effect : shield_frames) {
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, shield_event, std::nullopt,
                    effect.layers, std::nullopt, encounter_directory_offset);
                if (!delay(effect_delay)) return false;
            }
            // 2513 returns immediately after the fourth 49c1 frame. Unlike
            // learned selector 62, shield consumption has no four-tick tail
            // and does not redraw a clean result page.
            if (!delay_monster_all_target_slots()) return false;
            if (finish_monster_action_here) {
                if (!present_monster_turn_tail(event)) return false;
            }
            continue;
        }
        if (event.block_reason == AbilityBlockReason::magic_ward) {
            // +315c is not consumed. 24ed uses 2332's two-column party card,
            // DATA:2d7b and SP011, and holds the message for five ticks.
            play_voice_cue(context, {FigVoiceFile::sp, 0x0b,
                                     FigVoiceTiming::before_action});
            present_player_status_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, abilities.magic_ward_text(),
                encounter_directory_offset, 2, 0x6b);
            if (!delay(ward_card_delay)) return false;
            // 2512 returns with the ward card still visible.
            if (!delay_monster_all_target_slots()) return false;
            if (finish_monster_action_here) {
                if (!present_monster_turn_tail(event)) return false;
            }
            continue;
        }
        if (event.block_reason == AbilityBlockReason::resistance &&
            event.target_is_monster) {
            // FIG 59a1 reports an immune monster with DATA:2e9b in a compact
            // two-column panel at x=center-8, instead of a floating zero.
            present_monster_compact_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event,
                abilities.monster_immunity_text(),
                encounter_directory_offset, 2, 0x00, 8,
                retained_effect_surface ? &*retained_effect_surface : nullptr);
            if (!delay(immunity_card_delay)) return false;
            if (retained_immunity_handler) {
                // 5b06 finishes 5a91 by replacing the immunity card with a
                // clean dispatcher-anchor page. Only after that handler
                // returns does 585e debit the pool; 4417 then redraws the
                // dispatcher anchor with the paid resource bar before it
                // restores colours C0h..DFh. The anchor is pose 4 after
                // learned entry 4338, but remains pose 0 after direct ITEM
                // entry 1138.
                auto restored = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::optional<std::size_t>{dispatcher_effect_pose}, {},
                    std::nullopt,
                    encounter_directory_offset);
                if (dispatcher_palette_override) {
                    restored.palette = *dispatcher_palette_override;
                }
                present_battle_surface(context, restored);
                present_player_resource_cost(event);
                restored = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::optional<std::size_t>{dispatcher_effect_pose}, {},
                    std::nullopt,
                    encounter_directory_offset);
                if (dispatcher_palette_override) {
                    restored.palette = *dispatcher_palette_override;
                }
                for (auto step = 0; step < 5; ++step) {
                    brighten_fig_dispatcher_palette(
                        restored, base_surface.palette);
                    present_battle_surface(context, restored);
                    if (!delay(effect_delay)) return false;
                }
                // The resisted handler returns through the same 0c41/0d98
                // player tail as a successful status application. Preserve
                // its five-tick bare boundary before the next initiative.
                const auto clean = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    std::nullopt, {}, std::nullopt,
                    encounter_directory_offset, std::nullopt, false, false);
                present_battle_surface(context, clean);
                if (!delay(ward_card_delay)) return false;
                continue;
            }
        }
        if (!status_text.empty() &&
            fig_effect_leaves_player_status_card(event.effect_code) &&
            !event.source_is_monster &&
            !event.target_is_monster) {
            present_player_status_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event, status_text,
                encounter_directory_offset, 4, 0x6b, 6, 8, &fighters, 4);
            if (!delay(status_card_delay)) return false;
            // 57d6 returns with the 2338 status card still visible after its
            // 18 ticks; there is no clean recomposition or three-tick tail.
            apply_visual_event(visual, event, abilities);
            continue;
        }
        // Monster effect 61 walks all six party buff counters in original
        // runtime order and invokes 292c once for each value that was nonzero.
        for (std::size_t slot = 0; slot < 6U; ++slot) {
            if ((event.removed_player_buff_mask & (1U << slot)) == 0) continue;
            present_player_status_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event,
                abilities.player_removed_buff_text(slot),
                encounter_directory_offset);
            if (!delay(status_card_delay)) return false;
        }
        // Player effect 61 similarly calls 55e4 for the monster's ward,
        // attack enhancement and evasion enhancement, preserving that order.
        for (std::size_t slot = 0; slot < 3U; ++slot) {
            if ((event.removed_monster_buff_mask & (1U << slot)) == 0) continue;
            present_monster_compact_card(
                context, base_surface, encounter, items, menu_sprites,
                font, fallback, visual, event,
                abilities.monster_removed_buff_text(slot),
                encounter_directory_offset);
            if (!delay(status_card_delay)) return false;
        }
        if (event.removed_player_buff_mask != 0 ||
            event.removed_monster_buff_mask != 0) {
            // 55e4/292c wait 18 ticks per removed buff and return with the
            // final compact card still visible. Only the no-buff effect-61
            // path ends on the clean page prepared before these loops.
            apply_visual_event(visual, event, abilities);
            if (!delay_monster_all_target_slots()) return false;
            if (finish_monster_action_here) {
                if (!present_monster_turn_tail(event)) return false;
            }
            continue;
        }
        for (const auto& cue : non_effect_voices) {
            if (cue.timing == FigVoiceTiming::after_action) {
                play_voice_cue(context, cue);
            }
        }
        if (event.source_is_monster &&
            event.kind == BattleEventKind::monster_ability &&
            !event.monster_generic_path && event.damage == 0 &&
            event.healing == 0 && event.status_duration == 0 &&
            event.block_reason == AbilityBlockReason::none) {
            // 26af's monster-only buff/visual handlers return on their name or
            // status page. They do not synthesize a zero result or an inner
            // clean frame; the shared 22e0 tail owns the eventual cleanup.
            if (finish_monster_action_here) {
                if (!present_monster_turn_tail(event)) return false;
            }
            continue;
        }
        const auto result_value = event.damage != 0 ? event.damage : event.healing;
        const auto show_resistance_zero =
            event.block_reason == AbilityBlockReason::resistance &&
            !event.target_is_monster;
        const auto show_physical_zero =
            (event.kind == BattleEventKind::player_attack ||
             event.kind == BattleEventKind::ally_attack) &&
            event.damage == 0 && !event.evaded;
        const auto show_monster_status_zero =
            event.kind == BattleEventKind::status_damage &&
            event.target_is_monster && event.damage == 0;
        // FIG 2ac7 updates a party member before drawing its ten floating
        // frames, so the bottom card already shows the new HP. The monster
        // path 144e instead floats the value before applying damage/healing.
        if (result_value != 0 && !event.target_is_monster) {
            apply_visual_event(visual, event, abilities);
        }
        const auto presented_result_number =
            result_value != 0 || show_resistance_zero || show_physical_zero ||
            show_monster_status_zero;
        const auto retained_status_damage_handler =
            event.kind == BattleEventKind::status_damage &&
            event.target_is_monster && presented_result_number;
        const auto retained_ally_physical_handler =
            event.kind == BattleEventKind::ally_attack &&
            event.target_is_monster && !event.evaded &&
            presented_result_number;
        if (presented_result_number) {
            std::array<FigNumberPlacement, 10> placements{};
            if (event.target_is_monster) {
                const auto [target_x, target_y] = event_target_center(
                    encounter, items, context.game_root, visual, event, fighters);
                placements = fig_monster_number_timeline(
                    target_x / 4, target_y, result_value);
            } else {
                placements = fig_party_number_timeline(event.target);
            }
            if (retained_monster_damage_handler ||
                retained_ally_ability_damage_handler ||
                retained_status_damage_handler ||
                retained_ally_physical_handler) {
                // 144e first performs one complete 2db8/137a/flip and saves
                // that page in A800h before drawing any digits. It has no
                // timer wait, but is a real visible page and the immutable
                // indexed source copied for all ten number pages. Player
                // Effects and periodic damage set +3114 for a reaction sprite.
                // Captured-ally 1010 sets the same word before entering 144e
                // whenever attack exceeds defence, so its nonzero physical
                // result must retain that reaction sprite as well.
                auto reaction_frame = compose_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    retained_monster_damage_handler
                        ? std::optional<std::size_t>{dispatcher_effect_pose}
                        : std::nullopt,
                    {}, std::nullopt,
                    encounter_directory_offset, std::nullopt,
                    !retained_ally_physical_handler || event.damage != 0,
                    !retained_status_damage_handler &&
                        !retained_ally_physical_handler &&
                        !retained_ally_ability_damage_handler);
                if (dispatcher_palette_override) {
                    reaction_frame.palette = *dispatcher_palette_override;
                }
                if (retained_damage_target_flags) {
                    apply_fig_monster_result_palette(
                        reaction_frame.palette,
                        *retained_damage_target_flags, 0);
                } else if (retained_status_damage_handler ||
                           retained_ally_physical_handler) {
                    rotate_fig_existing_result_palette(
                        reaction_frame.palette, std::nullopt,
                        existing_result_palette_shift);
                }
                present_battle_surface(context, reaction_frame);
            }
            for (std::size_t page_index = 0;
                 page_index < placements.size(); ++page_index) {
                const auto& placement = placements[page_index];
                if (retained_monster_damage_handler ||
                    retained_ally_ability_damage_handler ||
                    retained_status_damage_handler ||
                    retained_ally_physical_handler) {
                    // 144e runs inside the >30h dispatcher handler, before
                    // 585e charges the selected resource.  A800h preserves
                    // the one-shot reaction sprite on every number page;
                    // 5ed8 rotates its E0h..E4h colour ramp every second
                    // flip while the dispatcher action anchor and 43ce's
                    // dark C0h..DFh survive. Learned actions retain pose 4;
                    // direct items retain the pose-0 page left by 1138.
                    auto number_frame = compose_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        retained_monster_damage_handler
                            ? std::optional<std::size_t>{dispatcher_effect_pose}
                            : std::nullopt,
                        {}, placement,
                        encounter_directory_offset, std::nullopt,
                        !retained_ally_physical_handler || event.damage != 0,
                        !retained_status_damage_handler &&
                            !retained_ally_physical_handler &&
                            !retained_ally_ability_damage_handler);
                    if (dispatcher_palette_override) {
                        number_frame.palette = *dispatcher_palette_override;
                    }
                    if (retained_damage_target_flags) {
                        apply_fig_monster_result_palette(
                            number_frame.palette,
                            *retained_damage_target_flags, page_index);
                    } else if (retained_status_damage_handler ||
                               retained_ally_physical_handler) {
                        rotate_fig_existing_result_palette(
                            number_frame.palette, page_index,
                            existing_result_palette_shift);
                    }
                    present_battle_surface(context, number_frame);
                } else {
                    auto result_pose = std::optional<std::size_t>{};
                    if (event.kind == BattleEventKind::player_attack &&
                        pose_count != 0) {
                        result_pose = poses[pose_count - 1U];
                    }
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        result_pose, {}, placement,
                        encounter_directory_offset);
                }
                if (!delay(effect_delay)) return false;
            }
            if (retained_monster_damage_handler &&
                dispatcher_palette_override && retained_damage_target_flags) {
                // The fifth two-page rotation returns E0h..E4h to the table
                // installed by 14a3, which remains active for the clean page
                // and the subsequent C0h..DFh restoration.
                apply_fig_monster_result_palette(
                    *dispatcher_palette_override,
                    *retained_damage_target_flags, placements.size() - 1U);
            }
        }
        if (retained_monster_damage_handler) {
            // The selected 144e handler commits monster HP and leaves one
            // clean, dark dispatcher-anchor page. The common 585e/4417
            // epilogue then debits the pool and restores C0h..DFh in five
            // steps without dropping the action anchor between those pages.
            apply_visual_event(visual, event, abilities);
            auto restored = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::optional<std::size_t>{dispatcher_effect_pose}, {},
                std::nullopt,
                encounter_directory_offset);
            if (dispatcher_palette_override) {
                restored.palette = *dispatcher_palette_override;
            }
            present_battle_surface(context, restored);
            present_player_resource_cost(event);
            restored = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event,
                std::optional<std::size_t>{dispatcher_effect_pose}, {},
                std::nullopt,
                encounter_directory_offset);
            if (dispatcher_palette_override) {
                restored.palette = *dispatcher_palette_override;
            }
            present_battle_surface(context, restored);
            for (auto step = 0; step < 5; ++step) {
                brighten_fig_dispatcher_palette(
                    restored, base_surface.palette);
                present_battle_surface(context, restored);
                if (!delay(effect_delay)) return false;
            }
            continue;
        }
        if (retained_ally_physical_handler) {
            // 1020 reuses 144e's retained normal-monster page and ten floating
            // number pages, then returns to 1039. The scope tail owns the
            // single bare clean page and its five-tick hold.
            apply_visual_event(visual, event, abilities);
            continue;
        }
        if (retained_ally_ability_damage_handler) {
            // Captured allies enter the same player-side effect selector but
            // return through 0fb9/1039 rather than the learned-action 585e
            // payment/restoration envelope. 144e still keeps its one-shot
            // reaction scratch and mapped E0h..E4h result palette for every
            // rising number; the scope tail owns the subsequent bare page.
            apply_visual_event(visual, event, abilities);
            continue;
        }
        if (!event.target_is_monster && presented_result_number &&
            (dispatcher_event ||
             event.kind == BattleEventKind::monster_attack)) {
            // 2ac7 returns with its tenth number page still visible. The
            // single-target caller returns immediately; 24ca's all-target
            // caller holds that same page three ticks per processed/skipped
            // party slot rather than replacing it with a clean page.
            if (result_value == 0) {
                apply_visual_event(visual, event, abilities);
            }
            if (!delay_monster_all_target_slots()) return false;
            if (finish_monster_action_here) {
                if (!present_monster_turn_tail(event)) return false;
            }
            continue;
        }
        if (event.kind == BattleEventKind::player_attack) {
            // 1358 returns from 144e with its last floating-number page still
            // visible, waits five ticks, then commits HP and performs the
            // clean 2db8/137a redraw. The common three-tick post-frame hold is
            // not part of this physical path.
            if (!delay(ward_card_delay)) return false;
        }
        if (result_value == 0 || event.target_is_monster) {
            apply_visual_event(visual, event, abilities);
        }
        if (event.kind == BattleEventKind::player_attack) {
            // 1358 uses 2db8/137a for the no-number commit before the next
            // action. A successful nonzero hit first exposes 2db8's completely
            // bare background page; 137a then redraws the surviving monster
            // and party while the physical-action flag still retains the
            // final fighter pose. The following initiative-loop rejoin owns
            // the monster-only page. The zero path below enters 1358 through
            // 12d5 and does not expose that first background flip.
            if (show_physical_zero) {
                // 12d5's immunity/insufficient-attack branch still rejoins
                // 1358 after the ten red zero pages. 137a consumes the
                // still-live physical-action flag, so its no-number commit
                // page retains the final pose rather than restoring the
                // ordinary portrait. The initiative-loop rejoin then exposes
                // one bare 2db8 page before the next actor's card.
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    pose_count != 0
                        ? std::optional<std::size_t>{poses[pose_count - 1U]}
                        : std::nullopt,
                    {}, std::nullopt, encounter_directory_offset);
                if (event_index + 1U < result.events.size() &&
                    result.events[event_index + 1U].kind !=
                        BattleEventKind::status_expired) {
                    // 0c41 calls 0da7 before the initiative-loop rejoin.  A
                    // natural expiry therefore draws directly over 1358's
                    // retained final physical pose; the monster-only 2db8
                    // page belongs after 0da7, not between the pose and card.
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false, false);
                }
                continue;
            }
            auto successor_index = event_index + 1U;
            while (successor_index < result.events.size() &&
                   result.events[successor_index].kind ==
                       BattleEventKind::skipped) {
                ++successor_index;
            }
            if (successor_index < result.events.size()) {
                // 20e7's low-HP self-heal decision is entered before the
                // ordinary initiative rejoin, so that successor consumes the
                // retained pose directly. Other surviving-monster actions
                // expose 2db8's empty page first.
                if (result.events[successor_index].kind !=
                    BattleEventKind::monster_heal) {
                    present_battle_surface(context, base_surface);
                }
                present_event_frame(
                    context, base_surface, encounter, items, fighters,
                    menu_sprites, font, fallback, visual, event,
                    pose_count != 0
                        ? std::optional<std::size_t>{poses[pose_count - 1U]}
                        : std::nullopt,
                    {}, std::nullopt,
                    encounter_directory_offset);
                const auto successor = result.events[successor_index].kind;
                if (successor == BattleEventKind::monster_attack ||
                    successor == BattleEventKind::monster_fled ||
                    successor == BattleEventKind::monster_escape_failed) {
                    // The ordinary initiative rejoin exposes a monster-only
                    // 2db8 page between 137a's retained physical pose and
                    // 296a/212a. Generic/single-target and self-heal paths
                    // already own their corresponding 20e7 preparation page.
                    present_event_frame(
                        context, base_surface, encounter, items, fighters,
                        menu_sprites, font, fallback, visual, event,
                        std::nullopt, {}, std::nullopt,
                        encounter_directory_offset, std::nullopt, false,
                        false);
                }
            }
            continue;
        }
        if (event.kind == BattleEventKind::status_damage) {
            // 0bf4 commits HP, repeats the same bare 2db8 composition and
            // leaves that card-free page visible for four timer ticks.
            const auto clean = compose_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset, std::nullopt,
                false, false);
            present_battle_surface(context, clean);
            if (!delay(monster_action_card_delay)) return false;
        } else {
            present_event_frame(
                context, base_surface, encounter, items, fighters,
                menu_sprites, font, fallback, visual, event, std::nullopt,
                {}, std::nullopt, encounter_directory_offset);
        }
        if (event.kind != BattleEventKind::status_damage &&
            monster_all_target_action) {
            if (!delay_monster_all_target_slots()) return false;
        }
        if (finish_monster_action_here) {
            if (!present_monster_turn_tail(event)) return false;
        } else if (!monster_action_event &&
                   event.kind != BattleEventKind::player_attack &&
                   event.kind != BattleEventKind::ally_attack &&
                   !dispatcher_event) {
            if (!delay(action_delay)) return false;
        }
    }
    return !frontend_abort && !context.platform.poll_frontend_quit();
}

void begin_battle_shared_state(SharedState& state) {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto base = 0x106 + index * 0x9f;
        state.set_u16(base + 0x41, state.u16(base + 0x0c));
        state.set_u16(base + 0x43, state.u16(base + 0x0e));
        state.set_u16(base + 0x5f, state.u16(base + 0x5d));
        state.set_u16(base + 0x67, state.u16(base + 0x65));
    }
}

void compact_inventory(SharedState& state) {
    std::array<std::uint16_t, 50> occupied{};
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
        const auto item = state.u16(0x382 + slot * 2U);
        if (item != 0) occupied[count++] = item;
    }
    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
        state.set_u16(0x382 + slot * 2U, occupied[slot]);
    }
}

void finish_battle_shared_state(SharedState& state) {
    // FIG 1000:0232 restores temporary battle buffs from the snapshots made by
    // 020d and preserves only the status bits explicitly allowed to cross the
    // battle boundary. Level-up deltas are applied after this restoration.
    for (std::size_t index = 0; index < 4; ++index) {
        const auto base = 0x106 + index * 0x9f;
        state.set_u16(base + 0x0c, state.u16(base + 0x41));
        state.set_u16(base + 0x0e, state.u16(base + 0x43));
        state.set_u16(base + 0x5d, state.u16(base + 0x5f));
        state.set_u16(base + 0x65, state.u16(base + 0x67));
        state.set_u16(base + 8, static_cast<std::uint16_t>(state.u16(base + 8) & 0xf200U));
    }
    compact_inventory(state);
    state.set_u16(0x4a0, 0);
}

constexpr std::array<std::size_t, 8> level_stat_offsets = {
    0x31, 0x2f, 0x37, 0x57, 0x3d, 0x0e, 0x4d, 0x33,
};

struct BattleLevelUpStep {
    std::array<std::uint16_t, 8> before{};
    std::array<std::uint16_t, 8> after{};
    std::optional<std::uint8_t> learned_ability;
};

std::array<std::uint16_t, 8> level_stats(const SharedState& state,
                                         std::size_t actor_base) {
    std::array<std::uint16_t, 8> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = state.u16(actor_base + level_stat_offsets[index]);
    }
    return result;
}

std::optional<BattleLevelUpStep> advance_one_level(
    SharedState& state, std::size_t actor_base,
    const std::array<BattleGrowthRow, BattleDatabase::growth_rows>& table) {
    const auto level = static_cast<std::size_t>(state.u16(actor_base + 0x31));
    if (level >= BattleDatabase::growth_rows ||
        state.u16(actor_base + 0x39) < state.u16(actor_base + 0x3b)) {
        return std::nullopt;
    }
    state.set_u16(actor_base + 0x39,
                  static_cast<std::uint16_t>(state.u16(actor_base + 0x39) -
                                             state.u16(actor_base + 0x3b)));
    BattleLevelUpStep step;
    step.before = level_stats(state, actor_base);
    const auto& current = table[level].fields;
    const auto& previous = table[level == 0 ? 0 : level - 1].fields;
    const auto add_delta = [&](std::size_t offset, std::size_t field) {
        state.set_u16(actor_base + offset,
                      static_cast<std::uint16_t>(state.u16(actor_base + offset) +
                                                 current[field] - previous[field]));
    };
    state.set_u16(actor_base + 8, 0);
    state.set_u16(actor_base + 0x31, static_cast<std::uint16_t>(level + 1U));
    add_delta(0x2f, 0);
    state.set_u16(actor_base + 0x2d, state.u16(actor_base + 0x2f));
    add_delta(0x57, 1);
    state.set_u16(actor_base + 0x55, state.u16(actor_base + 0x57));
    state.set_u16(actor_base + 0x3b, current[2]);
    add_delta(0x3d, 3);
    add_delta(0x0c, 3);
    add_delta(0x0e, 4);
    add_delta(0x4d, 5);
    add_delta(0x5d, 5);
    add_delta(0x33, 6);
    add_delta(0x37, 7);
    state.set_u16(actor_base + 0x35, state.u16(actor_base + 0x37));

    const auto packed_ability = current[8];
    const auto low = static_cast<std::uint8_t>(packed_ability);
    const auto high = static_cast<std::uint8_t>(packed_ability >> 8U);
    for (std::size_t slot = 0; slot < 50; ++slot) {
        const auto offset = actor_base + 0x6d + slot;
        if (state.u8(offset) == low || state.u8(offset) == high) {
            state.set_u8(offset, low);
            step.learned_ability = low;
            break;
        }
    }
    step.after = level_stats(state, actor_base);
    return step;
}

std::uint16_t apply_victory_rewards(SharedState& state,
                                    const BattleRewards& rewards) {
    const auto before_money = state.u16(0x104);
    const auto money_sum = static_cast<unsigned>(before_money) + rewards.money;
    state.set_u16(0x104, static_cast<std::uint16_t>(std::min(money_sum, 0xffffU)));

    const auto party_count = std::min<std::size_t>(state.u16(0x10), 4);
    std::size_t living = 0;
    for (std::size_t index = 0; index < party_count; ++index) {
        if ((state.u16(0x106 + index * 0x9f + 8) & 0x2000U) == 0) ++living;
    }
    if (living == 0) return 0;
    const auto share = static_cast<std::uint16_t>(rewards.experience / living);
    for (std::size_t index = 0; index < party_count; ++index) {
        const auto base = 0x106 + index * 0x9f;
        if ((state.u16(base + 8) & 0x2000U) != 0) continue;
        const auto sum = static_cast<unsigned>(state.u16(base + 0x39)) + share;
        state.set_u16(base + 0x39, static_cast<std::uint16_t>(std::min(sum, 0xffffU)));
    }
    return share;
}

bool wait_for_battle_ack(GameContext& context) {
    auto action = InputAction::none;
    while (action == InputAction::none) {
        action = context.platform.wait_for_input();
    }
    return action != InputAction::quit;
}

bool delay_for_or_frontend_quit(
    PlatformBackend& platform, std::chrono::milliseconds duration) {
    // DOS has no window-close event, but the merged frontend must not remain
    // trapped in an uninterruptible replacement for FIG's timer wait. Poll in
    // short slices while preserving the exact total duration when no quit is
    // pending. Only the dedicated frontend channel is sampled: the original
    // timer is not dismissible, so direction/confirmation keys remain queued.
    constexpr auto slice = std::chrono::milliseconds(20);
    while (duration.count() > 0) {
        if (platform.poll_frontend_quit()) return false;
        const auto current = std::min(duration, slice);
        platform.delay_for(current);
        duration -= current;
    }
    return !platform.poll_frontend_quit();
}

BattleSurface compose_settlement_scene(
    const BattleSurface& base_surface, const BattleEncounter& encounter,
    const ScriptArchive& items, const std::filesystem::path& game_root,
    const SpriteArchive& menu_sprites,
    std::span<const BattlePartyMember> party,
    std::span<const MonsterBattleState> monsters = {},
    const std::array<bool, 3>* media = nullptr) {
    auto frame = base_surface;
    if (media != nullptr) {
        draw_battle_media(frame, menu_sprites, *media);
    }
    if (!monsters.empty()) {
        draw_enemies(frame, encounter, items, game_root, menu_sprites, monsters);
    }
    draw_fig_party_cards(frame, menu_sprites, party);
    return frame;
}

std::optional<BattleSurface> present_victory_summary(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleSession& session,
    const BattleAbilityDatabase& abilities, const BattleRewards& rewards,
    std::uint16_t experience_share) {
    play_battle_music(context, context.game_root / "RX" / "WI01.RIX", false);
    auto frame = compose_settlement_scene(
        base_surface, encounter, items, context.game_root, menu_sprites,
        std::span<const BattlePartyMember>(session.party()).first(
            session.party_count()), {}, &session.battle_media());
    // FIG 04d4: 10x3 compact panel at x=25/y=75; 7284 prints the three
    // DATA:2e0b rows from x=27/y=84, then 3c77 uses digit set 6f.
    draw_message_panel(frame, menu_sprites, 25, 75, 10, 3);
    draw_fig_text(frame, font, fallback, abilities.victory_text(), 27, 84);
    draw_menu_number(frame, menu_sprites, rewards.money, 44, 103, 111);
    draw_menu_number(frame, menu_sprites, experience_share, 48, 119, 111);
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
    if (!wait_for_battle_ack(context)) return std::nullopt;
    return frame;
}

bool present_encounter_capture_reward(
    GameContext& context, BattleSurface frame,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleAbilityDatabase& abilities) {
    // FIG 0553 overlays this one-row card on the still-visible victory page.
    draw_message_panel(frame, menu_sprites, 20, 91, 14, 1);
    draw_fig_text(frame, font, fallback,
                  abilities.encounter_capture_text(), 25, 100);
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
    return wait_for_battle_ack(context);
}

bool present_defeat_summary(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleSession& session,
    const BattleAbilityDatabase& abilities) {
    play_battle_music(context, context.game_root / "RX" / "DEAD.RIX", false);
    auto frame = base_surface;
    draw_enemies(frame, encounter, items, context.game_root, menu_sprites,
                 session.monsters());
    // FIG's defeat epilogue does not draw the extracted DATA:2e3f text or a
    // party card.  After the final death page it leaves only the surviving
    // monsters on the BA background while DEAD.RIX plays for 54 ticks.  The
    // former compact red "全體陣亡" panel was a rewrite invention.
    static_cast<void>(font);
    static_cast<void>(fallback);
    static_cast<void>(abilities);
    context.platform.present({
        320, 200, frame.pixels,
        std::span<const std::uint8_t, 768>(frame.palette),
    });
    return delay_for_or_frontend_quit(
        context.platform, fig_timer_ticks(54));
}

bool present_level_ups(
    GameContext& context, const BattleSurface& base_surface,
    const BattleEncounter& encounter, const ScriptArchive& items,
    const SpriteArchive& menu_sprites, const LegacyFont& font,
    const LegacyFont& fallback, const BattleAbilityDatabase& abilities,
    const BattleDatabase& database, const std::array<bool, 3>& media) {
    const auto party_count = std::min<std::size_t>(
        context.shared_state.u16(0x10), 4U);
    for (std::size_t actor = 0; actor < party_count; ++actor) {
        const auto actor_base = 0x106U + actor * 0x9fU;
        const auto identity = context.shared_state.u16(0x72U + actor * 6U);
        const auto table_index = static_cast<std::size_t>(identity / 12U);
        if (table_index >= BattleDatabase::growth_table_count) continue;
        while (true) {
            const auto step = advance_one_level(
                context.shared_state, actor_base,
                database.growth_tables()[table_index]);
            if (!step) break;

            std::array<BattlePartyMember, 4> party{};
            for (std::size_t index = 0; index < party_count; ++index) {
                party[index] = BattlePartyMember::load(
                    context.shared_state, index);
            }
            auto frame = compose_settlement_scene(
                base_surface, encounter, items, context.game_root,
                menu_sprites,
                std::span<const BattlePartyMember>(party).first(party_count),
                {}, &media);
            draw_message_panel(frame, menu_sprites, 23, 10, 14, 10);

            auto title = std::vector<std::uint8_t>(
                abilities.level_up_title_text().begin(),
                abilities.level_up_title_text().end());
            if (title.size() >= 8U) {
                std::copy_n(context.shared_state.bytes().begin() +
                                static_cast<std::ptrdiff_t>(actor_base),
                            8U, title.begin());
            }
            // FIG's four editable name slots A374..A377 deliberately overlap
            // glyphs present in FIG.DSK.  The level-up title renders those
            // slots through the active NAME font first, then falls back to
            // FIG.DSK for the fixed "升級了" suffix.
            draw_fig_text(frame, fallback, font, title, 26, 19);
            draw_fig_text(frame, font, fallback,
                          abilities.level_up_stats_text(), 26, 35);
            auto top = 38;
            for (std::size_t stat = 0; stat < step->before.size(); ++stat) {
                draw_menu_number_right(frame, menu_sprites,
                                       step->before[stat], 38, top, 111);
                draw_menu_number_right(frame, menu_sprites,
                                       step->after[stat], 52, top, 111);
                top += 16;
            }
            if (step->learned_ability &&
                *step->learned_ability < abilities.abilities().size()) {
                draw_big5(frame, font, fallback,
                          abilities.ability(*step->learned_ability).name_big5,
                          42 * 4, 163, 0x00);
            }
            play_battle_music(
                context, context.game_root / "RX" / "WI02.RIX", false);
            context.platform.present({
                320, 200, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette),
            });
            if (!wait_for_battle_ack(context)) return false;
        }
    }
    return true;
}

}  // namespace

Marker BattleModule::run(GameContext& context, Marker input) {
    if (input != Marker::open_figure) return Marker::none;

    begin_battle_shared_state(context.shared_state);

    const auto database = BattleDatabase::load(context.game_root / "ORC.EXE");
    const auto raw_encounter = context.shared_state.u16(0x4a0);
    const auto music_number = (raw_encounter & 0x8000U) != 0
                                  ? 3U
                                  : ((raw_encounter & 0x4000U) != 0 ? 4U : 1U);
    auto encounter_offset = static_cast<std::uint16_t>(raw_encounter & 0x3fffU);
    const auto random_encounter_rules = encounter_offset == 0;
    if (encounter_offset == 0) {
        const auto time = context.platform.clock_time();
        encounter_offset = select_random_encounter(
            context.shared_state.u16(0x408), context.shared_state.viewport_x(),
            context.shared_state.viewport_y(), static_cast<std::uint8_t>(time.hundredth));
    }
    const auto selected = database.encounter_at_directory_offset(encounter_offset);
    if (!selected) {
        // ORC contains sixteen deliberately empty directory slots. Preserve
        // the original module boundary even when a script selects one.
        finish_battle_shared_state(context.shared_state);
        return Marker::continue_rpg;
    }
    const auto& encounter = selected->get();

    const auto background = load_sprites(
        context.game_root / normalize_dos_asset_path(encounter.background_path));
    if (!background.has_palette()) {
        throw std::runtime_error("FIG battle background has no VGA palette");
    }
    BattleSurface base_surface;
    base_surface.palette = background.palette();
    blit(base_surface, background, 0, 0, 0, false);
    auto surface = base_surface;
    const auto items = ScriptArchive::load(context.game_root / "ITEM.EXE");
    const auto menu_sprites = load_sprites(context.game_root / "MENU.RSK");
    if (menu_sprites.sprites().size() <= 180U) {
        throw std::runtime_error("FIG MENU.RSK is missing battle-card sprites");
    }
    draw_enemies(surface, encounter, items, context.game_root, menu_sprites);
    std::array<BattlePartyMember, 4> initial_party{};
    const auto initial_party_count = std::min<std::size_t>(
        context.shared_state.u16(0x10), initial_party.size());
    for (std::size_t index = 0; index < initial_party_count; ++index) {
        initial_party[index] = BattlePartyMember::load(
            context.shared_state, index);
    }
    draw_fig_party_cards(
        surface, menu_sprites,
        std::span<const BattlePartyMember>(initial_party).first(
            initial_party_count));
    const auto fighters = load_sprites(context.game_root / "SW" / "FMAN.RSK");

    if (!present_story_battle_setup(
            context, base_surface, encounter_offset)) {
        context.platform.stop_audio();
        finish_battle_shared_state(context.shared_state);
        return Marker::none;
    }

    std::ostringstream music;
    music << "FI0" << music_number << ".RIX";
    play_battle_music(context, context.game_root / "RX" / music.str(), true);
    context.platform.present({320, 200, surface.pixels,
                              std::span<const std::uint8_t, 768>(surface.palette)});

    auto action = InputAction::none;
    bool start_battle = true;
    if (!encounter.introduction_text.empty()) {
        const auto font = LegacyFont::load(context.game_root / "FIG.DSK");
        const auto name_font = context.name_font.empty()
            ? LegacyFont::load(context.game_root / "NAMEQ.DSK")
            : LegacyFont::parse(context.name_font);
        std::size_t text_offset = 0;
        while (true) {
            const auto page = render_dialogue_page(
                font, encounter.introduction_text, text_offset,
                280, 64, 1, &name_font);
            auto quit_during_text = false;
            auto skipped_text_delay = false;
            for (const auto glyph_end : page.glyph_end_offsets) {
                if (skipped_text_delay) break;
                const auto partial = render_dialogue_page(
                    font,
                    std::span<const std::uint8_t>(encounter.introduction_text)
                        .first(glyph_end),
                    text_offset, 280, 64, 1, &name_font);
                const auto frame = compose_introduction(
                    surface, partial, menu_sprites);
                context.platform.present_direct_update({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette),
                });
                const auto text_delay = context.shared_state.u16(0x3f2);
                if (text_delay != 0) {
                    context.platform.delay_for(fig_timer_ticks(text_delay));
                }
                const auto text_action = context.platform.poll_text_input();
                if (text_action == InputAction::quit) {
                    quit_during_text = true;
                    break;
                }
                skipped_text_delay = text_action != InputAction::none;
            }
            if (quit_during_text) {
                action = InputAction::quit;
                start_battle = false;
                break;
            }
            if (skipped_text_delay && !page.glyph_end_offsets.empty()) {
                const auto frame = compose_introduction(
                    surface, page, menu_sprites);
                context.platform.present_direct_update({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette),
                });
            }
            const auto wait_at_text_cursor = [&](bool animated) {
                auto marker = std::size_t{animated ? 149U : 145U};
                while (true) {
                    auto frame = compose_introduction(
                        surface, page, menu_sprites);
                    blit(frame, menu_sprites, marker,
                         40 + static_cast<int>(page.cursor_x),
                         125 + static_cast<int>(page.cursor_y));
                    context.platform.present({
                        320, 200, frame.pixels,
                        std::span<const std::uint8_t, 768>(frame.palette),
                    });
                    const auto input = context.platform.poll_input();
                    if (input != InputAction::none) return input;
                    context.platform.delay_for(std::chrono::milliseconds(20));
                    if (animated) {
                        ++marker;
                        if (marker == 153U) marker = 149U;
                    }
                }
            };
            if (page.has_more) {
                // 3eaa installs the static MENU 91h marker at the direct VGA
                // cursor. Like the original keyboard flag, any action advances
                // to the page after %% and consumes that action.
                action = wait_at_text_cursor(false);
                if (action == InputAction::quit) {
                    start_battle = false;
                    break;
                }
                text_offset = page.next_offset;
                continue;
            }

            if (encounter.prompt_order == BattlePromptOrder::none) {
                // 3ed3 cycles MENU 95h..98h at $$ until any key. Question
                // encounters set the skip-wait byte and proceed directly to
                // 5c98's Yes/No cards instead.
                action = wait_at_text_cursor(true);
                // Fixed ORC introductions are not optional: FIG advances on
                // either key and enters 028a. Only an explicit frontend quit
                // may abort the in-process module.
                start_battle = action != InputAction::quit;
                break;
            }

            std::size_t selected_prompt = 0;
            while (true) {
                const auto frame = compose_introduction(
                    surface, page, menu_sprites, selected_prompt);
                context.platform.present({
                    320, 200, frame.pixels,
                    std::span<const std::uint8_t, 768>(frame.palette),
                });
                action = context.platform.wait_for_input();
                if (action == InputAction::left || action == InputAction::right ||
                    action == InputAction::up || action == InputAction::down) {
                    selected_prompt ^= 1U;
                    continue;
                }
                if (action == InputAction::confirm) {
                    const auto selected_yes =
                        encounter.prompt_order == BattlePromptOrder::yes_no
                            ? selected_prompt == 0
                            : selected_prompt == 1;
                    start_battle = selected_yes;
                } else if (action == InputAction::cancel ||
                           action == InputAction::quit) {
                    start_battle = false;
                } else {
                    continue;
                }
                break;
            }
            break;
        }
    }
    if (action == InputAction::quit) {
        context.platform.stop_audio();
        finish_battle_shared_state(context.shared_state);
        return Marker::none;
    }

    if (start_battle) {
        const auto abilities =
            BattleAbilityDatabase::load(context.game_root / "FIG.EXE");
        const auto command_font = LegacyFont::load(context.game_root / "FIG.DSK");
        const auto command_name_font = context.name_font.empty()
            ? LegacyFont::load(context.game_root / "NAMEQ.DSK")
            : LegacyFont::parse(context.name_font);
        auto session = BattleSession::create(
            context.shared_state, encounter, items, random_encounter_rules);
        auto random = FigBattleRandom::load(
            context.game_root / "FIG.EXE", context.shared_state);
        bool quit_battle = false;
        std::optional<std::size_t> automatic_target;
        // FIG's 5ed8 mutates the live E0h..E4h DAC ramp. 144e executes ten
        // calls and therefore returns it to the entering order, while each
        // 3c44 weapon wipe advances it by two colours. Preserve that order
        // across actors and rounds for autonomous/status damage handlers,
        // which deliberately do not install a canonical ramp through 14a3.
        std::size_t existing_result_palette_shift = 0;
        // FIG collects every command before rolling initiative. The portable
        // menu does the same and feeds the resulting array into the shared
        // deterministic session instead of launching or emulating FIG.EXE.
        for (std::size_t round = 0;
             round < 1000 && session.outcome() == BattleOutcome::ongoing; ++round) {
            auto previous_page = surface;
            if (automatic_target &&
                context.platform.poll_input() != InputAction::none) {
                // FIG's timer ISR writes DS:3ca3 on any pending key. The next
                // collection pass clears automatic mode and returns to actor 0;
                // the interrupting key itself is consumed rather than reused.
                automatic_target.reset();
            }
            std::array<PlayerBattleCommand, 4> round_commands{};
            for (auto& command : round_commands) {
                command = {PlayerCommandKind::skip, 0, 0, 0};
            }
            if (automatic_target) {
                // FIG DS:2f18 bypasses command collection on later rounds and
                // repeats command type 2 for every currently commandable actor.
                for (std::size_t actor = 0; actor < session.party_count(); ++actor) {
                    const auto& member = session.party()[actor];
                    if (member.living() && (member.status_bits & 0x2c7eU) == 0) {
                        round_commands[actor] = {
                            PlayerCommandKind::basic_attack, 0, *automatic_target, 0,
                        };
                    }
                }
            } else {
                BattleCommandMenu menu(session, abilities, items);
                while (!menu.complete()) {
                    if (menu.notice() != BattleCommandNotice::none) {
                        const auto notice_text = abilities.notice_text(menu.notice());
                        const auto notice_page = render_dialogue_page(
                            command_font, notice_text, 0, 280, 64, 1,
                            &command_name_font);
                        auto skipped_text_delay = false;
                        auto quit_during_text = false;
                        for (const auto glyph_end : notice_page.glyph_end_offsets) {
                            if (skipped_text_delay) break;
                            const auto frame = compose_command_frame(
                                surface, session, menu, encounter, abilities,
                                command_font, command_name_font, menu_sprites,
                                fighters, items, context.game_root, glyph_end,
                                std::nullopt);
                            context.platform.present_direct_update({
                                320, 200, frame.pixels,
                                std::span<const std::uint8_t, 768>(frame.palette),
                            });
                            const auto text_delay =
                                context.shared_state.u16(0x3f2);
                            if (text_delay != 0) {
                                context.platform.delay_for(
                                    fig_timer_ticks(text_delay));
                            }
                            const auto text_action =
                                context.platform.poll_text_input();
                            if (text_action == InputAction::quit) {
                                menu.input(text_action);
                                quit_during_text = true;
                                break;
                            }
                            skipped_text_delay =
                                text_action != InputAction::none;
                        }
                        if (quit_during_text) {
                            quit_battle = true;
                            break;
                        }
                        if (skipped_text_delay &&
                            !notice_page.glyph_end_offsets.empty()) {
                            const auto frame = compose_command_frame(
                                surface, session, menu, encounter, abilities,
                                command_font, command_name_font, menu_sprites,
                                fighters, items, context.game_root,
                                notice_text.size(),
                                std::nullopt);
                            context.platform.present_direct_update({
                                320, 200, frame.pixels,
                                std::span<const std::uint8_t, 768>(frame.palette),
                            });
                        }

                        auto marker = std::size_t{149};
                        while (true) {
                            const auto frame = compose_command_frame(
                                surface, session, menu, encounter, abilities,
                                command_font, command_name_font, menu_sprites,
                                fighters, items, context.game_root,
                                std::nullopt, marker);
                            context.platform.present({
                                320, 200, frame.pixels,
                                std::span<const std::uint8_t, 768>(frame.palette),
                            });
                            const auto notice_action = context.platform.poll_input();
                            if (notice_action != InputAction::none) {
                                menu.input(notice_action);
                                break;
                            }
                            context.platform.delay_for(
                                std::chrono::milliseconds(20));
                            ++marker;
                            if (marker == 153U) marker = 149U;
                        }
                        if (menu.quit_requested()) {
                            quit_battle = true;
                            break;
                        }
                        continue;
                    }
                    const auto frame = compose_command_frame(
                        surface, session, menu, encounter, abilities,
                        command_font, command_name_font, menu_sprites,
                        fighters, items, context.game_root);
                    previous_page = frame;
                    context.platform.present({
                        320, 200, frame.pixels,
                        std::span<const std::uint8_t, 768>(frame.palette),
                    });
                    menu.input(context.platform.wait_for_input());
                    if (menu.quit_requested()) {
                        quit_battle = true;
                        break;
                    }
                }
                if (quit_battle) break;
                round_commands = menu.commands();
                if (menu.automatic_requested()) {
                    const auto chosen = std::find_if(
                        round_commands.begin(), round_commands.end(),
                        [](const PlayerBattleCommand& command) {
                            return command.kind == PlayerCommandKind::basic_attack;
                        });
                    if (chosen != round_commands.end()) {
                        automatic_target = chosen->target;
                    }
                }
            }
            auto visual = capture_visual_state(session, abilities);
            const auto round_result =
                session.play_round(round_commands, abilities, random.function());
            if (!present_round_events(
                    context, base_surface, previous_page, encounter, items,
                    abilities, command_font, command_name_font,
                    fighters, menu_sprites, visual, round_result,
                    encounter_offset, round_commands,
                    existing_result_palette_shift)) {
                quit_battle = true;
                break;
            }
            // 09b8's successful random-encounter escape returns from FIG as
            // soon as the second fighter pose and SV3 submission complete.
            // It never rejoins the ordinary round-boundary 2db8 page and its
            // two FIG timer ticks.
            if (session.outcome() == BattleOutcome::escaped) break;

            // The initiative loop rejoins through 2db8, not 137a.  This clean
            // page contains the background, monsters and persistent media but
            // deliberately no party cards; the next 2b64 command composition
            // installs those cards together with the menu.  Drawing ordinary
            // portraits here creates a page the original never displayed.
            surface = base_surface;
            draw_battle_media(surface, menu_sprites, session.battle_media());
            draw_summoned_ally_name_cards(
                surface, menu_sprites, command_font, command_name_font,
                visual);
            draw_enemies(surface, encounter, items, context.game_root,
                         menu_sprites,
                         session.monsters());
            context.platform.present({
                320, 200, surface.pixels,
                std::span<const std::uint8_t, 768>(surface.palette),
            });
            if (!delay_for_or_frontend_quit(
                    context.platform, fig_timer_ticks(2))) {
                quit_battle = true;
                break;
            }
        }
        if (quit_battle) {
            context.platform.stop_audio();
            session.store(context.shared_state);
            random.store(context.shared_state);
            finish_battle_shared_state(context.shared_state);
            return Marker::none;
        }
        const auto outcome = session.outcome();
        session.store(context.shared_state);
        random.store(context.shared_state);
        if (outcome == BattleOutcome::victory) {
            const auto rewards = session.rewards();
            const BattleRewards presentation_rewards{
                rewards.experience, rewards.money};
            const auto share = apply_victory_rewards(
                context.shared_state, presentation_rewards);
            auto victory_frame = present_victory_summary(
                context, base_surface, encounter, items, menu_sprites,
                command_font, command_name_font, session, abilities,
                presentation_rewards, share);
            auto frontend_quit = !victory_frame;
            if (!frontend_quit) {
                // FIG 0531 waits on the victory summary before 053b tests
                // inventory +3e4 and 0542 performs the one-in-three draw.
                // Deferring both the random cursor advance and item write is
                // observable at the portable frontend-quit boundary; the
                // former implementation granted the encounter item while the
                // still-unacknowledged victory page was on screen.
                const auto encounter_capture_granted =
                    session.try_grant_encounter_capture(
                        encounter.special_value, random.function());
                session.store(context.shared_state);
                random.store(context.shared_state);
                if (encounter_capture_granted) {
                    // 0586 writes the special item to +3e4, then 058c runs
                    // the stable 1a78 inventory compactor before 058f waits
                    // on the already drawn reward card.
                    compact_inventory(context.shared_state);
                    frontend_quit = !present_encounter_capture_reward(
                        context, std::move(*victory_frame), menu_sprites,
                        command_font, command_name_font, abilities);
                }
            }
            // FIG 0592 restores the pre-battle temporary-stat snapshots
            // before applying any growth-table delta.
            finish_battle_shared_state(context.shared_state);
            if (!frontend_quit) {
                frontend_quit = !present_level_ups(
                    context, base_surface, encounter, items, menu_sprites,
                    command_font, command_name_font, abilities, database,
                    session.battle_media());
            }
            if (frontend_quit) {
                context.platform.stop_audio();
                return Marker::none;
            }
        } else {
            auto frontend_quit = false;
            if (outcome == BattleOutcome::defeat) {
                frontend_quit = !present_defeat_summary(
                    context, base_surface, encounter, items, menu_sprites,
                    command_font, command_name_font, session, abilities);
            }
            finish_battle_shared_state(context.shared_state);
            if (frontend_quit) {
                context.platform.stop_audio();
                return Marker::none;
            }
        }
    } else {
        finish_battle_shared_state(context.shared_state);
    }
    context.platform.stop_audio();
    return Marker::continue_rpg;
}

}  // namespace swd2
