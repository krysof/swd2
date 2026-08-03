#include "swd2/asset_catalog.hpp"
#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_database.hpp"
#include "swd2/battle_module.hpp"
#include "swd2/event_program.hpp"
#include "swd2/demo_module.hpp"
#include "swd2/launcher.hpp"
#include "swd2/legacy_font.hpp"
#include "swd2/meo.hpp"
#include "swd2/map_resource.hpp"
#include "swd2/map_database.hpp"
#include "swd2/meo_module.hpp"
#include "swd2/mon_database.hpp"
#include "swd2/monster_definition.hpp"
#include "swd2/rpg_module.hpp"
#include "swd2/runtime.hpp"
#include "swd2/mz_executable.hpp"
#include "swd2/planar_sprite_set.hpp"
#include "swd2/rsk_decoder.hpp"
#include "swd2/script_archive.hpp"
#include "swd2/save_slot.hpp"
#include "swd2/sprite_archive.hpp"
#ifdef SWD2_HAVE_SDL2
#include "swd2/sdl_platform.hpp"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <memory>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

void persist_browser_saves() {
#ifdef __EMSCRIPTEN__
    // Flush the IDBFS mount installed by web/shell.html. The callback keeps
    // persistence asynchronous and reports failures without stopping play.
    EM_ASM({
        if (typeof FS !== 'undefined' && FS.syncfs) {
            FS.syncfs(false, function(error) {
                document.dispatchEvent(new CustomEvent('swd2-save-sync', {
                    detail: error ? String(error) : String()
                }));
            });
        }
    });
#endif
}

std::filesystem::path find_default_game_root() {
    for (const auto& candidate : {std::filesystem::path("game"), std::filesystem::path("../game"),
                                  std::filesystem::path("../../game")}) {
        if (std::filesystem::is_directory(candidate)) {
            return candidate;
        }
    }
    return "game";
}

swd2::Marker parse_marker(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (value == "--" || value == "NONE" || value == "00") return swd2::Marker::none;
    if (value == "MT") return swd2::Marker::menu_ready;
    if (value == "IF") return swd2::Marker::open_figure;
    if (value == "ED") return swd2::Marker::open_demo;
    if (value == "OC") return swd2::Marker::continue_rpg;
    if (value == "OM") return swd2::Marker::returned_from_demo;
    throw std::runtime_error("unknown marker: " + value);
}

std::vector<swd2::Marker> parse_script(const std::string& text) {
    std::vector<swd2::Marker> result;
    std::istringstream input(text);
    for (std::string token; std::getline(input, token, ',');) {
        result.push_back(parse_marker(token));
    }
    if (result.empty()) {
        throw std::runtime_error("empty marker script");
    }
    return result;
}

void inspect(const std::filesystem::path& game_root) {
    const auto catalog = swd2::AssetCatalog::scan(game_root);
    std::cout << "game: " << std::filesystem::absolute(game_root).lexically_normal() << '\n'
              << "assets: " << catalog.files << " files, " << catalog.bytes << " bytes\n\n";

    std::cout << std::left << std::setw(14) << "executable" << std::right << std::setw(9) << "bytes"
              << std::setw(8) << "header" << std::setw(8) << "relocs" << std::setw(12) << "entry(file)"
              << std::setw(10) << "overlay" << '\n';

    std::vector<std::filesystem::path> executables;
    for (const auto& entry : std::filesystem::directory_iterator(game_root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".EXE") {
            executables.push_back(entry.path());
        }
    }
    std::sort(executables.begin(), executables.end());

    for (const auto& path : executables) {
        const auto mz = swd2::dos::MzExecutable::load(path);
        std::cout << std::left << std::setw(14) << path.filename().string() << std::right << std::setw(9)
                  << mz.actual_size() << std::setw(8) << mz.header_size() << std::setw(8)
                  << mz.relocations().size() << "  0x" << std::hex << std::setw(8)
                  << std::setfill('0') << mz.entry_file_offset() << std::dec << std::setfill(' ')
                  << std::setw(10) << mz.overlay_size() << '\n';
    }

    std::cout << "\nresource types:\n";
    for (const auto& [extension, stats] : catalog.by_extension) {
        std::cout << "  " << std::left << std::setw(8) << extension << std::right << std::setw(5)
                  << stats.files << " files  " << std::setw(10) << stats.bytes << " bytes\n";
    }
}

