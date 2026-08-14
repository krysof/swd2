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
#include "swd2/rix_decoder.hpp"
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
    struct InputCheckpoint {
        std::size_t index{};
        swd2::ReplayInputBoundary boundary{swd2::ReplayInputBoundary::any};
        swd2::InputAction action{swd2::InputAction::none};
        std::uint64_t state_digest{};
        std::optional<std::uint64_t> map_digest;
        std::uint16_t map_location{};
        std::uint16_t world_x{};
        std::uint16_t world_y{};
        std::uint16_t actor_direction{};
    };

    enum class TimelineKind {
        frame,
        input,
        delay,
        music,
        voice,
        stop_music,
        stop_audio,
    };

    struct TimelineEvent {
        TimelineKind kind{TimelineKind::frame};
        std::uint64_t at_milliseconds{};
        std::size_t index{};
        std::uint64_t digest{};
        std::uint64_t duration{};
        bool flag{};
        swd2::ReplayInputBoundary boundary{swd2::ReplayInputBoundary::any};
        swd2::InputAction action{swd2::InputAction::none};
    };

    explicit ReplayPlatform(
        std::vector<swd2::ReplayInputStep> steps,
        const std::optional<std::filesystem::path>& frame_output = std::nullopt)
        : steps_(std::move(steps)) {
        if (!frame_output) return;
        if (!frame_output->parent_path().empty()) {
            std::filesystem::create_directories(frame_output->parent_path());
        }
        frame_capture_.open(*frame_output,
                            std::ios::binary | std::ios::trunc);
        if (!frame_capture_) {
            throw std::runtime_error("cannot create replay frame capture: " +
                                     frame_output->string());
        }
        constexpr std::array<char, 8> magic{
            'S', 'W', 'D', '2', 'F', 'R', 'M', '2'};
        frame_capture_.write(magic.data(),
                             static_cast<std::streamsize>(magic.size()));
        write_u64(static_cast<std::uint64_t>(steps_.size()));
        for (const auto& step : steps_) {
            frame_capture_.put(static_cast<char>(step.boundary));
            frame_capture_.put(static_cast<char>(step.action));
        }
        if (!frame_capture_) {
            throw std::runtime_error("cannot write replay frame capture header");
        }
    }

    void bind_context(const swd2::GameContext& context) noexcept {
        context_ = &context;
    }

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
        const auto action = steps_[cursor_].action;
        record_input(swd2::ReplayInputBoundary::frontend, action);
        ++cursor_;
        unmatched_nonblocking_calls_ = 0;
        return action == swd2::InputAction::quit;
    }
    swd2::ClockTime clock_time() const override { return {0, 0}; }
    void delay_for(std::chrono::milliseconds duration) override {
        if (duration.count() > 0) {
            const auto milliseconds =
                static_cast<std::uint64_t>(duration.count());
            timeline.push_back(TimelineEvent{
                TimelineKind::delay, delay_milliseconds, 0, 0,
                milliseconds});
            delay_milliseconds += milliseconds;
        }
    }
    void play_music(std::span<const std::uint8_t> bytes, bool loop) override {
        ++music_calls;
        timeline.push_back(TimelineEvent{
            TimelineKind::music, delay_milliseconds, music_calls - 1U,
            hash(bytes), 0, loop});
        mix(audio_digest, static_cast<std::uint64_t>(loop ? 1U : 0U));
        mix(audio_digest, bytes);
    }
    void play_voice(std::span<const std::uint8_t> bytes) override {
        ++voice_calls;
        timeline.push_back(TimelineEvent{
            TimelineKind::voice, delay_milliseconds, voice_calls - 1U,
            hash(bytes)});
        mix(audio_digest, std::uint64_t{2});
        mix(audio_digest, bytes);
    }
    void stop_music() override {
        ++stop_music_calls;
        timeline.push_back(TimelineEvent{
            TimelineKind::stop_music, delay_milliseconds,
            stop_music_calls - 1U});
        mix(audio_digest, std::uint64_t{3});
    }
    void stop_audio() override {
        ++stop_audio_calls;
        timeline.push_back(TimelineEvent{
            TimelineKind::stop_audio, delay_milliseconds,
            stop_audio_calls - 1U});
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
    std::vector<InputCheckpoint> input_checkpoints;
    std::vector<TimelineEvent> timeline;

    [[nodiscard]] std::size_t input_count() const noexcept {
        return steps_.size();
    }
    [[nodiscard]] std::size_t consumed_inputs() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t remaining_inputs() const noexcept {
        return steps_.size() - cursor_;
    }

    void finish_frame_capture() {
        if (!frame_capture_.is_open()) return;
        constexpr std::array<char, 4> done{'D', 'O', 'N', 'E'};
        frame_capture_.write(done.data(),
                             static_cast<std::streamsize>(done.size()));
        write_u64(static_cast<std::uint64_t>(frames));
        frame_capture_.flush();
        if (!frame_capture_) {
            throw std::runtime_error("failed to finish replay frame capture");
        }
        frame_capture_.close();
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

    static std::uint64_t hash(std::span<const std::uint8_t> bytes) noexcept {
        auto digest = std::uint64_t{14695981039346656037ULL};
        mix(digest, bytes);
        return digest;
    }

    void write_u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            frame_capture_.put(
                static_cast<char>(static_cast<std::uint8_t>(value >> shift)));
        }
    }

    void write_u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            frame_capture_.put(
                static_cast<char>(static_cast<std::uint8_t>(value >> shift)));
        }
    }

    void record_input(swd2::ReplayInputBoundary boundary,
                      swd2::InputAction action) {
        if (context_ == nullptr) {
            throw std::runtime_error("replay frontend has no bound game context");
        }
        InputCheckpoint checkpoint{
            cursor_, boundary, action, hash(context_->shared_state.bytes()),
            std::nullopt,
            context_->shared_state.map_location_directory_offset(),
            context_->shared_state.world_x(),
            context_->shared_state.world_y(),
            context_->shared_state.actor_direction()};
        if (context_->map_database) {
            checkpoint.map_digest = hash(
                context_->map_database->serialized_bytes());
        }
        input_checkpoints.push_back(checkpoint);
        timeline.push_back(TimelineEvent{
            TimelineKind::input, delay_milliseconds, cursor_,
            checkpoint.state_digest, 0, false, boundary, action});
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
        timeline.push_back(TimelineEvent{
            TimelineKind::frame, delay_milliseconds, frames - 1U,
            frame_hash, 0, direct});
        mix(frame_digest, static_cast<std::uint64_t>(direct ? 1U : 0U));
        mix(frame_digest, static_cast<std::uint64_t>(surface.width));
        mix(frame_digest, static_cast<std::uint64_t>(surface.height));
        mix(frame_digest, surface.pixels);
        mix(frame_digest, surface.palette);
        if (frame_capture_.is_open()) {
            constexpr std::array<char, 4> frame{'F', 'R', 'A', 'M'};
            frame_capture_.write(frame.data(),
                                 static_cast<std::streamsize>(frame.size()));
            if (surface.width > std::numeric_limits<std::uint32_t>::max() ||
                surface.height > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("replay frame dimensions are too large");
            }
            write_u32(static_cast<std::uint32_t>(surface.width));
            write_u32(static_cast<std::uint32_t>(surface.height));
            frame_capture_.put(direct ? '\1' : '\0');
            frame_capture_.put('\0');
            frame_capture_.put('\0');
            frame_capture_.put('\0');
            frame_capture_.write(
                reinterpret_cast<const char*>(surface.pixels.data()),
                static_cast<std::streamsize>(surface.pixels.size()));
            frame_capture_.write(
                reinterpret_cast<const char*>(surface.palette.data()),
                static_cast<std::streamsize>(surface.palette.size()));
            if (!frame_capture_) {
                throw std::runtime_error("failed to append replay frame capture");
            }
        }
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
            record_input(boundary, step.action);
            ++cursor_;
            unmatched_nonblocking_calls_ = 0;
            return step.action;
        }
        if (blocking) {
            throw std::runtime_error(
                "replay boundary mismatch at input " +
                std::to_string(cursor_) + ": runtime requested " +
                std::string(swd2::replay_boundary_name(boundary)) +
                " but next input requires " +
                std::string(swd2::replay_boundary_name(step.boundary)));
        }
        if (++unmatched_nonblocking_calls_ > 1'000'000U) {
            throw std::runtime_error(
                "replay made no progress across one million nonblocking polls "
                "at input " + std::to_string(cursor_) +
                ": runtime requested " +
                std::string(swd2::replay_boundary_name(boundary)) +
                " but next input requires " +
                std::string(swd2::replay_boundary_name(step.boundary)) +
                " (" + std::string(swd2::input_action_name(step.action)) +
                ") last consumed=" +
                (input_checkpoints.empty()
                     ? std::string("none")
                     : (std::to_string(input_checkpoints.back().index) +
                        ":" +
                        std::string(swd2::input_action_name(
                            input_checkpoints.back().action)))));
        }
        return swd2::InputAction::none;
    }

    std::vector<swd2::ReplayInputStep> steps_;
    std::ofstream frame_capture_;
    const swd2::GameContext* context_{};
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
        map_digest = fnv1a(context.map_database->serialized_bytes());
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
           << "  \"input_checkpoints\": [\n";
    for (std::size_t index = 0; index < platform.input_checkpoints.size(); ++index) {
        const auto& checkpoint = platform.input_checkpoints[index];
        output << "    {\"index\": " << checkpoint.index
               << ", \"boundary\": \""
               << swd2::replay_boundary_name(checkpoint.boundary)
               << "\", \"action\": \""
               << swd2::input_action_name(checkpoint.action)
               << "\", \"state_fnv1a64\": \""
               << hex_digest(checkpoint.state_digest)
               << "\", \"mapz_fnv1a64\": ";
        if (checkpoint.map_digest) {
            output << '"' << hex_digest(*checkpoint.map_digest) << '"';
        } else {
            output << "null";
        }
        output << ", \"map_location\": " << checkpoint.map_location
               << ", \"world_x\": " << checkpoint.world_x
               << ", \"world_y\": " << checkpoint.world_y
               << ", \"actor_direction\": "
               << checkpoint.actor_direction << '}';
        if (index + 1U != platform.input_checkpoints.size()) output << ',';
        output << '\n';
    }
    output << "  ],\n"
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
           << "  \"timeline\": [\n";
    for (std::size_t sequence = 0; sequence < platform.timeline.size(); ++sequence) {
        const auto& event = platform.timeline[sequence];
        output << "    {\"sequence\": " << sequence
               << ", \"at_milliseconds\": " << event.at_milliseconds;
        switch (event.kind) {
        case ReplayPlatform::TimelineKind::frame:
            output << ", \"kind\": \"frame\", \"frame\": " << event.index
                   << ", \"direct\": " << (event.flag ? "true" : "false")
                   << ", \"fnv1a64\": \"" << hex_digest(event.digest) << '"';
            break;
        case ReplayPlatform::TimelineKind::input:
            output << ", \"kind\": \"input\", \"input\": " << event.index
                   << ", \"boundary\": \""
                   << swd2::replay_boundary_name(event.boundary)
                   << "\", \"action\": \""
                   << swd2::input_action_name(event.action)
                   << "\", \"state_fnv1a64\": \""
                   << hex_digest(event.digest) << '"';
            break;
        case ReplayPlatform::TimelineKind::delay:
            output << ", \"kind\": \"delay\", \"milliseconds\": "
                   << event.duration;
            break;
        case ReplayPlatform::TimelineKind::music:
            output << ", \"kind\": \"music\", \"call\": " << event.index
                   << ", \"loop\": " << (event.flag ? "true" : "false")
                   << ", \"payload_fnv1a64\": \""
                   << hex_digest(event.digest) << '"';
            break;
        case ReplayPlatform::TimelineKind::voice:
            output << ", \"kind\": \"voice\", \"call\": " << event.index
                   << ", \"payload_fnv1a64\": \""
                   << hex_digest(event.digest) << '"';
            break;
        case ReplayPlatform::TimelineKind::stop_music:
            output << ", \"kind\": \"stop_music\", \"call\": "
                   << event.index;
            break;
        case ReplayPlatform::TimelineKind::stop_audio:
            output << ", \"kind\": \"stop_audio\", \"call\": "
                   << event.index;
            break;
        }
        output << '}';
        if (sequence + 1U != platform.timeline.size()) output << ',';
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
           << "  \"name_fnv1a64\": \""
           << hex_digest(fnv1a(context.name_font)) << "\",\n"
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
                    const std::optional<std::filesystem::path>& frame_output,
                    bool require_all_inputs,
                    std::optional<swd2::Marker> start_marker,
                    std::optional<swd2::Marker> resume_marker) {
    ReplayPlatform platform(std::move(inputs), frame_output);
    auto slot = swd2::SaveSlot::open(game_root, save_root, slot_number);
    swd2::GameContext context{
        game_root, slot.state(), platform, slot.map_database(),
        [&slot, &game_root, &save_root, write_save](
            std::uint8_t selected, const swd2::SharedState& state,
            const swd2::MapDatabase& map,
            std::span<const std::uint8_t> name_font) {
            if (!write_save) return;
            swd2::SaveSlot::save_as(save_root, selected, state, map, name_font);
            // Record changes the active DOS save pair.  The optional exit
            // checkpoint must follow that chosen slot rather than silently
            // writing the command-line seed slot as well.
            slot = swd2::SaveSlot::open(game_root, save_root, selected);
            persist_browser_saves();
        },
        [&slot, &game_root, &save_root](std::uint8_t selected) {
            slot = swd2::SaveSlot::open(game_root, save_root, selected);
            return swd2::LoadedSaveSlot{
                slot.state(), slot.map_database(), slot.name_font()};
        },
        slot.name_font(),
    };
    platform.bind_context(context);
    swd2::ModuleRegistry modules;
    modules.add(std::make_unique<swd2::MeoModule>());
    modules.add(std::make_unique<swd2::RpgModule>());
    modules.add(std::make_unique<swd2::BattleModule>());
    modules.add(std::make_unique<swd2::DemoModule>());
    swd2::LaunchResult result;
    if (resume_marker) {
        result = swd2::MonolithicRuntime(std::move(modules)).resume(
            context, *resume_marker);
    } else if (start_marker) {
        const auto module = [&]() {
            switch (*start_marker) {
            case swd2::Marker::menu_ready:
            case swd2::Marker::continue_rpg:
            case swd2::Marker::returned_from_demo:
                return swd2::Module::rpg;
            case swd2::Marker::open_figure:
                return swd2::Module::figure;
            case swd2::Marker::open_demo:
                return swd2::Module::demo;
            case swd2::Marker::none:
            case swd2::Marker::menu_rejected:
                throw std::runtime_error(
                    "--start-marker requires MT, IF, ED, OC, or OM");
            }
            throw std::runtime_error("unknown direct-start marker");
        }();
        auto* implementation = modules.find(module);
        if (!implementation) {
            throw std::runtime_error("direct-start module is not registered");
        }
        const auto output = implementation->run(context, *start_marker);
        result.transitions.push_back({module, *start_marker, output, true});
        result.final_marker = output;
        result.reason = swd2::StopReason::module_requested_exit;
    } else {
        result = swd2::MonolithicRuntime(std::move(modules)).run(context);
    }
    if (require_all_inputs && platform.remaining_inputs() != 0U) {
        throw std::runtime_error(
            "replay stopped before consuming all boundary-locked inputs");
    }
    if (require_all_inputs && platform.implicit_quit_calls != 0U) {
        throw std::runtime_error(
            "replay exhausted its input and relied on an implicit quit");
    }
    if (write_save) {
        if (!context.map_database) {
            throw std::runtime_error(
                "cannot checkpoint current SAVE state without live MAPZ state");
        }
        slot.save(context.shared_state, *context.map_database,
                  context.name_font);
    }
    // A capture without its DONE trailer is intentionally invalid. Finalize
    // only after strict replay invariants pass so an interrupted/partial run
    // cannot be mistaken for pixel-diff evidence.
    platform.finish_frame_capture();
    if (trace_output) {
        write_replay_trace(*trace_output, platform, context, result);
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
        [&slot, &game_root, &save_root, write_save](
            std::uint8_t selected, const swd2::SharedState& state,
            const swd2::MapDatabase& map,
            std::span<const std::uint8_t> name_font) {
            if (!write_save) return;
            swd2::SaveSlot::save_as(save_root, selected, state, map, name_font);
            slot = swd2::SaveSlot::open(game_root, save_root, selected);
            // Native renames are durable on return. In a browser /saves is
            // IDBFS, so every explicit system-menu save must also flush the
            // in-memory filesystem instead of waiting for the game to exit.
            persist_browser_saves();
        },
        [&slot, &game_root, &save_root](std::uint8_t selected) {
            slot = swd2::SaveSlot::open(game_root, save_root, selected);
            return swd2::LoadedSaveSlot{
                slot.state(), slot.map_database(), slot.name_font()};
        },
        slot.name_font(),
    };
    swd2::ModuleRegistry modules;
    modules.add(std::make_unique<swd2::MeoModule>());
    modules.add(std::make_unique<swd2::RpgModule>());
    modules.add(std::make_unique<swd2::BattleModule>());
    modules.add(std::make_unique<swd2::DemoModule>());
    static_cast<void>(swd2::MonolithicRuntime(std::move(modules)).run(context));
    if (write_save) {
        if (!context.map_database) {
            throw std::runtime_error(
                "cannot checkpoint current SAVE state without live MAPZ state");
        }
        slot.save(context.shared_state, *context.map_database,
                  context.name_font);
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
            try {
                static_cast<void>(swd2::PlanarSpriteSet::load(base));
                ++animation_sets;
                continue;
            } catch (...) {
                throw std::runtime_error(base.string() + ": " + error.what());
            }
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
    std::size_t sa_sets = 0;
    std::size_t sa_frames = 0;
    std::map<std::uint16_t, std::size_t> sa_frame_counts;
    for (const auto& entry :
         std::filesystem::directory_iterator(game_root / "SA")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".RSK") {
            continue;
        }
        const auto archive = swd2::SpriteArchive::parse(
            swd2::decode_rsk_block(read_binary_file(entry.path())).data);
        const auto stem = entry.path().stem().string();
        if (stem.size() != 5U || stem[0] != 'S' || stem[1] != 'A' ||
            !std::all_of(stem.begin() + 2, stem.end(), [](unsigned char byte) {
                return std::isdigit(byte) != 0;
            })) {
            throw std::runtime_error("SA archive has an invalid decimal name");
        }
        const auto resource = static_cast<std::uint16_t>(
            std::stoul(stem.substr(2)));
        if (resource == 0U || resource > 0xffU ||
            !sa_frame_counts.emplace(
                resource, archive.sprites().size()).second) {
            throw std::runtime_error(
                "SA archive resource id is invalid or duplicated");
        }
        ++sa_sets;
        sa_frames += archive.sprites().size();
    }
    const auto transitions =
        swd2::MapTransitionDatabase::load(game_root / "MAP0.EXE");
    const auto world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    std::set<std::uint16_t> seen_areas;
    std::set<std::string> referenced_music;
    std::set<std::string> referenced_events;
    std::set<std::string> referenced_fonts;
    std::set<std::uint16_t> referenced_sa;
    struct AreaLayoutAudit {
        std::uint16_t cell_base{};
        std::uint16_t width{};
        std::uint16_t height{};
        std::size_t cell_count{};
        std::set<std::uint16_t> map0_cells;
    };
    std::map<std::uint16_t, AreaLayoutAudit> area_layouts;
    std::size_t referenced_entities = 0;
    std::size_t split_layouts = 0;
    std::size_t inert_off_map_entities = 0;
    std::set<std::tuple<std::uint16_t, std::uint16_t, std::size_t,
                        std::uint16_t>> inert_entity_records;
    std::size_t inert_off_map_overlays = 0;
    std::size_t transition_rows = 0;
    std::size_t wrapped_transition_records = 0;
    std::size_t transition_wrap_steps = 0;
    std::size_t wrapped_transition_rows = 0;
    std::size_t inverted_transition_rows = 0;
    std::size_t in_map_transition_rows = 0;
    std::array<std::size_t, 22> special_transition_actions{};
    std::size_t special_entity_contexts = 0;
    std::size_t spawn_trigger_locations = 0;
    std::size_t spawn_special_locations = 0;
    std::size_t active_actor_entity_frames = 0;
    std::size_t active_sa_entity_frames = 0;
    for (const auto& location : world.locations()) {
        if (seen_areas.insert(location.area_offset).second) {
            auto graphics = game_root / swd2::normalize_dos_asset_path(
                location.area.graphics_path);
            auto layout = game_root / swd2::normalize_dos_asset_path(
                location.area.layout_path);
            graphics.replace_extension();
            layout.replace_extension();
            swd2::MapResource resource;
            try {
                resource = swd2::MapResource::load(graphics, layout);
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    "MAPA referenced map cannot be decoded at location " +
                    std::to_string(location.directory_offset) + " (" +
                    graphics.string() + " / " + layout.string() + "): " +
                    error.what());
            }
            AreaLayoutAudit layout_audit{
                resource.cell_base(), resource.layout().width,
                resource.layout().height, resource.cells().size(), {}};
            for (std::size_t cell_index = 0;
                 cell_index < resource.cells().size(); ++cell_index) {
                const auto cell = resource.cells()[cell_index];
                if ((cell & 0x07ffU) >= resource.tile_count()) {
                    throw std::runtime_error(
                        "MAPA RAP cell references a tile outside its dictionary");
                }
                if ((cell & 0x1000U) != 0U) {
                    layout_audit.map0_cells.insert(
                        static_cast<std::uint16_t>(
                            resource.cell_base() + cell_index * 2U));
                }
            }
            area_layouts[location.area_offset] = std::move(layout_audit);
            for (const auto& transition :
                 transitions.records(location.area.flags)) {
                const auto cell_end = static_cast<std::size_t>(resource.cell_base()) +
                    resource.cells().size() * 2U;
                if (cell_end > 0x10000U ||
                    (transition.first_cell & 1U) != 0U ||
                    (transition.last_cell & 1U) != 0U) {
                    throw std::runtime_error(
                        "MAP0 trigger or referenced RAP exceeds its 16-bit word space");
                }
                auto first = transition.first_cell;
                auto last = transition.last_cell;
                const auto row_stride = static_cast<std::uint16_t>(
                    resource.layout().width * 2U);
                bool record_wrapped = false;
                bool already_wrapped = false;
                for (std::uint16_t row = 0; row < transition.row_count; ++row) {
                    ++transition_rows;
                    wrapped_transition_rows += already_wrapped;
                    if (first > last) {
                        // When only DX has wrapped, RPG:e94's unsigned pair of
                        // comparisons makes this intermediate range empty.
                        ++inverted_transition_rows;
                    } else {
                        const auto intersects_map =
                            first < cell_end && last >= resource.cell_base();
                        if (intersects_map) {
                            ++in_map_transition_rows;
                            if (first < resource.cell_base() || last >= cell_end) {
                                throw std::runtime_error(
                                    "MAP0 trigger row only partially overlaps its RAP");
                            }
                        }
                    }
                    const auto next_first = static_cast<std::uint16_t>(
                        first + row_stride);
                    const auto next_last = static_cast<std::uint16_t>(
                        last + row_stride);
                    if (row + 1U < transition.row_count) {
                        const auto wrapped =
                            next_first < first || next_last < last;
                        transition_wrap_steps += wrapped;
                        record_wrapped = record_wrapped || wrapped;
                        already_wrapped = already_wrapped || wrapped;
                    }
                    first = next_first;
                    last = next_last;
                }
                wrapped_transition_records += record_wrapped;
                if (transition.is_special()) {
                    const auto action = transition.special_action();
                    if (action == 0U ||
                        action >= special_transition_actions.size()) {
                        throw std::runtime_error(
                            "MAP0 uses an unknown released special action");
                    }
                    ++special_transition_actions[action];
                    std::optional<std::size_t> event_entity;
                    switch (action) {
                    case 1: case 3: case 10: case 12: case 14: case 15:
                    case 16: case 18: case 19: case 20: case 21:
                        event_entity = 0U;
                        break;
                    case 2: event_entity = 3U; break;
                    case 4: case 13: case 17: event_entity = 1U; break;
                    default: break;  // 5..9/11 only replace the BMAN archive.
                    }
                    if (event_entity) {
                        ++special_entity_contexts;
                        if (*event_entity >= location.area.entity_count()) {
                            throw std::runtime_error(
                                "MAP0 special action references a missing entity");
                        }
                    }
                } else {
                    static_cast<void>(world.location_at_directory_offset(
                        transition.destination_directory_offset()));
                    if (transition.sets_travel_flag() &&
                        transition.flag_index >= 34U) {
                        throw std::runtime_error(
                            "MAP0 trigger uses a travel flag outside DS:3a8a");
                    }
                }
            }
            for (std::size_t overlay_index = 0;
                 overlay_index < resource.overlays().size(); ++overlay_index) {
                const auto& overlay = resource.overlays()[overlay_index];
                // RPG.EXE:057f/0611 rejects RRO coordinates outside the
                // current viewport before dereferencing the tile dictionary.
                // A coordinate outside the map can never enter any legal
                // viewport, so released garbage records there are inert.
                if (overlay.x >= resource.layout().width ||
                    overlay.y >= resource.layout().height) {
                    ++inert_off_map_overlays;
                    continue;
                }
                if ((overlay.tile & 0x07ffU) >= resource.tile_count()) {
                    throw std::runtime_error(
                        "MAPA RRO cell references a tile outside its dictionary: " +
                        graphics.string() + ", record " +
                        std::to_string(overlay_index) + ", tile " +
                        std::to_string(overlay.tile) + "/" +
                        std::to_string(resource.tile_count()));
                }
            }
            referenced_entities += location.area.entity_count();
            split_layouts += graphics != layout;
            referenced_music.insert(location.area.music_path);
            referenced_events.insert(location.area.event_archive_path);
            referenced_fonts.insert(location.area.event_font_path);
            for (std::size_t entity = 0; entity < location.area.entity_count();
                 ++entity) {
                const auto record = swd2::map_entity(location.area, entity);
                if ((record.sprite >> 8U) != 0U) {
                    referenced_sa.insert(
                        static_cast<std::uint16_t>(record.sprite >> 8U));
                }
                if (record.behavior != 3U && record.behavior != 7U) {
                    const auto animation_frame =
                        (record.animation_frame & 1U) != 0U
                        ? 1U : record.animation_frame;
                    auto frame = static_cast<std::size_t>(
                        record.sprite & 0xffU) + animation_frame;
                    if (record.behavior != 4U && record.behavior != 5U &&
                        record.behavior != 6U) {
                        frame += record.direction;
                    }
                    const auto resource = static_cast<std::uint16_t>(
                        record.sprite >> 8U);
                    if (resource == 0U) {
                        // No active released entity uses MAN1 areas; BMAN1's
                        // fifty frames therefore cover every raw zero-id row.
                        ++active_actor_entity_frames;
                        if ((location.area.flags & 0x8000U) != 0U ||
                            frame >= 50U) {
                            throw std::runtime_error(
                                "active MAPA entity uses an invalid MAN/BMAN frame");
                        }
                    } else {
                        ++active_sa_entity_frames;
                        const auto found = sa_frame_counts.find(resource);
                        if (found == sa_frame_counts.end() ||
                            frame >= found->second) {
                            throw std::runtime_error(
                                "active MAPA entity uses an invalid SA frame");
                        }
                    }
                }
                const auto cell = record.cell_offset;
                if (cell < resource.cell_base() ||
                    ((cell - resource.cell_base()) & 1U) != 0U ||
                    (cell - resource.cell_base()) / 2U >= resource.cells().size()) {
                    if (record.behavior != 3U) {
                        throw std::runtime_error(
                            "active MAPA entity cell is outside its referenced RAP layout: "
                            "location " + std::to_string(location.directory_offset) +
                            ", area " + std::to_string(location.area_offset) +
                            ", entity " + std::to_string(entity) + ", cell " +
                            std::to_string(cell) + "/" +
                            std::to_string(resource.cells().size() * 2U));
                    }
                    ++inert_off_map_entities;
                    inert_entity_records.emplace(
                        location.directory_offset, location.area_offset,
                        entity, cell);
                } else if (record.behavior != 3U) {
                    const auto cell_index = static_cast<std::size_t>(
                        (cell - resource.cell_base()) / 2U);
                    if (cell_index + 2U >= resource.cells().size() ||
                        cell_index % resource.layout().width + 2U >=
                            resource.layout().width) {
                        throw std::runtime_error(
                            "active MAPA entity three-cell footprint crosses RAP bounds");
                    }
                }
            }
        }
        const auto& layout = area_layouts.at(location.area_offset);
        const auto viewport_end_x = static_cast<std::size_t>(location.viewport_x) + 40U;
        const auto viewport_end_y = static_cast<std::size_t>(location.viewport_y) + 25U;
        const auto actor_world_x = static_cast<std::size_t>(location.viewport_x) +
            ((static_cast<std::size_t>(location.actor_screen_x) + 2U) >> 1U);
        const auto actor_world_y = static_cast<std::size_t>(location.viewport_y) +
            ((static_cast<std::size_t>(location.actor_screen_y) + 16U) >> 3U);
        const auto expected_position = static_cast<std::size_t>(layout.cell_base) +
            (static_cast<std::size_t>(location.viewport_y) * layout.width +
             location.viewport_x) * 2U;
        if (viewport_end_x > layout.width || viewport_end_y > layout.height ||
            actor_world_x >= layout.width || actor_world_y >= layout.height ||
            expected_position > 0xffffU ||
            location.map_position != expected_position ||
            location.map_position < layout.cell_base ||
            ((location.map_position - layout.cell_base) & 1U) != 0U ||
            (location.map_position - layout.cell_base) / 2U >=
                layout.cell_count) {
            throw std::runtime_error(
                "MAPA location placement/viewport is inconsistent with RAP: " +
                std::to_string(location.directory_offset));
        }
        const auto actor_cell = static_cast<std::uint16_t>(
            layout.cell_base +
            (actor_world_y * layout.width + actor_world_x) * 2U);
        if (layout.map0_cells.contains(actor_cell)) {
            ++spawn_trigger_locations;
            const auto trigger = transitions.match(
                location.area.flags, actor_cell, layout.width);
            if (!trigger) {
                throw std::runtime_error(
                    "MAPA spawn has MAP0 bit 1000h without a matching trigger");
            }
            spawn_special_locations += trigger->is_special();
        }
    }
    for (const auto& path : referenced_music) {
        static_cast<void>(swd2::decode_rix(read_binary_file(
            game_root / swd2::normalize_dos_asset_path(path))));
    }
    for (const auto& path : referenced_events) {
        static_cast<void>(swd2::ScriptArchive::load(
            game_root / swd2::normalize_dos_asset_path(path)));
    }
    for (const auto& path : referenced_fonts) {
        static_cast<void>(swd2::LegacyFont::load(
            game_root / swd2::normalize_dos_asset_path(path)));
    }
    for (const auto resource : referenced_sa) {
        std::ostringstream name;
        name << "SA" << std::setw(3) << std::setfill('0') << resource
             << ".RSK";
        static_cast<void>(swd2::SpriteArchive::parse(
            swd2::decode_rsk_block(read_binary_file(
                game_root / "SA" / name.str())).data));
    }
    const std::set<std::tuple<std::uint16_t, std::uint16_t, std::size_t,
                              std::uint16_t>> expected_inert_entities{
        {304U, 23807U, 0U, 10656U},
        {316U, 24147U, 0U, 10656U},
        {418U, 24717U, 0U, 9190U},
        {490U, 25685U, 0U, 9190U},
    };
    const std::array<std::size_t, 22> expected_special_actions{
        0U, 1U, 1U, 1U, 1U, 2U, 2U, 3U, 1U, 1U, 1U,
        1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U};
    if (maps != 129U || tiles != 123958U || cells != 2140501U ||
        overlays != 71411U || animation_sets != 0U || de_sets != 37U ||
        de_frames != 971U || sa_sets != 84U || sa_frames != 896U ||
        referenced_sa.size() != 77U || transitions.area_count() != 152U ||
        transitions.record_count() != 481U ||
        transition_rows != 2835U || wrapped_transition_records != 7U ||
        transition_wrap_steps != 10U || wrapped_transition_rows != 319U ||
        inverted_transition_rows != 3U || in_map_transition_rows != 2761U ||
        special_transition_actions != expected_special_actions ||
        special_entity_contexts != 15U || spawn_trigger_locations != 23U ||
        spawn_special_locations != 23U ||
        active_actor_entity_frames != 197U ||
        active_sa_entity_frames != 437U ||
        world.locations().size() != 466U || seen_areas.size() != 152U ||
        referenced_entities != 822U || split_layouts != 2U ||
        inert_off_map_entities != 4U ||
        inert_entity_records != expected_inert_entities ||
        inert_off_map_overlays != 2490U ||
        referenced_music.size() != 22U || referenced_events.size() != 7U ||
        referenced_fonts.size() != 6U) {
        throw std::runtime_error(
            "map/resource coverage changed: maps=" + std::to_string(maps) +
            ", tiles=" + std::to_string(tiles) + ", cells=" +
            std::to_string(cells) + ", overlays=" +
            std::to_string(overlays) + ", placements=" +
            std::to_string(world.locations().size()) + ", areas=" +
            std::to_string(seen_areas.size()) + ", entities=" +
            std::to_string(referenced_entities) + ", split layouts=" +
            std::to_string(split_layouts) + ", inert entities=" +
            std::to_string(inert_off_map_entities) + ", inert overlays=" +
            std::to_string(inert_off_map_overlays) + ", music=" +
            std::to_string(referenced_music.size()) + ", events=" +
            std::to_string(referenced_events.size()) + ", fonts=" +
            std::to_string(referenced_fonts.size()));
    }
    std::cout << "verified " << maps << " maps: " << tiles << " tiles, " << cells
              << " cells, " << overlays << " overlay records; " << animation_sets
              << " non-map tile sets skipped; " << de_sets << " DE sprite sets, "
              << de_frames << " frames; " << sa_sets << " SA entity archives/"
              << sa_frames << " frames (" << referenced_sa.size()
              << " referenced, " << active_sa_entity_frames
              << " active entity frames); " << transitions.area_count()
              << " MAP0 areas, " << transitions.record_count()
              << " transition records/" << transition_rows << " expanded rows ("
              << wrapped_transition_records << " wrapping, "
              << spawn_trigger_locations << " spawn-triggered); "
              << world.locations().size()
              << " MAPA placements/" << seen_areas.size() << " unique areas/"
              << referenced_entities << " entities (" << inert_off_map_entities
              << " inert off-map), " << inert_off_map_overlays
              << " inert off-map RRO records and all referenced map/music/script/font"
                 " assets decoded\n";
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
    const auto map_transitions =
        swd2::MapTransitionDatabase::load(game_root / "MAP0.EXE");
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
    std::set<std::uint16_t> opcode37_destinations;
    std::array<std::size_t, 11> opcode3_field_mutations{};
    std::size_t sa_opcode3_context_writes = 0;
    std::size_t sa_opcode39_context_writes = 0;
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
                if (command.opcode == 3U && command.arguments.size() >= 2U &&
                    command.arguments[0] < opcode3_field_mutations.size()) {
                    ++opcode3_field_mutations[command.arguments[0]];
                }
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

            const auto& active_area = world.location_at_directory_offset(
                active_location_offset).area;
            if (command.opcode == 3U && command.arguments.size() >= 2U &&
                command.arguments[0] == 0U &&
                state.entity < active_area.entity_count() &&
                (active_area.entity_fields[0][state.entity] >> 8U) != 0U) {
                ++sa_opcode3_context_writes;
            }
            if (command.opcode == 39U && command.arguments.size() >= 2U &&
                (command.arguments[0] & 1U) == 0U) {
                const auto entity = static_cast<std::size_t>(
                    command.arguments[0] / 2U);
                if (entity < active_area.entity_count() &&
                    (active_area.entity_fields[0][entity] >> 8U) != 0U) {
                    ++sa_opcode39_context_writes;
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
                opcode37_destinations.insert(destination.directory_offset);
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
    std::size_t static_event_slots = 0;
    std::size_t static_unique_records = 0;
    std::size_t reachable_event_slots = 0;
    std::size_t reachable_unique_records = 0;
    std::size_t unreachable_event_slots = 0;
    std::size_t unreachable_unique_records = 0;
    std::size_t static_commands = 0;
    std::set<std::uint16_t> static_opcodes;
    auto static_directory_digest = std::uint64_t{14695981039346656037ULL};
    auto unreachable_directory_digest = std::uint64_t{14695981039346656037ULL};
    const auto mix_static_byte = [](std::uint64_t& digest,
                                    std::uint8_t value) {
        digest ^= value;
        digest *= 1099511628211ULL;
    };
    const auto mix_static_word = [&](std::uint64_t& digest,
                                     std::uint16_t value) {
        mix_static_byte(digest, static_cast<std::uint8_t>(value));
        mix_static_byte(digest, static_cast<std::uint8_t>(value >> 8U));
    };
    const auto mix_static_name = [&](std::uint64_t& digest,
                                     const std::string& value) {
        for (const auto character : value) {
            mix_static_byte(digest, static_cast<std::uint8_t>(character));
        }
        mix_static_byte(digest, 0U);
    };
    for (const auto& [archive_name, audit] : audits) {
        const auto root_count = roots_by_archive.at(archive_name).size();
        total_roots += root_count;
        total_records += audit->visited.size();
        total_commands += audit->commands;

        // Directory words 0..9 are the sentinel and CHNA resource header;
        // RPG event pointers are byte offsets into slots 10 onward. Decode
        // every one of those slots, including records which have no path from
        // any released MAPA entity. This makes a successful story replay
        // unnecessary for discovering hidden or abandoned script data: the
        // graph classifies it statically and the two digests fail closed if a
        // slot, alias, physical record or byte stream changes.
        if (audit->archive.entry_count() < 10U) {
            throw std::runtime_error(
                archive_name + " has no complete CHNA resource header");
        }
        std::set<std::uint16_t> all_physical_records;
        std::set<std::uint16_t> reachable_physical_records;
        std::set<std::uint16_t> unreachable_physical_records;
        std::size_t archive_reachable_slots = 0;
        std::size_t archive_unreachable_slots = 0;
        for (std::size_t index = 10U;
             index < audit->archive.entry_count(); ++index) {
            const auto target = static_cast<std::uint16_t>(index * 2U);
            const auto physical = audit->archive.offsets()[index];
            const auto stream = audit->archive.event_stream(index);
            const auto decoded = swd2::decode_event_record(stream);
            static_commands += decoded.commands.size();
            for (const auto& command : decoded.commands) {
                static_opcodes.insert(command.opcode);
            }
            ++static_event_slots;
            all_physical_records.insert(physical);
            mix_static_name(static_directory_digest, archive_name);
            mix_static_word(static_directory_digest, target);
            mix_static_word(static_directory_digest, physical);
            for (const auto byte : stream) {
                mix_static_byte(static_directory_digest, byte);
            }
            const auto reachable = audit->visited.contains(target);
            if (reachable) {
                ++reachable_event_slots;
                ++archive_reachable_slots;
                reachable_physical_records.insert(physical);
            } else {
                ++unreachable_event_slots;
                ++archive_unreachable_slots;
                unreachable_physical_records.insert(physical);
                mix_static_name(unreachable_directory_digest, archive_name);
                mix_static_word(unreachable_directory_digest, target);
                mix_static_word(unreachable_directory_digest, physical);
                for (const auto byte : stream) {
                    mix_static_byte(unreachable_directory_digest, byte);
                }
            }
        }
        // An alias is reachable if any released directory slot naming that
        // physical record is reachable. Do not count the same bytes as dead
        // merely because a second, unused alias points to them.
        for (auto iterator = unreachable_physical_records.begin();
             iterator != unreachable_physical_records.end();) {
            if (reachable_physical_records.contains(*iterator)) {
                iterator = unreachable_physical_records.erase(iterator);
            } else {
                ++iterator;
            }
        }
        static_unique_records += all_physical_records.size();
        reachable_unique_records += reachable_physical_records.size();
        unreachable_unique_records += unreachable_physical_records.size();
        std::cout << archive_name << ": roots=" << root_count
                  << ", reachable records=" << audit->visited.size()
                  << '/' << archive_reachable_slots << " event slots, dead slots="
                  << archive_unreachable_slots << ", dead physical records="
                  << unreachable_physical_records.size() << ", physical="
                  << reachable_physical_records.size() << "/"
                  << all_physical_records.size()
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
    if (static_event_slots != 1768U ||
        static_unique_records != 1607U ||
        reachable_event_slots != 1065U ||
        reachable_unique_records != 1064U ||
        unreachable_event_slots != 703U ||
        unreachable_unique_records != 543U ||
        reachable_event_slots + unreachable_event_slots != static_event_slots ||
        reachable_unique_records + unreachable_unique_records !=
            static_unique_records ||
        static_commands != 7462U || static_opcodes != expected_opcodes ||
        static_directory_digest != 0xca749c59020d6a1fULL ||
        unreachable_directory_digest != 0x4562b736f681dbeeULL) {
        throw std::runtime_error(
            "complete CHNA static directory audit differs from the release: slots=" +
            std::to_string(static_event_slots) + ", physical=" +
            std::to_string(static_unique_records) + ", reachable physical=" +
            std::to_string(reachable_unique_records) + ", dead physical=" +
            std::to_string(unreachable_unique_records) + ", commands=" +
            std::to_string(static_commands) + ", opcodes=" +
            std::to_string(static_opcodes.size()));
    }
    const std::array<std::size_t, 11> expected_opcode3_field_mutations{
        200U, 0U, 2U, 63U, 0U, 0U, 0U, 0U, 0U, 403U, 0U};
    if (opcode3_field_mutations != expected_opcode3_field_mutations ||
        sa_opcode3_context_writes != 0U ||
        sa_opcode39_context_writes != 155U) {
        throw std::runtime_error(
            "reachable transient entity-frame writes differ from the release");
    }

    // RPG:0edc performs the same immediate centre-cell MAP0 probe after an
    // opcode-37 area load as e94 does after a normal portal. Lock every
    // released opcode-37 destination whose spawn starts a special action;
    // these nested events/resources run before the outer copied CHNA stream
    // resumes and therefore cannot be deferred to the main map loop.
    std::set<std::uint16_t> opcode37_spawn_special_destinations;
    for (const auto destination_offset : opcode37_destinations) {
        const auto& destination =
            world.location_at_directory_offset(destination_offset);
        auto graphics = game_root / swd2::normalize_dos_asset_path(
            destination.area.graphics_path);
        auto layout = game_root / swd2::normalize_dos_asset_path(
            destination.area.layout_path);
        graphics.replace_extension();
        layout.replace_extension();
        const auto resource = swd2::MapResource::load(graphics, layout);
        const auto actor_x = static_cast<std::size_t>(destination.viewport_x) +
            ((static_cast<std::size_t>(destination.actor_screen_x) + 2U) >> 1U);
        const auto actor_y = static_cast<std::size_t>(destination.viewport_y) +
            ((static_cast<std::size_t>(destination.actor_screen_y) + 16U) >> 3U);
        if (actor_x >= resource.layout().width ||
            actor_y >= resource.layout().height) {
            throw std::runtime_error(
                "opcode 37 destination spawn is outside its RAP layout");
        }
        const auto cell_index = actor_y * resource.layout().width + actor_x;
        if ((resource.cells()[cell_index] & 0x1000U) == 0U) continue;
        const auto actor_cell = static_cast<std::uint16_t>(
            resource.cell_base() + cell_index * 2U);
        const auto trigger = map_transitions.match(
            destination.area.flags, actor_cell, resource.layout().width);
        if (!trigger) {
            throw std::runtime_error(
                "opcode 37 destination has an unmatched MAP0 spawn cell");
        }
        if (trigger->is_special()) {
            opcode37_spawn_special_destinations.insert(destination_offset);
        }
    }
    const std::set<std::uint16_t> expected_opcode37_spawn_special{
        376U, 510U, 756U};
    if (opcode37_spawn_special_destinations !=
        expected_opcode37_spawn_special) {
        throw std::runtime_error(
            "opcode 37 immediate MAP0 spawn contexts differ from the release");
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
        return std::tuple{
            result.commands_executed,
            fnv1a(execution_state.bytes()),
            fnv1a(execution_world.serialized_bytes())};
    };

    std::array<std::size_t, 2> context_commands{};
    std::array<std::uint64_t, 2> context_state_digests{
        14695981039346656037ULL, 14695981039346656037ULL};
    std::array<std::uint64_t, 2> context_map_digests{
        14695981039346656037ULL, 14695981039346656037ULL};
    const auto mix_context_value = [](std::uint64_t& digest,
                                      std::uint64_t value) {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            digest ^= static_cast<std::uint8_t>(value >> shift);
            digest *= 1099511628211ULL;
        }
    };
    const auto mix_context_name = [](std::uint64_t& digest,
                                     const std::string& value) {
        for (const auto character : value) {
            digest ^= static_cast<std::uint8_t>(character);
            digest *= 1099511628211ULL;
        }
        digest ^= std::uint8_t{};
        digest *= 1099511628211ULL;
    };
    for (std::size_t scenario = 0; scenario < context_commands.size(); ++scenario) {
        for (const auto& [archive_name, target, location_offset, entity] :
             visited_states) {
            const auto [commands, state_digest, map_digest] = execute_context(
                archive_name, target, location_offset, entity, scenario != 0);
            context_commands[scenario] += commands;
            for (auto* digest : {&context_state_digests[scenario],
                                 &context_map_digests[scenario]}) {
                mix_context_name(*digest, archive_name);
                mix_context_value(*digest, target);
                mix_context_value(*digest, location_offset);
                mix_context_value(*digest, entity);
                mix_context_value(*digest, commands);
            }
            mix_context_value(context_state_digests[scenario], state_digest);
            mix_context_value(context_map_digests[scenario], map_digest);
        }
    }
    if (visited_states.size() != 6862U ||
        dynamic_entity_contexts.size() != 80U ||
        context_commands != std::array<std::size_t, 2>{30811U, 31321U} ||
        context_state_digests != std::array<std::uint64_t, 2>{
            0xf24db76385bbe8ceULL, 0xf5a352b9d6818d9aULL} ||
        context_map_digests != std::array<std::uint64_t, 2>{
            0xa6442adb4ff266bfULL, 0x42c8b3a7963f403eULL}) {
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
              << opcode37_spawn_special_destinations.size()
              << " opcode-37 immediate MAP0 spawn contexts; "
              << opcode3_field_mutations[0]
              << " opcode-3 transient sprite-base writes; "
              << sa_opcode39_context_writes
              << " opcode-39 SA frame contexts; "
              << visited_states.size() * 2U
              << " entity-context executions, "
              << context_commands[0] + context_commands[1]
              << " VM commands completed; state checkpoints "
              << hex_digest(context_state_digests[0]) << '/'
              << hex_digest(context_state_digests[1])
              << ", MAPZ checkpoints "
              << hex_digest(context_map_digests[0]) << '/'
              << hex_digest(context_map_digests[1]) << "; all "
              << static_event_slots << " CHNA event slots/"
              << static_unique_records << " physical records/"
              << static_commands << " commands decoded, "
              << unreachable_event_slots << " statically unreachable slots/"
              << unreachable_unique_records << " physical records, directory hashes "
              << hex_digest(static_directory_digest) << '/'
              << hex_digest(unreachable_directory_digest) << '\n';
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
                 " [--trace-output FILE.json] [--frame-output FILE.swd2frames]"
                 " [--start-marker MT|IF|ED|OC|OM]"
                 " [--resume-marker MT|IF|ED|OC|OM]"
                 " --run-script CONFIRM,CONFIRM,RIGHT,DOWN,QUIT\n"
              << "  " << program
              << " [--game DIR] [--save-dir DIR] [--slot 1..5] [--no-save]"
                 " [--start-marker MT|IF|ED|OC|OM]"
                 " [--resume-marker MT|IF|ED|OC|OM]"
                 " --run-replay INPUT.txt --trace-output FILE.json"
                 " [--frame-output FILE.swd2frames]\n";
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
        std::optional<std::filesystem::path> replay_frames;
        std::optional<swd2::Marker> start_marker;
        std::optional<swd2::Marker> resume_marker;
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
            } else if (argument == "--frame-output" && i + 1 < argc) {
                replay_frames = std::filesystem::path(argv[++i]);
            } else if (argument == "--start-marker" && i + 1 < argc) {
                start_marker = parse_marker(argv[++i]);
            } else if (argument == "--resume-marker" && i + 1 < argc) {
                resume_marker = parse_marker(argv[++i]);
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
        if (start_marker && resume_marker) {
            throw std::runtime_error(
                "--start-marker and --resume-marker are mutually exclusive");
        }
        if (replay_trace && mode != Mode::run) {
            throw std::runtime_error("--trace-output requires --run-script or --run-replay");
        }
        if (replay_frames && mode != Mode::run) {
            throw std::runtime_error("--frame-output requires --run-script or --run-replay");
        }
        if (replay_frames && !strict_replay) {
            throw std::runtime_error("--frame-output requires strict --run-replay input");
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
                           replay_trace, replay_frames, strict_replay,
                           start_marker, resume_marker);
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
