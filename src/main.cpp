#include "swd2/asset_catalog.hpp"
#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_database.hpp"
#include "swd2/battle_module.hpp"
#include "swd2/event_program.hpp"
#include "swd2/event_vm.hpp"
#include "swd2/demo_module.hpp"
#include "swd2/launcher.hpp"
#include "swd2/legacy_font.hpp"
#include "swd2/meo.hpp"
#include "swd2/map_resource.hpp"
#include "swd2/map_database.hpp"
#include "swd2/map_transition_database.hpp"
#include "swd2/meo_module.hpp"
#include "swd2/mon_database.hpp"
#include "swd2/monster_definition.hpp"
#include "swd2/rpg_module.hpp"
#include "swd2/replay_input.hpp"
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
#include <map>
#include <optional>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
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
    if (value == "01" || value == "REJECTED") return swd2::Marker::menu_rejected;
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

class ReplayPlatform final : public swd2::PlatformBackend {
public:
    explicit ReplayPlatform(std::vector<swd2::ReplayInputStep> steps)
        : steps_(std::move(steps)) {}

    void present(const swd2::IndexedSurfaceView& surface) override {
        record_surface(surface, false);
    }
    void present_direct_update(const swd2::IndexedSurfaceView& surface) override {
        record_surface(surface, true);
    }
    swd2::InputAction wait_for_input() override {
        ++wait_calls;
        return consume(swd2::ReplayInputBoundary::wait, true);
    }
    swd2::InputAction poll_input() override {
        ++poll_calls;
        return consume(swd2::ReplayInputBoundary::poll, false);
    }
    swd2::InputAction poll_text_input() override {
        ++text_calls;
        return consume(swd2::ReplayInputBoundary::text, false);
    }
    bool poll_frontend_quit() override {
        ++frontend_calls;
        if (cursor_ == steps_.size() ||
            steps_[cursor_].boundary != swd2::ReplayInputBoundary::frontend) {
            return false;
        }
        const auto action = steps_[cursor_++].action;
        unmatched_nonblocking_calls_ = 0;
        return action == swd2::InputAction::quit;
    }
    swd2::ClockTime clock_time() const override { return {0, 0}; }
    void delay_for(std::chrono::milliseconds duration) override {
        if (duration.count() > 0) {
            delay_milliseconds += static_cast<std::uint64_t>(duration.count());
        }
    }
    void play_music(std::span<const std::uint8_t> bytes, bool loop) override {
        ++music_calls;
        mix(audio_digest, static_cast<std::uint64_t>(loop ? 1U : 0U));
        mix(audio_digest, bytes);
    }
    void play_voice(std::span<const std::uint8_t> bytes) override {
        ++voice_calls;
        mix(audio_digest, std::uint64_t{2});
        mix(audio_digest, bytes);
    }
    void stop_music() override {
        ++stop_music_calls;
        mix(audio_digest, std::uint64_t{3});
    }
    void stop_audio() override {
        ++stop_audio_calls;
        mix(audio_digest, std::uint64_t{4});
    }

    std::size_t frames{};
    std::size_t direct_updates{};
    std::size_t last_width{};
    std::size_t last_height{};
    std::size_t wait_calls{};
    std::size_t poll_calls{};
    std::size_t text_calls{};
    std::size_t frontend_calls{};
    std::size_t music_calls{};
    std::size_t voice_calls{};
    std::size_t stop_music_calls{};
    std::size_t stop_audio_calls{};
    std::size_t implicit_quit_calls{};
    std::uint64_t delay_milliseconds{};
    std::uint64_t frame_digest{14695981039346656037ULL};
    std::uint64_t audio_digest{14695981039346656037ULL};
    std::vector<std::uint64_t> frame_hashes;