void trace_launcher(const std::string& marker_script) {
    const auto markers = parse_script(marker_script);
    std::size_t cursor = 0;
    swd2::Launcher launcher(1'000);
    const auto result = launcher.run([&](swd2::Module, swd2::Marker) {
        if (cursor == markers.size()) {
            return swd2::ModuleResult{false, swd2::Marker::none};
        }
        return swd2::ModuleResult{true, markers[cursor++]};
    });

    for (const auto& transition : result.transitions) {
        std::cout << std::setw(9) << swd2::module_name(transition.module) << "  input="
                  << swd2::marker_name(transition.input) << "  output="
                  << swd2::marker_name(transition.output)
                  << (transition.launched ? "" : "  [launch failed]") << '\n';
    }
    std::cout << "stop: " << swd2::stop_reason_name(result.reason)
              << ", marker=" << swd2::marker_name(result.final_marker) << '\n';
}

class ScriptPlatform final : public swd2::PlatformBackend {
public:
    explicit ScriptPlatform(std::vector<swd2::InputAction> actions) : actions_(std::move(actions)) {}

    void present(const swd2::IndexedSurfaceView& surface) override {
        last_width = surface.width;
        last_height = surface.height;
        ++frames;
    }
    swd2::InputAction wait_for_input() override {
        if (cursor_ == actions_.size()) {
            return swd2::InputAction::quit;
        }
        return actions_[cursor_++];
    }
    swd2::ClockTime clock_time() const override { return {0, 0}; }
    void play_music(std::span<const std::uint8_t>, bool) override {}
    void play_voice(std::span<const std::uint8_t>) override {}
    void stop_audio() override {}

    std::size_t frames{};
    std::size_t last_width{};
    std::size_t last_height{};

private:
    std::vector<swd2::InputAction> actions_;
    std::size_t cursor_{};
};

std::vector<swd2::InputAction> parse_actions(const std::string& text) {
    std::vector<swd2::InputAction> result;
    std::istringstream input(text);
    for (std::string token; std::getline(input, token, ',');) {
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (token == "UP") result.push_back(swd2::InputAction::up);
        else if (token == "DOWN") result.push_back(swd2::InputAction::down);
        else if (token == "LEFT") result.push_back(swd2::InputAction::left);
        else if (token == "RIGHT") result.push_back(swd2::InputAction::right);
        else if (token == "OK" || token == "CONFIRM") result.push_back(swd2::InputAction::confirm);
        else if (token == "CANCEL") result.push_back(swd2::InputAction::cancel);
        else if (token == "TICK" || token == "NONE") result.push_back(swd2::InputAction::none);
        else if (token == "QUIT") result.push_back(swd2::InputAction::quit);
        else throw std::runtime_error("unknown scripted input: " + token);
    }
    return result;
}

void run_monolithic(const std::filesystem::path& game_root, const std::string& script,
                    const std::filesystem::path& save_root, std::uint8_t slot_number,
                    bool write_save) {
    ScriptPlatform platform(parse_actions(script));
    auto slot = swd2::SaveSlot::open(game_root, save_root, slot_number);
    swd2::GameContext context{
        game_root, slot.state(), platform, slot.map_database(),
        [&save_root](std::uint8_t selected, const swd2::SharedState& state,
                     const swd2::MapDatabase& map) {
            swd2::SaveSlot::save_as(save_root, selected, state, map);
            persist_browser_saves();
        },
    };
    swd2::ModuleRegistry modules;
    modules.add(std::make_unique<swd2::MeoModule>());
    modules.add(std::make_unique<swd2::RpgModule>());
    modules.add(std::make_unique<swd2::BattleModule>());
    modules.add(std::make_unique<swd2::DemoModule>());
    const auto result = swd2::MonolithicRuntime(std::move(modules)).run(context);
    if (write_save) slot.save(context.shared_state);
    for (const auto& transition : result.transitions) {
        std::cout << swd2::module_name(transition.module) << " -> "
                  << swd2::marker_name(transition.output) << '\n';
    }
    std::cout << "single-process frames=" << platform.frames << ", position="
              << context.shared_state.world_x() << ',' << context.shared_state.world_y()
              << ", stop=" << swd2::stop_reason_name(result.reason) << '\n';
}

#ifdef SWD2_HAVE_SDL2
void play_monolithic(const std::filesystem::path& game_root,
                     const std::filesystem::path& save_root,
                     std::uint8_t slot_number, bool write_save) {
    swd2::SdlPlatform platform;
    auto slot = swd2::SaveSlot::open(game_root, save_root, slot_number);
    swd2::GameContext context{
        game_root, slot.state(), platform, slot.map_database(),
        [&save_root](std::uint8_t selected, const swd2::SharedState& state,
                     const swd2::MapDatabase& map) {
            swd2::SaveSlot::save_as(save_root, selected, state, map);
        },
    };
    swd2::ModuleRegistry modules;
    modules.add(std::make_unique<swd2::MeoModule>());
    modules.add(std::make_unique<swd2::RpgModule>());
    modules.add(std::make_unique<swd2::BattleModule>());
    modules.add(std::make_unique<swd2::DemoModule>());
    static_cast<void>(swd2::MonolithicRuntime(std::move(modules)).run(context));
    if (write_save) {
        slot.save(context.shared_state);
        persist_browser_saves();
    }
}
#endif

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void extract_resource(const std::filesystem::path& input_path,
                      const std::filesystem::path& output_path) {
    const auto source = read_binary_file(input_path);
    const auto decoded = swd2::decode_rsk_block(source);
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open output file: " + output_path.string());
    }
    output.write(reinterpret_cast<const char*>(decoded.data.data()),
                 static_cast<std::streamsize>(decoded.data.size()));
    if (!output) {
        throw std::runtime_error("failed to write output file: " + output_path.string());
    }
    std::cout << input_path << ": " << source.size() << " -> " << decoded.data.size()
              << " bytes (" << (decoded.compressed ? "compressed" : "stored") << ")\n";
}

