#include "swd2/demo_module.hpp"

#include "swd2/demo_timeline.hpp"
#include "swd2/rsk_decoder.hpp"
#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace swd2 {

namespace {

struct DemoSurface {
    // FUN_1000_0421 clears the DOS work page with zero, not the sprite
    // transparency key (0xfe).
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(320 * 200, 0);
    std::array<std::uint8_t, 768> palette{};
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("DEMO module cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> load_sword_bytes(const std::filesystem::path& game_root,
                                           unsigned number) {
    return decode_rsk_block(
               read_file(game_root / ("SWORD" + std::to_string(number) + ".RSK")))
        .data;
}

SpriteArchive load_sword(const std::filesystem::path& game_root, unsigned number) {
    return SpriteArchive::parse(load_sword_bytes(game_root, number));
}

SpriteArchive load_expanded_sword(const std::filesystem::path& game_root, unsigned number) {
    return SpriteArchive::parse(decode_demo_rle(load_sword_bytes(game_root, number)));
}

void blit(DemoSurface& surface, const SpriteArchive& archive, std::size_t index,
          int left, int top, bool transparent) {
    const auto& sprite = archive.sprites().at(index);
    const auto pixels = archive.pixels(index);
    for (std::size_t y = 0; y < sprite.height; ++y) {
        for (std::size_t x = 0; x < sprite.width; ++x) {
            const auto destination_x = left + static_cast<int>(x);
            const auto destination_y = top + static_cast<int>(y);
            const auto color = pixels[y * sprite.width + x];
            if (destination_x >= 0 && destination_x < 320 && destination_y >= 0 &&
                destination_y < 200 && (!transparent || color != 0xfe)) {
                surface.pixels[static_cast<std::size_t>(destination_y) * 320 + destination_x] = color;
            }
        }
    }
}

bool present_frame(GameContext& context, DemoSurface& surface, std::uint64_t& timer_ticks,
                   unsigned elapsed_timer_ticks) {
    context.platform.present({320, 200, surface.pixels,
                              std::span<const std::uint8_t, 768>(surface.palette)});
    const auto action = context.platform.poll_input();

    // DEMO.EXE synchronizes against its 70 Hz IRQ counter.  Use a cumulative
    // conversion so integer milliseconds alternate between 14/15 (or 28/29)
    // rather than accumulating drift over the 960-frame sequence.
    const auto before = timer_ticks * 1000U / 70U;
    timer_ticks += elapsed_timer_ticks;
    const auto after = timer_ticks * 1000U / 70U;
    context.platform.delay_for(std::chrono::milliseconds(after - before));
    return action != InputAction::quit && action != InputAction::cancel &&
           action != InputAction::confirm;
}

bool hold(GameContext& context, DemoSurface& surface, std::uint64_t& timer_ticks,
          unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        if (!present_frame(context, surface, timer_ticks, 1)) return false;
    }
    return true;
}

template <typename Change>
bool palette_transition(GameContext& context, DemoSurface& surface,
                        std::uint64_t& timer_ticks, Change change) {
    while (change(surface.palette)) {
        if (!present_frame(context, surface, timer_ticks, 1)) return false;
    }
    return true;
}

}  // namespace