    [[nodiscard]] std::size_t input_count() const noexcept {
        return steps_.size();
    }
    [[nodiscard]] std::size_t consumed_inputs() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t remaining_inputs() const noexcept {
        return steps_.size() - cursor_;
    }

private:
    static void mix(std::uint64_t& digest, std::uint8_t value) noexcept {
        digest ^= value;
        digest *= 1099511628211ULL;
    }
    static void mix(std::uint64_t& digest,
                    std::span<const std::uint8_t> bytes) noexcept {
        for (const auto byte : bytes) mix(digest, byte);
    }
    static void mix(std::uint64_t& digest, std::uint64_t value) noexcept {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            mix(digest, static_cast<std::uint8_t>(value >> shift));
        }
    }

    void record_surface(const swd2::IndexedSurfaceView& surface, bool direct) {
        if (surface.width == 0 || surface.height == 0 ||
            surface.pixels.size() != surface.width * surface.height) {
            throw std::runtime_error("replay frontend received an invalid frame");
        }
        last_width = surface.width;
        last_height = surface.height;
        ++frames;
        if (direct) ++direct_updates;
        auto frame_hash = std::uint64_t{14695981039346656037ULL};
        mix(frame_hash, static_cast<std::uint64_t>(direct ? 1U : 0U));
        mix(frame_hash, static_cast<std::uint64_t>(surface.width));
        mix(frame_hash, static_cast<std::uint64_t>(surface.height));
        mix(frame_hash, surface.pixels);
        mix(frame_hash, surface.palette);
        frame_hashes.push_back(frame_hash);
        mix(frame_digest, static_cast<std::uint64_t>(direct ? 1U : 0U));
        mix(frame_digest, static_cast<std::uint64_t>(surface.width));
        mix(frame_digest, static_cast<std::uint64_t>(surface.height));
        mix(frame_digest, surface.pixels);
        mix(frame_digest, surface.palette);
    }

    swd2::InputAction consume(swd2::ReplayInputBoundary boundary,
                              bool blocking) {
        if (cursor_ == steps_.size()) {
            ++implicit_quit_calls;
            return swd2::InputAction::quit;
        }
        const auto& step = steps_[cursor_];
        if (step.boundary == swd2::ReplayInputBoundary::any ||
            step.boundary == boundary) {
            ++cursor_;
            unmatched_nonblocking_calls_ = 0;
            return step.action;
        }
        if (blocking) {
            throw std::runtime_error(
                "replay boundary mismatch: runtime requested " +
                std::string(swd2::replay_boundary_name(boundary)) +
                " but next input requires " +
                std::string(swd2::replay_boundary_name(step.boundary)));
        }
        if (++unmatched_nonblocking_calls_ > 1'000'000U) {
            throw std::runtime_error(
                "replay made no progress across one million nonblocking polls");
        }
        return swd2::InputAction::none;
    }

    std::vector<swd2::ReplayInputStep> steps_;
    std::size_t cursor_{};
    std::size_t unmatched_nonblocking_calls_{};
};

std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) noexcept {
    auto digest = std::uint64_t{14695981039346656037ULL};
    for (const auto byte : bytes) {
        digest ^= byte;
        digest *= 1099511628211ULL;
    }
    return digest;
}

std::uint64_t fnv1a_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot hash replay artifact: " + path.string());
    }
    auto digest = std::uint64_t{14695981039346656037ULL};
    for (char value{}; input.get(value);) {
        digest ^= static_cast<std::uint8_t>(value);
        digest *= 1099511628211ULL;
    }
    if (!input.eof()) {
        throw std::runtime_error("failed while hashing replay artifact: " +
                                 path.string());
    }
    return digest;
}

std::string hex_digest(std::uint64_t digest) {
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << digest;
    return output.str();
}