void render_sprite(const std::filesystem::path& input_path, std::size_t sprite_index,
                   const std::filesystem::path& output_path) {
    auto decoded = swd2::decode_rsk_block(read_binary_file(input_path));
    const auto archive = swd2::SpriteArchive::parse(std::move(decoded.data));
    const auto& info = archive.sprites().at(sprite_index);
    const auto pixels = archive.pixels(sprite_index);

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open output image: " + output_path.string());
    }
    output << "P6\n" << info.width << ' ' << info.height << "\n255\n";
    for (const auto color : pixels) {
        std::array<std::uint8_t, 3> rgb{};
        if (archive.has_palette()) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto component = archive.palette()[static_cast<std::size_t>(color) * 3 + channel];
                rgb[channel] = static_cast<std::uint8_t>(
                    std::min<unsigned>(255, (static_cast<unsigned>(component) * 255U + 31U) / 63U));
            }
        } else {
            rgb.fill(color);
        }
        output.write(reinterpret_cast<const char*>(rgb.data()), 3);
    }
    if (!output) {
        throw std::runtime_error("failed to write output image: " + output_path.string());
    }
    std::cout << "rendered sprite " << sprite_index << " (" << info.width << 'x' << info.height
              << ") to " << output_path << '\n';
}

void render_meo(const std::filesystem::path& game_root, const std::filesystem::path& output_path) {
    auto decoded = swd2::decode_rsk_block(read_binary_file(game_root / "MEO.RSK"));
    const auto archive = swd2::SpriteArchive::parse(std::move(decoded.data));
    const auto frame = swd2::render_meo_frame(archive, 0, 0, 0);

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open MEO output image: " + output_path.string());
    }
    output << "P6\n320 200\n255\n";
    for (const auto color : frame.pixels) {
        std::array<std::uint8_t, 3> rgb{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto component = frame.palette[static_cast<std::size_t>(color) * 3 + channel];
            rgb[channel] = static_cast<std::uint8_t>(
                std::min<unsigned>(255, (static_cast<unsigned>(component) * 255U + 31U) / 63U));
        }
        output.write(reinterpret_cast<const char*>(rgb.data()), 3);
    }
    std::cout << "rendered reconstructed MEO frame to " << output_path << '\n';
}