Marker DemoModule::run(GameContext& context, Marker) {
    const auto sword1 = load_sword(context.game_root, 1);
    const auto sword2 = load_sword(context.game_root, 2);
    const auto sword3 = load_sword(context.game_root, 3);
    const auto sword4 = load_sword(context.game_root, 4);
    const auto sword5 = load_sword(context.game_root, 5);
    const auto sword6 = load_expanded_sword(context.game_root, 6);
    const auto sword7 = load_expanded_sword(context.game_root, 7);
    const auto sword8 = load_sword(context.game_root, 8);
    const auto music = read_file(context.game_root / "SWORD.RIX");

    const auto archive_for = [&](DemoSpriteSource source) -> const SpriteArchive& {
        switch (source) {
            case DemoSpriteSource::sword1: return sword1;
            case DemoSpriteSource::sword2: return sword2;
            case DemoSpriteSource::sword3: return sword3;
            case DemoSpriteSource::sword4: return sword4;
            case DemoSpriteSource::sword5: return sword5;
            case DemoSpriteSource::sword6_expanded: return sword6;
            case DemoSpriteSource::sword7_expanded: return sword7;
            case DemoSpriteSource::sword8: return sword8;
        }
        throw std::runtime_error("invalid DEMO sprite source");
    };

    std::uint64_t timer_ticks = 0;
    bool continue_demo = true;
    DemoTimeline timeline;
    DemoSurface last_frame;
    while (continue_demo) {
        auto state = timeline.next();
        if (!state) break;
        if (state->start_music) context.platform.play_music(music, false);

        DemoSurface frame;
        // 0a5c deliberately installs the palette belonging to SWORD5 after
        // loading all five main-scene archives.
        frame.palette = sword5.palette();
        for (const auto& command : state->draws) {
            blit(frame, archive_for(command.source), command.sprite,
                 command.x, command.y, command.transparent);
        }
        continue_demo = present_frame(context, frame, timer_ticks, 2);
        last_frame = std::move(frame);
    }

    if (continue_demo) {
        // The DOS epilogue is a SWORD8 title tableau, not a second guessed
        // animation loop. It waits two seconds, fades the old palette down,
        // installs/fades the SWORD8 palette, reveals three figures, waits five
        // seconds, then leaves a small high-colour mark visible for eight.
        auto title = std::move(last_frame);
        continue_demo = hold(context, title, timer_ticks, 2U * 70U);

        if (continue_demo) {
            continue_demo = palette_transition(
                context, title, timer_ticks,
                [](std::array<std::uint8_t, 768>& palette) {
                    bool changed = false;
                    for (auto& component : palette) {
                        if (component != 0) {
                            --component;
                            changed = true;
                        }
                    }
                    return changed;
                });
        }
        if (continue_demo) {
            std::fill(title.pixels.begin(), title.pixels.end(), 0);
            blit(title, sword8, 0, 0, 40, false);
            continue_demo = hold(context, title, timer_ticks, 20);
        }
        if (continue_demo) {
            const auto target = sword8.palette();
            continue_demo = palette_transition(
                context, title, timer_ticks,
                [&target](std::array<std::uint8_t, 768>& palette) {
                    bool changed = false;
                    for (std::size_t i = 0; i < palette.size(); ++i) {
                        if (palette[i] < target[i]) {
                            ++palette[i];
                            changed = true;
                        }
                    }
                    return changed;
                });
        }
        if (continue_demo) continue_demo = hold(context, title, timer_ticks, 4U * 70U);
        if (continue_demo) {
            auto reveal_source = title;
            blit(reveal_source, sword8, 1, 49, 44, true);
            blit(reveal_source, sword8, 2, 120, 40, true);
            blit(reveal_source, sword8, 3, 200, 50, true);
            const auto mask = portable_demo_reveal_mask();
            DemoByteMaskReveal reveal(mask);
            while (continue_demo && reveal.step(reveal_source.pixels, title.pixels)) {
                continue_demo = present_frame(context, title, timer_ticks, 1);
            }
        }
        if (continue_demo) continue_demo = hold(context, title, timer_ticks, 5U * 70U);
        if (continue_demo) {
            continue_demo = palette_transition(
                context, title, timer_ticks,
                [](std::array<std::uint8_t, 768>& palette) {
                    bool changed = false;
                    for (std::size_t i = 0; i < 738; ++i) {
                        if (palette[i] != 0) {
                            --palette[i];
                            changed = true;
                        }
                    }
                    for (std::size_t i = 741; i < 744; ++i) {
                        palette[i] = static_cast<std::uint8_t>(
                            std::min<unsigned>(63, palette[i] + 2));
                    }
                    return changed;
                });
        }
        if (continue_demo) {
            blit(title, sword8, 4, 94, 125, false);
            blit(title, sword8, 5, 105, 155, false);
            static_cast<void>(hold(context, title, timer_ticks, 8U * 70U));
        }
    }

    context.platform.stop_audio();
    return Marker::none;
}

}  // namespace swd2