void write_replay_trace(const std::filesystem::path& path,
                        const ReplayPlatform& platform,
                        const swd2::GameContext& context,
                        const swd2::LaunchResult& result) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::optional<std::uint64_t> map_digest;
    if (context.map_database) {
        auto probe = path;
        probe += ".mapz-probe";
        context.map_database->save(probe);
        try {
            map_digest = fnv1a_file(probe);
        } catch (...) {
            std::filesystem::remove(probe);
            throw;
        }
        std::filesystem::remove(probe);
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create replay trace: " + path.string());
    }
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"input\": {\"total\": " << platform.input_count()
           << ", \"consumed\": " << platform.consumed_inputs()
           << ", \"remaining\": " << platform.remaining_inputs()
           << ", \"implicit_quit_calls\": " << platform.implicit_quit_calls
           << "},\n"
           << "  \"boundaries\": {\"wait\": " << platform.wait_calls
           << ", \"poll\": " << platform.poll_calls
           << ", \"text\": " << platform.text_calls
           << ", \"frontend\": " << platform.frontend_calls << "},\n"
           << "  \"video\": {\"frames\": " << platform.frames
           << ", \"direct_updates\": " << platform.direct_updates
           << ", \"last_width\": " << platform.last_width
           << ", \"last_height\": " << platform.last_height
           << ", \"fnv1a64\": \"" << hex_digest(platform.frame_digest)
           << "\"},\n"
           << "  \"frame_fnv1a64\": [\n";
    for (std::size_t index = 0; index < platform.frame_hashes.size(); ++index) {
        output << "    \"" << hex_digest(platform.frame_hashes[index]) << '"';
        if (index + 1U != platform.frame_hashes.size()) output << ',';
        output << '\n';
    }
    output << "  ],\n"
           << "  \"audio\": {\"music_calls\": " << platform.music_calls
           << ", \"voice_calls\": " << platform.voice_calls
           << ", \"stop_music_calls\": " << platform.stop_music_calls
           << ", \"stop_audio_calls\": " << platform.stop_audio_calls
           << ", \"fnv1a64\": \"" << hex_digest(platform.audio_digest)
           << "\"},\n"
           << "  \"delay_milliseconds\": " << platform.delay_milliseconds
           << ",\n"
           << "  \"state_fnv1a64\": \""
           << hex_digest(fnv1a(context.shared_state.bytes())) << "\",\n"
           << "  \"mapz_fnv1a64\": ";
    if (map_digest) output << '"' << hex_digest(*map_digest) << '"';
    else output << "null";
    output << ",\n"
           << "  \"stop_reason\": \"" << swd2::stop_reason_name(result.reason)
           << "\",\n"
           << "  \"final_marker\": \"" << swd2::marker_name(result.final_marker)
           << "\",\n"
           << "  \"transitions\": [\n";
    for (std::size_t index = 0; index < result.transitions.size(); ++index) {
        const auto& transition = result.transitions[index];
        output << "    {\"module\": \"" << swd2::module_name(transition.module)
               << "\", \"input\": \"" << swd2::marker_name(transition.input)
               << "\", \"output\": \"" << swd2::marker_name(transition.output)
               << "\", \"launched\": "
               << (transition.launched ? "true" : "false") << '}';
        if (index + 1U != result.transitions.size()) output << ',';
        output << '\n';
    }
    output << "  ]\n}\n";
    if (!output) {
        throw std::runtime_error("failed to write replay trace: " + path.string());
    }
}