void render_map(const std::filesystem::path& base_path, const std::filesystem::path& output_path) {
    const auto map = swd2::MapResource::load(base_path);
    const auto image = map.render();
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open map output image: " + output_path.string());
    }
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (const auto color : image.pixels) {
        std::array<std::uint8_t, 3> rgb{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto component = image.palette[static_cast<std::size_t>(color) * 3 + channel];
            rgb[channel] = static_cast<std::uint8_t>(
                std::min<unsigned>(255, (static_cast<unsigned>(component) * 255U + 31U) / 63U));
        }
        output.write(reinterpret_cast<const char*>(rgb.data()), 3);
    }
    std::cout << "rendered " << image.width << 'x' << image.height << " map with "
              << map.tile_count() << " tiles and " << map.overlays().size() << " overlays to "
              << output_path << '\n';
}

void render_planar_sprite(const std::filesystem::path& base_path, std::size_t frame_index,
                          const std::filesystem::path& output_path) {
    const auto animation = swd2::PlanarSpriteSet::load(base_path);
    const auto& frame = animation.frame(frame_index);
    std::ofstream output(output_path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot open planar sprite output image");
    output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (const auto color : frame.pixels) {
        std::array<std::uint8_t, 3> rgb{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto component = animation.palette()[static_cast<std::size_t>(color) * 3 + channel];
            rgb[channel] = static_cast<std::uint8_t>(
                std::min<unsigned>(255, (static_cast<unsigned>(component) * 255U + 31U) / 63U));
        }
        output.write(reinterpret_cast<const char*>(rgb.data()), 3);
    }
    std::cout << "rendered planar sprite frame " << frame_index << " (" << frame.width << 'x'
              << frame.height << ") to " << output_path << '\n';
}

void verify_resources(const std::filesystem::path& game_root) {
    const std::set<std::string> supported = {".RSK", ".RS1", ".RS2", ".RS3",
                                             ".RS4", ".RAP", ".RRO"};
    std::uint64_t files = 0;
    std::uint64_t packed_bytes = 0;
    std::uint64_t unpacked_bytes = 0;
    std::uint64_t unwrapped_files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(game_root)) {
        if (!entry.is_regular_file() || !supported.contains(entry.path().extension().string())) {
            continue;
        }
        const auto source = read_binary_file(entry.path());
        const bool wrapped = source.size() >= 4 && source[source.size() - 4] == 0x60 &&
                             source[source.size() - 3] == 0xea &&
                             source[source.size() - 2] == 0x00 &&
                             source[source.size() - 1] == 0x00;
        if (!wrapped) {
            // 166 map .RSK files are raw companion metadata rather than compressed blocks.
            ++unwrapped_files;
            continue;
        }
        try {
            const auto decoded = swd2::decode_rsk_block(source);
            ++files;
            packed_bytes += source.size();
            unpacked_bytes += decoded.data.size();
        } catch (const std::exception& error) {
            throw std::runtime_error(entry.path().string() + ": " + error.what());
        }
    }
    std::cout << "verified " << files << " resources: " << packed_bytes << " packed bytes -> "
              << unpacked_bytes << " unpacked bytes; " << unwrapped_files
              << " raw map companions skipped\n";
}

void verify_maps(const std::filesystem::path& game_root) {
    std::uint64_t maps = 0;
    std::uint64_t cells = 0;
    std::uint64_t overlays = 0;
    std::uint64_t tiles = 0;
    std::uint64_t animation_sets = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(game_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".RSK" ||
            entry.file_size() != 818) {
            continue;
        }
        const auto relative = std::filesystem::relative(entry.path(), game_root);
        const auto top = relative.begin()->string();
        if (top.size() != 2 || top[0] != 'T' || top[1] < '1' || top[1] > '9') {
            continue;  // DE uses the same tile metadata wrapper for character animations.
        }
        auto base = entry.path();
        base.replace_extension();
        try {
            const auto map = swd2::MapResource::load(base);
            ++maps;
            cells += map.cells().size();
            overlays += map.overlays().size();
            tiles += map.tile_count();
        } catch (const std::exception& error) {
            if (std::string(error.what()) == "unsupported RAP tile size") {
                ++animation_sets;
                continue;
            }
            throw std::runtime_error(base.string() + ": " + error.what());
        }
    }
    std::size_t de_sets = 0;
    std::size_t de_frames = 0;
    for (const auto& entry : std::filesystem::directory_iterator(game_root / "DE")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".RSK") continue;
        auto base = entry.path();
        base.replace_extension();
        const auto animation = swd2::PlanarSpriteSet::load(base);
        ++de_sets;
        de_frames += animation.frame_count();
    }
    std::cout << "verified " << maps << " maps: " << tiles << " tiles, " << cells
              << " cells, " << overlays << " overlay records; " << animation_sets
              << " non-map tile sets skipped; " << de_sets << " DE sprite sets, "
              << de_frames << " frames\n";
}

void verify_battles(const std::filesystem::path& game_root) {
    const auto battles = swd2::BattleDatabase::load(game_root / "ORC.EXE");
    const auto abilities = swd2::BattleAbilityDatabase::load(game_root / "FIG.EXE");
    const auto mon = swd2::MonDatabase::load(game_root / "MON.EXE");
    const auto items = swd2::ScriptArchive::load(game_root / "ITEM.EXE");
    std::set<std::string> backgrounds;
    std::set<std::uint16_t> definitions;
    std::set<std::uint16_t> monster_sprites;
    for (const auto& encounter : battles.encounters()) {
        backgrounds.insert(encounter.background_path);
        for (const auto definition : encounter.monster_definition_ids) {
            if (definition != 0) definitions.insert(definition);
        }
    }
    for (const auto& path : backgrounds) {
        const auto archive = swd2::SpriteArchive::parse(swd2::decode_rsk_block(
            read_binary_file(game_root / swd2::normalize_dos_asset_path(path))).data);
        if (archive.sprites().front().width != 320 || archive.sprites().front().height != 200 ||
            !archive.has_palette()) {
            throw std::runtime_error("invalid FIG background: " + path);
        }
    }
    for (const auto definition : definitions) {
        const auto record_index = static_cast<std::size_t>(definition) + 2;
        if (record_index >= items.entry_count()) {
            throw std::runtime_error("ORC definition is outside ITEM.EXE");
        }
        const auto monster = swd2::MonsterDefinition::parse(
            definition, items.entry(record_index));
        monster_sprites.insert(monster.sprite_number);
    }
    for (const auto sprite : monster_sprites) {
        std::ostringstream name;
        name << "CD" << std::setw(3) << std::setfill('0') << sprite << ".RSK";
        const auto archive = swd2::SpriteArchive::parse(swd2::decode_rsk_block(read_binary_file(
            game_root / (sprite < 300 ? "CD" : "AD") / name.str())).data);
        if (archive.sprites().empty()) throw std::runtime_error("empty FIG monster archive");
    }
    std::size_t fighter_archives = 0;
    for (const auto& path : {game_root / "SW" / "FMAN.RSK", game_root / "BMAN1.RSK",
                             game_root / "BMAN2.RSK", game_root / "BMAN3.RSK",
                             game_root / "BMAN4.RSK", game_root / "BMAN5.RSK",
                             game_root / "BMAN6.RSK"}) {
        static_cast<void>(swd2::SpriteArchive::parse(
            swd2::decode_rsk_block(read_binary_file(path)).data));
        ++fighter_archives;
    }
    std::cout << "verified ORC battle data: " << battles.directory_entry_count()
              << " directory entries, " << battles.encounter_count() << " unique encounters, "
              << backgrounds.size() << " backgrounds, " << definitions.size()
              << " monster definitions, " << monster_sprites.size() << " monster archives, "
              << fighter_archives << " fighter archives; MON 17x17 matrix and "
              << mon.value_entry_count() << " value entries; "
              << abilities.abilities().size() << " FIG ability records\n";
}