void run_monolithic(const std::filesystem::path& game_root,
                    std::vector<swd2::ReplayInputStep> inputs,
                    const std::filesystem::path& save_root, std::uint8_t slot_number,
                    bool write_save,
                    const std::optional<std::filesystem::path>& trace_output,
                    bool require_all_inputs) {
    ReplayPlatform platform(std::move(inputs));
    auto slot = swd2::SaveSlot::open(game_root, save_root, slot_number);
    swd2::GameContext context{
        game_root, slot.state(), platform, slot.map_database(),
        [&save_root](std::uint8_t selected, const swd2::SharedState& state,
                     const swd2::MapDatabase& map) {
            swd2::SaveSlot::save_as(save_root, selected, state, map);
            persist_browser_saves();
        },
        [&slot, &game_root, &save_root](std::uint8_t selected) {
            slot = swd2::SaveSlot::open(game_root, save_root, selected);
            return swd2::LoadedSaveSlot{slot.state(), slot.map_database()};
        },
    };
    swd2::ModuleRegistry modules;
    modules.add(std::make_unique<swd2::MeoModule>());
    modules.add(std::make_unique<swd2::RpgModule>());
    modules.add(std::make_unique<swd2::BattleModule>());
    modules.add(std::make_unique<swd2::DemoModule>());
    const auto result = swd2::MonolithicRuntime(std::move(modules)).run(context);
    if (write_save) slot.save(context.shared_state);
    if (trace_output) {
        write_replay_trace(*trace_output, platform, context, result);
    }
    if (require_all_inputs && platform.remaining_inputs() != 0U) {
        throw std::runtime_error(
            "replay stopped before consuming all boundary-locked inputs");
    }
    if (require_all_inputs && platform.implicit_quit_calls != 0U) {
        throw std::runtime_error(
            "replay exhausted its input and relied on an implicit quit");
    }
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
            // Native renames are durable on return. In a browser /saves is
            // IDBFS, so every explicit system-menu save must also flush the
            // in-memory filesystem instead of waiting for the game to exit.
            persist_browser_saves();
        },
        [&slot, &game_root, &save_root](std::uint8_t selected) {
            slot = swd2::SaveSlot::open(game_root, save_root, selected);
            return swd2::LoadedSaveSlot{slot.state(), slot.map_database()};
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

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open replay input file: " + path.string());
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        throw std::runtime_error("failed to read replay input file: " + path.string());
    }
    return contents.str();
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
    const auto transitions =
        swd2::MapTransitionDatabase::load(game_root / "MAP0.EXE");
    std::cout << "verified " << maps << " maps: " << tiles << " tiles, " << cells
              << " cells, " << overlays << " overlay records; " << animation_sets
              << " non-map tile sets skipped; " << de_sets << " DE sprite sets, "
              << de_frames << " frames; " << transitions.area_count()
              << " MAP0 areas, " << transitions.record_count()
              << " transition records\n";
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
    std::set<std::uint16_t> weapon_items;
    const std::regex weapon_name{R"(^SW([0-9]{3})\.RSK$)"};
    for (const auto& entry : std::filesystem::directory_iterator(game_root / "SW")) {
        if (!entry.is_regular_file()) continue;
        std::smatch match;
        const auto filename = entry.path().filename().string();
        if (!std::regex_match(filename, match, weapon_name)) continue;
        const auto weapon = static_cast<std::uint16_t>(std::stoul(match[1].str()));
        if (!weapon_items.insert(weapon).second) {
            throw std::runtime_error("duplicate FIG weapon archive id");
        }
        const auto archive = swd2::SpriteArchive::parse(
            swd2::decode_rsk_block(
                read_binary_file(entry.path())).data);
        if (archive.sprites().empty()) {
            throw std::runtime_error("empty FIG weapon archive: " + filename);
        }
    }
    if (weapon_items.size() != 50U || !weapon_items.contains(0) ||
        !weapon_items.contains(117) || !weapon_items.contains(164) ||
        !weapon_items.contains(324)) {
        throw std::runtime_error("released FIG weapon archive set changed");
    }
    std::cout << "verified ORC battle data: " << battles.directory_entry_count()
              << " directory entries, " << battles.encounter_count() << " unique encounters, "
              << backgrounds.size() << " backgrounds, " << definitions.size()
              << " monster definitions, " << monster_sprites.size() << " monster archives, "
              << fighter_archives << " fighter archives, "
              << weapon_items.size() << " weapon archives; MON 17x17 matrix and "
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