void verify_legacy_program_data(const std::filesystem::path& game_root) {
    const auto world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    std::size_t archives = 0;
    std::size_t entries = 0;
    std::size_t unique_records = 0;
    std::size_t decoded_event_records = 0;
    std::size_t decoded_commands = 0;
    for (const auto& entry : std::filesystem::directory_iterator(game_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".EXE" ||
            !swd2::ScriptArchive::probe(entry.path())) {
            continue;
        }
        const auto archive = swd2::ScriptArchive::load(entry.path());
        ++archives;
        entries += archive.entry_count();
        unique_records += archive.unique_record_count();
        std::set<std::uint16_t> visited;
        for (std::size_t index = 1; index < archive.entry_count(); ++index) {
            if (!visited.insert(archive.offsets()[index]).second) continue;
            try {
                const auto record = swd2::decode_event_record(archive.entry(index));
                if (record.consumed_bytes == archive.entry(index).size()) {
                    ++decoded_event_records;
                    decoded_commands += record.commands.size();
                }
            } catch (...) {
                // Initial archive entries are filenames/metadata, not bytecode.
            }
        }
    }

    std::size_t fonts = 0;
    std::size_t glyphs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(game_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".DSK") continue;
        try {
            const auto font = swd2::LegacyFont::load(entry.path());
            ++fonts;
            glyphs += font.glyph_count();
        } catch (...) {
            // GA contains unrelated DSK formats; only main-game glyph subsets count.
        }
    }
    std::cout << "verified MAPA world database: " << world.locations().size()
              << " locations, " << world.unique_area_count() << " unique area records; "
              << archives << " MZ-wrapped script archives: " << entries
              << " directory entries, " << unique_records << " unique records; " << fonts
              << " DSK fonts, " << glyphs << " Big5 glyphs; " << decoded_event_records
              << " event records, " << decoded_commands << " commands decoded\n";
}

void usage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << " [--game DIR] --inspect\n"
              << "  " << program << " [--game DIR] --verify-resources\n"
              << "  " << program << " [--game DIR] --verify-maps\n"
              << "  " << program << " [--game DIR] --verify-battles\n"
              << "  " << program << " [--game DIR] --verify-legacy-program-data\n"
              << "  " << program << " --extract INPUT OUTPUT\n"
              << "  " << program << " --render INPUT INDEX OUTPUT.ppm\n"
              << "  " << program << " [--game DIR] --render-meo OUTPUT.ppm\n"
              << "  " << program << " --render-map BASE_PATH OUTPUT.ppm\n"
              << "  " << program << " --render-planar BASE_PATH INDEX OUTPUT.ppm\n"
              << "  " << program << " --trace MT,ED,--,IF,OC,--\n";
    std::cout << "  " << program
              << " [--game DIR] [--save-dir DIR] [--slot 1..5] [--no-save]"
                 " --run-script CONFIRM,CONFIRM,CONFIRM,RIGHT,DOWN,QUIT\n";
#ifdef SWD2_HAVE_SDL2
    std::cout << "  " << program
              << " [--game DIR] [--save-dir DIR] [--slot 1..5] [--no-save] --play\n";
#endif
}

}  // namespace