void verify_reachable_events(const std::filesystem::path& game_root) {
    const auto map_database_path = game_root / "MAPA.EXE";
    const auto world = swd2::MapDatabase::load(map_database_path);
    std::map<std::string, std::set<std::uint16_t>> roots_by_archive;
    for (const auto& location : world.locations()) {
        for (const auto target : location.area.entity_fields[9]) {
            // A zero in an area's initial entity table means no interaction.
            // Zero reached later through opcode 2/3 remains a valid empty
            // CHNA directory slot and is handled by the queue below.
            if (target != 0) {
                roots_by_archive[location.area.event_archive_path].insert(target);
            }
        }
    }

    struct ArchiveAudit {
        explicit ArchiveAudit(swd2::ScriptArchive loaded)
            : archive(std::move(loaded)) {}
        swd2::ScriptArchive archive;
        std::set<std::uint16_t> visited;
        std::set<std::uint16_t> opcodes;
        std::size_t commands{};
    };

    struct EventState {
        std::string archive_name;
        std::uint16_t target{};
        std::uint16_t active_location_offset{};
        std::size_t entity{};
    };
    std::queue<EventState> pending;
    for (const auto& location : world.locations()) {
        for (std::size_t entity = 0;
             entity < location.area.entity_count(); ++entity) {
            const auto target = location.area.entity_fields[9][entity];
            if (target != 0) {
                pending.push({location.area.event_archive_path, target,
                              location.directory_offset, entity});
            }
        }
    }
    std::map<std::string, std::unique_ptr<ArchiveAudit>> audits;
    const auto audit_for = [&](const std::string& archive_name) -> ArchiveAudit& {
        auto found = audits.find(archive_name);
        if (found == audits.end()) {
            found = audits.emplace(
                archive_name,
                std::make_unique<ArchiveAudit>(swd2::ScriptArchive::load(
                    game_root / swd2::normalize_dos_asset_path(archive_name)))).first;
        }
        return *found->second;
    };
    const auto enqueue = [&](const std::string& archive_name,
                             std::uint16_t target,
                             std::uint16_t active_location_offset,
                             std::size_t entity) {
        pending.push({archive_name, target, active_location_offset, entity});
    };

    std::set<std::uint16_t> all_opcodes;
    std::set<std::tuple<std::string, std::uint16_t, std::uint16_t,
                        std::size_t>> visited_states;
    std::size_t inert_odd_targets = 0;
    std::size_t map_mutations = 0;
    std::size_t runtime_validated_mutations = 0;
    std::size_t event_pointer_mutations = 0;
    std::size_t invisible_fixed_bss_operations = 0;
    std::set<std::tuple<std::string, std::uint16_t, std::uint16_t,
                        std::size_t>> dynamic_entity_contexts;
    while (!pending.empty()) {
        auto state = std::move(pending.front());
        pending.pop();
        if (!visited_states.emplace(state.archive_name, state.target,
                                    state.active_location_offset,
                                    state.entity).second) {
            continue;
        }
        const auto& archive_name = state.archive_name;
        const auto target = state.target;
        auto& audit = audit_for(archive_name);
        const auto first_record_visit = audit.visited.insert(target).second;
        if ((target & 1U) != 0 || target / 2U >= audit.archive.entry_count()) {
            throw std::runtime_error(
                archive_name + " has a reachable branch outside its directory: " +
                std::to_string(target));
        }
        const auto record = swd2::decode_event_record(
            audit.archive.event_stream(target / 2U));
        if (first_record_visit) audit.commands += record.commands.size();
        auto active_location_offset = state.active_location_offset;
        auto active_area_archive = world.location_at_directory_offset(
            active_location_offset).area.event_archive_path;
        for (std::size_t command_index = 0;
             command_index < record.commands.size(); ++command_index) {
            const auto& command = record.commands[command_index];
            if (first_record_visit) {
                audit.opcodes.insert(command.opcode);
                all_opcodes.insert(command.opcode);
            }

            if (((command.opcode >= 23U && command.opcode <= 27U) ||
                 command.opcode == 39U) && !command.arguments.empty() &&
                (command.arguments[0] & 1U) == 0U) {
                const auto entity_index =
                    static_cast<std::size_t>(command.arguments[0] / 2U);
                const auto entity_count = world.location_at_directory_offset(
                    active_location_offset).area.entity_count();
                if (entity_index >= entity_count) {
                    const auto documented_release_slot =
                        archive_name == "CHNA0.EXE" && target == 740U &&
                        command_index == 75U && command.opcode == 25U &&
                        command.arguments[0] == 22U &&
                        active_location_offset == 144U &&
                        entity_index == 11U && entity_count == 5U;
                    if (!documented_release_slot) {
                        throw std::runtime_error(
                            "reachable event uses an unexplained fixed-BSS entity slot");
                    }
                    if (first_record_visit) ++invisible_fixed_bss_operations;
                }
            }

            const auto enqueue_branch = [&](std::uint16_t next) {
                if ((next & 1U) != 0) {
                    // CHNA1 entry 189 contains opcode 40 (item zero, odd
                    // target 287). Item zero necessarily matches an empty
                    // inventory slot on that path, so the malformed target is
                    // inert in the original as well. Count it explicitly so
                    // another unexplained odd target cannot be introduced.
                    if (first_record_visit) ++inert_odd_targets;
                    return;
                }
                enqueue(archive_name, next, active_location_offset,
                        state.entity);
            };

            if (command.opcode == 2 && !command.arguments.empty()) {
                // Opcode 17 can immediately reload this pointer from the
                // already-open archive. It also remains live on a relocated
                // area's future interactions, whose CHNA path may differ.
                enqueue_branch(command.arguments[0]);
                enqueue(active_area_archive, command.arguments[0],
                        active_location_offset, state.entity);
            } else if (command.opcode == 3 &&
                       command.arguments.size() >= 2U &&
                       command.arguments[0] == 9U) {
                enqueue(active_area_archive, command.arguments[1],
                        active_location_offset, state.entity);
            } else if ((command.opcode == 4 || command.opcode == 15 ||
                        command.opcode == 21 || command.opcode == 40) &&
                       command.arguments.size() >= 2U) {
                enqueue_branch(command.arguments[1]);
            } else if (command.opcode == 13 && !command.arguments.empty()) {
                enqueue_branch(command.arguments[0]);
            }

            if (command.opcode == 37 && !command.arguments.empty() &&
                (command.arguments[0] & 0x8000U) == 0U) {
                const auto& destination = world.location_at_directory_offset(
                    static_cast<std::uint16_t>(command.arguments[0] & 0x1fffU));
                active_location_offset = destination.directory_offset;
                active_area_archive = destination.area.event_archive_path;
            }

            if (command.opcode != 34) continue;
            std::size_t cursor = 0;
            bool terminated = false;
            while (cursor < command.arguments.size()) {
                const auto location_offset = command.arguments[cursor++];
                if (location_offset == 0xf800U) {
                    terminated = true;
                    break;
                }
                if (cursor + 3U > command.arguments.size()) {
                    throw std::runtime_error(
                        "reachable opcode 34 MAPZ mutation is truncated");
                }
                const auto field = command.arguments[cursor++];
                const auto byte_offset = static_cast<std::int16_t>(
                    command.arguments[cursor++]);
                const auto operation = command.arguments[cursor++];
                const auto additive = operation == 0x4144U;
                auto value = operation;
                if (additive) {
                    if (cursor == command.arguments.size()) {
                        throw std::runtime_error(
                            "reachable additive opcode 34 mutation is truncated");
                    }
                    value = command.arguments[cursor++];
                }
                if (first_record_visit) ++map_mutations;

                // Validate every released write against the real runtime
                // mutator in isolation. This catches typed-area assumptions,
                // invalid path-pointer reparses and unaligned writes without
                // inventing a story order for mutually exclusive records.
                if (first_record_visit) {
                    auto mutation_probe =
                        swd2::MapDatabase::load(map_database_path);
                    mutation_probe.mutate_area_word(location_offset, field,
                                                    byte_offset, value, additive);
                    ++runtime_validated_mutations;
                }

                const auto& destination =
                    world.location_at_directory_offset(location_offset);
                const auto count = static_cast<std::int64_t>(
                    destination.area.entity_count());
                const auto relative = static_cast<std::int64_t>(6) +
                    static_cast<std::int64_t>(field) * count * 2 + byte_offset;
                const auto event_archive_pointer =
                    static_cast<std::int64_t>(6) + 11 * count * 2 + 3 * 2;
                if (relative < event_archive_pointer + 2 &&
                    relative + 2 > event_archive_pointer) {
                    throw std::runtime_error(
                        "reachable opcode 34 dynamically changes a CHNA path");
                }
                const auto event_field_begin =
                    static_cast<std::int64_t>(6) + 9 * count * 2;
                const auto event_field_end = event_field_begin + count * 2;
                if (relative >= event_field_end || relative + 2 <= event_field_begin) {
                    continue;
                }
                if (count == 0 || relative < event_field_begin ||
                    relative + 2 > event_field_end ||
                    ((relative - event_field_begin) & 1) != 0) {
                    throw std::runtime_error(
                        "reachable opcode 34 partially overwrites an event pointer");
                }
                if (first_record_visit) ++event_pointer_mutations;
                if (additive || (value & 1U) != 0) {
                    throw std::runtime_error(
                        "reachable opcode 34 installs a dynamic/odd event pointer");
                }
                const auto entity = static_cast<std::size_t>(
                    (relative - event_field_begin) / 2);
                // Opcode 34 patches every directory alias of the same area.
                // Enqueue each alias path even though this release happens to
                // keep all aliases inside one CHNA archive.
                for (const auto& alias : world.locations()) {
                    if (alias.area_offset == destination.area_offset) {
                        dynamic_entity_contexts.emplace(
                            alias.area.event_archive_path, value,
                            alias.directory_offset, entity);
                        enqueue(alias.area.event_archive_path, value,
                                alias.directory_offset, entity);
                    }
                }
            }
            if (!terminated) {
                throw std::runtime_error(
                    "reachable opcode 34 mutation has no f800 terminator");
            }
        }
    }

    std::size_t total_roots = 0;
    std::size_t total_records = 0;
    std::size_t total_commands = 0;
    for (const auto& [archive_name, audit] : audits) {
        const auto root_count = roots_by_archive.at(archive_name).size();
        total_roots += root_count;
        total_records += audit->visited.size();
        total_commands += audit->commands;
        std::cout << archive_name << ": roots=" << root_count
                  << ", reachable records=" << audit->visited.size()
                  << ", commands=" << audit->commands
                  << ", opcodes=" << audit->opcodes.size() << '\n';
    }
    std::set<std::uint16_t> expected_opcodes;
    for (std::uint16_t opcode = 0; opcode < 62U; ++opcode) {
        if (opcode != 10U && opcode != 11U) expected_opcodes.insert(opcode);
    }
    if (roots_by_archive.size() != 7U || total_roots != 684U ||
        total_records != 1065U || total_commands != 6380U ||
        all_opcodes != expected_opcodes || inert_odd_targets != 1U ||
        map_mutations != 235U || runtime_validated_mutations != map_mutations ||
        event_pointer_mutations != 37U || invisible_fixed_bss_operations != 1U) {
        throw std::runtime_error(
            "reachable RPG event graph differs from the audited release");
    }

    class EventExecutionAuditHost final : public swd2::EventVmHost {
    public:
        explicit EventExecutionAuditHost(bool affirmative)
            : affirmative_(affirmative) {}

        void show_dialogue(std::uint16_t,
                           std::span<const std::uint8_t>) override {}
        void delay(std::uint16_t) override {}
        bool present_event_command(
            std::uint16_t, std::span<const std::uint16_t>) override {
            return true;
        }
        bool show_positioned_text(
            std::uint16_t, std::uint16_t,
            std::span<const std::uint8_t>) override {
            return true;
        }
        bool run_shop(std::span<const std::uint16_t>,
                      swd2::SharedState&) override {
            return true;
        }
        std::optional<bool> run_combined_shop(
            std::span<const std::uint16_t>, swd2::SharedState&) override {
            // Exercise one successful event-pointer reload in the affirmative
            // scenario, then cancel the reopened selector so shop cycles are
            // finite just as they are under real player input.
            return affirmative_ && combined_shop_calls_++ == 0U;
        }
        std::optional<bool> confirm_event_branch(
            swd2::SharedState&) override {
            return affirmative_;
        }
        std::optional<swd2::InventoryUiResult> run_inventory(
            swd2::SharedState&) override {
            return swd2::InventoryUiResult::cancelled;
        }

    private:
        bool affirmative_{};
        std::size_t combined_shop_calls_{};
    };

    const auto execute_context = [&](const std::string& archive_name,
                                     std::uint16_t target,
                                     std::uint16_t location_offset,
                                     std::size_t entity,
                                     bool affirmative) {
        auto execution_world = swd2::MapDatabase::load(map_database_path);
        auto execution_state =
            swd2::SharedState::load(game_root / "SAVE.DA1");
        swd2::install_map_location(
            execution_state, execution_world, location_offset);
        auto& execution_area = execution_world.location_at_directory_offset(
            location_offset).area;
        EventExecutionAuditHost host(affirmative);
        const auto result = swd2::execute_event(
            audit_for(archive_name).archive, target, execution_state,
            &execution_area, entity, host, 10'000, &execution_world);
        if (result.status != swd2::EventVmStatus::completed) {
            throw std::runtime_error(
                archive_name + ":" + std::to_string(target) +
                " failed deterministic entity-context execution");
        }
        return result.commands_executed;
    };

    std::array<std::size_t, 2> context_commands{};
    for (std::size_t scenario = 0; scenario < context_commands.size(); ++scenario) {
        for (const auto& [archive_name, target, location_offset, entity] :
             visited_states) {
            context_commands[scenario] += execute_context(
                archive_name, target, location_offset, entity, scenario != 0);
        }
    }
    if (visited_states.size() != 6862U ||
        dynamic_entity_contexts.size() != 80U ||
        context_commands != std::array<std::size_t, 2>{30811U, 31321U}) {
        throw std::runtime_error(
            "deterministic RPG entity-context execution differs from the audit");
    }
    std::cout << "verified reachable RPG event graph: "
              << roots_by_archive.size() << " archives, " << total_roots
              << " roots, " << total_records << " records, "
              << total_commands << " commands, " << all_opcodes.size()
              << "/62 dispatch opcodes; " << map_mutations
              << " runtime-validated MAPZ mutations (" << event_pointer_mutations
              << " event-pointer writes); " << inert_odd_targets
              << " documented inert odd target, "
              << invisible_fixed_bss_operations
              << " invisible fixed-BSS slot operation; "
              << visited_states.size() * 2U
              << " entity-context executions, "
              << context_commands[0] + context_commands[1]
              << " VM commands completed\n";
}

void usage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << " [--game DIR] --inspect\n"
              << "  " << program << " [--game DIR] --verify-resources\n"
              << "  " << program << " [--game DIR] --verify-maps\n"
              << "  " << program << " [--game DIR] --verify-battles\n"
              << "  " << program << " [--game DIR] --verify-legacy-program-data\n"
              << "  " << program << " [--game DIR] --verify-events\n"
              << "  " << program << " --extract INPUT OUTPUT\n"
              << "  " << program << " --render INPUT INDEX OUTPUT.ppm\n"
              << "  " << program << " [--game DIR] --render-meo OUTPUT.ppm\n"
              << "  " << program << " --render-map BASE_PATH OUTPUT.ppm\n"
              << "  " << program << " --render-planar BASE_PATH INDEX OUTPUT.ppm\n"
              << "  " << program << " --trace MT,ED,--,IF,OC,--\n";
    std::cout << "  " << program
              << " [--game DIR] [--save-dir DIR] [--slot 1..5] [--no-save]"
                 " [--trace-output FILE.json]"
                 " --run-script CONFIRM,CONFIRM,RIGHT,DOWN,QUIT\n"
              << "  " << program
              << " [--game DIR] [--save-dir DIR] [--slot 1..5] [--no-save]"
                 " --run-replay INPUT.txt --trace-output FILE.json\n";
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
        enum class Mode { inspect, trace, verify, verify_maps, verify_battles, verify_legacy,
                          verify_events, extract, render,
                          render_meo, render_map, render_planar, run, play } mode = Mode::inspect;
        std::string trace;
        std::filesystem::path extract_input;
        std::filesystem::path extract_output;
        std::filesystem::path replay_input;
        std::optional<std::filesystem::path> replay_trace;
        bool strict_replay = false;
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
            } else if (argument == "--verify-events") {
                mode = Mode::verify_events;
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
                replay_input.clear();
                strict_replay = false;
            } else if (argument == "--run-replay" && i + 1 < argc) {
                mode = Mode::run;
                replay_input = argv[++i];
                strict_replay = true;
            } else if (argument == "--trace-output" && i + 1 < argc) {
                replay_trace = std::filesystem::path(argv[++i]);
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
        if (replay_trace && mode != Mode::run) {
            throw std::runtime_error("--trace-output requires --run-script or --run-replay");
        }

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
        } else if (mode == Mode::verify_events) {
            verify_reachable_events(game_root);
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
            const auto input_text = strict_replay
                ? read_text_file(replay_input)
                : trace;
            run_monolithic(game_root, swd2::parse_replay_input(input_text),
                           save_root, slot_number, write_save,
                           replay_trace, strict_replay);
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