int main(int argc, char** argv) {
    try {
        auto game_root = find_default_game_root();
        std::filesystem::path save_root;
        std::uint8_t slot_number = 1;
        bool write_save = true;
        enum class Mode { inspect, trace, verify, verify_maps, verify_battles, verify_legacy, extract, render,
                          render_meo, render_map, render_planar, run, play } mode = Mode::inspect;
        std::string trace;
        std::filesystem::path extract_input;
        std::filesystem::path extract_output;
        std::size_t render_index = 0;

        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--game" && i + 1 < argc) {
                game_root = argv[++i];
            } else if (argument == "--save-dir" && i + 1 < argc) {
                save_root = argv[++i];
            } else if (argument == "--slot" && i + 1 < argc) {
                const auto parsed = std::stoul(argv[++i]);
                if (parsed < 1 || parsed > 5) {
                    throw std::runtime_error("--slot must be in the range 1..5");
                }
                slot_number = static_cast<std::uint8_t>(parsed);
            } else if (argument == "--no-save") {
                write_save = false;
            } else if (argument == "--inspect") {
                mode = Mode::inspect;
            } else if (argument == "--trace" && i + 1 < argc) {
                mode = Mode::trace;
                trace = argv[++i];
            } else if (argument == "--verify-resources") {
                mode = Mode::verify;
            } else if (argument == "--verify-maps") {
                mode = Mode::verify_maps;
            } else if (argument == "--verify-battles") {
                mode = Mode::verify_battles;
            } else if (argument == "--verify-legacy-program-data") {
                mode = Mode::verify_legacy;
            } else if (argument == "--extract" && i + 2 < argc) {
                mode = Mode::extract;
                extract_input = argv[++i];
                extract_output = argv[++i];
            } else if (argument == "--render" && i + 3 < argc) {
                mode = Mode::render;
                extract_input = argv[++i];
                render_index = static_cast<std::size_t>(std::stoul(argv[++i]));
                extract_output = argv[++i];
            } else if (argument == "--render-meo" && i + 1 < argc) {
                mode = Mode::render_meo;
                extract_output = argv[++i];
            } else if (argument == "--render-map" && i + 2 < argc) {
                mode = Mode::render_map;
                extract_input = argv[++i];
                extract_output = argv[++i];
            } else if (argument == "--render-planar" && i + 3 < argc) {
                mode = Mode::render_planar;
                extract_input = argv[++i];
                render_index = static_cast<std::size_t>(std::stoul(argv[++i]));
                extract_output = argv[++i];
            } else if (argument == "--run-script" && i + 1 < argc) {
                mode = Mode::run;
                trace = argv[++i];
            } else if (argument == "--play") {
                mode = Mode::play;
            } else if (argument == "--help" || argument == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("unknown or incomplete argument: " + argument);
            }
        }

        if (save_root.empty()) save_root = game_root / "portable-saves";

        if (mode == Mode::trace) {
            trace_launcher(trace);
        } else if (mode == Mode::verify) {
            verify_resources(game_root);
        } else if (mode == Mode::verify_maps) {
            verify_maps(game_root);
        } else if (mode == Mode::verify_battles) {
            verify_battles(game_root);
        } else if (mode == Mode::verify_legacy) {
            verify_legacy_program_data(game_root);
        } else if (mode == Mode::extract) {
            extract_resource(extract_input, extract_output);
        } else if (mode == Mode::render) {
            render_sprite(extract_input, render_index, extract_output);
        } else if (mode == Mode::render_meo) {
            render_meo(game_root, extract_output);
        } else if (mode == Mode::render_map) {
            render_map(extract_input, extract_output);
        } else if (mode == Mode::render_planar) {
            render_planar_sprite(extract_input, render_index, extract_output);
        } else if (mode == Mode::run) {
            run_monolithic(game_root, trace, save_root, slot_number, write_save);
        } else if (mode == Mode::play) {
#ifdef SWD2_HAVE_SDL2
            play_monolithic(game_root, save_root, slot_number, write_save);
#else
            throw std::runtime_error("this build has no SDL2 frontend");
#endif
        } else {
            inspect(game_root);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
