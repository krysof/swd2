#include "swd2/asset_catalog.hpp"
#include "swd2/audio_loop_clock.hpp"
#include "swd2/battle_ability_database.hpp"
#include "swd2/battle_ai.hpp"
#include "swd2/battle_composite_effect.hpp"
#include "swd2/battle_command_menu.hpp"
#include "swd2/battle_database.hpp"
#include "swd2/battle_effects.hpp"
#include "swd2/battle_item_definition.hpp"
#include "swd2/battle_module.hpp"
#include "swd2/battle_party.hpp"
#include "swd2/battle_presentation.hpp"
#include "swd2/battle_random.hpp"
#include "swd2/battle_rules.hpp"
#include "swd2/battle_session.hpp"
#include "swd2/dialogue.hpp"
#include "swd2/demo_module.hpp"
#include "swd2/demo_timeline.hpp"
#include "swd2/event_program.hpp"
#include "swd2/event_vm.hpp"
#include "swd2/field_action_system.hpp"
#include "swd2/inventory_system.hpp"
#include "swd2/item_database.hpp"
#include "swd2/launcher.hpp"
#include "swd2/legacy_font.hpp"
#include "swd2/meo.hpp"
#include "swd2/map_resource.hpp"
#include "swd2/map_database.hpp"
#include "swd2/map_transition_database.hpp"
#include "swd2/meo_module.hpp"
#include "swd2/mon_database.hpp"
#include "swd2/monster_definition.hpp"
#include "swd2/runtime.hpp"
#include "swd2/save_slot.hpp"
#include "swd2/rpg_module.hpp"
#include "swd2/rpg_entity_system.hpp"
#include "swd2/rpg_presentation.hpp"
#include "swd2/replay_input.hpp"
#include "swd2/rix_decoder.hpp"
#include "swd2/mz_executable.hpp"
#include "swd2/planar_sprite_set.hpp"
#include "swd2/rsk_decoder.hpp"
#include "swd2/shared_state.hpp"
#include "swd2/script_archive.hpp"
#include "swd2/sprite_archive.hpp"
#include "swd2/voc_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <memory>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <tuple>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_launcher() {
    using swd2::Marker;
    using swd2::Module;
    const std::vector<Marker> outputs = {
        Marker::menu_ready,
        Marker::open_demo,
        Marker::none,  // DEMO result is deliberately ignored by the launcher.
        Marker::open_figure,
        Marker::continue_rpg,
        Marker::none,
    };
    std::size_t cursor = 0;
    std::vector<Module> calls;
    const auto result = swd2::Launcher().run([&](Module module, Marker) {
        calls.push_back(module);
        return swd2::ModuleResult{true, outputs.at(cursor++)};
    });

    const std::vector<Module> expected = {
        Module::menu, Module::rpg, Module::demo, Module::rpg, Module::figure, Module::rpg,
    };
    require(calls == expected, "launcher module sequence differs from SWD2.EXE");
    require(result.reason == swd2::StopReason::module_requested_exit, "unexpected launcher stop reason");
    require(result.final_marker == Marker::none, "unexpected final marker");

    const auto rejected = swd2::Launcher().run(
        [](Module, Marker) {
            return swd2::ModuleResult{true, Marker::menu_rejected};
        });
    require(rejected.reason == swd2::StopReason::menu_rejected &&
                rejected.final_marker == Marker::menu_rejected &&
                rejected.transitions.size() == 1U &&
                swd2::marker_name(rejected.final_marker) == "01",
            "launcher did not preserve MEO's literal rejection marker");
}

void test_paths() {
    require(swd2::normalize_dos_asset_path("C:MENU.RSK") == std::filesystem::path("MENU.RSK"),
            "drive-relative path conversion failed");
    require(swd2::normalize_dos_asset_path("C:\\SWD2\\BA\\BA01.RSK") ==
                std::filesystem::path("BA/BA01.RSK"),
            "absolute SWD2 path conversion failed");
}

void test_replay_input() {
    using swd2::InputAction;
    using swd2::ReplayInputBoundary;
    using swd2::ReplayInputStep;
    const auto steps = swd2::parse_replay_input(
        "# deterministic boundary stream\n"
        "POLL:RIGHT*2, WAIT:ENTER TEXT:NONE*3\n"
        "FRONTEND:QUIT, ESC");
    const std::vector<ReplayInputStep> expected = {
        {ReplayInputBoundary::poll, InputAction::right},
        {ReplayInputBoundary::poll, InputAction::right},
        {ReplayInputBoundary::wait, InputAction::confirm},
        {ReplayInputBoundary::text, InputAction::none},
        {ReplayInputBoundary::text, InputAction::none},
        {ReplayInputBoundary::text, InputAction::none},
        {ReplayInputBoundary::frontend, InputAction::quit},
        {ReplayInputBoundary::any, InputAction::cancel},
    };
    require(steps == expected &&
                swd2::replay_boundary_name(steps[0].boundary) == "POLL" &&
                swd2::input_action_name(steps[2].action) == "CONFIRM",
            "deterministic replay input grammar differs");

    auto rejected = false;
    try {
        static_cast<void>(swd2::parse_replay_input("POLL:LEFT*0"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "replay parser accepted a zero repeat count");
    rejected = false;
    try {
        static_cast<void>(swd2::parse_replay_input("FRONTEND:CONFIRM"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "replay parser accepted a gameplay frontend action");
    rejected = false;
    try {
        static_cast<void>(swd2::parse_replay_input(
            "POLL:NONE*600000 POLL:NONE*600000"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "replay parser accepted more than one million actions");
}

void test_rpg_mode_x_event_offset() {
    std::vector<std::uint8_t> previous(16, 0xee);
    std::vector<std::uint8_t> rendered(16);
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        rendered[i] = static_cast<std::uint8_t>(i);
    }
    const auto shifted = swd2::composite_mode_x_address_offset(
        previous, rendered, 1, 8);
    require(shifted == std::vector<std::uint8_t>({
                0xee, 0xee, 0xee, 0xee, 0, 1, 2, 3,
                4, 5, 6, 7, 8, 9, 10, 11}),
            "RPG mode-X byte offset did not retain/carry planar pixel groups");

    std::vector<std::uint8_t> screen(320U * 200U, 0xee);
    std::vector<std::uint8_t> scene(screen.size(), 0);
    scene[0] = 11;
    scene[319] = 22;
    scene[198U * 320U + 319U] = 33;
    const auto earthquake = swd2::composite_mode_x_address_offset(
        screen, scene, 81);
    require(earthquake[1U * 320U + 4U] == 11 &&
                earthquake[2U * 320U + 3U] == 22 &&
                earthquake[0] == 0xee &&
                std::count(earthquake.begin(), earthquake.end(), 33) == 0,
            "RPG opcode-49 81-byte earthquake displacement was not exact");
}

void test_original_launcher(const std::filesystem::path& game_root) {
    const auto executable = swd2::dos::MzExecutable::load(game_root / "SWD2.EXE");
    require(executable.actual_size() == 751, "unexpected SWD2.EXE size");
    require(executable.header_size() == 512, "unexpected SWD2.EXE header size");
    require(executable.relocations().size() == 2, "unexpected SWD2.EXE relocation count");
    require(executable.entry_file_offset() == 512, "unexpected SWD2.EXE entry offset");
    require(executable.overlay_size() == 0, "unexpected SWD2.EXE overlay");
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_rpg_opcode55_monochrome(const std::filesystem::path& game_root) {
    const auto mz = swd2::dos::MzExecutable::load(game_root / "RPG.EXE");
    const auto file = read_file(game_root / "RPG.EXE");
    const auto image = std::span<const std::uint8_t>(file).subspan(
        mz.header_size(), mz.load_image_size());

    // Dispatch entry 37h is RPG:5c35. It preserves the event SI, calls
    // 0dbf:0306 (palette snapshot), calls 0dbf:0314 (four-plane conversion),
    // restores SI and returns. These bytes also reject the old MSCDEX/audio
    // interpretation: the far targets resolve inside RPG's own load image.
    const std::array<std::uint8_t, 13> handler{
        0x56, 0x9a, 0x06, 0x03, 0xbf, 0x0d, 0x9a,
        0x14, 0x03, 0xbf, 0x0d, 0x5e, 0xc3};
    require(image.size() >= 0x5c35U + handler.size() &&
                std::equal(handler.begin(), handler.end(), image.begin() + 0x5c35U),
            "RPG opcode-55 far-call handler changed");
    require(image.size() > 0xdf63U &&
                image[0xdef6] == 0xbeU && image[0xdef7] == 0x5cU &&
                image[0xdef8] == 0x5aU && image[0xdefe] == 0xb9U &&
                image[0xdeff] == 0x80U && image[0xdf00] == 0x01U &&
                image[0xdf01] == 0xf3U && image[0xdf02] == 0xa5U &&
                image[0xdf04] == 0xb8U && image[0xdf05] == 0x00U &&
                image[0xdf06] == 0xa0U && image[0xdf2f] == 0x3cU &&
                image[0xdf30] == 0x10U && image[0xdf33] == 0x3cU &&
                image[0xdf34] == 0x20U,
            "RPG opcode-55 palette/plane helpers changed");

    std::array<std::uint8_t, 768> palette{};
    palette[0x10U * 3U] = 0;
    palette[0x10U * 3U + 1U] = 0;
    palette[0x10U * 3U + 2U] = 0;
    palette[0x11U * 3U] = 63;
    palette[0x11U * 3U + 1U] = 63;
    palette[0x11U * 3U + 2U] = 63;
    palette[0x12U * 3U] = 16;
    palette[0x12U * 3U + 1U] = 32;
    palette[0x12U * 3U + 2U] = 48;
    palette[0x1fU * 3U] = 60;
    palette[0x1fU * 3U + 1U] = 20;
    palette[0x1fU * 3U + 2U] = 4;
    std::vector<std::uint8_t> pixels{0x0f, 0x10, 0x11, 0x12, 0x1f, 0x20};
    swd2::apply_rpg_event_monochrome_filter(pixels, palette);
    require(pixels == std::vector<std::uint8_t>(
                          {0x0f, 0x1f, 0x10, 0x17, 0x19, 0x20}),
            "RPG opcode-55 inverse-luminance conversion differs from 0dbf:0314");

    const auto chna1 = swd2::ScriptArchive::load(game_root / "CHNA1.EXE");
    const auto chna5 = swd2::ScriptArchive::load(game_root / "CHNA5.EXE");
    const auto count_opcode = [](const swd2::ScriptArchive& archive,
                                 std::size_t entry, std::uint16_t opcode) {
        const auto record = swd2::decode_event_record(archive.entry(entry));
        return std::count_if(record.commands.begin(), record.commands.end(),
                             [opcode](const auto& command) {
                                 return command.opcode == opcode;
                             });
    };
    require(count_opcode(chna1, 229, 55) == 15 &&
                count_opcode(chna5, 31, 55) == 7,
            "original monochrome cutscenes no longer expose their opcode-55 frames");
}

void test_rpg_save_slot_selector(const std::filesystem::path& game_root) {
    const auto mz = swd2::dos::MzExecutable::load(game_root / "RPG.EXE");
    const auto file = read_file(game_root / "RPG.EXE");
    const auto image_start = static_cast<std::size_t>(mz.header_size());
    const auto image_size = static_cast<std::size_t>(mz.load_image_size());
    const auto image = std::span<const std::uint8_t>(
        file.data() + image_start, image_size);
    const auto entry = static_cast<std::size_t>(mz.header().initial_cs) * 16U +
                       mz.header().initial_ip;
    const auto prompt = swd2::extract_rpg_embedded_text(image, entry, 0x3a4a);
    require(prompt == std::vector<std::uint8_t>({
                0xa1, 0x40, 0xa4, 0x40, 0xa1, 0x40, 0xa4, 0x47,
                0xa1, 0x40, 0xa4, 0x54, 0xa1, 0x40, 0xa5, 0x7c,
                0xa1, 0x40, 0xa4, 0xad}),
            "RPG save selector did not recover its original Big5 prompt");
    const auto travel_labels = swd2::extract_rpg_embedded_data(
        image, entry, 0x3ace, 34U * 8U);
    require(travel_labels.size() == 272U && travel_labels[0] == 0xa6U &&
                travel_labels[1] == 0x77U && travel_labels[6] == 0xa1U &&
                travel_labels[7] == 0x40U &&
                travel_labels[264] == 0xa5U && travel_labels[271] == 0xf0U,
            "RPG travel selector did not recover its 34 fixed Big5 labels");
    const auto shop_prompt = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c32);
    require(shop_prompt == std::vector<std::uint8_t>({
                0xbd, 0xd0, 0xbf, 0xef, 0xbe, 0xdc, 0xa7, 0x41,
                0xaa, 0xba, 0xbb, 0xdd, 0xad, 0x6e, 0xa1, 0x43}),
            "RPG shop did not recover its original DATA:3c32 prompt");
    const auto shop_sale_prompt = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c1e);
    const auto shop_money_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c44);
    const auto shop_inventory_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c54);
    const auto shop_confirmation = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c6c);
    const auto shop_quantity_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c80);
    const auto shop_unsellable_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3c0a);
    const auto item_discard_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3686);
    const auto item_alchemy_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x36b0);
    const auto item_discard_prompt = swd2::extract_rpg_embedded_text(
        image, entry, 0x3754);
    const auto item_alchemy_select_prompt = swd2::extract_rpg_embedded_text(
        image, entry, 0x36c8);
    const auto item_alchemy_level_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x36f8);
    const auto item_alchemy_data = swd2::extract_rpg_embedded_data(
        image, entry, 0x2a42, 0x2f08U - 0x2a42U);
    require(shop_sale_prompt == std::vector<std::uint8_t>({
                0xb3, 0x6f, 0xbc, 0xcb, 0xaa, 0xab, 0xab, 0x7e, 0xa7,
                0xda, 0xa5, 0x58, 0xbb, 0xf9, 0xbb, 0xc8, 0xa8, 0xe2}) &&
                shop_money_error.size() == 14U &&
                shop_money_error.front() == 0xa7U &&
                shop_money_error.back() == 0x49U &&
                shop_inventory_error.size() == 22U &&
                shop_inventory_error.front() == 0xaaU &&
                shop_inventory_error.back() == 0x49U &&
                shop_confirmation == std::vector<std::uint8_t>({
                    0xa7, 0x41, 0xad, 0x6e, 0xb6, 0x52, 0xa4, 0x55, 0xb3,
                    0x6f, 0xaa, 0xab, 0xab, 0x7e, 0xb6, 0xdc, 0xa1, 0x48}) &&
                shop_quantity_error.size() == 14U &&
                shop_quantity_error.front() == 0xb3U &&
                shop_quantity_error.back() == 0x49U &&
                shop_unsellable_error == std::vector<std::uint8_t>({
                    0xb3, 0x6f, 0xbc, 0xcb, 0xaa, 0xab, 0xab, 0x7e, 0xa7,
                    0xda, 0xa4, 0xa3, 0xa6, 0xac, 0xc1, 0xca, 0xa1, 0x49}) &&
                item_discard_error == std::vector<std::uint8_t>({
                    0xb3, 0x6f, 0xbc, 0xcb, 0xaa, 0xab, 0xab, 0x7e, 0xa4,
                    0xa3, 0xaf, 0xe0, 0xa5, 0xe1, 0xb1, 0xf3, 0xa1, 0x49}) &&
                item_alchemy_error == std::vector<std::uint8_t>({
                    0xb3, 0x6f, 0xaa, 0xab, 0xab, 0x7e, 0xb5, 0x4c, 0xaa,
                    0x6b, 0xa9, 0xf1, 0xa4, 0x4a, 0xb7, 0xd2, 0xa7, 0xaf,
                    0xb3, 0xfd, 0xa1, 0x49}) &&
                item_discard_prompt == std::vector<std::uint8_t>({
                    0xbd, 0x54, 0xa9, 0x77, 0xad, 0x6e,
                    0xa5, 0xe1, 0xb1, 0xf3, 0xa1, 0x48}) &&
                item_alchemy_select_prompt == std::vector<std::uint8_t>({
                    0xbd, 0xd0, 0xa6, 0x41, 0xbf, 0xef, 0xa4, 0x40, 0xbc,
                    0xcb, 0xa9, 0xf1, 0xa4, 0x4a, 0xb7, 0xd2, 0xa7, 0xaf,
                    0xb3, 0xfd, 0xa1, 0x49}) &&
                item_alchemy_level_error == std::vector<std::uint8_t>({
                    0xb5, 0xa5, 0xaf, 0xc5, 0xa4, 0xa3, 0xa8, 0xac, 0xa1,
                    0x49, 0xb5, 0x4c, 0xaa, 0x6b, 0xb7, 0xd2, 0xa6, 0xa8,
                    0xa1, 0x49}) &&
                item_alchemy_data.size() == 0x4c6U &&
                item_alchemy_data[0] == 0x00U &&
                item_alchemy_data[1] == 0x2fU &&
                item_alchemy_data[0x4beU] == 0xcaU &&
                item_alchemy_data[0x4bfU] == 0x01U,
            "RPG shop/item confirmation Big5 streams were not recovered exactly");

    const auto item_definitions = swd2::ItemDatabase::load(
        game_root / "ITEM.EXE");
    require(item_definitions.at(98).alchemy_class == 0x20U &&
                item_definitions.at(98).alchemy_rank == 40U &&
                swd2::resolve_item_alchemy_product(
                    item_definitions, item_alchemy_data, 98, 99) == 458U &&
                item_definitions.at(458).alchemy_required_level == 1U,
            "RPG 4397 alchemy matrix/rank resolver changed");
    // RPG:49d0 skips the wait cursor only when DATA:359b is one.  Otherwise it
    // starts at MENU frame 95h (149), advances through 98h (152), and wraps
    // before 99h.  Keep the machine-code anchors beside the recovered strings
    // so the portable animation cannot silently drift from a guessed sequence.
    require(image.size() > 0x4ad4U &&
                image[0x4a94] == 0x80U && image[0x4a95] == 0x3eU &&
                image[0x4a96] == 0x9bU && image[0x4a97] == 0x35U &&
                image[0x4a98] == 0x01U &&
                image[0x4aa5] == 0xc7U && image[0x4aa7] == 0xd3U &&
                image[0x4aa9] == 0x95U &&
                image[0x4acb] == 0xffU && image[0x4ace] == 0x60U &&
                image[0x4acf] == 0x81U && image[0x4ad2] == 0x60U &&
                image[0x4ad3] == 0x99U,
            "RPG 49d0 MENU 149..152 acknowledgement loop changed");
    const auto equipment_actor_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x369a);
    const auto equipment_two_hand_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x370e);
    const auto equipment_slot_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3728);
    const auto field_action_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3620);
    const auto ability_value_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3630);
    const auto ability_material_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x364a);
    const auto ability_dead_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x3678);
    const auto ability_inventory_error = swd2::extract_rpg_embedded_text(
        image, entry, 0x36e0);
    const auto field_abilities = swd2::extract_rpg_embedded_data(
        image, entry, 0x1dce, 151U * 20U);
    const auto ability_resource_labels = swd2::extract_rpg_embedded_data(
        image, entry, 0x3664, 5U * 4U);
    const auto system_menu_labels = swd2::extract_rpg_embedded_text(
        image, entry, 0x39e6);
    const auto system_exit_prompt = swd2::extract_rpg_embedded_text(
        image, entry, 0x3a36);
    const auto status_menu_labels = swd2::extract_rpg_embedded_data(
        image, entry, 0x3806, 28U * 8U);
    const auto status_value_labels = swd2::extract_rpg_embedded_data(
        image, entry, 0x38e6, 15U * 4U);
    require(swd2::rpg_status_label_indices(0U) ==
                    std::vector<std::size_t>{0U} &&
                swd2::rpg_status_label_indices(0x5ffeU) ==
                    std::vector<std::size_t>{1U} &&
                swd2::rpg_status_label_indices(0x3ffeU) ==
                    std::vector<std::size_t>{2U} &&
                swd2::rpg_status_label_indices(0x1403U) ==
                    std::vector<std::size_t>({3U, 5U, 14U}),
            "RPG 4960 status-name priority differs from the original bits");
    require(equipment_actor_error == std::vector<std::uint8_t>({
                0xa6, 0xb9, 0xa4, 0x48, 0xb5, 0x4c, 0xaa, 0x6b, 0xa8, 0xcf,
                0xa5, 0xce, 0xb3, 0x6f, 0xb8, 0xcb, 0xb3, 0xc6, 0xa1, 0x49}) &&
                equipment_two_hand_error.size() == 24U &&
                equipment_two_hand_error.front() == 0xc2U &&
                equipment_two_hand_error.back() == 0x49U &&
                equipment_slot_error == std::vector<std::uint8_t>({
                    0xb5, 0x4c, 0xaa, 0x6b, 0xb8, 0xcb, 0xb3, 0xc6, 0xa6,
                    0x62, 0xb3, 0x6f, 0xb3, 0xa1, 0xa6, 0xec, 0xa1, 0x43}) &&
                field_action_error == std::vector<std::uint8_t>({
                    0xa6, 0x62, 0xa6, 0xb9, 0xb5, 0x4c, 0xaa,
                    0x6b, 0xa8, 0xcf, 0xa5, 0xce, 0xa1, 0x49}) &&
                ability_value_error.size() == 24U &&
                ability_value_error.front() == 0xbcU &&
                ability_material_error.size() == 24U &&
                ability_material_error.front() == 0xc3U &&
                ability_dead_error == std::vector<std::uint8_t>({
                    0xc3, 0x78, 0xa6, 0xba, 0xa9, 0xfc,
                    0xb0, 0x67, 0xa4, 0xa4, 0xa1, 0x49}) &&
                ability_inventory_error.size() == 22U &&
                ability_inventory_error.front() == 0xaaU &&
                field_abilities.size() == 3020U &&
                field_abilities[76U * 20U] == 0xa5U &&
                field_abilities[76U * 20U + 13U] == 0xa4U &&
                field_abilities[76U * 20U + 14U] == 0x38U &&
                ability_resource_labels == std::vector<std::uint8_t>({
                    0xa5, 0x50, 0xb3, 0x4e,
                    0xc5, 0xe9, 0xa4, 0x4f,
                    0xc5, 0xe9, 0xa4, 0x4f,
                    0xa5, 0x50, 0xb3, 0x4e,
                    0xc3, 0xc4, 0xa7, 0xf7}) &&
                system_menu_labels.size() == 78U &&
                system_menu_labels[0] == 0xadU &&
                system_menu_labels[1] == 0xb5U &&
                system_menu_labels[76] == 0xa2U &&
                system_menu_labels[77] == 0xe1U &&
                system_exit_prompt == std::vector<std::uint8_t>({
                    0xbd, 0x54, 0xa9, 0x77, 0xad, 0x6e, 0xa6, 0x5e, 0xa8,
                    0xec, 0xa2, 0xd2, 0xa2, 0xdd, 0xa2, 0xe1, 0xa1, 0x48}) &&
                status_menu_labels.size() == 224U &&
                status_menu_labels[0] == 0xa5U &&
                status_menu_labels[1] == 0xcdU &&
                status_menu_labels[216] == 0xaaU &&
                status_menu_labels[223] == '$' &&
                status_value_labels.size() == 60U &&
                std::equal(status_value_labels.begin(),
                           status_value_labels.begin() + 4,
                           std::array<std::uint8_t, 4>{
                               0xb0, 0xb7, 0xb1, 0x64}.begin()) &&
                std::equal(status_value_labels.end() - 4,
                           status_value_labels.end(),
                           std::array<std::uint8_t, 4>{
                               0xa7, 0xf4, 0xbf, 0xa3}.begin()),
            "RPG equipment/system-menu Big5 streams were not exact");
    const auto category_labels = swd2::extract_rpg_embedded_data(
        image, entry, 0x299a, 42U * 4U);
    const auto equipment_labels = swd2::extract_rpg_embedded_text(
        image, entry, 0x388e);
    const auto equipment_stats = swd2::extract_rpg_embedded_data(
        image, entry, 0x3a60, 40U);
    require(category_labels.size() == 168U && category_labels[0] == 0xa4U &&
                category_labels[167] == 0xdaU &&
                equipment_labels.size() == 86U &&
                equipment_labels[0] == 0xc0U &&
                equipment_labels[84] == 0xa4U && equipment_labels[85] == 0x47U &&
                equipment_stats.size() == 40U &&
                equipment_stats[0] == 0xbeU && equipment_stats[8] == '$' &&
                equipment_stats[9] == '$' && equipment_stats[39] == '$',
            "RPG inventory/equipment Big5 tables were not recovered exactly");

    auto menu_data = swd2::decode_rsk_block(read_file(game_root / "MENU.RSK")).data;
    const auto menu = swd2::SpriteArchive::parse(std::move(menu_data));
    std::vector<std::uint8_t> surface(320U * 200U, 0x55);
    swd2::draw_rpg_selector_panel(surface, 320, 200, menu, 4, 0, 7, 4);
    const auto corner = menu.pixels(92);
    const auto corner_opaque = std::find_if(
        corner.begin(), corner.end(), [](std::uint8_t value) { return value != 0xfeU; });
    require(surface[16] == 0x55 && corner_opaque != corner.end() &&
                surface[static_cast<std::size_t>(corner_opaque - corner.begin()) + 16U] ==
                    *corner_opaque &&
                surface[4U * 320U + 16U] == menu.pixels(83)[0] &&
                surface[12U * 320U + 48U] == menu.pixels(87)[0] &&
                surface[76U * 320U + 272U] == menu.pixels(91)[0],
            "RPG generic selector panel did not use exact MENU frame geometry");

    std::fill(surface.begin(), surface.end(), 0x55);
    swd2::draw_rpg_selector_scrollbar(
        surface, 320, 200, menu, 24, 36, 5, 8, 42, 0);
    const auto thumb = menu.pixels(99);
    const auto thumb_opaque = std::find_if(
        thumb.begin(), thumb.end(),
        [](std::uint8_t value) { return value != 0xfeU; });
    const auto& thumb_info = menu.sprites()[99];
    const auto thumb_offset = static_cast<std::size_t>(
        thumb_opaque - thumb.begin());
    const auto thumb_row = thumb_offset / thumb_info.width;
    const auto thumb_column = thumb_offset % thumb_info.width;
    require(thumb_opaque != thumb.end() &&
                surface[(56U + thumb_row) * 320U + 300U + thumb_column] ==
                    *thumb_opaque,
            "RPG 2907 scrollbar thumb was not based at panel top+20");

    const auto differing_pixel = [&](std::size_t pressed,
                                     std::size_t normal) {
        const auto pressed_pixels = menu.pixels(pressed);
        const auto normal_pixels = menu.pixels(normal);
        auto offset = std::size_t{0};
        while (offset < pressed_pixels.size() &&
               (pressed_pixels[offset] == 0xfeU ||
                pressed_pixels[offset] == normal_pixels[offset])) {
            ++offset;
        }
        require(offset < pressed_pixels.size(),
                "RPG scrollbar endpoint frames have no visible pressed delta");
        return offset;
    };
    const auto top_delta = differing_pixel(95, 94);
    std::fill(surface.begin(), surface.end(), 0x55);
    swd2::draw_rpg_selector_scrollbar(
        surface, 320, 200, menu, 24, 36, 5, 8, 42, 0,
        swd2::RpgListSelection::ScrollCue::toward_start);
    require(surface[(40U + top_delta / menu.sprites()[95].width) * 320U +
                    296U + top_delta % menu.sprites()[95].width] ==
                menu.pixels(95)[top_delta],
            "RPG 2907 did not show MENU 95 for an upward scroll input");
    const auto bottom_delta = differing_pixel(98, 97);
    std::fill(surface.begin(), surface.end(), 0x55);
    swd2::draw_rpg_selector_scrollbar(
        surface, 320, 200, menu, 24, 36, 5, 8, 42, 42,
        swd2::RpgListSelection::ScrollCue::toward_end);
    require(surface[(168U + bottom_delta / menu.sprites()[98].width) * 320U +
                    296U + bottom_delta % menu.sprites()[98].width] ==
                menu.pixels(98)[bottom_delta],
            "RPG 2907 did not show MENU 98 for a downward scroll input");

    std::fill(surface.begin(), surface.end(), 0x55);
    swd2::draw_rpg_compact_panel(surface, 320, 200, menu, 4, 0, 2, 1);
    require(surface[16] == menu.pixels(80)[0] &&
                surface[40] == menu.pixels(81)[0] &&
                surface[56] == menu.pixels(82)[0] &&
                surface[8U * 320U + 16U] == menu.pixels(168)[0] &&
                surface[24U * 320U + 16U] == menu.pixels(171)[0],
            "RPG compact information panel did not use frames 80/168/171 exactly");

    swd2::RpgSaveSlotSelector selector;
    require(selector.input(swd2::InputAction::up) ==
                swd2::RpgSaveSelectorResult::waiting && selector.slot() == 0 &&
                selector.input(swd2::InputAction::left) ==
                    swd2::RpgSaveSelectorResult::waiting && selector.slot() == 4 &&
                selector.input(swd2::InputAction::right) ==
                    swd2::RpgSaveSelectorResult::waiting && selector.slot() == 4 &&
                selector.input(swd2::InputAction::confirm) ==
                    swd2::RpgSaveSelectorResult::waiting && selector.confirming() &&
                selector.input(swd2::InputAction::up) ==
                    swd2::RpgSaveSelectorResult::waiting &&
                selector.input(swd2::InputAction::right) ==
                    swd2::RpgSaveSelectorResult::waiting &&
                selector.confirmation_choice() == 1 &&
                selector.input(swd2::InputAction::confirm) ==
                    swd2::RpgSaveSelectorResult::cancelled,
            "RPG save selector movement/No branch differs from 4e4b/46cc");

    swd2::RpgSaveSlotSelector escape_confirmation;
    static_cast<void>(escape_confirmation.input(swd2::InputAction::confirm));
    require(escape_confirmation.input(swd2::InputAction::cancel) ==
                swd2::RpgSaveSelectorResult::committed,
            "RPG nested save-confirmation Escape/default-Yes quirk was lost");
    swd2::RpgSaveSlotSelector escape_no;
    static_cast<void>(escape_no.input(swd2::InputAction::confirm));
    static_cast<void>(escape_no.input(swd2::InputAction::right));
    require(escape_no.input(swd2::InputAction::cancel) ==
                swd2::RpgSaveSelectorResult::cancelled,
            "RPG nested save-confirmation Escape ignored the selected No choice");
    swd2::RpgSaveSlotSelector escape_selector;
    require(escape_selector.input(swd2::InputAction::cancel) ==
                swd2::RpgSaveSelectorResult::cancelled,
            "RPG top-level save selector Escape should cancel");

    auto list_selection = swd2::rpg_list_selection_input(
        {}, 50, 8, swd2::InputAction::end);
    require(list_selection.selected == 42U &&
                list_selection.first_visible == 42U &&
                list_selection.scroll_cue ==
                    swd2::RpgListSelection::ScrollCue::toward_end,
            "RPG End key did not retain row zero on the final list page");
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::up);
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::down);
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::home);
    require(list_selection.selected == 1U &&
                list_selection.first_visible == 0U,
            "RPG Home key changed the selector's row within its panel");
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::page_down);
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::page_down);
    require(list_selection.selected == 17U &&
                list_selection.first_visible == 16U,
            "RPG PgDn did not advance one viewport while retaining its row");
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::end);
    list_selection = swd2::rpg_list_selection_input(
        list_selection, 50, 8, swd2::InputAction::page_up);
    require(list_selection.selected == 35U &&
                list_selection.first_visible == 34U &&
                list_selection.scroll_cue ==
                    swd2::RpgListSelection::ScrollCue::toward_start,
            "RPG PgUp/final partial-page behavior differs from 298d");
    const auto edge_scroll = swd2::rpg_list_selection_input(
        {7, 0}, 50, 8, swd2::InputAction::down);
    require(edge_scroll.selected == 8U && edge_scroll.first_visible == 1U &&
                edge_scroll.scroll_cue ==
                    swd2::RpgListSelection::ScrollCue::toward_end,
            "RPG Down key did not scroll while retaining the bottom row");

    require(swd2::rpg_party_target_for_direction(
                swd2::InputAction::left, 4) == 0 &&
                swd2::rpg_party_target_for_direction(
                    swd2::InputAction::right, 4) == 1 &&
                swd2::rpg_party_target_for_direction(
                    swd2::InputAction::up, 4) == 2 &&
                swd2::rpg_party_target_for_direction(
                    swd2::InputAction::down, 4) == 3 &&
                !swd2::rpg_party_target_for_direction(
                    swd2::InputAction::right, 1) &&
                !swd2::rpg_party_target_for_direction(
                    swd2::InputAction::confirm, 4),
            "RPG party target keys did not map directly to diamond portraits");

    auto ba_data = swd2::decode_rsk_block(
        read_file(game_root / "BA" / "BA01.RSK")).data;
    const auto ba = swd2::SpriteArchive::parse(std::move(ba_data));
    std::vector<std::uint8_t> target_page(320U * 200U, 1);
    swd2::apply_rpg_party_target_highlight(
        target_page, 320, 200, ba.palette(), 2, 4);
    const auto dimmed = swd2::fig_palette_translation(ba.palette(), 3)[1];
    require(target_page[104U * 320U + 64U] == dimmed &&
                target_page[104U * 320U + 160U] == dimmed &&
                target_page[80U * 320U + 112U] == 1 &&
                target_page[131U * 320U + 112U] == dimmed &&
                target_page[104U * 320U + 63U] == 1 &&
                target_page[155U * 320U + 64U] == 1,
            "RPG 2634 target page did not dim only the three unselected cards");

    std::vector<std::uint8_t> binary_page(320U * 200U, 1);
    swd2::apply_rpg_binary_choice_highlight(
        binary_page, 320, 200, ba.palette(), 44, 62, 90, 1);
    require(binary_page[90U * 320U + 176U] == dimmed &&
                binary_page[90U * 320U + 248U] == 1 &&
                binary_page[90U * 320U + 175U] == 1 &&
                binary_page[122U * 320U + 176U] == 1,
            "RPG 46cc/54d5 Yes-No page did not dim only the unselected card");

    const std::array<std::uint8_t, 12> target_highlight_entry = {
        0xe8, 0x23, 0x00, 0x2e, 0xc7, 0x06,
        0x3f, 0x76, 0x0c, 0x00, 0x2e, 0xc7,
    };
    require(image.size() > 0x2fa5U + 17U &&
                std::equal(target_highlight_entry.begin(),
                           target_highlight_entry.end(),
                           image.begin() + 0x2634U) &&
                image[0x2fa5U] == 0x8bU && image[0x2fa6U] == 0x0eU &&
                image[0x2fa7U] == 0x10U && image[0x2fa8U] == 0x00U &&
                image[0x2fa9U] == 0xb0U && image[0x2faaU] == 0x0fU &&
                image[0x2fabU] == 0xd2U && image[0x2facU] == 0xe8U,
            "RPG 2634/2fa5 target highlight/input handlers changed");
    require(image[0x4719U] == 0x2eU && image[0x471aU] == 0xc7U &&
                image[0x471dU] == 0x76U && image[0x471eU] == 0x10U &&
                image[0x4720U] == 0x2eU && image[0x4725U] == 0x20U &&
                image[0x4733U] == 0xb9U && image[0x4734U] == 0x02U &&
                image[0x4736U] == 0xe8U &&
                image[0x5538U] == 0x2eU && image[0x5539U] == 0xc7U &&
                image[0x553dU] == 0x10U && image[0x5544U] == 0x20U &&
                image[0x5553U] == 0xb9U && image[0x5554U] == 0x02U &&
                image[0x5556U] == 0xe8U,
            "RPG save/sale binary-choice palette highlight handlers changed");
    require(image[0x25a8U] == 0x8bU && image[0x25a9U] == 0x44U &&
                image[0x25aaU] == 0x08U && image[0x25abU] == 0xa9U &&
                image[0x25acU] == 0x00U && image[0x25adU] == 0x20U &&
                image[0x25b0U] == 0xc7U && image[0x25b5U] == 0x00U &&
                image[0x25b6U] == 0xe8U &&
                image[0x25d0U] == 0xc7U && image[0x25d5U] == 0x00U &&
                image[0x25e0U] == 0xe8U &&
                image[0x25edU] == 0xc7U && image[0x25f2U] == 0x00U,
            "RPG 24b8 death/low-health/status overlay path changed");
    const std::array<std::pair<std::size_t, std::array<std::uint8_t, 5>>, 5>
        list_input_branches{{
            {0x29ccU, {0x80, 0x3e, 0x45, 0x69, 0x01}},
            {0x2a3dU, {0x80, 0x3e, 0x46, 0x69, 0x01}},
            {0x2a64U, {0x80, 0x3e, 0x3e, 0x69, 0x01}},
            {0x2a89U, {0x80, 0x3e, 0x3c, 0x69, 0x01}},
            {0x2aa4U, {0x80, 0x3e, 0x44, 0x69, 0x01}},
        }};
    require(image.size() > 0x2aa9U &&
                std::all_of(
                    list_input_branches.begin(), list_input_branches.end(),
                    [&](const auto& branch) {
                        return std::equal(branch.second.begin(),
                                          branch.second.end(),
                                          image.begin() + branch.first);
                    }),
            "RPG 298d Down/PgDn/PgUp/Home/End input branches changed");
    require(image[0x565fU] == 0xbeU && image[0x5660U] == 0x1eU &&
                image[0x5661U] == 0x3cU && image[0x5662U] == 0xe8U &&
                image[0x5665U] == 0xffU && image[0x5666U] == 0x06U &&
                image[0x5667U] == 0xd5U && image[0x5668U] == 0x60U &&
                image[0x5669U] == 0x83U && image[0x566dU] == 0x04U &&
                image[0x566eU] == 0xe8U &&
                image[0x5885U] == 0xc6U && image[0x5886U] == 0x06U &&
                image[0x5887U] == 0x9bU && image[0x5888U] == 0x35U &&
                image[0x5889U] == 0x01U && image[0x588aU] == 0xbeU &&
                image[0x588bU] == 0x6cU && image[0x588cU] == 0x3cU &&
                image[0x5893U] == 0xc6U && image[0x5896U] == 0x3cU &&
                image[0x5898U] == 0xe8U,
            "RPG 565f/5884 sale and purchase confirmation paths changed");
    require(image[0x560aU] == 0xbeU && image[0x560bU] == 0x05U &&
                image[0x560dU] == 0xe8U && image[0x5610U] == 0xa8U &&
                image[0x5611U] == 0x08U && image[0x5617U] == 0xbeU &&
                image[0x5618U] == 0x0aU && image[0x5619U] == 0x3cU &&
                image[0x561aU] == 0xe8U,
            "RPG 560a unsellable-item feedback path changed");
    require(image[0x3f95U] == 0xbeU && image[0x3f96U] == 0x9aU &&
                image[0x3f97U] == 0x36U && image[0x3f98U] == 0xe8U &&
                image[0x4239U] == 0xbeU && image[0x423aU] == 0x0eU &&
                image[0x423bU] == 0x37U && image[0x423cU] == 0xebU &&
                image[0x423eU] == 0xbeU && image[0x423fU] == 0x28U &&
                image[0x4240U] == 0x37U && image[0x4241U] == 0x56U &&
                image[0x4242U] == 0xe8U,
            "RPG 3f95/4239 equipment feedback branches changed");
    require(image[0x3b30U] == 0xa8U && image[0x3b31U] == 0x01U &&
                image[0x3b34U] == 0xbeU && image[0x3b35U] == 0x20U &&
                image[0x3b36U] == 0x36U && image[0x3b37U] == 0xe8U &&
                image[0x3b93U] == 0x3dU && image[0x3b94U] == 0x28U &&
                image[0x3b98U] == 0xf7U && image[0x3b9dU] == 0x40U &&
                image[0x3bacU] == 0x3dU && image[0x3badU] == 0x29U,
            "RPG 3b30/3b93 field-item unavailable feedback path changed");
}

void test_resource_decoder(const std::filesystem::path& game_root) {
    auto compressed = swd2::decode_rsk_block(read_file(game_root / "MEO.RSK"));
    require(compressed.compressed, "MEO.RSK should use compressed storage");
    require(compressed.has_standard_footer, "MEO.RSK footer was not recognized");
    require(compressed.data.size() == 64636, "unexpected MEO.RSK output size");
    require(compressed.data[0] == 0xba && compressed.data[1] == 0x2d,
            "unexpected MEO.RSK decoded directory");
    const auto sprites = swd2::SpriteArchive::parse(std::move(compressed.data));
    require(sprites.sprites().size() == 5, "unexpected MEO.RSK sprite count");
    require(sprites.sprites()[0].width == 305 && sprites.sprites()[0].height == 171,
            "unexpected MEO.RSK background dimensions");
    require(sprites.has_palette(), "MEO.RSK VGA palette was not found");
    const auto frame = swd2::render_meo_frame(sprites, 0, 0, 0);
    require(frame.pixels[3 * 320 + 3] == sprites.pixels(0)[0],
            "MEO background was not placed at 3,3");

    const auto meo_mz = swd2::dos::MzExecutable::load(game_root / "MEO.EXE");
    const auto meo_file = read_file(game_root / "MEO.EXE");
    auto meo_image = std::span<const std::uint8_t>(meo_file).subspan(
        meo_mz.header_size(), meo_mz.load_image_size());
    require(swd2::meo_copy_protection_is_patched(meo_image),
            "shipped MEO NOP copy-protection patch was not detected");
    auto unpatched_meo = std::vector<std::uint8_t>(
        meo_image.begin(), meo_image.end());
    unpatched_meo[0x160] = 0x75;
    unpatched_meo[0x161] = 0x04;
    require(!swd2::meo_copy_protection_is_patched(unpatched_meo),
            "original MEO JNE copy-protection branch was not detected");
    auto unknown_meo = unpatched_meo;
    unknown_meo[0x160] = 0xeb;
    auto unknown_rejected = false;
    try {
        static_cast<void>(swd2::meo_copy_protection_is_patched(unknown_meo));
    } catch (const std::runtime_error&) {
        unknown_rejected = true;
    }
    require(unknown_rejected,
            "unknown MEO copy-protection patch was silently accepted");

    swd2::MeoCopyProtection protection;
    require(protection.input(swd2::MeoInput::confirm, 5) == swd2::MeoStatus::waiting,
            "MEO accepted before three confirmations");
    protection.input(swd2::MeoInput::confirm, 5);
    require(protection.input(swd2::MeoInput::confirm, 5) == swd2::MeoStatus::accepted,
            "patched MEO should accept any three confirmations");

    swd2::MeoCopyProtection original_protection(false);
    require(original_protection.input(swd2::MeoInput::confirm, 1) ==
                swd2::MeoStatus::waiting &&
                original_protection.input(swd2::MeoInput::confirm, 1) ==
                swd2::MeoStatus::waiting &&
                original_protection.input(swd2::MeoInput::confirm, 1) ==
                swd2::MeoStatus::accepted,
            "original MEO branch did not require three matching answers");
    swd2::MeoCopyProtection original_rejection(false);
    require(original_rejection.input(swd2::MeoInput::confirm, 2) ==
                swd2::MeoStatus::waiting &&
                original_rejection.input(swd2::MeoInput::confirm, 2) ==
                swd2::MeoStatus::waiting &&
                original_rejection.input(swd2::MeoInput::confirm, 2) ==
                swd2::MeoStatus::rejected,
            "original MEO branch accepted three wrong answers");

    const auto stored = swd2::decode_rsk_block(read_file(game_root / "ST" / "SP345.RSK"));
    require(!stored.compressed, "SP345.RSK should use stored storage");
    require(stored.data.size() == 15, "unexpected SP345.RSK output size");
    require(stored.data[0] == 4 && stored.data[1] == 0, "unexpected stored RSK bytes");

    auto menu_block = swd2::decode_rsk_block(read_file(game_root / "MENU.RSK"));
    const auto menu = swd2::SpriteArchive::parse(std::move(menu_block.data));
    require(menu.sprites().size() == 181 &&
                menu.sprites()[0].width == 64 && menu.sprites()[0].height == 32 &&
                menu.sprites()[1].width == 168 && menu.sprites()[1].height == 22 &&
                menu.sprites()[83].width == 32 && menu.sprites()[83].height == 8 &&
                menu.sprites()[94].width == 16 && menu.sprites()[94].height == 16 &&
                menu.sprites()[178].width == 24 && menu.sprites()[178].height == 19,
            "MENU.RSK command/selector component geometry differs from FIG");

    auto battle_background_block =
        swd2::decode_rsk_block(read_file(game_root / "BA" / "BA01.RSK"));
    const auto battle_background =
        swd2::SpriteArchive::parse(std::move(battle_background_block.data));
    const auto table1 = swd2::fig_palette_translation(
        battle_background.palette(), 1);
    const auto table2 = swd2::fig_palette_translation(
        battle_background.palette(), 2);
    const auto table3 = swd2::fig_palette_translation(
        battle_background.palette(), 3);
    const auto table4 = swd2::fig_palette_translation(
        battle_background.palette(), 4);
    std::set<std::uint8_t> low_colors(menu.pixels(154).begin(), menu.pixels(154).end());
    const auto low_health_blend = swd2::fig_palette_blend_translation(
        battle_background.palette(), 106, 35);
    const std::array<std::uint8_t, 32> expected_table1 = {
        0x00, 0xa2, 0x2f, 0xa3, 0x08, 0x08, 0x08, 0x00,
        0x00, 0x1d, 0x19, 0xa1, 0x1b, 0x19, 0x8c, 0x5e,
        0xbf, 0xbf, 0x19, 0x1a, 0x1a, 0x1b, 0x1c, 0x1c,
        0x1d, 0x1e, 0x1e, 0x1e, 0x1f, 0x1f, 0x00, 0x00,
    };
    const std::array<std::uint8_t, 32> expected_table2 = {
        0x00, 0x05, 0xa2, 0xa2, 0x07, 0xa3, 0x08, 0x08,
        0x00, 0x1b, 0x17, 0xa0, 0x19, 0x09, 0x8b, 0x5f,
        0x16, 0x17, 0x5e, 0x18, 0xbf, 0x19, 0x1a, 0x1b,
        0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 0x1e, 0x1f, 0x00,
    };
    const std::array<std::uint8_t, 32> expected_table3 = {
        0x00, 0xd0, 0xa1, 0x05, 0xa2, 0x06, 0xa3, 0x08,
        0x08, 0x1a, 0x0c, 0x01, 0x18, 0x0c, 0x8a, 0x13,
        0x14, 0x15, 0x5f, 0x16, 0x17, 0x18, 0x19, 0x19,
        0x1a, 0x1b, 0x1c, 0x1d, 0x1d, 0x1e, 0x1f, 0x00,
    };
    require(std::equal(expected_table1.begin(), expected_table1.end(),
                       table1.begin()) &&
                std::equal(expected_table2.begin(), expected_table2.end(),
                           table2.begin()) &&
                std::equal(expected_table3.begin(), expected_table3.end(),
                           table3.begin()) &&
                table4[0] == 86 && table4[106] == 106 &&
                table4[255] == 52 &&
                low_colors == std::set<std::uint8_t>{106, 0xfe} &&
                low_health_blend == table4,
            "FIG 314d/771b generated different nearest-colour tables");

    const auto death_source = menu.pixels(153);
    const auto death_zero = std::find(death_source.begin(), death_source.end(), 0);
    const auto death_copy = std::find_if(
        death_source.begin(), death_source.end(),
        [](std::uint8_t color) { return color != 0 && color != 0xfe; });
    std::vector<std::uint8_t> death_composite(320U * 200U, 1);
    swd2::composite_legacy_masked_sprite(
        death_composite, 320, 200, battle_background.palette(),
        menu, 153, 0, 0, 0);
    const auto death_destination = [&](auto iterator) {
        const auto offset = static_cast<std::size_t>(iterator - death_source.begin());
        return (offset / menu.sprites()[153].width) * 320U +
               offset % menu.sprites()[153].width;
    };
    require(death_zero != death_source.end() &&
                death_copy != death_source.end() &&
                death_composite[death_destination(death_zero)] == table1[1] &&
                death_composite[death_destination(death_copy)] == *death_copy,
            "FIG/RPG masked overlay did not shade/copy its two present pixel classes");

    const auto low_source = menu.pixels(154);
    const auto low_tint = std::find(low_source.begin(), low_source.end(), 106);
    const auto low_transparent =
        std::find(low_source.begin(), low_source.end(), 0xfe);
    std::vector<std::uint8_t> masked_skip_composite(320U * 200U, 1);
    swd2::composite_legacy_masked_sprite(
        masked_skip_composite, 320, 200, battle_background.palette(),
        menu, 154, 0, 0, 0);
    std::vector<std::uint8_t> low_composite(320U * 200U, 1);
    swd2::composite_legacy_translucent_sprite(
        low_composite, 320, 200, battle_background.palette(),
        menu, 154, 0, 0);
    const auto low_destination = [&](auto iterator) {
        const auto offset = static_cast<std::size_t>(iterator - low_source.begin());
        return (offset / menu.sprites()[154].width) * 320U +
               offset % menu.sprites()[154].width;
    };
    require(low_tint != low_source.end() &&
                low_transparent != low_source.end() &&
                masked_skip_composite[low_destination(low_tint)] == 106 &&
                masked_skip_composite[low_destination(low_transparent)] == 1 &&
                low_composite[low_destination(low_tint)] == table4[1] &&
                low_composite[low_destination(low_transparent)] == 1,
            "FIG/RPG low-health overlay did not apply its dynamic 35/64 table");

    std::vector<std::uint8_t> translated_surface(100U * 50U, 1);
    swd2::apply_fig_palette_translation(
        translated_surface, 100, 50, battle_background.palette(),
        2, 5, 16, 32, 3);
    require(translated_surface[5U * 100U + 8U] == 0xd0 &&
                translated_surface[36U * 100U + 71U] == 0xd0 &&
                translated_surface[4U * 100U + 8U] == 1 &&
                translated_surface[5U * 100U + 7U] == 1 &&
                translated_surface[37U * 100U + 8U] == 1 &&
                translated_surface[5U * 100U + 72U] == 1,
            "FIG 77ef did not transform exactly 16 Mode-X bytes by 32 lines");

    const auto fig_mz = swd2::dos::MzExecutable::load(game_root / "FIG.EXE");
    const auto fig_file = read_file(game_root / "FIG.EXE");
    const auto fig_image = std::span<const std::uint8_t>(fig_file).subspan(
        fig_mz.header_size(), fig_mz.load_image_size());
    const auto fig_data_paragraph = static_cast<std::size_t>(
        fig_image[1] | static_cast<std::uint16_t>(fig_image[2]) << 8U);
    require(fig_data_paragraph * 16U + 0x4d37U <= fig_image.size() &&
                fig_image[fig_data_paragraph * 16U + 0x4d35U] == 0x00U &&
                fig_image[fig_data_paragraph * 16U + 0x4d36U] == 0x0fU,
            "FIG initialized dialogue foreground/background colors changed");
    const std::array<std::uint8_t, 16> masked_entry = {
        0x2e, 0xc6, 0x06, 0xf3, 0x76, 0x01, 0xe8,
        0x07, 0x00, 0x2e, 0xc6, 0x06, 0xf3, 0x76, 0x00, 0xc3,
    };
    require(fig_image.size() > 0x798bU + masked_entry.size() &&
                std::equal(masked_entry.begin(), masked_entry.end(),
                           fig_image.begin() + 0x798bU) &&
                fig_image[0x2cd7U] == 0xe8U &&
                fig_image[0x2cd8U] == 0xb1U &&
                fig_image[0x2cd9U] == 0x4cU &&
                fig_image[0x2d01U] == 0xe8U &&
                fig_image[0x2d02U] == 0x97U &&
                fig_image[0x2d03U] == 0x4cU &&
                fig_image[0x2debU] == 0x2eU &&
                fig_image[0x2decU] == 0xc6U &&
                fig_image[0x2defU] == 0x76U &&
                fig_image[0x2df0U] == 0xefU &&
                fig_image[0x2eacU] == 0x2eU &&
                fig_image[0x2eb0U] == 0x76U &&
                fig_image[0x2eb1U] == 0x00U,
            "FIG 2bd9/2deb/798b palette-compositor paths changed");
}

void test_voc_decoder(const std::filesystem::path& game_root) {
    const auto sample = swd2::decode_voc(read_file(game_root / "VC" / "SP052.VOC"));
    require(sample.sample_rate == 7575 && sample.mono_samples.size() == 20'912 &&
                sample.mono_samples.front() == -1280,
            "Creative Voice time constant/unsigned PCM decoding is incorrect");

    std::vector<std::filesystem::path> voc_paths;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(game_root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".VOC") {
            voc_paths.push_back(entry.path());
        }
    }
    std::sort(voc_paths.begin(), voc_paths.end());

    std::size_t files = 0;
    std::size_t samples = 0;
    std::array<std::uint64_t, 2> resampled_samples{};
    std::array<std::uint64_t, 2> resampled_hashes{
        1'469'598'103'934'665'603ULL,
        1'469'598'103'934'665'603ULL,
    };
    constexpr std::array<std::uint32_t, 2> target_rates{44'100U, 48'000U};
    for (const auto& path : voc_paths) {
        const auto decoded = swd2::decode_voc(read_file(path));
        require(decoded.sample_rate >= 4'000 && decoded.sample_rate <= 15'625 &&
                    !decoded.mono_samples.empty(),
                "original VOC asset decoded to invalid PCM metadata");
        ++files;
        samples += decoded.mono_samples.size();
        for (std::size_t rate = 0; rate < target_rates.size(); ++rate) {
            const auto converted =
                swd2::resample_voice(decoded, target_rates[rate]);
            require(converted.sample_rate == target_rates[rate] &&
                        converted.mono_samples.size() ==
                            static_cast<std::uint64_t>(
                                decoded.mono_samples.size()) *
                                target_rates[rate] / decoded.sample_rate,
                    "portable VOC resampler changed the rational duration");
            resampled_samples[rate] += converted.mono_samples.size();
            for (const auto value : converted.mono_samples) {
                const auto word = static_cast<std::uint16_t>(value);
                resampled_hashes[rate] ^= word & 0xffU;
                resampled_hashes[rate] *= 1'099'511'628'211ULL;
                resampled_hashes[rate] ^= word >> 8U;
                resampled_hashes[rate] *= 1'099'511'628'211ULL;
            }
        }
    }
    require(files == 67 && samples == 834'549,
            "not every original VOC asset passed portable PCM decoding");
    require(resampled_samples ==
                std::array<std::uint64_t, 2>{4'864'003U, 5'294'165U} &&
                resampled_hashes == std::array<std::uint64_t, 2>{
                    0x37885f7710bb6cf3ULL,
                    0xd2a8ac5fdc8445c3ULL,
                },
            "portable VOC resampling changed across host sample rates");
}

void test_rix_decoder(const std::filesystem::path& game_root) {
    std::size_t files = 0;
    std::size_t frames = 0;
    std::size_t commands = 0;
    std::size_t timer_ticks = 0;
    std::size_t opl_writes = 0;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(game_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".RIX") continue;
        const auto sequence = swd2::decode_rix(read_file(entry.path()));
        require(!sequence.instruments.empty() && !sequence.frames.empty() &&
                    sequence.total_timer_ticks != 0,
                "original RIX asset decoded to an empty timeline");
        const auto opl = swd2::translate_rix_to_opl(sequence);
        require(opl.rhythm_mode == sequence.rhythm_mode &&
                    opl.total_timer_ticks == sequence.total_timer_ticks &&
                    !opl.writes.empty() &&
                    std::all_of(opl.writes.begin(), opl.writes.end(),
                                [&](const swd2::OplRegisterWrite& write) {
                                    return write.timer_tick < sequence.total_timer_ticks;
                                }),
                "original RIX asset produced an invalid OPL register timeline");
        ++files;
        frames += sequence.frames.size();
        timer_ticks += sequence.total_timer_ticks;
        opl_writes += opl.writes.size();
        for (const auto& frame : sequence.frames) commands += frame.commands.size();
    }
    require(files == 43 && frames == 9'942 && commands == 41'848 &&
                timer_ticks == 101'716 && opl_writes == 166'187,
            "not every original RIX command stream passed strict decoding");

    const auto short_music = swd2::decode_rix(read_file(game_root / "RX" / "FI02.RIX"));
    const auto pcm = swd2::synthesize_rix(short_music, 8'000);
    std::uint64_t pcm_hash = 1'469'598'103'934'665'603ULL;
    for (const auto sample : pcm.mono_samples) {
        const auto word = static_cast<std::uint16_t>(sample);
        pcm_hash ^= word & 0xffU;
        pcm_hash *= 1'099'511'628'211ULL;
        pcm_hash ^= word >> 8U;
        pcm_hash *= 1'099'511'628'211ULL;
    }
    require(short_music.instruments.size() == 7 && short_music.frames.size() == 21 &&
                short_music.total_timer_ticks == 147 &&
                pcm.sample_rate == 8'000 && pcm.mono_samples.size() == 16'800 &&
                pcm_hash == 0x17083cd9e61a091cULL &&
                std::any_of(pcm.mono_samples.begin(), pcm.mono_samples.end(),
                            [](std::int16_t sample) { return sample != 0; }),
            "portable YM3812 core produced non-deterministic RIX PCM");

    // A centered pitch command must select the driver's first generated
    // micro-tuning table: C uses F-number 343 (0x157), and raw note 60 is
    // transposed down by the driver's fixed 12-note input bias to octave 4.
    swd2::RixSequence exact;
    exact.instruments.resize(1);
    exact.frames = {
        {1, {}},
        {1,
         {{swd2::RixCommandKind::pitch, 0, 128U << 6U},
          {swd2::RixCommandKind::note, 0, 60}}},
    };
    exact.total_timer_ticks = 2;
    const auto exact_opl = swd2::translate_rix_to_opl(exact);
    std::vector<swd2::OplRegisterWrite> command_writes;
    std::copy_if(exact_opl.writes.begin(), exact_opl.writes.end(),
                 std::back_inserter(command_writes),
                 [](const auto& write) { return write.timer_tick == 1; });
    require(command_writes.size() == 6 &&
                command_writes[0].register_index == 0xa0 &&
                command_writes[0].value == 0x57 &&
                command_writes[1].register_index == 0xb0 &&
                command_writes[1].value == 0x01 &&
                command_writes[4].register_index == 0xa0 &&
                command_writes[4].value == 0x57 &&
                command_writes[5].register_index == 0xb0 &&
                command_writes[5].value == 0x31,
            "FIG's exact RIX pitch table/note register programming regressed");

    // Independent differential oracle: DOSBox-X DROv2 capture of the original
    // DEMO.EXE/SWORD.RIX begins at the first key-on. DRO suppresses unchanged
    // registers and initializes its waveform cache to zero. The first 175
    // live (register,value) pairs (through 3.94 seconds of the original) have
    // this FNV-1a fingerprint. This checks instruments, notes, key transitions
    // and their order against an independent execution of the DOS program.
    const auto sword = swd2::translate_rix_to_opl(
        swd2::decode_rix(read_file(game_root / "SWORD.RIX")));
    const auto capture_start = std::find_if(
        sword.writes.begin(), sword.writes.end(), [](const auto& write) {
            return write.timer_tick == 0 && write.register_index == 0xb0 &&
                   write.value == 0x2d;
        });
    require(capture_start != sword.writes.end(),
            "SWORD RIX has no original-capture alignment key-on");
    std::array<std::uint8_t, 256> dro_register_cache{};
    for (auto write = sword.writes.begin(); write != capture_start; ++write) {
        dro_register_cache[write->register_index] = write->value;
    }
    std::fill(dro_register_cache.begin() + 0xe0,
              dro_register_cache.begin() + 0xf6, 0);
    std::size_t captured_pairs = 0;
    std::uint64_t capture_hash = 1'469'598'103'934'665'603ULL;
    for (auto write = capture_start;
         write != sword.writes.end() && captured_pairs < 175; ++write) {
        auto& cached = dro_register_cache[write->register_index];
        if (cached != write->value) {
            capture_hash ^= write->register_index;
            capture_hash *= 1'099'511'628'211ULL;
            capture_hash ^= write->value;
            capture_hash *= 1'099'511'628'211ULL;
            ++captured_pairs;
        }
        cached = write->value;
    }
    require(captured_pairs == 175 && capture_hash == 0x2ec64712362b42bbULL,
            "RIX OPL writes differ from original DEMO.EXE DRO capture");

    // A fixed, floored PCM body loses (ticks*rate)%70 sample units each time
    // it loops. Verify that the portable rational clock carries those units
    // indefinitely and never differs from the 70 Hz duration by one sample.
    for (const auto rate : {8'000U, 44'100U, 48'000U}) {
        swd2::AudioLoopClock loop(short_music.total_timer_ticks, rate);
        const auto numerator =
            static_cast<std::uint64_t>(short_music.total_timer_ticks) * rate;
        require(loop.samples_per_loop() == numerator / 70U &&
                    loop.sample_remainder() == numerator % 70U,
                "RIX loop clock did not split its integer and fractional samples");
        std::uint64_t emitted = 0;
        for (std::uint64_t completed = 1; completed <= 100'000U; ++completed) {
            emitted += loop.samples_per_loop();
            if (loop.advance_loop_boundary()) ++emitted;
            require(emitted == completed * numerator / 70U,
                    "RIX loop clock accumulated long-run sample drift");
        }
    }
}

void test_shared_state(const std::filesystem::path& game_root) {
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    require(state.area_graphics_path() == "E:\\SWD2\\T1\\AREA1.RS4",
            "SAVE.DA1 graphics path was not decoded");
    require(state.area_collision_path() == "E:\\SWD2\\T1\\AREA1.RRO",
            "SAVE.DA1 collision path was not decoded");
    require(state.music_path() == "E:\\SWD2\\RX\\MI01.RIX",
            "SAVE.DA1 music path was not decoded");
    require(state.viewport_columns() == 40 && state.viewport_rows() == 25,
            "SAVE.DA1 viewport dimensions were not decoded");
    require(state.map_width() == 180 && state.map_height() == 180,
            "SAVE.DA1 map dimensions were not decoded");
    require(state.viewport_x() == 106 && state.viewport_y() == 1,
            "SAVE.DA1 viewport origin was not decoded");
    require(state.actor_screen_x() == 0x26 && state.actor_screen_y() == 0x50,
            "SAVE.DA1 actor screen position was not decoded");
    require(state.actor_x_offset() == 1 && state.actor_y_offset() == -4 &&
                state.actor_sprite_base() == 0 && state.actor_direction() == 0,
            "SAVE.DA1 actor render fields were not decoded");
    require(state.world_x() == 126 && state.world_y() == 13,
            "SAVE.DA1 actor world position was not derived correctly");
    const swd2::SharedTransfer original{swd2::Marker::open_figure, state};
    const auto serialized = original.bytes();
    const auto restored = swd2::SharedTransfer::from_bytes(serialized);
    require(restored.marker == swd2::Marker::open_figure, "shared marker round trip failed");
    require(restored.state.bytes() == state.bytes(), "shared state round trip failed");
}

void test_item_inventory(const std::filesystem::path& game_root) {
    const auto items = swd2::ItemDatabase::load(game_root / "ITEM.EXE");
    require(items.size() == 522, "unexpected ITEM definition count");

    const auto item_texts = swd2::ItemTextDatabase::load(game_root / "ITEM2.EXE");
    require(item_texts.size() == items.size(),
            "ITEM2 pointer table is not parallel to ITEM");
    require(item_texts.at(0).name == std::vector<std::uint8_t>({0xb5, 0x4c}) &&
                item_texts.at(0).description.empty(),
            "ITEM2 bare none-name record was not decoded");
    require(item_texts.at(51).name ==
                std::vector<std::uint8_t>({0xa9, 0xdb, 0xbb, 0xee, 0xba, 0x58}) &&
                item_texts.at(51).description.size() == 58 &&
                item_texts.at(504).empty(),
            "ITEM2 bracketed name/description boundaries were not decoded");

    std::size_t nonempty_texts = 0;
    std::size_t name_bytes = 0;
    std::size_t description_bytes = 0;
    std::size_t paged_descriptions = 0;
    std::size_t description_page_breaks = 0;
    std::uint64_t text_hash = 1'469'598'103'934'665'603ULL;
    const auto hash_byte = [&](std::uint8_t byte) {
        text_hash ^= byte;
        text_hash *= 1'099'511'628'211ULL;
    };
    for (std::size_t id = 0; id < item_texts.size(); ++id) {
        const auto& text = item_texts.at(static_cast<std::uint16_t>(id));
        if (!text.empty()) ++nonempty_texts;
        name_bytes += text.name.size();
        description_bytes += text.description.size();
        auto page_breaks = std::size_t{0};
        for (std::size_t offset = 0; offset + 1U < text.description.size();
             ++offset) {
            if (text.description[offset] == '%' &&
                text.description[offset + 1U] == '%') {
                ++page_breaks;
            }
        }
        if (page_breaks != 0U) ++paged_descriptions;
        description_page_breaks += page_breaks;
        hash_byte(static_cast<std::uint8_t>(id));
        hash_byte(static_cast<std::uint8_t>(id >> 8U));
        hash_byte(static_cast<std::uint8_t>(text.name.size()));
        hash_byte(static_cast<std::uint8_t>(text.name.size() >> 8U));
        for (const auto byte : text.name) hash_byte(byte);
        hash_byte(static_cast<std::uint8_t>(text.description.size()));
        hash_byte(static_cast<std::uint8_t>(text.description.size() >> 8U));
        for (const auto byte : text.description) hash_byte(byte);
    }
    require(nonempty_texts == 449 && name_bytes == 2'484 &&
                description_bytes == 22'492 && paged_descriptions == 7U &&
                description_page_breaks == 8U &&
                text_hash == 0x2aa57a4be3a622d9ULL,
            "ITEM2 full text corpus differs from the original archive");
    const auto item_font = swd2::LegacyFont::load(game_root / "CHAIN.DSK");
    const auto rendered_item_name = swd2::render_dialogue_page(
        item_font, item_texts.at(51).name, 0, 96, 15, 15);
    const auto rendered_item_description = swd2::render_dialogue_page(
        item_font, item_texts.at(51).description, 0, 112, 96, 14);
    require(item_font.glyph_count() == 1'908 &&
                std::count_if(rendered_item_name.pixels.begin(),
                              rendered_item_name.pixels.end(),
                              [](std::uint8_t pixel) { return pixel != 0; }) > 100 &&
                std::count_if(rendered_item_description.pixels.begin(),
                              rendered_item_description.pixels.end(),
                              [](std::uint8_t pixel) { return pixel != 0; }) > 500,
            "ITEM2 text did not render through the original CHAIN.DSK glyphs");

    const auto& item61 = items.at(61);
    require(item61.type == 14 && item61.id == 61 &&
                item61.preview_sprite == 61 && item61.preview_x == 0 &&
                item61.preview_y == 0 &&
                item61.character_restrictions == 13 &&
                item61.use_flags == 0x042a && !item61.field_usable() &&
                item61.battle_usable() && !item61.consumed_on_use() &&
                item61.discardable() &&
                item61.equipment_category() == 4 && item61.effect_code == 107 &&
                item61.price == 999 && item61.stat_words[0] == 70 &&
                item61.stat_words[1] == static_cast<std::uint16_t>(-30) &&
                item61.stat_words[2] == static_cast<std::uint16_t>(-30),
            "ITEM common prefix fields were decoded at the wrong offsets");

    auto shop_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    for (std::size_t slot = 0; slot < 50; ++slot) shop_state.set_u16(0x382 + slot * 2, 0);
    for (std::size_t i = 0; i < 5; ++i) shop_state.set_u16(0x3e6 + i * 2, 0);
    shop_state.set_u16(0x104, 100);
    swd2::InventorySystem shop(shop_state, items);
    require(shop.purchase(117) && shop.item(0) == 117 && shop_state.u16(0x104) == 75,
            "RPG shop did not deduct ITEM +0b price and insert its object");
    require(shop.purchase(68) && shop.purchase(68) && shop.item(1) == 68 &&
                shop.item(2) == 0 && shop_state.u16(0x3e6) == 2 &&
                shop_state.u16(0x104) == 55,
            "RPG special item did not share its 44h counter");
    shop_state.set_u16(0x3e6, 20);
    require(!shop.purchase(68) && shop_state.u16(0x104) == 55,
            "RPG shop exceeded the original special-item cap");
    shop_state.set_u16(0x382, 0);
    shop.compact();
    require(shop.item(0) == 68 && shop.item(1) == 0,
            "RPG inventory compaction was not stable");

    shop_state.set_u16(0x104, 65'500);
    shop_state.set_u16(0x382, 61);
    shop_state.set_u16(0x384, 68);
    require(shop.sale_value(0) == 750 && shop.sell(0) &&
                shop_state.u16(0x104) == 0xffff && shop.item(0) == 68,
            "RPG shop sale did not use three-quarter value/saturating money/compaction");
    shop_state.set_u16(0x382, 77);  // ITEM +05 lacks the original sale bit.
    require(!shop.sale_value(0) && !shop.sell(0) && shop_state.u16(0x104) == 0xffff,
            "RPG shop accepted an unsellable ITEM record");

    auto equip_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    equip_state.set_u16(0x382, 167);  // category three, allowed for actor zero
    swd2::InventorySystem equipment(equip_state, items);
    const auto actor0 = std::size_t{0x106};
    const auto old_defense = equip_state.u16(actor0 + 0x0e);
    const auto old_secondary = equip_state.u16(actor0 + 0x5d);
    const auto exchange = equipment.exchange_equipment(0, 0, 1);
    require(exchange.status == swd2::EquipmentExchangeStatus::exchanged &&
                exchange.equipped_item == 167 && exchange.returned_item == 165 &&
                equip_state.u16(0x382) == 165 && equip_state.u16(actor0 + 0x12) == 167 &&
                equip_state.u16(actor0 + 0x0e) == old_defense + 22 &&
                equip_state.u16(actor0 + 0x5d) == old_secondary - 2,
            "RPG equipment exchange did not swap ids/apply signed stat words");

    equip_state.set_u16(0x382, 166);  // restriction bit 8 forbids actor identity zero
    require(equipment.exchange_equipment(0, 0, 1).status ==
                swd2::EquipmentExchangeStatus::character_restricted &&
                equipment.exchange_equipment(0, 0, 0).status ==
                swd2::EquipmentExchangeStatus::character_restricted,
            "RPG equipment character restriction mask was not enforced");
    equip_state.set_u16(0x382, 167);
    require(equipment.exchange_equipment(0, 0, 0).status ==
                swd2::EquipmentExchangeStatus::category_mismatch,
            "RPG equipment category/row mapping was not enforced");

    // Category nine is the duplicated two-handed representation. Replacing
    // actor zero's existing two-handed id 122 returns one copy; selecting an
    // empty cell then removes the pair and its bonuses only once.
    equip_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    equip_state.set_u16(0x382, 117);
    equip_state.set_u16(0x384, 0);
    swd2::InventorySystem hands(equip_state, items);
    const auto equip_two_handed = hands.exchange_equipment(0, 0, 2);
    require(equip_two_handed.status == swd2::EquipmentExchangeStatus::exchanged &&
                equip_two_handed.returned_item == 122 &&
                equip_state.u16(actor0 + 0x14) == 117 &&
                equip_state.u16(actor0 + 0x16) == 117 &&
                equip_state.u8(actor0 + 0x2c) == 1,
            "RPG two-handed item was not mirrored into both hand slots");
    const auto unequip_two_handed = hands.exchange_equipment(1, 0, 2);
    require(unequip_two_handed.status == swd2::EquipmentExchangeStatus::exchanged &&
                equip_state.u16(0x384) == 117 &&
                equip_state.u16(actor0 + 0x14) == 0 &&
                equip_state.u16(actor0 + 0x16) == 0 &&
                equip_state.u8(actor0 + 0x2c) == 0,
            "RPG empty-cell two-handed unequip contract was not reproduced");

    equip_state.set_u16(actor0 + 0x14, 117);
    equip_state.set_u16(actor0 + 0x16, 118);
    equip_state.set_u8(actor0 + 0x2c, 0);
    equip_state.set_u16(0x382, 117);
    const auto conflict = hands.exchange_equipment(0, 0, 3);
    require(conflict.status == swd2::EquipmentExchangeStatus::two_handed_conflict &&
                equip_state.u16(0x382) == 117 && equip_state.u16(actor0 + 0x14) == 117 &&
                equip_state.u16(actor0 + 0x16) == 118,
            "RPG two-handed conflict mutated occupied hand slots");

    const auto& item51 = items.at(51);
    require(item51.use_flags == 0x102f && item51.field_usable() &&
                item51.battle_usable() && item51.consumed_on_use() &&
                item51.discardable(),
            "ITEM +05 inventory action bits were not decoded as an unaligned word");
}

void test_field_actions(const std::filesystem::path& game_root) {
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x10, 4);
    const auto base = std::size_t{0x106};
    state.set_u16(base + 8, 0x0300);
    state.set_u16(base + 0x2d, 10);
    state.set_u16(base + 0x2f, 100);
    state.set_u16(base + 0x35, 20);
    state.set_u16(base + 0x37, 80);
    state.set_u16(base + 0x45, 20);
    state.set_u16(base + 0x55, 5);
    state.set_u16(base + 0x57, 40);

    swd2::FieldActionRuntime field_runtime;
    swd2::FieldActionSystem actions(state, &field_runtime);
    require(swd2::FieldActionSystem::requires_target(1) &&
                !swd2::FieldActionSystem::requires_target(0x0c) &&
                swd2::FieldActionSystem::affects_all_party(0x12),
            "RPG field-action target modes differ from the 42-entry dispatcher");
    require(actions.apply(1, 0).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 0x2d) == 40 && state.u16(base + 0x35) == 45,
            "RPG percentage restorative did not add max percent plus +45/4");

    const auto target_one = base + 0x9f;
    state.set_u16(target_one + 8, 0);
    state.set_u16(target_one + 0x2d, 0);
    state.set_u16(target_one + 0x2f, 100);
    state.set_u16(target_one + 0x35, 0);
    state.set_u16(target_one + 0x37, 100);
    state.set_u16(target_one + 0x45, 80);
    require(actions.apply(1, 1).status == swd2::FieldActionStatus::applied &&
                state.u16(target_one + 0x2d) == 30 &&
                state.u16(target_one + 0x35) == 30,
            "RPG percentage restorative used target +45 instead of DS:35fc owner");

    state.set_u16(base + 8, 0x0300);
    state.set_u16(base + 0x2d, 1);
    state.set_u16(base + 0x35, 2);
    state.set_u16(base + 0x55, 3);
    state.set_u16(base + 0x57, 40);
    state.set_u16(target_one + 8, 0x0100);
    state.set_u16(target_one + 0x55, 4);
    state.set_u16(target_one + 0x57, 50);
    require(actions.apply(0x12).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 8) == 0 && state.u16(base + 0x2d) == 100 &&
                state.u16(base + 0x35) == 80 && state.u16(base + 0x55) == 40 &&
                state.u16(target_one + 8) == 0 &&
                state.u16(target_one + 0x55) == 50,
            "RPG all-party action 12h omitted 358d's +55 ability-pool restore");

    state.set_u16(base + 8, 0x0300);
    state.set_u16(base + 0x2d, 0);
    state.set_u16(base + 0x35, 0);
    require(actions.apply(0x10, 0).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 8) == 0 && state.u16(base + 0x2d) == 35 &&
                state.u16(base + 0x35) == 29,
            "RPG restorative/cure action 10h did not match 352a..3547");

    state.set_u16(base + 8, 0x2000);
    state.set_u16(base + 0x2d, 0);
    state.set_u16(base + 0x35, 0);
    require(actions.apply(0x1c, 0).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 8) == 0 && state.u16(base + 0x2d) == 15 &&
                state.u16(base + 0x35) == 29 &&
                field_runtime.primary_operand == 10 &&
                field_runtime.secondary_operand == 30,
            "RPG field resurrection lost its stale-secondary operand quirk");

    state.set_u16(base + 8, 0);
    state.set_u16(base + 0x2d, 10);
    state.set_u16(base + 0x35, 20);
    require(actions.apply(0x13, 0).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 0x2d) == 65 && state.u16(base + 0x35) == 75,
            "RPG fixed restorative did not add secondary-pool/4 before clamping");

    state.set_u16(base + 8, 0x0700);
    require(actions.apply(0x1f, 0).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 8) == 0x0400,
            "RPG action 1fh did not clear the paired 0100/0200 status mask");
    const auto attack = state.u16(base + 0x0c);
    const auto strength = state.u16(base + 0x3d);
    require(actions.apply(0x24, 0).status == swd2::FieldActionStatus::applied &&
                state.u16(base + 0x0c) == static_cast<std::uint16_t>(attack + 3) &&
                state.u16(base + 0x3d) == static_cast<std::uint16_t>(strength + 3),
            "RPG permanent action 24h did not update both linked actor words");

    state.set_u16(0x408, 0x4000);
    require(actions.apply(0x28).status == swd2::FieldActionStatus::map_restricted,
            "RPG action 28h ignored the map 4000h restriction");
    state.set_u16(0x408, 0x8000);
    require(actions.apply(0x28).status == swd2::FieldActionStatus::travel_current &&
                actions.apply(0x29).status == swd2::FieldActionStatus::travel_select,
            "RPG field travel actions did not honor their complementary map gates");
    for (std::size_t i = 0; i < 8; ++i) state.set_u8(0x51e + i, 0);
    state.set_u8(0x51f, 1);
    state.set_u8(0x521, 1);
    state.set_u8(0x522, 0x0f);
    require(actions.unlocked_travel_indices() == std::vector<std::uint8_t>{1, 3} &&
                swd2::FieldActionSystem::travel_directory_offset(0) == 0x0046 &&
                swd2::FieldActionSystem::travel_directory_offset(33) == 0x0372 &&
                !swd2::FieldActionSystem::travel_directory_offset(34),
            "RPG DS:3a8a travel table/SAVE+51e unlock list was not reproduced");
}

void test_map_resource(const std::filesystem::path& game_root) {
    const auto map = swd2::MapResource::load(game_root / "T2" / "TW-2A");
    require(map.tile_count() == 2029, "unexpected TW-2A tile count");
    require(map.layout().width == 180 && map.layout().height == 180,
            "unexpected TW-2A map dimensions");
    const auto image = map.render(true);
    require(image.width == 1440 && image.height == 1440, "unexpected rendered map dimensions");
    require(!map.overlays().empty(), "TW-2A overlay records were not decoded");

    const auto animated = swd2::MapResource::load(game_root / "T1" / "AREA1");
    require(animated.animation_words()[0] == 5U &&
                animated.animation_words()[1] == 720U &&
                animated.animation_words()[2] == 735U &&
                animated.animation_words()[3] == 0x0305U,
            "AREA1 palette-cycle metadata was not decoded");
    auto palette = animated.palette();
    const auto original = palette;
    std::array<std::uint16_t, 24> runtime{};
    std::copy_n(animated.animation_words().begin(), 4, runtime.begin());
    require(!swd2::advance_map_palette(palette, runtime) &&
                runtime[3] == 0x0405U && palette == original,
            "RPG 5e16 palette cycle ignored its fixed-point phase");
    require(swd2::advance_map_palette(palette, runtime) &&
                runtime[3] == 0x0005U &&
                std::equal(palette.begin() + 720, palette.begin() + 723,
                           original.begin() + 735),
            "RPG 5e16 palette cycle did not wrap the final RGB color");
    for (std::size_t offset = 723; offset <= 735; offset += 3U) {
        require(std::equal(palette.begin() + static_cast<std::ptrdiff_t>(offset),
                           palette.begin() + static_cast<std::ptrdiff_t>(offset + 3U),
                           original.begin() + static_cast<std::ptrdiff_t>(offset - 3U)),
                "RPG 5e16 palette cycle did not shift an RGB triplet");
    }

    const auto padded = swd2::MapResource::load(game_root / "T2" / "HOL2");
    require(padded.layout().directory_bytes == 4U &&
                padded.layout().image_end == 64808U &&
                padded.layout().layer_count == 1U &&
                padded.cell_base() == 8U && padded.layout().width == 180U &&
                padded.layout().height == 180U &&
                padded.cells().size() == 32400U &&
                (padded.cells().back() & 0x07ffU) < padded.tile_count(),
            "HOL2 RAP pointer-directory/cell extent oracle changed");
    const auto padded_image = padded.render(true);
    require(padded_image.width == 1440U && padded_image.height == 1440U,
            "portable full-map renderer did not consume exact HOL2 cells");

    const auto non_square = swd2::MapResource::load(game_root / "T6" / "EIW");
    require(non_square.layout().directory_bytes == 4U &&
                non_square.cell_base() == 8U &&
                non_square.layout().width == 100U &&
                non_square.layout().height == 60U &&
                non_square.cells().size() == 6000U,
            "RAP height,width words were transposed on a non-square map");

    const auto layered = swd2::MapResource::load(game_root / "T6" / "ZD");
    const auto pre_actor_overlays = static_cast<std::size_t>(std::count_if(
        layered.overlays().begin(), layered.overlays().end(),
        [](const auto& overlay) { return (overlay.tile & 0x2000U) != 0U; }));
    require(layered.layout().directory_bytes == 6U &&
                layered.layout().image_end == 0x1a08U &&
                layered.layout().layer_count == 2U &&
                layered.cell_base() == 10U && layered.layout().width == 93U &&
                layered.layout().height == 25U &&
                layered.cells().size() == 2325U &&
                layered.has_fixed_background_layer() &&
                layered.overlays().size() == 552U &&
                pre_actor_overlays == 325U,
            "ZD two-record RAP directory was not decoded");
    const auto world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    const auto& zd_left = world.location_at_directory_offset(498U);
    const auto& zd_right = world.location_at_directory_offset(504U);
    require(zd_left.area_offset == 25723U && zd_left.area.flags == 4268U &&
                zd_left.map_position == 10U && zd_left.viewport_x == 0U &&
                zd_left.viewport_y == 0U && zd_right.area_offset == 25723U &&
                zd_right.map_position == 110U && zd_right.viewport_x == 50U &&
                zd_right.viewport_y == 0U,
            "MAPA ZD placement/layer flag oracle changed");
    const auto hash_pixels = [](const std::vector<std::uint8_t>& pixels) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto pixel : pixels) {
            hash ^= pixel;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    const auto zd_left_bare = layered.render_viewport_background(0U, 0U, false);
    const auto zd_left_background = layered.render_viewport_background(0U, 0U);
    const auto zd_left_frame = layered.render_viewport(0U, 0U);
    const auto zd_right_frame = layered.render_viewport(50U, 0U);
    require(zd_left_frame.width == 320U && zd_left_frame.height == 200U &&
                hash_pixels(zd_left_bare.pixels) == 2094815236618880533ULL &&
                hash_pixels(zd_left_background.pixels) ==
                    17375729876272745673ULL &&
                hash_pixels(zd_left_frame.pixels) == 3074925849497365130ULL &&
                hash_pixels(zd_right_frame.pixels) == 1382700188505759108ULL,
            "ZD fixed-background/scrolling-layer viewport oracle changed");
}

void test_map_database(const std::filesystem::path& game_root) {
    const auto immutable = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    const auto mutable_save = swd2::MapDatabase::load(game_root / "MAPZ.DA1");
    require(immutable.has_trailing_sentinel() && !mutable_save.has_trailing_sentinel(),
            "MAPA and MAPZ end-marker variants were not distinguished");
    const auto mapz_bytes = read_file(game_root / "MAPZ.DA1");
    require(std::equal(mapz_bytes.begin(), mapz_bytes.end(),
                       mutable_save.serialized_bytes().begin(),
                       mutable_save.serialized_bytes().end()),
            "MAPZ deterministic checkpoint view differs from serialized bytes");
    require(immutable.locations().size() == 466 && immutable.unique_area_count() == 152,
            "unexpected MAPA world database dimensions");

    const auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto& location = mutable_save.location_at_directory_offset(
        state.map_location_directory_offset());
    require(location.directory_offset == 8 && location.map_position == state.u16(0x40d),
            "SAVE.DA did not select the expected MAPZ location");
    require(location.viewport_x == state.viewport_x() &&
                location.viewport_y == state.viewport_y() &&
                location.actor_screen_x == state.actor_screen_x() &&
                location.actor_screen_y == state.actor_screen_y() &&
                location.actor_direction == state.actor_direction(),
            "MAPZ initial placement differs from SAVE.DA");
    require(location.area.entity_fields[0].size() == 5,
            "MAPZ AREA1 entity arrays were not decoded");
    const auto gate = swd2::map_entity(location.area, 1);
    require(gate.sprite == 0x4400 && gate.cell_offset == 0x9d36 && gate.behavior == 4 &&
                gate.render_x_offset == -2 && gate.render_y_offset == -8 &&
                gate.event_directory_offset == 0x12c,
            "MAPZ AREA1 entity fields were not transposed correctly");
    require(location.area.graphics_path == "\\SWD2\\T1\\AREA1.RSK" &&
                location.area.layout_path == "\\SWD2\\T1\\AREA1.RSK" &&
                location.area.music_path == "\\SWD2\\RX\\MI01.RIX" &&
                location.area.event_archive_path == "CHNA1.EXE" &&
                location.area.event_font_path == "CHNA1.DSK",
            "MAPZ AREA1 resource pointers were not decoded");
}

void test_map_transition_database(const std::filesystem::path& game_root) {
    const auto transitions =
        swd2::MapTransitionDatabase::load(game_root / "MAP0.EXE");
    std::size_t special_count = 0;
    std::size_t relative_count = 0;
    std::size_t travel_flag_count = 0;
    for (std::uint16_t directory_offset = 10; directory_offset < 314;
         directory_offset = static_cast<std::uint16_t>(directory_offset + 2U)) {
        for (const auto& record : transitions.records(directory_offset)) {
            special_count += record.is_special();
            relative_count += record.uses_relative_placement();
            travel_flag_count += record.sets_travel_flag();
        }
    }
    require(transitions.area_count() == 152U &&
                transitions.record_count() == 481U &&
                special_count == 25U && relative_count == 20U &&
                travel_flag_count == 35U,
            "MAP0 transition directory dimensions changed");

    const auto initial = transitions.match(0xe00a, 0x25ae, 180);
    require(initial && initial->row_count == 1U &&
                initial->flag_index == 0U &&
                initial->first_cell == 0x25aeU &&
                initial->last_cell == 0x25aeU &&
                initial->action == 0x200aU &&
                initial->sets_travel_flag() && !initial->is_special() &&
                initial->destination_directory_offset() == 10U,
            "MAP0 initial AREA1 portal was not decoded");

    const auto last_row = static_cast<std::uint16_t>(
        0x70bcU + 46U * 180U * 2U);
    const auto rectangular = transitions.match(0x0034, last_row, 180);
    require(rectangular && rectangular->row_count == 47U &&
                rectangular->first_cell == 0x70bcU &&
                rectangular->last_cell == 0x710cU &&
                rectangular->is_special() &&
                rectangular->special_action() == 7U &&
                !transitions.match(0x0034,
                                   static_cast<std::uint16_t>(last_row + 0x52U),
                                   180),
            "MAP0 multi-row special-trigger expansion differs from e94");

    // AREA 0x2010 deliberately advances its 180-row portal past 0xffff.
    // RPG:e94 wraps AX/DX, making row 126 cover 000c..0124; the former
    // uint32_t rewrite silently made this released portal row unreachable.
    const auto wrapped = transitions.match(0x2010U, 0x000cU, 180U);
    require(wrapped && wrapped->row_count == 180U &&
                wrapped->first_cell == 0x4edcU &&
                wrapped->last_cell == 0x4ff4U &&
                wrapped->action == 0x0046U &&
                wrapped->destination_directory_offset() == 0x0046U &&
                !transitions.match(0x2010U, 0x000aU, 180U),
            "MAP0 16-bit trigger-row wrapping differs from RPG:e94");
}

void test_rpg_entity_system(const std::filesystem::path& game_root) {
    const auto map = swd2::MapResource::load(game_root / "T1" / "AREA1");
    std::size_t open_x = 0;
    std::size_t open_y = 0;
    bool found = false;
    for (std::size_t y = 1; y + 1 < map.layout().height && !found; ++y) {
        for (std::size_t x = 1; x + 3 < map.layout().width; ++x) {
            bool clear = true;
            for (std::size_t dy = 0; dy < 2; ++dy) {
                for (std::size_t dx = 0; dx < 3; ++dx) {
                    clear = clear &&
                        (map.cells()[(y + dy) * map.layout().width + x + dx] &
                         0xf800U) == 0;
                }
            }
            if (clear) {
                open_x = x;
                open_y = y;
                found = true;
                break;
            }
        }
    }
    require(found, "AREA1 has no open rectangle for entity movement test");

    swd2::SharedState::Storage empty{};
    auto state = swd2::SharedState::from_bytes(empty);
    state.set_u16(0x40f, 8);
    state.set_u16(0x417, map.layout().width);
    state.set_u16(0x419, map.layout().height);

    swd2::MapAreaRecord area;
    for (auto& field : area.entity_fields) field.resize(1);
    const auto anchor = static_cast<std::uint16_t>(
        8U + (open_y * map.layout().width + open_x) * 2U);
    area.entity_fields[2][0] = anchor;
    area.entity_fields[3][0] = 0;
    area.entity_fields[4][0] = 2;
    area.entity_fields[7][0] = 4;
    area.entity_fields[8][0] = 4;

    const auto mz = swd2::dos::MzExecutable::load(game_root / "RPG.EXE");
    const auto file = read_file(game_root / "RPG.EXE");
    const auto image_start = static_cast<std::size_t>(mz.header_size());
    const auto image_size = static_cast<std::size_t>(mz.load_image_size());
    const auto image = std::span<const std::uint8_t>(
        file.data() + image_start, image_size);
    require(image.size() > 0x5f20 && image[0x4f1c] == 0xc7 &&
                image[0x4f1d] == 0x06,
            "RPG autonomous movement code-stream oracle changed");

    swd2::RpgEntityRuntime runtime;
    swd2::advance_rpg_entities(area, map, state, runtime, image);
    require(area.entity_fields[2][0] ==
                static_cast<std::uint16_t>(anchor + map.layout().width * 2U) &&
                area.entity_fields[1][0] == 0 && area.entity_fields[10][0] == 1 &&
                runtime.delay_remaining[0] == 2 && runtime.roam_y[0] == 1 &&
                runtime.code_stream_offset == 2,
            "RPG behavior-0 entity did not consume the original 4f1ch stream");
    swd2::advance_rpg_entities(area, map, state, runtime, image);
    require(runtime.delay_remaining[0] == 1 && runtime.code_stream_offset == 2,
            "RPG entity delay counter did not suppress movement-stream consumption");

    // RPG:10fd recopies the eleven MAPZ fields on an area change but never
    // clears 3cbah/4260h/4580h/4648h. A same-index entity in the destination
    // therefore inherits the remaining delay and does not consume a new code
    // word until it expires.
    auto destination_area = area;
    destination_area.entity_fields[2][0] = anchor;
    swd2::advance_rpg_entities(destination_area, map, state, runtime, image);
    require(destination_area.entity_fields[2][0] == anchor &&
                runtime.delay_remaining[0] == 0 &&
                runtime.roam_y[0] == 1 &&
                runtime.code_stream_offset == 2,
            "RPG area reload incorrectly cleared autonomous entity BSS state");

    swd2::RpgEntityRuntime fixed_bss;
    fixed_bss.delay_remaining = {3U, 7U};
    fixed_bss.roam_x = {4U, 9U};
    fixed_bss.roam_y = {5U, 11U};
    swd2::MapAreaRecord smaller_area;
    for (auto& field : smaller_area.entity_fields) field.resize(1);
    smaller_area.entity_fields[3][0] = 3U;
    swd2::advance_rpg_entities(smaller_area, map, state, fixed_bss, image);
    require(fixed_bss.delay_remaining ==
                std::vector<std::uint16_t>({3U, 7U}) &&
                fixed_bss.roam_x ==
                    std::vector<std::uint16_t>({4U, 9U}) &&
                fixed_bss.roam_y ==
                    std::vector<std::uint16_t>({5U, 11U}),
            "RPG smaller area truncated the original fixed entity BSS arrays");

    area.entity_fields[3][0] = 2;
    area.entity_fields[10][0] = 3;
    runtime.delay_remaining[0] = 0;
    swd2::advance_rpg_entities(area, map, state, runtime, image);
    require(area.entity_fields[10][0] == 0 && runtime.delay_remaining[0] == 2,
            "RPG behavior-2 entity animation did not wrap at field 7");

    area.entity_fields[3][0] = 0;
    runtime.delay_remaining[0] = 0;
    runtime.code_stream_offset = 0x1000;
    swd2::advance_rpg_entities(area, map, state, runtime, image);
    require(runtime.code_stream_offset == 2,
            "RPG entity stream did not preserve its compute-before-wrap quirk");

    swd2::SharedState::Storage formation_bytes{};
    auto formation = swd2::SharedState::from_bytes(formation_bytes);
    formation.set_u16(0x10, 3);
    formation.set_u16(0x102, 0);
    static constexpr std::array<std::uint16_t, 12> trail = {
        3, 0, 9, 6, 3, 7, 0, 9, 6, 3, 7, 0};
    for (std::size_t slot = 0; slot < trail.size(); ++slot) {
        formation.set_u16(0x12 + slot * 2U,
                          static_cast<std::uint16_t>(100U + slot * 10U));
        formation.set_u16(0x2a + slot * 2U,
                          static_cast<std::uint16_t>(200U + slot * 10U));
        formation.set_u16(0x8a + slot * 2U,
                          static_cast<std::uint16_t>(slot & 3U));
        formation.set_u16(0xa2 + slot * 2U, 0x55U);
        formation.set_u16(0xba + slot * 2U, trail[slot]);
    }
    formation.set_actor_direction(9);
    swd2::advance_rpg_party_formation(formation, -2, 8);
    require(formation.u16(0xba) == 9U &&
                formation.u16(0x12) == 100U &&
                formation.u16(0x2a) == 200U,
            "RPG 1f42 changed the leader slot or lost its current direction");
    for (std::size_t slot = 1; slot < trail.size(); ++slot) {
        auto expected_x = static_cast<std::uint16_t>(100U + slot * 10U - 2U);
        auto expected_y = static_cast<std::uint16_t>(200U + slot * 10U + 8U);
        if (trail[slot] == 0U) expected_y = static_cast<std::uint16_t>(expected_y + 8U);
        if (trail[slot] == 9U) expected_x = static_cast<std::uint16_t>(expected_x + 2U);
        if (trail[slot] == 6U) expected_x = static_cast<std::uint16_t>(expected_x - 2U);
        if (trail[slot] == 3U) expected_y = static_cast<std::uint16_t>(expected_y - 8U);
        const auto valid_direction = trail[slot] == 0U || trail[slot] == 9U ||
                                     trail[slot] == 6U || trail[slot] == 3U;
        require(formation.u16(0x12 + slot * 2U) == expected_x &&
                    formation.u16(0x2a + slot * 2U) == expected_y &&
                    formation.u16(0xba + slot * 2U) ==
                        (slot == 1U ? 9U : trail[slot - 1U]) &&
                    formation.u16(0xa2 + slot * 2U) ==
                        (valid_direction ? trail[slot] : 0x55U),
                "RPG 1f42 formation trail does not match the descending slot shift");
    }
    swd2::advance_rpg_party_animation(formation);
    for (std::size_t slot = 0; slot < trail.size(); ++slot) {
        const auto initial = static_cast<std::uint16_t>(slot & 3U);
        const auto expected = slot < 9U
            ? static_cast<std::uint16_t>((initial + 1U) & 3U)
            : initial;
        require(formation.u16(0x8a + slot * 2U) == expected,
                "RPG 1e63 did not animate exactly three slots per active party member");
    }

    std::uint16_t encounter_cursor = 0;
    std::uint16_t rejected_cursor = 0;
    for (std::uint16_t cursor = 0x1000U; cursor < 0x2000U; cursor += 2U) {
        const auto offset = 0x4f1cU + cursor;
        const auto word = static_cast<std::uint16_t>(image[offset]) |
                          (static_cast<std::uint16_t>(image[offset + 1U]) << 8U);
        if ((word & 4U) != 0U && encounter_cursor == 0U) encounter_cursor = cursor;
        if ((word & 4U) == 0U && rejected_cursor == 0U) rejected_cursor = cursor;
    }
    require(encounter_cursor != 0U && rejected_cursor != 0U,
            "RPG 1fa8 random code window lacks both tested gate outcomes");

    swd2::SharedState::Storage world_bytes{};
    auto world = swd2::SharedState::from_bytes(world_bytes);
    world.set_u16(0x10, 2U);
    const auto actor_zero = 0x106U;
    const auto actor_one = actor_zero + 0x9fU;
    world.set_u16(actor_zero + 8U, 0x0200U);
    world.set_u16(actor_zero + 0x2dU, 2U);
    world.set_u16(actor_one + 8U, 0x0200U);
    world.set_u16(actor_one + 0x2dU, 1U);
    swd2::RpgWorldStepRuntime world_runtime;
    world_runtime.poison_steps = 9U;
    const auto poison = swd2::advance_rpg_world_step(
        world, world_runtime, image, true);
    require(poison.poison_flash && !poison.random_encounter &&
                poison.defeated_party_members == std::vector<std::size_t>{1U} &&
                world.u16(actor_zero + 0x2dU) == 1U &&
                world.u16(actor_zero + 8U) == 0x0200U &&
                world.u16(actor_one + 0x2dU) == 0U &&
                world.u16(actor_one + 8U) == 0x2000U &&
                world_runtime.poison_steps == 0U,
            "RPG 1fa8 ten-step poison pulse differs from the party status loop");

    world.set_u16(0x49c, rejected_cursor);
    world_runtime.encounter_steps = 39U;
    world_runtime.encounter_hits = 0U;
    const auto rejected = swd2::advance_rpg_world_step(
        world, world_runtime, image, true);
    require(!rejected.random_encounter &&
                world_runtime.encounter_steps == 40U &&
                world_runtime.encounter_hits == 0U &&
                world.u16(0x49c) == static_cast<std::uint16_t>(rejected_cursor + 2U),
            "RPG 1fa8 cleared its 40-step counter after a rejected code word");

    world.set_u16(0x49c, encounter_cursor);
    world.set_u16(0x4a0, 0x1234U);
    world_runtime.encounter_steps = 39U;
    world_runtime.encounter_hits = 3U;
    const auto encounter = swd2::advance_rpg_world_step(
        world, world_runtime, image, true);
    const auto encounter_word = static_cast<std::uint16_t>(
        image[0x4f1cU + encounter_cursor]) |
        (static_cast<std::uint16_t>(image[0x4f1cU + encounter_cursor + 1U]) << 8U);
    require(encounter.random_encounter && world.u16(0x4a0) == 0U &&
                world_runtime.encounter_hits == 4U &&
                world_runtime.encounter_steps == (encounter_word & 0x1fU),
            "RPG 1fa8 fourth accepted code word did not request a random battle");

    const auto disabled_before = world_runtime;
    const auto disabled = swd2::advance_rpg_world_step(
        world, world_runtime, image, false);
    require(!disabled.poison_flash && !disabled.random_encounter &&
                world_runtime.poison_steps == disabled_before.poison_steps &&
                world_runtime.encounter_steps == disabled_before.encounter_steps &&
                world_runtime.encounter_hits == disabled_before.encounter_hits,
            "RPG 1fa8 advanced field hazards in an auxiliary-zero area");
}

void test_save_slot(const std::filesystem::path& game_root) {
    const auto temporary = std::filesystem::temp_directory_path() /
        ("swd2-save-slot-" + std::to_string(
            static_cast<unsigned long long>(
                std::filesystem::file_time_type::clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(temporary);
    try {
        auto slot = swd2::SaveSlot::open(game_root, temporary, 4);
        require(slot.slot() == 4 &&
                    slot.state_path().filename() == "SAVE.DA4" &&
                    slot.map_path().filename() == "MAPZ.DA4" &&
                    std::filesystem::file_size(slot.state_path()) ==
                        swd2::SharedState::byte_size,
                "portable save slot did not seed the selected SAVE/MAPZ pair");

        auto state = slot.state();
        state.set_u16(0x104, 4321);
        const auto location_offset = std::uint16_t{14};
        const auto field = std::uint16_t{3};
        const auto byte_offset = std::int16_t{76};
        auto map = slot.map_database();
        const auto before =
            map->location_at_directory_offset(location_offset).area.entity_fields[3][38];
        map->mutate_area_word(location_offset, field, byte_offset, 1, true);
        map->mutate_area_word(338, 10, 2, 19, true);
        map->mutate_area_word(338, 10, 4, 19, true);
        map->mutate_area_word(636, 3, 3, 8, false);
        slot.save(state);

        auto reopened = swd2::SaveSlot::open(game_root, temporary, 4);
        require(reopened.state().u16(0x104) == 4321 &&
                    reopened.map_database()
                            ->location_at_directory_offset(location_offset)
                            .area.entity_fields[3][38] ==
                        static_cast<std::uint16_t>(before + 1U) &&
                    reopened.map_database()
                            ->location_at_directory_offset(338)
                            .area.graphics_path == "\\SWD2\\T4\\AREA7.RAP" &&
                    reopened.map_database()
                            ->location_at_directory_offset(636)
                            .area.entity_fields[3][1] == 0x0800U,
                "portable save slot did not persist both SAVE and MAPZ mutations");

        const auto state_temporary = temporary / ".swd2-slot4-save.tmp";
        const auto map_temporary = temporary / ".swd2-slot4-map.tmp";
        const auto transaction = temporary / ".swd2-slot4.txn";
        auto stale_state = reopened.state();
        stale_state.set_u16(0x104, 1111);
        stale_state.save(state_temporary);
        auto stale_map = swd2::MapDatabase::load(reopened.map_path());
        stale_map.mutate_area_word(location_offset, field, byte_offset,
                                   0x0222U, false);
        stale_map.save(map_temporary);
        const auto discarded = swd2::SaveSlot::open(game_root, temporary, 4);
        require(discarded.state().u16(0x104) == 4321 &&
                    discarded.map_database()
                            ->location_at_directory_offset(location_offset)
                            .area.entity_fields[3][38] ==
                        static_cast<std::uint16_t>(before + 1U) &&
                    !std::filesystem::exists(state_temporary) &&
                    !std::filesystem::exists(map_temporary),
                "unmarked save temporaries were not discarded as an old transaction");

        auto recovery_state = discarded.state();
        recovery_state.set_u16(0x104, 5432);
        recovery_state.save(state_temporary);
        auto recovery_map = swd2::MapDatabase::load(discarded.map_path());
        recovery_map.mutate_area_word(location_offset, field, byte_offset,
                                      0x0456U, false);
        recovery_map.save(map_temporary);
        {
            std::ofstream marker(transaction, std::ios::binary);
            marker << "SWD2PAIR1\n";
        }
        // Simulate interruption after MAPZ was installed but before SAVE was
        // renamed. The marker and remaining SAVE temporary must roll forward.
        std::filesystem::copy_file(
            map_temporary, discarded.map_path(),
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(map_temporary);
        const auto recovered = swd2::SaveSlot::open(game_root, temporary, 4);
        require(recovered.state().u16(0x104) == 5432 &&
                    recovered.map_database()
                            ->location_at_directory_offset(location_offset)
                            .area.entity_fields[3][38] == 0x0456U &&
                    !std::filesystem::exists(transaction) &&
                    !std::filesystem::exists(state_temporary),
                "interrupted SAVE/MAPZ pair did not roll forward atomically");

        state.set_u16(0x104, 9876);
        swd2::SaveSlot::save_as(temporary, 5, state, *map);
        auto copied = swd2::SaveSlot::open(game_root, temporary, 5);
        require(copied.state().u16(0x104) == 9876 &&
                    copied.map_database()
                            ->location_at_directory_offset(location_offset)
                            .area.entity_fields[3][38] ==
                        static_cast<std::uint16_t>(before + 1U) &&
                    copied.map_database()
                            ->location_at_directory_offset(338)
                            .area.layout_path == "\\SWD2\\T4\\AREA7.RAP" &&
                    copied.map_database()
                            ->location_at_directory_offset(636)
                            .area.entity_fields[3][1] == 0x0800U,
                "RPG save-item callback could not atomically write a chosen SAVE/MAPZ pair");

        // New Game replaces MAPZ wholesale with MAPZ.DAQ after the frontend
        // initially opened a numbered slot.  An exit checkpoint must commit
        // that live pair, not combine the new SAVE block with the slot's stale
        // database.  Also prove the SaveSlot snapshot follows the replacement
        // for a subsequent one-argument checkpoint.
        auto replacement_map = swd2::MapDatabase::load(game_root / "MAPZ.DAQ");
        auto replacement_state = swd2::SharedState::load(game_root / "SAVE.DAQ");
        replacement_state.set_u16(0x104, 6789);
        copied.save(replacement_state, replacement_map);
        const auto replacement_bytes = replacement_map.serialized_bytes();
        auto replacement_reopened = swd2::SaveSlot::open(
            game_root, temporary, 5);
        auto committed_replacement =
            replacement_reopened.map_database()->serialized_bytes();
        require(replacement_reopened.state().u16(0x104) == 6789 &&
                    committed_replacement.size() == replacement_bytes.size() &&
                    std::equal(committed_replacement.begin(),
                               committed_replacement.end(),
                               replacement_bytes.begin()),
                "frontend checkpoint paired replacement SAVE with stale MAPZ");
        replacement_state.set_u16(0x104, 6790);
        copied.save(replacement_state);
        replacement_reopened = swd2::SaveSlot::open(game_root, temporary, 5);
        committed_replacement =
            replacement_reopened.map_database()->serialized_bytes();
        require(replacement_reopened.state().u16(0x104) == 6790 &&
                    committed_replacement.size() == replacement_bytes.size() &&
                    std::equal(committed_replacement.begin(),
                               committed_replacement.end(),
                               replacement_bytes.begin()),
                "SaveSlot did not retain the live replacement MAPZ snapshot");

        const auto matrix_root = temporary / "five-slot-matrix";
        for (std::uint8_t number = 1; number <= 5; ++number) {
            auto matrix_slot = swd2::SaveSlot::open(
                game_root, matrix_root, number);
            auto matrix_state = matrix_slot.state();
            matrix_state.set_u16(
                0x104, static_cast<std::uint16_t>(10'000U + number));
            auto matrix_map = matrix_slot.map_database();
            matrix_map->mutate_area_word(
                14, 3, 76, static_cast<std::uint16_t>(0x100U + number),
                false);
            matrix_map->mutate_area_word(338, 10, 2, 19, true);
            matrix_map->mutate_area_word(338, 10, 4, 19, true);
            matrix_slot.save(matrix_state);
        }
        for (std::uint8_t number = 1; number <= 5; ++number) {
            const auto matrix_slot = swd2::SaveSlot::open(
                game_root, matrix_root, number);
            require(matrix_slot.slot() == number &&
                        matrix_slot.state().u16(0x104) == 10'000U + number &&
                        matrix_slot.map_database()
                                ->location_at_directory_offset(14)
                                .area.entity_fields[3][38] == 0x100U + number &&
                        matrix_slot.map_database()
                                ->location_at_directory_offset(338)
                                .area.graphics_path == "\\SWD2\\T4\\AREA7.RAP",
                    "five-slot SAVE/MAPZ matrix did not round-trip independently");
        }

        const auto source = swd2::SaveSlot::open(game_root, matrix_root, 1);
        for (std::uint8_t number = 1; number <= 5; ++number) {
            swd2::SaveSlot::save_as(
                matrix_root, number, source.state(), *source.map_database());
        }
        for (std::uint8_t number = 1; number <= 5; ++number) {
            const auto copied_slot = swd2::SaveSlot::open(
                game_root, matrix_root, number);
            require(copied_slot.state().u16(0x104) == 10'001U &&
                        copied_slot.map_database()
                                ->location_at_directory_offset(14)
                                .area.entity_fields[3][38] == 0x101U &&
                        copied_slot.map_database()
                                ->location_at_directory_offset(338)
                                .area.layout_path == "\\SWD2\\T4\\AREA7.RAP",
                    "save-as did not copy one SAVE/MAPZ pair across all five slots");
        }

        const auto partial_root = temporary / "partial-pair";
        std::filesystem::create_directories(partial_root);
        std::filesystem::copy_file(
            game_root / "SAVE.DA1", partial_root / "SAVE.DA1");
        bool partial_rejected = false;
        try {
            static_cast<void>(swd2::SaveSlot::open(
                game_root, partial_root, 1));
        } catch (const std::runtime_error&) {
            partial_rejected = true;
        }
        require(partial_rejected,
                "portable save slot accepted a lone SAVE without its MAPZ pair");

        bool rejected = false;
        try {
            static_cast<void>(swd2::SaveSlot::open(game_root, temporary, 0));
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        require(rejected, "portable save slot accepted a number outside 1..5");
    } catch (...) {
        std::filesystem::remove_all(temporary);
        throw;
    }
    std::filesystem::remove_all(temporary);
}

void test_planar_sprite_set(const std::filesystem::path& game_root) {
    const auto animation = swd2::PlanarSpriteSet::load(game_root / "DE" / "DE001");
    require(animation.frame_count() == 32, "unexpected DE001 frame count");
    const auto& frame = animation.frame(0);
    require(frame.width == 320 && frame.height == 200 &&
                frame.pixels.size() == 64'000,
            "DE001 planar frame dimensions were not decoded");
    require(std::count(frame.pixels.begin(), frame.pixels.end(), 0xfe) == 202 * 64,
            "DE001 transparent pixel lookup was not decoded");
    std::uint64_t frame_hash = 1469598103934665603ULL;
    for (const auto pixel : frame.pixels) {
        frame_hash ^= pixel;
        frame_hash *= 1099511628211ULL;
    }
    require(frame_hash == 12960686047150932659ULL,
            "DE001 8x8 four-plane tile expansion differs from the DOS layout");
    const auto shared_dictionary = swd2::PlanarSpriteSet::load(
        game_root / "DE" / "DE001", game_root / "DE" / "DE002");
    require(shared_dictionary.frame_count() != 0,
            "RAP-only DE002 did not reuse the preceding DE001 dictionary");
}

void test_battle_database(const std::filesystem::path& game_root) {
    const auto battles = swd2::BattleDatabase::load(game_root / "ORC.EXE");
    require(battles.directory_entry_count() == 626 &&
                battles.occupied_directory_entry_count() == 610 &&
                battles.encounter_count() == 550,
            "unexpected ORC.EXE directory dimensions");
    require(battles.growth_tables()[0][0].fields[0] == 21 &&
                battles.growth_tables()[3][59].fields[0] == 1125 &&
                battles.growth_tables()[3][59].fields[8] == 0,
            "ORC.EXE 60-row party growth tables were not decoded");
    const auto encounter = battles.encounter_at_directory_offset(392);
    require(encounter && encounter->get().background_path == "C:\\SWD2\\BA\\BA14.RSK" &&
                encounter->get().monster_definition_ids[0] == 500 &&
                encounter->get().definition_slots == std::vector<std::uint16_t>{0} &&
                encounter->get().horizontal_positions == std::vector<std::uint16_t>{33},
            "ORC.EXE scripted encounter 392 was not decoded");
    require(!battles.encounter_at_directory_offset(68),
            "ORC.EXE empty directory slot was treated as an encounter");
    require(std::count_if(
                battles.encounters().begin(), battles.encounters().end(),
                [](const swd2::BattleEncounter& encounter) {
                    return encounter.prompt_order ==
                           swd2::BattlePromptOrder::yes_no;
                }) == 13 &&
                std::count_if(
                    battles.encounters().begin(), battles.encounters().end(),
                    [](const swd2::BattleEncounter& encounter) {
                        return encounter.prompt_order ==
                               swd2::BattlePromptOrder::no_yes;
                    }) == 7,
            "ORC.EXE trailing YN/NY pre-battle prompt controls were not decoded");
    require(swd2::select_random_encounter(0xe00a, 106, 1, 0) == 100 &&
                swd2::select_random_encounter(0x000a, 10, 100, 7) == 178,
            "FIG random-encounter region lookup was not reconstructed");

    const auto items = swd2::ScriptArchive::load(game_root / "ITEM.EXE");
    const auto monster = swd2::MonsterDefinition::parse(500, items.entry(502));
    require(monster.sprite_number == 500 && monster.vertical_position == 69 &&
                monster.resistance_flags == std::array<std::uint8_t, 5>{0, 0, 0, 1, 1} &&
                monster.ai_type == 0 && monster.hit_points == 120 && monster.level == 22 &&
                monster.status_strength == 1 && monster.primary_ability_chance == 1 &&
                monster.generic_ability == 10 && monster.initiative_range == 4 &&
                monster.physical_attack == 90 && monster.speed == 3 &&
                monster.ability_points == 55 &&
                monster.experience_reward == 132 && monster.money_reward == 20 &&
                monster.evasion == 1 && monster.physical_defense == 55,
            "ITEM.EXE monster combat fields differ from FIG 1000:3396");
    const auto battle_item = swd2::BattleItemDefinition::parse(51, items.entry(53));
    require(battle_item.type == 0x0e && battle_item.use_flags == 0x2f &&
                battle_item.target_flags == 0x10 && battle_item.effect_code == 0x1c &&
                battle_item.consumed_on_use() && !battle_item.targets_monster(),
            "ITEM.EXE unaligned FIG battle-item fields were not decoded");
    const auto composite_item =
        swd2::BattleItemDefinition::parse(248, items.entry(250));
    require(composite_item.effect_code == 0x6b &&
                composite_item.first_composite_effect == 0x08 &&
                composite_item.second_composite_effect == 0x0f &&
                composite_item.targets_party() &&
                !composite_item.targets_monster(),
            "ITEM.EXE direct composite +9/+a fields were not decoded");

    const auto abilities = swd2::BattleAbilityDatabase::load(game_root / "FIG.EXE");
    require(abilities.item_category_label(0) ==
                std::array<std::uint8_t, 4>{0xa4, 0xa3, 0xa9, 0xfa} &&
                abilities.item_category_label(41) ==
                std::array<std::uint8_t, 4>{0xc0, 0x73, 0xb1, 0xda},
            "FIG embedded 42-entry item-category table changed");
    const std::array<std::array<std::uint8_t, 4>, 5>
        expected_resource_labels = {{
            {{0xa5, 0x50, 0xb3, 0x4e}},
            {{0xc5, 0xe9, 0xa4, 0x4f}},
            {{0xc5, 0xe9, 0xa4, 0x4f}},
            {{0xa5, 0x50, 0xb3, 0x4e}},
            {{0xc3, 0xc4, 0xa7, 0xf7}},
        }};
    for (std::size_t resource = 0;
         resource < expected_resource_labels.size(); ++resource) {
        const auto actual = abilities.ability_resource_label(resource + 1U);
        require(actual.size() == expected_resource_labels[resource].size() &&
                    std::equal(actual.begin(), actual.end(),
                               expected_resource_labels[resource].begin()),
                "FIG DATA:2ce1 ability-resource label changed");
    }
    require(abilities.ability_resource_label(0).empty() &&
                abilities.ability_resource_label(6).empty(),
            "FIG ability-resource label bounds changed");
    std::uint64_t notice_hash = 1469598103934665603ULL;
    std::array<std::size_t, 6> notice_sizes{};
    for (std::size_t index = 0; index < notice_sizes.size(); ++index) {
        const auto text = abilities.notice_text(
            static_cast<swd2::BattleCommandNotice>(index + 1U));
        notice_sizes[index] = text.size();
        for (const auto byte : text) {
            notice_hash ^= byte;
            notice_hash *= 1099511628211ULL;
        }
    }
    require(notice_sizes == std::array<std::size_t, 6>{24, 14, 14, 24, 24, 20} &&
                notice_hash == 5687326376259025163ULL &&
                abilities.notice_text(swd2::BattleCommandNotice::none).empty(),
            "FIG 184f/4043 modal Big5 notice strings changed");
    std::uint64_t player_status_hash = 1469598103934665603ULL;
    for (const auto effect : {0x63U, 0x66U, 0x67U, 0x68U, 0x69U}) {
        const auto text = abilities.player_status_text(effect);
        require(text.size() == 8U,
                "FIG 57d6 player-status label has the wrong length");
        for (const auto byte : text) {
            player_status_hash ^= byte;
            player_status_hash *= 1099511628211ULL;
        }
    }
    require(player_status_hash == 1336891300955925920ULL &&
                abilities.player_status_text(0x62).empty(),
            "FIG DATA:2d8d..2db5 player-status labels changed");
    std::uint64_t player_removed_hash = 1469598103934665603ULL;
    for (std::size_t slot = 0; slot < 6U; ++slot) {
        const auto text = abilities.player_removed_buff_text(slot);
        require(text.size() == 8U,
                "FIG 292c removed-player-buff label has the wrong length");
        for (const auto byte : text) {
            player_removed_hash ^= byte;
            player_removed_hash *= 1099511628211ULL;
        }
    }
    std::uint64_t monster_removed_hash = 1469598103934665603ULL;
    for (std::size_t slot = 0; slot < 3U; ++slot) {
        const auto text = abilities.monster_removed_buff_text(slot);
        require(text.size() == 8U,
                "FIG 55e4 removed-monster-buff label has the wrong length");
        for (const auto byte : text) {
            monster_removed_hash ^= byte;
            monster_removed_hash *= 1099511628211ULL;
        }
    }
    require(player_removed_hash == 9876306540757890984ULL &&
                monster_removed_hash == 11430098229595941862ULL &&
                abilities.player_removed_buff_text(6).empty() &&
                abilities.monster_removed_buff_text(3).empty(),
            "FIG 292c/55e4 dispel feedback strings changed");
    const std::array<std::uint8_t, 6> ward_text = {
        0xc5, 0x40, 0xa1, 0x40, 0xc5, 0x5d};
    const std::array<std::uint8_t, 6> immunity_text = {
        0xa5, 0xa2, 0xa1, 0x40, 0xae, 0xc4};
    require(abilities.magic_ward_text().size() == ward_text.size() &&
                std::equal(abilities.magic_ward_text().begin(),
                           abilities.magic_ward_text().end(), ward_text.begin()) &&
                abilities.monster_immunity_text().size() == immunity_text.size() &&
                std::equal(abilities.monster_immunity_text().begin(),
                           abilities.monster_immunity_text().end(),
                           immunity_text.begin()),
            "FIG 2332/59a1 ward/immunity feedback strings changed");
    std::uint64_t recovered_status_hash = 1469598103934665603ULL;
    for (std::size_t slot = 0; slot < 4U; ++slot) {
        const auto text = abilities.recovered_player_status_text(slot);
        require(text.size() == 8U,
                "FIG 0da7 recovered-player-status label has the wrong length");
        for (const auto byte : text) {
            recovered_status_hash ^= byte;
            recovered_status_hash *= 1099511628211ULL;
        }
    }
    require(recovered_status_hash == 4834106636869358567ULL &&
                abilities.recovered_player_status_text(4).empty(),
            "FIG DATA:2dbf..2ddd recovered-status labels changed");
    const std::array<std::uint8_t, 6> death_reaction_text = {
        0xa5, 0x69, 0xb4, 0x63, 0xa1, 0x49};
    require(abilities.death_reaction_text().size() == death_reaction_text.size() &&
                std::equal(abilities.death_reaction_text().begin(),
                           abilities.death_reaction_text().end(),
                           death_reaction_text.begin()),
            "FIG DATA:2d73 death-reaction label changed");
    const std::array<std::uint8_t, 8> monster_escape_failed_text = {
        0xb0, 0x6b, 0xa8, 0xab, 0xa5, 0xa2, 0xb1, 0xd1};
    const std::array<std::uint8_t, 8> monster_escape_text = {
        0xb0, 0x6b, 0xa1, 0x40, 0xa1, 0x40, 0xa8, 0xab};
    require(abilities.monster_escape_text(false).size() ==
                    monster_escape_failed_text.size() &&
                std::equal(abilities.monster_escape_text(false).begin(),
                           abilities.monster_escape_text(false).end(),
                           monster_escape_failed_text.begin()) &&
                abilities.monster_escape_text(true).size() ==
                    monster_escape_text.size() &&
                std::equal(abilities.monster_escape_text(true).begin(),
                           abilities.monster_escape_text(true).end(),
                           monster_escape_text.begin()),
            "FIG 212a/214c monster-escape labels changed");
    const std::array<std::uint8_t, 6> monster_attack_text = {
        0xa7, 0xf0, 0xa1, 0x40, 0xc0, 0xbb};
    const std::array<std::uint8_t, 6> physical_failure_text = {
        0xa5, 0xa2, 0xa1, 0x40, 0xb1, 0xd1};
    const std::array<std::uint8_t, 6> evasion_text = {
        0xb0, 0x7b, 0xa1, 0x40, 0xb8, 0xfa};
    require(abilities.monster_attack_text().size() == monster_attack_text.size() &&
                std::equal(abilities.monster_attack_text().begin(),
                           abilities.monster_attack_text().end(),
                           monster_attack_text.begin()) &&
                abilities.physical_failure_text().size() ==
                    physical_failure_text.size() &&
                std::equal(abilities.physical_failure_text().begin(),
                           abilities.physical_failure_text().end(),
                           physical_failure_text.begin()) &&
                abilities.evasion_text().size() == evasion_text.size() &&
                std::equal(abilities.evasion_text().begin(),
                           abilities.evasion_text().end(), evasion_text.begin()),
            "FIG 296a physical action/failure/evasion labels changed");
    const std::array<std::uint8_t, 6> summoned_flee_text = {
        0xb0, 0x6b, 0xa1, 0x40, 0xa8, 0xab};
    const std::array<std::uint8_t, 6> summoned_ability_text = {
        0xa9, 0x5f, 0xa1, 0x40, 0xb3, 0x4e};
    const std::array<std::uint8_t, 6> capture_action_text = {
        0xb7, 0xd2, 0xa7, 0xaf, 0xb3, 0xfd};
    const std::array<std::uint8_t, 22> missing_medium_text = {
        0xa8, 0x53, 0xa6, 0xb3, 0xb4, 0x43, 0xa4, 0xb6,
        0xa1, 0x49, 0xb5, 0x4c, 0xaa, 0x6b, 0xa5, 0xce,
        0xa6, 0xb9, 0xb3, 0x4e, 0xa1, 0x49,
    };
    const std::array<std::uint8_t, 14> summon_replacement_text = {
        0xad, 0x6e, 0xb4, 0xc0, 0xb4, 0xab, 0xa8,
        0xba, 0xa4, 0x40, 0xb0, 0xa6, 0xa1, 0x48,
    };
    require(abilities.summoned_ally_flee_text().size() ==
                    summoned_flee_text.size() &&
                std::equal(abilities.summoned_ally_flee_text().begin(),
                           abilities.summoned_ally_flee_text().end(),
                           summoned_flee_text.begin()) &&
                abilities.summoned_ally_ability_text().size() ==
                    summoned_ability_text.size() &&
                std::equal(abilities.summoned_ally_ability_text().begin(),
                           abilities.summoned_ally_ability_text().end(),
                           summoned_ability_text.begin()) &&
                abilities.capture_action_text().size() ==
                    capture_action_text.size() &&
                std::equal(abilities.capture_action_text().begin(),
                           abilities.capture_action_text().end(),
                           capture_action_text.begin()) &&
                abilities.missing_medium_text().size() ==
                    missing_medium_text.size() &&
                std::equal(abilities.missing_medium_text().begin(),
                           abilities.missing_medium_text().end(),
                           missing_medium_text.begin()) &&
                abilities.summon_replacement_text().size() ==
                    summon_replacement_text.size() &&
                std::equal(abilities.summon_replacement_text().begin(),
                           abilities.summon_replacement_text().end(),
                           summon_replacement_text.begin()),
            "FIG 10fc/58fa/5d24 summoned/capture/mediator labels changed");
    const std::array<std::span<const std::uint8_t>, 5> settlement_texts = {
        abilities.victory_text(), abilities.level_up_title_text(),
        abilities.defeat_text(), abilities.encounter_capture_text(),
        abilities.level_up_stats_text(),
    };
    std::array<std::size_t, 5> settlement_sizes{};
    std::uint64_t settlement_hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < settlement_texts.size(); ++index) {
        settlement_sizes[index] = settlement_texts[index].size();
        for (const auto byte : settlement_texts[index]) {
            settlement_hash ^= byte;
            settlement_hash *= 1099511628211ULL;
        }
    }
    require(settlement_sizes ==
                    std::array<std::size_t, 5>{34, 14, 10, 16, 112} &&
                settlement_hash == 9341449023210361313ULL,
            "FIG 04bf/0553/0643/07d2 settlement labels changed");
    const auto composite_eighty =
        swd2::BattleCompositeEffect::parse(80, items);
    const auto composite_ninety =
        swd2::BattleCompositeEffect::parse(90, items);
    require(composite_eighty.first_effect == 0x60 &&
                composite_eighty.second_effect == 0x46 &&
                composite_ninety.first_effect == 0x38 &&
                composite_ninety.second_effect == 0x3a,
            "FIG effect-6b ITEM indirection was not decoded");
    require(abilities.abilities().size() == 151 &&
                abilities.ability(1).target_flags == 0xa100 &&
                abilities.ability(1).effect_code == 0x4e &&
                abilities.ability(1).cost == 30 &&
                abilities.ability(1).base_power == 90 &&
                abilities.ability(36).target_flags == 0xa482 &&
                abilities.ability(36).effect_code == 0x36 &&
                abilities.ability(114).effect_code == 0x5e &&
                abilities.ability(128).effect_code == 0x48 &&
                abilities.ability(148).effect_code == 0x65 &&
                abilities.ability(150).base_power == 2000 &&
                abilities.item_name(51) ==
                    std::array<std::uint8_t, 12>{
                        0xa9, 0xdb, 0xbb, 0xee, 0xba, 0x58,
                        0xa1, 0x40, 0xa1, 0x40, 0xa1, 0x40} &&
                abilities.item_name(500) ==
                    std::array<std::uint8_t, 12>{
                        0xa5, 0xdb, 0xb7, 0xe0, 0xba, 0xeb,
                        0xa1, 0x40, 0xa1, 0x40, 0xa1, 0x40},
            "FIG.EXE ability/item-name tables were not decoded");
    require(swd2::fig_primary_effect_resource(0x32) == 43 &&
                swd2::fig_primary_effect_resource(0x38) == 34 &&
                swd2::fig_primary_effect_resource(0x42) == 220 &&
                swd2::fig_primary_effect_resource(0x56) == 300 &&
                swd2::fig_primary_effect_resource(0x65) == 345 &&
                !swd2::fig_primary_effect_resource(0x47) &&
                !swd2::fig_primary_effect_resource(0x69),
            "FIG DS:2bbd visual dispatcher did not select exact SP/ST roots");
    const auto effect_32 = swd2::fig_effect_resource_sequence(0x32);
    const auto effect_33 = swd2::fig_effect_resource_sequence(0x33);
    const auto effect_35 = swd2::fig_effect_resource_sequence(0x35);
    const auto effect_65 = swd2::fig_effect_resource_sequence(0x65);
    require(effect_32.size() == 40 && effect_32.front() == 43 &&
                effect_32[7] == 50 && effect_32[8] == 43 &&
                effect_32.back() == 50 &&
                effect_33.size() == 2 && effect_33[0] == 51 &&
                effect_33[1] == 551 &&
                effect_35.size() == 48 && effect_35.front() == 0 &&
                effect_35[23] == 23 && effect_35[24] == 0 &&
                effect_35.back() == 23 &&
                effect_65.size() == 64 && effect_65.front() == 345 &&
                effect_65.back() == 408,
            "FIG effect handlers did not preserve their full archive load order");
    require(swd2::fig_effect_voice_resource(0x00) == 1 &&
                swd2::fig_effect_voice_resource(0x31) == 49 &&
                swd2::fig_effect_voice_resource(0x3b) == 49 &&
                swd2::fig_effect_voice_resource(0x3d) == 61 &&
                swd2::fig_effect_voice_resource(0x47) == 0x47 &&
                swd2::fig_effect_voice_resource(0x63) == 72 &&
                swd2::fig_effect_voice_resource(0x65) == 101 &&
                !swd2::fig_effect_voice_resource(0x6a),
            "FIG 5b37 SP###.VOC dispatcher mapping was not reconstructed");
    require(!swd2::fig_monster_ability_voice_resource(0x33, 0x32) &&
                !swd2::fig_monster_ability_voice_resource(0x53, 0x65) &&
                swd2::fig_monster_ability_voice_resource(0x56, 0x40) == 0x48 &&
                swd2::fig_monster_ability_voice_resource(10, 0x20) == 1 &&
                swd2::fig_monster_ability_voice_resource(10, 0x48) == 0x48,
            "FIG 262f enemy ability voice exceptions changed");

    swd2::BattleSessionEvent composite_first;
    composite_first.kind = swd2::BattleEventKind::player_ability;
    composite_first.source = 1;
    composite_first.ability_id = 90;
    composite_first.effect_code = 0x38;
    auto composite_same_target = composite_first;
    composite_same_target.target = 1;
    auto composite_second = composite_first;
    composite_second.effect_code = 0x3a;
    swd2::BattleSessionEvent composite_missing;
    composite_missing.kind = swd2::BattleEventKind::missing_medium;
    composite_missing.source = 1;
    composite_missing.ability_id = 90;
    composite_missing.effect_code = 0x40;
    require(swd2::fig_same_presented_action(
                composite_first, composite_same_target) &&
                swd2::fig_same_effect_phase(
                    composite_first, composite_same_target) &&
                swd2::fig_same_presented_action(
                    composite_first, composite_second) &&
                !swd2::fig_same_effect_phase(
                    composite_first, composite_second) &&
                swd2::fig_same_presented_action(
                    composite_second, composite_missing) &&
                !swd2::fig_same_effect_phase(
                    composite_second, composite_missing),
            "FIG 6b composite grouping repeated actor setup or lost a nested effect phase");

    swd2::BattleSessionEvent physical;
    physical.kind = swd2::BattleEventKind::player_attack;
    physical.damage = 12;
    physical.critical = true;
    physical.defeated = true;
    physical.target_is_monster = true;
    require(swd2::fig_non_effect_voice_cues(physical, 24) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 7,
                     swd2::FigVoiceTiming::after_first_pose},
                    {swd2::FigVoiceFile::sp, 4,
                     swd2::FigVoiceTiming::after_pose},
                }),
            "FIG player physical/critical voice sequence invented a monster-death SP015");
    physical.damage = 0;
    physical.defeated = false;
    require(swd2::fig_non_effect_voice_cues(physical, 24) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 7,
                     swd2::FigVoiceTiming::after_first_pose},
                    {swd2::FigVoiceFile::sp, 4,
                     swd2::FigVoiceTiming::after_pose},
                    {swd2::FigVoiceFile::k1, 0,
                     swd2::FigVoiceTiming::after_action},
                }),
            "FIG failed player physical attack voice ordering differs from 13e8/12d5");
    physical.evaded = true;
    require(swd2::fig_non_effect_voice_cues(physical, 24) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 7,
                     swd2::FigVoiceTiming::after_first_pose},
                    {swd2::FigVoiceFile::sp, 4,
                     swd2::FigVoiceTiming::after_pose},
                }),
            "FIG evaded player attack incorrectly selected K1.VOC");
    swd2::BattleSessionEvent monster_hit;
    monster_hit.kind = swd2::BattleEventKind::monster_attack;
    monster_hit.damage = 4;
    monster_hit.defeated = true;
    require(swd2::fig_non_effect_voice_cues(monster_hit) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 106,
                     swd2::FigVoiceTiming::before_action},
                    {swd2::FigVoiceFile::sp, 15,
                     swd2::FigVoiceTiming::after_action},
                }),
            "FIG monster physical/death voices did not select SP106/SP015");
    swd2::BattleSessionEvent ally_miss;
    ally_miss.kind = swd2::BattleEventKind::ally_attack;
    ally_miss.evaded = true;
    require(swd2::fig_non_effect_voice_cues(ally_miss) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 106,
                     swd2::FigVoiceTiming::before_action},
                }),
            "FIG captured ally did not start SP106 before a missed attack");
    swd2::BattleSessionEvent fled;
    fled.kind = swd2::BattleEventKind::monster_fled;
    require(swd2::fig_non_effect_voice_cues(fled) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sv3, 0,
                     swd2::FigVoiceTiming::before_action},
                }),
            "FIG flee event did not select SV3.VOC");
    swd2::BattleSessionEvent player_escape;
    player_escape.kind = swd2::BattleEventKind::player_escaped;
    require(swd2::fig_player_escape_poses() ==
                    std::array<std::size_t, 2>{0, 5} &&
                swd2::fig_player_ability_poses() ==
                    std::array<std::size_t, 2>{0, 4} &&
                swd2::fig_player_attack_pose_ticks() ==
                    std::array<std::uint16_t, 3>{3, 1, 1} &&
                swd2::fig_player_ability_pose_ticks() ==
                    std::array<std::uint16_t, 2>{3, 3} &&
                swd2::fig_player_attack_result_hold_ticks() == 5 &&
                swd2::fig_player_escape_failure_placement() ==
                    swd2::FigPlayerEscapeFailurePlacement{
                        0x1a, 0x4b, 4, 0x1c, 0x54} &&
                swd2::fig_non_effect_voice_cues(player_escape) ==
                    std::vector<swd2::FigVoiceCue>({
                        {swd2::FigVoiceFile::sv3, 0,
                         swd2::FigVoiceTiming::after_pose},
                    }),
            "FIG 09b8 normal escape poses/card/SV3 ordering differs");
    require(
        swd2::fig_support_presentation(0x01) ==
                swd2::FigSupportPresentation::single_target &&
            swd2::fig_support_presentation(0x0c) ==
                swd2::FigSupportPresentation::all_targets &&
            swd2::fig_support_presentation(0x12) ==
                swd2::FigSupportPresentation::all_targets &&
            swd2::fig_support_presentation(0x22) ==
                swd2::FigSupportPresentation::commit_without_redraw &&
            swd2::fig_support_presentation(0x27) ==
                swd2::FigSupportPresentation::commit_without_redraw &&
            swd2::fig_support_presentation(0x28) ==
                swd2::FigSupportPresentation::none &&
            swd2::fig_support_presentation(0x29) ==
                swd2::FigSupportPresentation::none &&
            swd2::fig_selector_is_immediate_return(0x00) &&
            swd2::fig_selector_is_immediate_return(0x28) &&
            swd2::fig_selector_is_immediate_return(0x29) &&
            !swd2::fig_selector_is_immediate_return(0x27) &&
            !swd2::fig_selector_is_immediate_return(0x2a) &&
            swd2::fig_effect_tail_hold_ticks(0x62) == 4 &&
            swd2::fig_monster_status_damage_tail_hold_ticks() == 4 &&
            swd2::fig_monster_ability_flash_color(0) == 0x8d &&
            swd2::fig_monster_ability_flash_color(1) == 0x5c &&
            swd2::fig_monster_ability_flash_color(2) == 0x81 &&
            swd2::fig_monster_ability_flash_color(3) == 0xaa &&
            swd2::fig_monster_ability_flash_color(4) == 0x7c &&
            swd2::fig_monster_ability_flash_color(5) == 0x81 &&
            swd2::fig_monster_ability_flash_steps() == 8 &&
            swd2::fig_monster_attack_shake_steps() == 8 &&
            swd2::fig_monster_attack_shake_scanlines() == 3 &&
            swd2::fig_monster_turn_tail_ticks() ==
                std::array<std::uint16_t, 2>{5, 3} &&
            swd2::fig_all_target_slot_span(0, std::size_t{2}, 4) == 2 &&
            swd2::fig_all_target_slot_span(2, std::nullopt, 4) == 2 &&
            swd2::fig_all_target_slot_span(0, std::nullopt, 1) == 1 &&
            swd2::fig_effect_tail_hold_ticks(0x61) == 0 &&
            swd2::fig_effect_leaves_player_status_card(0x63) &&
            swd2::fig_effect_leaves_player_status_card(0x66) &&
            swd2::fig_effect_leaves_player_status_card(0x69) &&
            !swd2::fig_effect_leaves_player_status_card(0x62) &&
            !swd2::fig_effect_leaves_player_status_card(0x65) &&
            swd2::fig_support_presentation(0x2a) ==
                swd2::FigSupportPresentation::single_target &&
            swd2::fig_support_pre_commit_ticks(
                swd2::FigSupportPresentation::single_target) == 3 &&
            swd2::fig_support_pre_commit_ticks(
                swd2::FigSupportPresentation::all_targets) == 9 &&
            swd2::fig_support_post_commit_ticks(
                swd2::FigSupportPresentation::single_target) == 9 &&
            swd2::fig_support_post_commit_ticks(
                swd2::FigSupportPresentation::commit_without_redraw) == 0,
        "FIG 5841/58a9 support presentation classes or waits differ");
    player_escape.ability_id = 7;
    require(swd2::fig_non_effect_voice_cues(player_escape).empty(),
            "FIG effect-47 escape invented the normal command's SV3 sample");
    swd2::BattleSessionEvent captured;
    captured.kind = swd2::BattleEventKind::monster_captured;
    captured.defeated = true;
    require(swd2::fig_non_effect_voice_cues(captured) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 16,
                     swd2::FigVoiceTiming::before_action},
                    {swd2::FigVoiceFile::sp, 15,
                     swd2::FigVoiceTiming::after_action},
                }),
            "FIG capture did not preserve SP016 then SP015 ordering");
    captured.kind = swd2::BattleEventKind::capture_failed;
    captured.defeated = false;
    require(swd2::fig_non_effect_voice_cues(captured) ==
                std::vector<swd2::FigVoiceCue>({
                    {swd2::FigVoiceFile::sp, 16,
                     swd2::FigVoiceTiming::before_action},
                }),
            "FIG failed capture did not preserve its SP016 cue");
}

void test_mon_database(const std::filesystem::path& game_root) {
    const auto mon = swd2::MonDatabase::load(game_root / "MON.EXE");
    require(mon.interaction(0, 0) == 38 && mon.interaction(0, 16) == 36 &&
                mon.interaction(4, 7) == mon.interaction(7, 4),
            "MON.EXE 17x17 interaction matrix was not decoded");
    require(mon.value_tables().size() == 15 && mon.value_entry_count() == 161 &&
                mon.value_tables()[0].front().definition_id == 318 &&
                mon.value_tables()[14].back().definition_id == 481,
            "MON.EXE terminated value tables were not decoded");
}

void test_battle_rules() {
    const auto sequence = [](std::vector<std::uint16_t> values) {
        return [values = std::move(values), cursor = std::size_t{}]
               (std::uint16_t modulus) mutable {
            if (cursor >= values.size() || values[cursor] >= modulus) {
                throw std::runtime_error("invalid deterministic battle-random sequence");
            }
            return values[cursor++];
        };
    };

    const std::array<swd2::InitiativeStats, 4> actors = {
        swd2::InitiativeStats{5, 10}, swd2::InitiativeStats{5, 20},
        swd2::InitiativeStats{5, 15}, swd2::InitiativeStats{5, 1},
    };
    const std::array<swd2::InitiativeStats, 2> enemies = {
        swd2::InitiativeStats{5, 25}, swd2::InitiativeStats{5, 0},
    };
    const auto turns = swd2::roll_turn_order(
        actors, enemies, sequence({0, 0, 0, 0, 0, 4}));
    require(turns.size() == 6 && turns[0].monster && turns[0].index == 0 &&
                !turns[1].monster && turns[1].index == 1 &&
                !turns[2].monster && turns[2].index == 2 &&
                !turns[3].monster && turns[3].index == 0 &&
                turns[4].monster && turns[4].index == 1 &&
                !turns[5].monster && turns[5].index == 3,
            "FIG initiative sorting/order was not reproduced");

    const std::array<swd2::InitiativeStats, 4> crowded_actors = {
        swd2::InitiativeStats{1, 10}, swd2::InitiativeStats{1, 20},
        swd2::InitiativeStats{1, 30}, swd2::InitiativeStats{1, 40},
    };
    const std::array<swd2::InitiativeStats, 7> crowded_runtime = {
        swd2::InitiativeStats{1, 50}, swd2::InitiativeStats{1, 60},
        swd2::InitiativeStats{1, 70}, swd2::InitiativeStats{1, 80},
        swd2::InitiativeStats{1, 90}, swd2::InitiativeStats{1, 100},
        swd2::InitiativeStats{1, 110},
    };
    const auto crowded_turns = swd2::roll_turn_order(
        crowded_actors, crowded_runtime,
        sequence(std::vector<std::uint16_t>(11, 0)));
    require(crowded_turns.size() == 11 &&
                std::none_of(crowded_turns.begin(), crowded_turns.end(),
                             [](const swd2::BattleTurn& turn) {
                                 return turn.monster && turn.index == 6;
                             }) &&
                std::count_if(crowded_turns.begin(), crowded_turns.end(),
                              [](const swd2::BattleTurn& turn) {
                                  return !turn.monster && turn.index == 0;
                              }) == 2,
            "FIG ten-slot initiative selector quirk was not preserved");

    swd2::MonsterTargetState monster{100, 30, 1, false, true};
    std::uint16_t critical_countdown = 5;
    const auto player_result = swd2::player_basic_attack(
        {10, 40}, monster, critical_countdown, sequence({3, 0, 1}));
    require(player_result.hit && player_result.critical && player_result.damage == 26 &&
                !player_result.defeated && monster.hit_points == 74 &&
                critical_countdown == 20,
            "FIG player physical attack/critical formula was not reproduced");

    swd2::MonsterTargetState dodger{100, 10, 2, false, true};
    critical_countdown = 7;
    const auto evaded = swd2::player_basic_attack(
        {8, 30}, dodger, critical_countdown, sequence({1, 1, 0}));
    require(!evaded.hit && evaded.evaded && dodger.hit_points == 100 &&
                critical_countdown == 6,
            "FIG monster evasion path was not reproduced");

    swd2::PlayerTargetState actor{59, 30, 1, 0, false, false, true};
    const auto enemy_result = swd2::monster_basic_attack(
        {22, 90, 4}, actor, sequence({2, 1, 4}));
    require(enemy_result.hit && enemy_result.damage == 62 && enemy_result.defeated &&
                enemy_result.inflicted_status && actor.hit_points == 0 &&
                (actor.status_bits & 0x2200U) == 0x2200U,
            "FIG enemy physical attack/death/status formula was not reproduced");

    swd2::MonsterTargetState ally_target{100, 30, 9, true, true};
    const auto ally_attack = swd2::summoned_ally_basic_attack(
        77, ally_target, sequence({1}));
    require(ally_attack.hit && !ally_attack.evaded &&
                ally_attack.damage == 47 && ally_target.hit_points == 53,
            "FIG captured-ally fixed attack/evasion formula was not reproduced");

    swd2::PlayerTargetState armored{10, 30, 0, 0, false, false, false};
    const auto retry = swd2::monster_basic_attack(
        {2, 10, 1}, armored, sequence({0, 2}));
    require(retry.hit && retry.damage == 2 && armored.hit_points == 8,
            "FIG enemy low-attack retry formula was not reproduced");
}

void test_battle_random(const std::filesystem::path& game_root) {
    auto random = swd2::FigBattleRandom::load(game_root / "FIG.EXE", 0x1000);
    require(random.draw(6) == 4 && random.draw(20) == 16 &&
                random.draw(11) == 7 && random.draw(10) == 3 &&
                random.draw(4) == 2 && random.draw(3) == 0 &&
                random.cursor() == 0x100c,
            "FIG code-window random sequence differs from 1000:2b41");

    auto wrapping = swd2::FigBattleRandom::load(game_root / "FIG.EXE", 0x1ffe);
    static_cast<void>(wrapping.draw(7));
    require(wrapping.cursor() == 0x1000 && wrapping.draw(6) == 4 &&
                wrapping.cursor() == 0x1002,
            "FIG random cursor did not wrap from 1ffe to 1000");
    auto boundary = swd2::FigBattleRandom::load(game_root / "FIG.EXE", 0x2000);
    require(boundary.draw(7) == 6U && boundary.cursor() == 0x1000U,
            "FIG rejected RPG's boundary 2000h random cursor before its 228ah read");
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    wrapping.store(state);
    require(state.u16(0x49c) == 0x1002,
            "FIG random cursor did not persist in shared state +49c");
}

void test_battle_party(const std::filesystem::path& game_root) {
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    auto member = swd2::BattlePartyMember::load(state, 0);
    require(member.party_index == 0 && member.identity == 0 &&
                member.status_bits == 0 && member.physical_attack == 39 &&
                member.physical_defense == 30 && member.hit_points == 59 &&
                member.maximum_hit_points == 59 && member.level == 9 &&
                member.initiative_range == 14 && member.secondary_points == 52 &&
                member.maximum_secondary_points == 52 && member.strength == 30 &&
                member.wisdom == 60 && member.base_reaction == 22 &&
                member.ability_points == 36 &&
                member.maximum_ability_points == 36 && member.speed == 26 &&
                member.evasion == 1 && member.abilities[0] == 76 &&
                member.abilities[1] == 70 && member.abilities[2] == 0 &&
                member.right_hand_item == 122 &&
                member.left_hand_item == 122 &&
                member.single_weapon_animation,
            "SAVE actor zero was not decoded as a typed FIG battle record");
    require(swd2::fig_fighter_base_frame(0, 0, 24) == 0 &&
                swd2::fig_fighter_base_frame(12, 1, 24) == 6 &&
                swd2::fig_fighter_base_frame(24, 2, 24) == 12 &&
                swd2::fig_fighter_base_frame(36, 3, 24) == 18 &&
                swd2::fig_fighter_base_frame(0xffff, 2, 24) == 12,
            "FIG FMAN identity>>1 six-frame grouping was not reproduced");
    const auto normal_pose = swd2::fig_fighter_placement(12, 1, 2, 24);
    const auto critical_pose = swd2::fig_fighter_placement(36, 3, 3, 24);
    require(normal_pose.frame == 8 && normal_pose.left == 108 &&
                normal_pose.top == 157 && critical_pose.frame == 21 &&
                critical_pose.left == 256 && critical_pose.top == 154,
            "FIG 137a FMAN pose offset tables/Mode-X conversion differ");
    require(swd2::fig_weapon_animations(member) ==
                    std::vector<swd2::FigWeaponAnimation>{{122, false}} &&
                swd2::fig_weapon_placement(160, 80) ==
                    swd2::FigWeaponPlacement{144, 30},
            "FIG +2c single-weapon 14b1 path/anchor differs");
    require(swd2::fig_page_wipe_scanline_ends() ==
                std::array<int, 4>{50, 100, 150, 200},
            "FIG 3c44 weapon wipe no longer copies four 50-line chunks");
    auto dual_weapon = member;
    dual_weapon.single_weapon_animation = false;
    require(swd2::fig_weapon_animations(dual_weapon) ==
                    std::vector<swd2::FigWeaponAnimation>{
                        {122, false}, {122, true}},
            "FIG 152a/155c dual-weapon order/mirroring differs");
    dual_weapon.right_hand_item = 0;
    dual_weapon.left_hand_item = 125;
    require(swd2::fig_weapon_animations(dual_weapon) ==
                    std::vector<swd2::FigWeaponAnimation>{{125, true}},
            "FIG left-only weapon did not select mirrored 155c");
    dual_weapon.left_hand_item = 0;
    require(swd2::fig_weapon_animations(dual_weapon) ==
                    std::vector<swd2::FigWeaponAnimation>{{0, false}},
            "FIG unarmed attack omitted the shipped SW000 animation");
    require(member.living() && member.initiative().random_range == 14 &&
                member.initiative().base == 26 &&
                member.attack_stats().physical_attack == 39 &&
                member.physical_target().hit_points == 59 &&
                member.ability_target().maximum_hit_points == 59,
            "typed FIG actor conversions differ from battle rule structures");

    member.hit_points = 10;
    member.status_bits = 0x0200;
    member.physical_attack = 99;
    member.ability_points = 7;
    member.maximum_secondary_points = 44;
    member.strength = 45;
    member.wisdom = 46;
    member.base_reaction = 47;
    member.speed = 8;
    member.evasion = 9;
    member.abilities[2] = 5;
    member.store(state);
    constexpr std::size_t base = 0x106;
    require(state.u16(base + 0x2d) == 10 && state.u16(base + 8) == 0x0200 &&
                state.u16(base + 0x0c) == 99 && state.u16(base + 0x55) == 7 &&
                state.u16(base + 0x37) == 44 && state.u16(base + 0x3d) == 45 &&
                state.u16(base + 0x45) == 46 && state.u16(base + 0x4f) == 47 &&
                state.u16(base + 0x5d) == 8 && state.u16(base + 0x65) == 9 &&
                state.u8(base + 0x6f) == 5,
            "typed FIG actor changes were not stored at exact shared-state offsets");
}

void test_fig_effect_timeline(const std::filesystem::path& game_root) {
    require(swd2::fig_summoned_action_card_placement(0) ==
                    swd2::FigSummonedActionCardPlacement{8, 15, 16, 24} &&
                swd2::fig_summoned_action_card_placement(1) ==
                    swd2::FigSummonedActionCardPlacement{88, 15, 96, 24},
            "FIG 10fc summoned action-card Mode-X placement differs");
    require(swd2::fig_summoned_name_card_placement(0) ==
                    swd2::FigSummonedNameCardPlacement{0, 0, 4, 1, 8, 9} &&
                swd2::fig_summoned_name_card_placement(1) ==
                    swd2::FigSummonedNameCardPlacement{0x14, 0, 4, 1, 88, 9},
            "FIG 2f1f summoned name-card Mode-X placement differs");
    require(swd2::fig_monster_status_icon_frame(0) == 0xa1 &&
                swd2::fig_monster_status_icon_frame(1) == 0xa4 &&
                swd2::fig_monster_status_icon_frame(2) == 0x9e &&
                swd2::fig_monster_status_icon_frame(3) == 0xa5 &&
                swd2::fig_monster_status_icon_frame(4) == 0x9f &&
                swd2::fig_monster_status_icon_frame(5) == 0 &&
                swd2::fig_monster_status_icon_placement(30, 0) ==
                    swd2::FigMonsterStatusIconPlacement{136, 0, 152, 4} &&
                swd2::fig_monster_status_icon_placement(30, 3) ==
                    swd2::FigMonsterStatusIconPlacement{136, 0, 152, 28},
            "FIG 2deb persistent monster-status stack differs");
    swd2::BattleSessionEvent monster_reaction;
    monster_reaction.target_is_monster = true;
    monster_reaction.kind = swd2::BattleEventKind::player_attack;
    monster_reaction.damage = 1;
    require(swd2::fig_monster_reaction_phase(monster_reaction) ==
                swd2::FigMonsterReactionPhase::weapon_frame,
            "FIG physical hit did not select the one-shot weapon reaction");
    monster_reaction.kind = swd2::BattleEventKind::player_ability;
    require(swd2::fig_monster_reaction_phase(monster_reaction) ==
                swd2::FigMonsterReactionPhase::first_result_frame,
            "FIG magic hit did not select the first 144e reaction frame");
    monster_reaction.damage = 0;
    require(swd2::fig_monster_reaction_phase(monster_reaction) ==
                swd2::FigMonsterReactionPhase::none,
            "FIG zero-damage ability incorrectly selected a hit reaction");
    monster_reaction.kind = swd2::BattleEventKind::status_damage;
    require(swd2::fig_monster_reaction_phase(monster_reaction) ==
                swd2::FigMonsterReactionPhase::first_result_frame,
            "FIG zero periodic roll omitted its literal 144e reaction frame");
    monster_reaction.target_is_monster = false;
    require(swd2::fig_monster_reaction_phase(monster_reaction) ==
                swd2::FigMonsterReactionPhase::none,
            "FIG party target incorrectly selected a monster reaction frame");
    require(swd2::fig_player_status_bit(0x5e) == 0x0020 &&
                swd2::fig_player_status_bit(0x64) == 0x0002 &&
                swd2::fig_player_status_bit(0x5f) == 0x0004 &&
                swd2::fig_player_status_bit(0x65) == 0x0080 &&
                !swd2::fig_player_status_bit(0x63),
            "FIG monster-special effect/status-bit mapping differs");
    require(swd2::fig_required_medium(0x32) == 0 &&
                swd2::fig_required_medium(0x34) == 0 &&
                swd2::fig_required_medium(0x35) == 0 &&
                swd2::fig_required_medium(0x36) == 0 &&
                swd2::fig_required_medium(0x40) == 0 &&
                swd2::fig_required_medium(0x37) == 1 &&
                swd2::fig_required_medium(0x43) == 1 &&
                swd2::fig_required_medium(0x45) == 2 &&
                swd2::fig_required_medium(0x48) == 2 &&
                !swd2::fig_required_medium(0x33) &&
                swd2::fig_medium_from_target_flags(0x00e0) == 0 &&
                swd2::fig_medium_from_target_flags(0x0060) == 1 &&
                swd2::fig_medium_from_target_flags(0x0020) == 2 &&
                !swd2::fig_medium_from_target_flags(0x0010) &&
                swd2::fig_dismissed_medium(0x35) == 0 &&
                swd2::fig_dismissed_medium(0x43) == 1 &&
                swd2::fig_dismissed_medium(0x53) == 2 &&
                !swd2::fig_dismissed_medium(0x52) &&
                swd2::fig_medium_placement(0) ==
                    swd2::FigEffectPlacement{0x4a * 4, 1} &&
                swd2::fig_medium_placement(1) ==
                    swd2::FigEffectPlacement{0x44 * 4, 1} &&
                swd2::fig_medium_placement(2) ==
                    swd2::FigEffectPlacement{0x3e * 4, 1},
            "FIG 23b1/2731/58fa persistent mediator mapping differs");
    const auto effect_32 = swd2::fig_effect_timeline(0x32);
    require(effect_32.size() == 20 && effect_32.front().layers.size() == 2 &&
                effect_32.front().layers[0].resource == 43 &&
                effect_32.front().layers[1].resource == 44 &&
                effect_32.front().layers[1].vertical == 100 &&
                effect_32[4].layers[0].resource == 43,
            "FIG effect 32 paired/reloaded 48da timeline differs from 48a2");

    const auto effect_33 = swd2::fig_effect_timeline(0x33);
    require(effect_33.size() == 13 &&
                effect_33.front().layers[0].resource == 51 &&
                effect_33.front().layers[0].horizontal == -5 &&
                effect_33.front().layers[0].vertical == 110 &&
                effect_33[4].layers[0].horizontal == -7 &&
                effect_33[4].layers[0].vertical == 100 &&
                effect_33[5].layers[0].resource == 551 &&
                effect_33.back().layers[0].horizontal == 1 &&
                effect_33.back().layers[0].vertical == 9,
            "FIG effect 33 cumulative dual-archive flight path differs from 490c");

    const auto effect_35 = swd2::fig_effect_timeline(0x35);
    require(effect_35.size() == 24 && effect_35.front().layers.size() == 2 &&
                effect_35.front().layers[0].resource == 0 &&
                effect_35.front().layers[1].resource == 1 &&
                effect_35[12].layers[0].resource == 0,
            "FIG effect 35 full-screen paired compositor differs from 49d6");

    const auto effect_42 = swd2::fig_effect_timeline(0x42);
    require(effect_42.size() == 16 && effect_42[0].layers[0].resource == 220 &&
                effect_42[0].layers[0].horizontal == 16 &&
                effect_42[11].layers[0].resource == 225 &&
                effect_42[12].layers.size() == 2 &&
                effect_42[12].layers[0].resource == 228 &&
                effect_42[12].layers[1].sprite_frame == 1 &&
                effect_42.back().layers[0].resource == 231,
            "FIG effect 42 travelling/composite chain differs from 4bd8");

    const auto effect_44 = swd2::fig_effect_timeline(0x44);
    require(effect_44.size() == 10 && effect_44[0].layers[0].vertical == 2 &&
                effect_44[4].layers[0].vertical == 54 &&
                effect_44[4].layers[0].sprite_frame == 0 &&
                effect_44[5].layers[0].sprite_frame == 1 &&
                effect_44[8].layers[0].resource == 247 &&
                effect_44[9].layers[0].sprite_frame == 1,
            "FIG effect 44 held/released frame path differs from 4e15");

    const auto effect_46 = swd2::fig_effect_timeline(0x46);
    require(effect_46.size() == 20 &&
                effect_46.front().layers[0].sprite_frame == 0 &&
                effect_46[9].layers[0].sprite_frame == 9 &&
                effect_46[10].layers[0].sprite_frame == 0,
            "FIG effect 46 archive frame reset differs from 4eb0");

    const auto effect_4d = swd2::fig_effect_timeline(0x4d);
    require(effect_4d.size() == 6 && effect_4d[0].layers.size() == 1 &&
                effect_4d[0].layers[0].resource == 283 &&
                effect_4d[0].layers[0].sprite_frame == 0 &&
                effect_4d[3].layers[0].sprite_frame == 0 &&
                swd2::resolve_fig_effect_placement(
                    effect_4d[0].layers[0], 43, 91) ==
                    swd2::FigEffectPlacement{160, 71},
            "FIG effect 4d frame reset/target placement differs from 5035");

    const auto effect_5a = swd2::fig_effect_timeline(0x5a);
    require(effect_5a.size() == 8 &&
                swd2::resolve_fig_effect_placement(
                    effect_5a[0].layers[0], 43, 91) ==
                    swd2::FigEffectPlacement{140, 75} &&
                effect_5a[3].layers[0].resource == 313 &&
                swd2::resolve_fig_effect_placement(
                    effect_5a[3].layers[0], 43, 91) ==
                    swd2::FigEffectPlacement{156, 0},
            "FIG effect 5a two-stage placement differs from 5364");

    const auto effect_5b = swd2::fig_effect_timeline(0x5b);
    const std::array<swd2::FigEffectPlacement, 5> path = {{
        {152, 0}, {140, 14}, {124, 56}, {92, 92}, {-4, 94},
    }};
    require(effect_5b.size() == path.size(),
            "FIG effect 5b did not preserve all five path frames");
    for (std::size_t index = 0; index < path.size(); ++index) {
        require(effect_5b[index].layers[0].sprite_frame == index &&
                    swd2::resolve_fig_effect_placement(
                        effect_5b[index].layers[0], 43, 91) == path[index],
                "FIG effect 5b cumulative DS:30bc/30c6 path differs");
    }

    const auto effect_5d = swd2::fig_effect_timeline(0x5d);
    require(effect_5d.size() == 13 && effect_5d[0].layers.size() == 3 &&
                effect_5d[0].layers[0].resource == 319 &&
                effect_5d[0].layers[0].sprite_frame == 0 &&
                effect_5d[0].layers[1].sprite_frame == 1 &&
                effect_5d[0].layers[2].sprite_frame == 2 &&
                effect_5d[0].layers[0].vertical == 4 &&
                effect_5d[0].layers[1].vertical == 94 &&
                effect_5d[0].layers[2].vertical == 139 &&
                effect_5d.back().layers[0].resource == 331,
            "FIG effect 5d simultaneous SP319 layers differ from 5437");

    const auto effect_65 = swd2::fig_effect_timeline(0x65);
    require(effect_65.size() == 32 && effect_65.front().layers.size() == 2 &&
                effect_65.front().layers[0].resource == 345 &&
                effect_65.front().layers[1].resource == 346 &&
                effect_65.front().layers[1].vertical == 100 &&
                effect_65.back().layers[0].resource == 407 &&
                effect_65.back().layers[1].resource == 408 &&
                swd2::resolve_fig_effect_placement(
                    effect_65.front().layers[0], 43, 91) ==
                    swd2::FigEffectPlacement{32, 0},
            "FIG effect 65 paired 48da compositor differs from 56fb");

    const auto one_digit = swd2::fig_monster_number_timeline(40, 100, 7);
    const auto two_digits = swd2::fig_monster_number_timeline(40, 100, 42);
    const auto five_digits = swd2::fig_monster_number_timeline(40, 100, 65535);
    require(one_digit.front() == swd2::FigNumberPlacement{40, 95} &&
                one_digit.back() == swd2::FigNumberPlacement{40, 59} &&
                two_digits.front() == swd2::FigNumberPlacement{38, 95} &&
                five_digits.front() == swd2::FigNumberPlacement{32, 95},
            "FIG monster result digits differ from 144e/3d6c right alignment");
    const auto party_digits = swd2::fig_party_number_timeline(2);
    require(party_digits.front() == swd2::FigNumberPlacement{48, 160} &&
                party_digits.back() == swd2::FigNumberPlacement{48, 142},
            "FIG party result digits differ from the 2ac7 ten-frame rise");

    // Every hard-coded frame index is checked against the shipped archive.
    // This catches off-by-one mistakes in reconstructed 49c1 loops even when
    // the runtime renderer would safely skip a malformed layer.
    std::map<std::uint16_t, std::size_t> maximum_frames;
    for (std::uint16_t effect = 0x32; effect <= 0x65; ++effect) {
        const auto resources = swd2::fig_effect_resource_sequence(effect);
        const auto timeline = swd2::fig_effect_timeline(effect);
        require(resources.empty() || !timeline.empty(),
                "FIG archive-backed effect retained a generic presentation fallback");
        for (const auto& step : timeline) {
            for (const auto& layer : step.layers) {
                require(std::find(resources.begin(), resources.end(), layer.resource) !=
                            resources.end(),
                        "FIG exact effect timeline references an undeclared archive");
                auto& maximum = maximum_frames[layer.resource];
                maximum = std::max(maximum, layer.sprite_frame);
            }
        }
    }
    for (const auto& [resource, maximum_frame] : maximum_frames) {
        auto number = std::to_string(resource);
        number.insert(number.begin(), 3U - number.size(), '0');
        const auto path = game_root / (resource < 300 ? "SP" : "ST") /
                          ("SP" + number + ".RSK");
        require(std::filesystem::exists(path),
                "FIG effect timeline references a missing SP/ST archive");
        auto decoded = swd2::decode_rsk_block(read_file(path));
        const auto archive = swd2::SpriteArchive::parse(std::move(decoded.data));
        require(maximum_frame < archive.sprites().size(),
                "FIG effect timeline references a missing archive frame");
    }
}

void test_battle_effects(const std::filesystem::path& game_root) {
    const auto sequence = [](std::vector<std::uint16_t> values) {
        return [values = std::move(values), cursor = std::size_t{}]
               (std::uint16_t modulus) mutable {
            if (cursor >= values.size() || values[cursor] >= modulus) {
                throw std::runtime_error("invalid deterministic ability-random sequence");
            }
            return values[cursor++];
        };
    };
    const auto abilities = swd2::BattleAbilityDatabase::load(game_root / "FIG.EXE");

    std::array<swd2::PlayerSupportState, 2> support = {
        swd2::PlayerSupportState{59, 59, 52, 52, 36, 36, 0, 39, 30, 60, 22, 26},
        swd2::PlayerSupportState{10, 100, 20, 80, 5, 40, 0x0200, 20, 5, 8, 9, 10},
    };
    const auto quarter_heal = swd2::apply_player_support_effect(1, 0, 1, support);
    require(quarter_heal.supported && !quarter_heal.all_targets &&
                quarter_heal.targets.size() == 1 &&
                quarter_heal.targets[0].healing == 40 &&
                quarter_heal.targets[0].resulting_player_support_state &&
                quarter_heal.targets[0].resulting_player_support_state->hit_points == 50 &&
                quarter_heal.targets[0].resulting_player_support_state->secondary_points == 55 &&
                quarter_heal.targets[0].resulting_player_support_state->status_bits == 0x0200 &&
                support[1].hit_points == 50 && support[1].secondary_points == 55 &&
                support[1].status_bits == 0x0200,
            "FIG 25-percent support formula and caster +45 quarter bonus differ");
    const auto heal_and_clear = swd2::apply_player_support_effect(0x10, 0, 1, support);
    require(heal_and_clear.supported && support[1].hit_points == 95 &&
                support[1].secondary_points == 80 && support[1].status_bits == 0,
            "FIG support healing/status-clear effect 10 was not reproduced");
    const auto temporary_buff = swd2::apply_player_support_effect(0x24, 0, 1, support);
    require(temporary_buff.supported && support[1].strength == 8 &&
                support[1].physical_attack == 23,
            "FIG paired +3 support buff effect 24 was not reproduced");
    support[1].status_bits = 0x22fe;
    const auto resurrect = swd2::apply_player_support_effect(0x1c, 0, 1, support);
    require(resurrect.supported && support[1].status_bits == 0 &&
                support[1].hit_points == 100,
            "FIG resurrection support effect did not clear death/recover state");

    // FIG 46f4/46fa contains a shipped typo: both stores target DS:2bb9,
    // leaving the secondary percentage in DS:2bbb from the previous support
    // handler.  Prime it with selector 1 (25/25), then prove selector 1c uses
    // 10% HP but the stale 25% secondary value, each plus caster +45/4.
    std::array<swd2::PlayerSupportState, 2> resurrection_quirk{};
    resurrection_quirk[0].hit_points = 1;
    resurrection_quirk[0].maximum_hit_points = 1;
    resurrection_quirk[0].wisdom = 40;
    resurrection_quirk[1].hit_points = 1;
    resurrection_quirk[1].maximum_hit_points = 100;
    resurrection_quirk[1].secondary_points = 1;
    resurrection_quirk[1].maximum_secondary_points = 100;
    swd2::PlayerSupportRuntime support_runtime;
    swd2::apply_player_support_effect(
        1, 0, 1, resurrection_quirk, &support_runtime);
    resurrection_quirk[1].hit_points = 0;
    resurrection_quirk[1].secondary_points = 0;
    resurrection_quirk[1].status_bits = 0x2000;
    swd2::apply_player_support_effect(
        0x1c, 0, 1, resurrection_quirk, &support_runtime);
    require(resurrection_quirk[1].hit_points == 20 &&
                resurrection_quirk[1].secondary_points == 35 &&
                resurrection_quirk[1].status_bits == 0 &&
                support_runtime.primary_operand == 10 &&
                support_runtime.secondary_operand == 25,
            "FIG resurrection did not retain the real DS:2bbb stale-operand quirk");

    swd2::MonsterBattleState timed_monster;
    timed_monster.hit_points = timed_monster.maximum_hit_points = 100;
    timed_monster.physical_attack = 90;
    timed_monster.evasion = 9;
    timed_monster.status_turns = {2, 0, 1, 0, 1};
    timed_monster.special_status_turns = 1;
    timed_monster.attack_buff_turns = 1;
    timed_monster.evasion_buff_turns = 1;
    const auto timed = swd2::advance_monster_turn_status(
        timed_monster, 50, 2, 5, sequence({7}));
    require(timed.skipped && !timed.defeated && timed.periodic_triggered &&
                timed.periodic_damage == 7 &&
                timed.expired_buff_mask == 0x01U &&
                timed.expired_status_mask == 0x10U &&
                timed_monster.hit_points == 93 && timed_monster.status_turns[0] == 1 &&
                timed_monster.status_turns[2] == 1 && timed_monster.status_turns[4] == 0 &&
                timed_monster.special_status_turns == 0 &&
                timed_monster.attack_buff_turns == 1 &&
                timed_monster.physical_attack == 90 &&
                timed_monster.evasion_buff_turns == 1 && timed_monster.evasion == 9,
            "FIG monster status/buff start-of-turn ordering was not reproduced");
    const auto still_skipped = swd2::advance_monster_turn_status(
        timed_monster, 50, 2, 5, sequence({3}));
    const auto active_again = swd2::advance_monster_turn_status(
        timed_monster, 50, 2, 5, sequence({2}));
    require(still_skipped.skipped &&
                still_skipped.expired_buff_mask == 0x04U &&
                still_skipped.expired_status_mask == 0x01U &&
                timed_monster.status_turns[0] == 0 &&
                !active_again.skipped &&
                active_again.expired_buff_mask == 0x02U &&
                active_again.expired_status_mask == 0 &&
                timed_monster.attack_buff_turns == 0 &&
                timed_monster.physical_attack == 50 &&
                timed_monster.evasion_buff_turns == 0 &&
                timed_monster.evasion == 2 && timed_monster.hit_points == 88,
            "FIG persistent slot-two damage / action-skip counters differ");
    swd2::MonsterBattleState zero_periodic;
    zero_periodic.hit_points = zero_periodic.maximum_hit_points = 20;
    zero_periodic.status_turns[2] = 1;
    const auto zero_periodic_result = swd2::advance_monster_turn_status(
        zero_periodic, 0, 0, 5, sequence({0}));
    require(zero_periodic_result.periodic_triggered &&
                zero_periodic_result.periodic_damage == 0 &&
                !zero_periodic_result.skipped && zero_periodic.hit_points == 20,
            "FIG slot-two zero roll was incorrectly treated as no status tick");

    swd2::PlayerBattleState timed_player{100, 100, 0x00a6, {0, 0, 0}};
    timed_player.special_status_turns = {1, 1, 1, 1};
    timed_player.buff_turns = {1, 1, 1, 1, 1, 2};
    timed_player.physical_attack = 90;
    timed_player.physical_defense = 80;
    timed_player.speed = 70;
    timed_player.evasion = 9;
    timed_player.base_physical_attack = 40;
    timed_player.base_physical_defense = 30;
    timed_player.base_speed = 20;
    timed_player.base_evasion = 1;
    const auto player_expiry = swd2::advance_player_turn_status(timed_player);
    require(player_expiry.expired_buff_mask == 0x1fU &&
                player_expiry.recovered_status_mask == 0x0fU &&
                timed_player.status_bits == 0 &&
                timed_player.buff_turns ==
                    std::array<std::uint16_t, 6>{0, 0, 0, 0, 0, 2} &&
                timed_player.physical_attack == 40 &&
                timed_player.physical_defense == 30 && timed_player.speed == 20 &&
                timed_player.evasion == 1,
            "FIG post-player-turn status expiry/stat restoration differs");

    std::array<swd2::PlayerBattleState, 1> tactical_players = {
        swd2::PlayerBattleState{100, 100, 0, {0, 0, 0}},
    };
    tactical_players[0].base_speed = 20;
    tactical_players[0].base_physical_defense = 30;
    tactical_players[0].base_physical_attack = 40;
    const auto speed_buff = swd2::apply_player_tactical_effect(
        0x63, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({3, 2}));
    require(speed_buff.supported && speed_buff.canonical_ability_id == 86 &&
                tactical_players[0].speed == 53 &&
                tactical_players[0].buff_turns[0] == 6,
            "FIG player speed-buff power/duration rolls were not reproduced");
    const auto defense_buff = swd2::apply_player_tactical_effect(
        0x66, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({2, 1}));
    const auto attack_buff = swd2::apply_player_tactical_effect(
        0x67, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({1, 3}));
    require(defense_buff.supported && tactical_players[0].physical_defense == 62 &&
                tactical_players[0].buff_turns[1] == 5 &&
                attack_buff.supported && tactical_players[0].physical_attack == 71 &&
                tactical_players[0].buff_turns[2] == 7,
            "FIG player defense/attack buffs differ from the dispatcher");
    const auto evasion_player_buff = swd2::apply_player_tactical_effect(
        0x68, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({2, 4}));
    const auto ward_buff = swd2::apply_player_tactical_effect(
        0x69, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({5}));
    require(evasion_player_buff.supported && tactical_players[0].evasion == 9 &&
                tactical_players[0].buff_turns[3] == 8 &&
                ward_buff.supported && tactical_players[0].buff_turns[4] == 7,
            "FIG player evasion/ward timers differ from the dispatcher");
    const auto shield_one = swd2::apply_player_tactical_effect(
        0x62, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({}));
    const auto shield_two = swd2::apply_player_tactical_effect(
        0x62, 10, 0, 0, tactical_players, nullptr, 0, 0,
        abilities, sequence({}));
    require(shield_one.supported && shield_two.supported &&
                tactical_players[0].buff_turns[5] == 2,
            "FIG player magic-shield charge increment was not reproduced");
    swd2::MonsterBattleState dispelled_monster;
    dispelled_monster.special_status_turns = 3;
    dispelled_monster.attack_buff_turns = 4;
    dispelled_monster.evasion_buff_turns = 5;
    dispelled_monster.physical_attack = 99;
    dispelled_monster.evasion = 9;
    const auto dispel = swd2::apply_player_tactical_effect(
        0x61, 10, 0, 0, tactical_players, &dispelled_monster, 45, 2,
        abilities, sequence({}));
    require(dispel.supported && dispel.target_is_monster &&
                dispel.removed_monster_buff_mask == 0x07U &&
                dispelled_monster.special_status_turns == 0 &&
                dispelled_monster.attack_buff_turns == 0 &&
                dispelled_monster.evasion_buff_turns == 0 &&
                dispelled_monster.physical_attack == 45 &&
                dispelled_monster.evasion == 2,
            "FIG player dispel did not clear/restore enemy temporary buffs");
    std::array<swd2::MonsterBattleState, 4> monsters{};
    for (auto& monster : monsters) {
        monster.hit_points = 100;
        monster.maximum_hit_points = 100;
    }
    monsters[1].hit_points = monsters[1].maximum_hit_points = 200;
    monsters[1].resistances[2] = 2;
    monsters[2].resistances[2] = 3;
    monsters[3].resistances[2] = 1;
    const auto damage = swd2::apply_player_ability_effect(
        0x32, 10, 0, monsters, abilities, sequence({3, 3, 3}));
    require(damage.supported && damage.all_targets && damage.canonical_ability_id == 54 &&
                damage.targets.size() == 4 && monsters[0].hit_points == 57 &&
                monsters[1].hit_points == 114 && monsters[2].hit_points == 143 &&
                monsters[3].hit_points == 100 && damage.targets[1].damage == 86 &&
                damage.targets[2].absorbed && damage.targets[3].resisted,
            "FIG spell damage/double/absorb/immunity semantics were not reproduced");

    const auto status = swd2::apply_player_ability_effect(
        0x5e, 10, 0, monsters, abilities, sequence({4}));
    require(status.supported && status.targets.size() == 1 &&
                status.targets[0].status_duration == 6 && monsters[0].status_turns[0] == 6,
            "FIG monster status-duration formula was not reproduced");
    monsters[3].resistances[4] = 1;
    const auto resisted = swd2::apply_player_ability_effect(
        0x5e, 10, 3, monsters, abilities, sequence({}));
    require(resisted.targets.size() == 1 && resisted.targets[0].resisted &&
                resisted.targets[0].block_reason ==
                    swd2::AbilityBlockReason::resistance &&
                monsters[3].status_turns[0] == 0,
            "FIG status immunity was not reproduced");
    const auto visual_only = swd2::apply_player_ability_effect(
        0x31, 10, 0, monsters, abilities, sequence({}));
    require(visual_only.supported && visual_only.canonical_ability_id == 51 &&
                visual_only.targets.empty(),
            "FIG visual-only/reposition effect was treated as an invalid spell");

    std::array<swd2::PlayerBattleState, 4> players = {
        swd2::PlayerBattleState{100, 100, 0, {0, 0, 0}},
        swd2::PlayerBattleState{80, 80, 0, {0, 2, 0}},
        swd2::PlayerBattleState{50, 70, 0, {0, 3, 0}},
        swd2::PlayerBattleState{100, 100, 0, {0, 1, 0}},
    };
    std::uint16_t ability_points = 100;
    const auto enemy_spell = swd2::apply_monster_ability(
        54, 22, ability_points, 0, players, abilities, sequence({2}));
    require(enemy_spell.cast && enemy_spell.all_targets && enemy_spell.cost == 9 &&
                enemy_spell.power == 42 && ability_points == 91 &&
                players[0].hit_points == 58 && players[1].hit_points == 0 &&
                (players[1].status_bits & 0x2000U) != 0 &&
                players[2].hit_points == 70 && players[3].hit_points == 100 &&
                enemy_spell.targets[1].damage == 84 &&
                enemy_spell.targets[2].absorbed && enemy_spell.targets[2].healing == 20 &&
                enemy_spell.targets[3].resisted,
            "FIG enemy ability cost/power/resistance semantics were not reproduced");
    ability_points = 29;
    const auto unaffordable = swd2::apply_monster_ability(
        1, 22, ability_points, 0, players, abilities, sequence({}));
    require(!unaffordable.cast && ability_points == 29,
            "FIG enemy ability affordability check was not reproduced");

    std::array<swd2::PlayerBattleState, 1> prepaid_players = {
        swd2::PlayerBattleState{100, 100, 0, {0, 0, 0}},
    };
    const auto prepaid = swd2::apply_prepaid_monster_ability(
        54, 42, 0, prepaid_players, abilities);
    require(prepaid.cast && prepaid.cost == 9 && prepaid.power == 42 &&
                prepaid.targets.size() == 1 && prepaid.targets[0].damage == 42 &&
                prepaid_players[0].hit_points == 58,
            "prepaid FIG enemy ability state handler rerolled or misapplied power");
    prepaid_players[0].hit_points = 100;
    prepaid_players[0].status_bits = 0;
    prepaid_players[0].buff_turns[5] = 2;
    const auto shielded = swd2::apply_prepaid_monster_ability(
        54, 42, 0, prepaid_players, abilities);
    require(shielded.targets.size() == 1 && shielded.targets[0].resisted &&
                shielded.targets[0].block_reason ==
                    swd2::AbilityBlockReason::magic_shield &&
                prepaid_players[0].hit_points == 100 &&
                prepaid_players[0].buff_turns[5] == 1,
            "FIG +3164 magic shield did not consume one charge/block damage");
    prepaid_players[0].buff_turns[4] = 3;
    const auto warded = swd2::apply_prepaid_monster_ability(
        54, 42, 0, prepaid_players, abilities);
    require(warded.targets.size() == 1 && warded.targets[0].resisted &&
                warded.targets[0].block_reason ==
                    swd2::AbilityBlockReason::magic_ward &&
                prepaid_players[0].hit_points == 100 &&
                prepaid_players[0].buff_turns[4] == 3 &&
                prepaid_players[0].buff_turns[5] == 1,
            "FIG +315c magic ward did not precede/preserve +3164 shield charges");

    std::array<swd2::PlayerBattleState, 1> special_player = {
        swd2::PlayerBattleState{100, 100, 0, {0, 0, 1}},
    };
    swd2::MonsterBattleState special_monster;
    special_monster.hit_points = special_monster.maximum_hit_points = 100;
    special_monster.physical_attack = 20;
    special_monster.evasion = 1;
    std::uint16_t special_points = 50;  // already paid by the AI chooser
    const auto immune_status = swd2::apply_prepaid_monster_special(
        114, 6, special_points, 0, special_player, special_monster,
        abilities, sequence({}));
    require(immune_status.resolution == swd2::MonsterSpecialResolution::applied &&
                immune_status.targets.size() == 1 &&
                immune_status.targets[0].resisted && special_player[0].status_bits == 0,
            "FIG enemy effect 5e did not honor actor +2b immunity");
    const auto unresisted_status = swd2::apply_prepaid_monster_special(
        58, 6, special_points, 0, special_player, special_monster,
        abilities, sequence({4}));
    require(unresisted_status.resolution == swd2::MonsterSpecialResolution::applied &&
                special_player[0].special_status_turns[3] == 6 &&
                (special_player[0].status_bits & 0x0080U) != 0,
            "FIG enemy effect 65 status bit/duration was not reproduced");

    const auto evasion_buff = swd2::apply_prepaid_monster_special(
        33, 9, special_points, 0, special_player, special_monster,
        abilities, sequence({3, 2}));
    require(evasion_buff.resolution == swd2::MonsterSpecialResolution::applied &&
                special_monster.evasion_buff_turns == 7 &&
                special_monster.evasion == 9,
            "FIG enemy effect 68 duration/evasion rolls were not reproduced");
    special_points = 30;  // cost 20 has just been paid for a duplicate cast
    const auto duplicate_buff = swd2::apply_prepaid_monster_special(
        33, 9, special_points, 0, special_player, special_monster,
        abilities, sequence({}));
    require(duplicate_buff.resolution ==
                swd2::MonsterSpecialResolution::fallback_basic_attack &&
                special_points == 50,
            "FIG duplicate enemy buff did not refund cost/fall back to attack");
    const auto cinematic_only = swd2::apply_prepaid_monster_special(
        53, 1, special_points, 0, special_player, special_monster,
        abilities, sequence({}));
    require(cinematic_only.resolution ==
                swd2::MonsterSpecialResolution::applied &&
                cinematic_only.targets.empty(),
            "FIG enemy presentation-only ability was treated as unsupported");
    const auto monster_effect_49 = swd2::apply_prepaid_monster_special(
        5, 55, special_points, 0, special_player, special_monster,
        abilities, sequence({}));
    const auto monster_effect_4c = swd2::apply_prepaid_monster_special(
        10, 101, special_points, 0, special_player, special_monster,
        abilities, sequence({}));
    require(monster_effect_49.resolution ==
                swd2::MonsterSpecialResolution::applied &&
                monster_effect_49.targets.empty() &&
                monster_effect_4c.resolution ==
                swd2::MonsterSpecialResolution::applied &&
                monster_effect_4c.targets.empty(),
            "FIG shipped enemy-only effects 49/4c remained unsupported");
    special_player[0].buff_turns = {1, 0, 2, 0, 3, 4};
    const auto cleanse = swd2::apply_prepaid_monster_special(
        71, 1, special_points, 0, special_player, special_monster,
        abilities, sequence({}));
    require(cleanse.resolution == swd2::MonsterSpecialResolution::applied &&
                cleanse.removed_player_buff_mask == 0x35U &&
                cleanse.targets.size() == 1 &&
                special_player[0].buff_turns ==
                    std::array<std::uint16_t, 6>{0, 0, 0, 0, 0, 0},
            "FIG enemy effect 61 did not preserve/report its six cleared buffs");

    const auto item_records =
        swd2::ScriptArchive::load(game_root / "ITEM.EXE");
    std::set<std::uint16_t> shipped_special_abilities;
    for (std::uint16_t item_id = 0x13aU;
         static_cast<std::size_t>(item_id) + 2U < item_records.entry_count();
         ++item_id) {
        const auto record = item_records.entry(
            static_cast<std::size_t>(item_id) + 2U);
        if (record.size() < 0x50U) continue;
        const auto monster = swd2::MonsterDefinition::parse(item_id, record);
        if (monster.special_ability_a != 0) {
            shipped_special_abilities.insert(monster.special_ability_a);
        }
        if (monster.special_ability_b != 0) {
            shipped_special_abilities.insert(monster.special_ability_b);
        }
    }
    std::vector<std::uint16_t> unsupported_shipped_specials;
    for (const auto ability_id : shipped_special_abilities) {
        std::array<swd2::PlayerBattleState, 1> players = {
            swd2::PlayerBattleState{100, 100, 0, {0, 0, 0}},
        };
        players[0].buff_turns.fill(1);
        swd2::MonsterBattleState monster;
        monster.hit_points = monster.maximum_hit_points = 100;
        monster.physical_attack = 20;
        monster.evasion = 1;
        std::uint16_t points = 60000;
        const auto applied = swd2::apply_prepaid_monster_special(
            ability_id, 10, points, 0, players, monster, abilities,
            [](std::uint16_t modulus) {
                if (modulus == 0) {
                    throw std::runtime_error("zero exhaustive-special modulus");
                }
                return std::uint16_t{};
            });
        if (applied.resolution ==
            swd2::MonsterSpecialResolution::unsupported) {
            unsupported_shipped_specials.push_back(ability_id);
        }
    }
    require(shipped_special_abilities.size() == 19 &&
                unsupported_shipped_specials.empty(),
            "FIG shipped monster special-ability domain remains unsupported");

    // Captured monsters deliberately do not enter the enemy-only 26af
    // dispatcher.  FIG 10fa7 calls 1048, which uses the ordinary player
    // DS:2bbd table after the ally AI has already paid/rolled in 22f3.  Audit
    // every generic/A/B ability referenced by a shipped monster definition so
    // the portable ally adapter cannot silently omit a player-side selector.
    std::set<std::uint16_t> shipped_ally_abilities;
    for (std::uint16_t item_id = 0x13aU;
         static_cast<std::size_t>(item_id) + 2U < item_records.entry_count();
         ++item_id) {
        const auto record = item_records.entry(
            static_cast<std::size_t>(item_id) + 2U);
        if (record.size() < 0x50U) continue;
        const auto monster = swd2::MonsterDefinition::parse(item_id, record);
        for (const auto ability_id : {
                 monster.generic_ability,
                 monster.special_ability_a,
                 monster.special_ability_b,
             }) {
            if (ability_id != 0) shipped_ally_abilities.insert(ability_id);
        }
    }
    std::vector<std::uint16_t> unsupported_shipped_ally_abilities;
    for (const auto ability_id : shipped_ally_abilities) {
        const auto effect_code = abilities.ability(ability_id).effect_code;
        const auto zero_random = [](std::uint16_t modulus) {
            if (modulus == 0) {
                throw std::runtime_error("zero exhaustive-ally modulus");
            }
            return std::uint16_t{};
        };
        bool supported = false;
        if (effect_code <= 0x30U) {
            std::array<swd2::PlayerSupportState, 1> players{};
            players[0].hit_points = players[0].maximum_hit_points = 100;
            players[0].secondary_points =
                players[0].maximum_secondary_points = 100;
            players[0].ability_points =
                players[0].maximum_ability_points = 100;
            supported = swd2::apply_player_support_effect(
                            effect_code, 0, 0, players)
                            .supported;
        } else {
            std::array<swd2::PlayerBattleState, 1> players{};
            players[0].hit_points = players[0].maximum_hit_points = 100;
            swd2::MonsterBattleState monster;
            monster.hit_points = monster.maximum_hit_points = 10000;
            monster.physical_attack = 20;
            monster.evasion = 1;
            const auto tactical = swd2::apply_player_tactical_effect(
                effect_code, 10, 0, 0, players, &monster, 20, 1,
                abilities, zero_random);
            if (tactical.supported) {
                supported = true;
            } else {
                std::array<swd2::MonsterBattleState, 1> monsters = {monster};
                supported = swd2::apply_player_ability_effect(
                                effect_code, 10, 0, monsters,
                                abilities, zero_random)
                                .supported;
            }
        }
        if (!supported) {
            unsupported_shipped_ally_abilities.push_back(ability_id);
        }
    }
    require(shipped_ally_abilities.size() == 68 &&
                unsupported_shipped_ally_abilities.empty(),
            "FIG shipped captured-ally ability domain has an unsupported player dispatch");
}

void test_battle_session(const std::filesystem::path& game_root) {
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto database = swd2::BattleDatabase::load(game_root / "ORC.EXE");
    const auto selected = database.encounter_at_directory_offset(392);
    require(selected.has_value(), "test formation 392 is absent from ORC.EXE");
    const auto items = swd2::ScriptArchive::load(game_root / "ITEM.EXE");
    const auto abilities = swd2::BattleAbilityDatabase::load(game_root / "FIG.EXE");
    auto session = swd2::BattleSession::create(state, selected->get(), items);
    require(session.party_count() == 3 && session.monsters().size() == 1 &&
                session.monster_definitions()[0].id == 500 &&
                session.monsters()[0].hit_points == 120 &&
                session.outcome() == swd2::BattleOutcome::ongoing,
            "portable battle session did not compose SAVE/ORC/ITEM state");

    swd2::BattleCommandMenu menu(session, abilities, items);
    require(menu.page() == swd2::BattleCommandMenuPage::commands &&
                menu.actor() == 0 && menu.cursor() == 2 &&
                menu.entries().size() == 4 &&
                menu.entries()[0].kind == swd2::PlayerCommandKind::ability &&
                menu.entries()[1].kind == swd2::PlayerCommandKind::item &&
                menu.entries()[2].kind == swd2::PlayerCommandKind::basic_attack &&
                menu.entries()[3].kind == swd2::PlayerCommandKind::skip,
            "FIG four-way command menu did not expose the exact tile order");

    auto insufficient_state = state;
    insufficient_state.set_u16(0x106 + 0x55, 0);
    auto insufficient_session = swd2::BattleSession::create(
        insufficient_state, selected->get(), items);
    swd2::BattleCommandMenu insufficient_menu(
        insufficient_session, abilities, items);
    insufficient_menu.input(swd2::InputAction::left);
    insufficient_menu.input(swd2::InputAction::confirm);
    insufficient_menu.input(swd2::InputAction::confirm);
    require(insufficient_menu.notice() ==
                swd2::BattleCommandNotice::insufficient_resource &&
                insufficient_menu.page() ==
                    swd2::BattleCommandMenuPage::abilities,
            "FIG insufficient ability resource did not enter DATA:2cb7 modal");
    insufficient_menu.input(swd2::InputAction::down);
    require(insufficient_menu.notice() == swd2::BattleCommandNotice::none &&
                insufficient_menu.cursor() == 0,
            "FIG 3e19 closing key leaked into the underlying ability selector");

    auto element_state = state;
    element_state.set_u8(0x106 + 0x6d, 41);
    for (std::size_t slot = 0; slot < 5; ++slot) {
        element_state.set_u16(0x3e6 + slot * 2U, 0);
    }
    auto element_session = swd2::BattleSession::create(
        element_state, selected->get(), items);
    swd2::BattleCommandMenu element_menu(element_session, abilities, items);
    element_menu.input(swd2::InputAction::left);
    element_menu.input(swd2::InputAction::confirm);
    element_menu.input(swd2::InputAction::confirm);
    require(element_menu.notice() ==
                swd2::BattleCommandNotice::missing_elements,
            "FIG missing five-element counters did not enter DATA:2d05 modal");

    const auto disabled_ability = std::find_if(
        abilities.abilities().begin() + 1, abilities.abilities().end(),
        [](const swd2::BattleAbility& ability) {
            return ability.effect_code != 0 &&
                   (ability.target_flags & 0x4000U) != 0;
        });
    require(disabled_ability != abilities.abilities().end(),
            "FIG ability table has no intrinsic-disable regression record");
    auto unavailable_state = state;
    unavailable_state.set_u8(
        0x106 + 0x6d,
        static_cast<std::uint8_t>(disabled_ability - abilities.abilities().begin()));
    auto unavailable_session = swd2::BattleSession::create(
        unavailable_state, selected->get(), items);
    swd2::BattleCommandMenu unavailable_menu(
        unavailable_session, abilities, items);
    unavailable_menu.input(swd2::InputAction::left);
    unavailable_menu.input(swd2::InputAction::confirm);
    unavailable_menu.input(swd2::InputAction::confirm);
    require(unavailable_menu.notice() ==
                swd2::BattleCommandNotice::ability_unavailable,
            "FIG record +0d bit 40 did not enter DATA:2cf5 modal");

    auto summon_menu_state = state;
    summon_menu_state.set_u16(0x382, 319);
    summon_menu_state.set_u16(0x106 + 0x35, 52);
    auto summon_menu_session = swd2::BattleSession::create(
        summon_menu_state, selected->get(), items);
    swd2::BattleCommandMenu summon_resource_menu(
        summon_menu_session, abilities, items);
    summon_resource_menu.input(swd2::InputAction::right);
    summon_resource_menu.input(swd2::InputAction::confirm);
    summon_resource_menu.input(swd2::InputAction::confirm);
    require(summon_resource_menu.notice() ==
                swd2::BattleCommandNotice::insufficient_summon_resource,
            "FIG summon resource equality did not enter DATA:2e65 modal");

    std::optional<std::uint16_t> unusable_item_id;
    for (std::size_t id = 1; id < 0x13aU && id + 2U < items.entry_count(); ++id) {
        const auto record = items.entry(id + 2U);
        if (record.size() >= 9U && (record[5] & 2U) == 0) {
            unusable_item_id = static_cast<std::uint16_t>(id);
            break;
        }
    }
    require(unusable_item_id.has_value(),
            "ITEM has no battle-unusable modal regression record");
    auto unusable_item_state = state;
    unusable_item_state.set_u16(0x382, *unusable_item_id);
    auto unusable_item_session = swd2::BattleSession::create(
        unusable_item_state, selected->get(), items);
    swd2::BattleCommandMenu unusable_item_menu(
        unusable_item_session, abilities, items);
    unusable_item_menu.input(swd2::InputAction::right);
    unusable_item_menu.input(swd2::InputAction::confirm);
    unusable_item_menu.input(swd2::InputAction::confirm);
    require(unusable_item_menu.notice() ==
                swd2::BattleCommandNotice::item_unusable,
            "FIG ITEM +5 battle-use failure did not enter DATA:2cd1 modal");
    menu.input(swd2::InputAction::confirm);
    require(menu.page() == swd2::BattleCommandMenuPage::attack_modes &&
                menu.entries().size() == 2,
            "FIG Attack command did not enter its two-way mode submenu");
    menu.input(swd2::InputAction::confirm);
    require(menu.complete() &&
                menu.commands()[0].kind == swd2::PlayerCommandKind::basic_attack &&
                menu.commands()[1].kind == swd2::PlayerCommandKind::basic_attack &&
                menu.commands()[2].kind == swd2::PlayerCommandKind::basic_attack &&
                menu.commands()[0].target == 0 && menu.commands()[1].target == 0 &&
                menu.commands()[2].target == 0,
            "FIG group attack did not fill every commandable party slot");

    const auto multi_encounter = std::find_if(
        database.encounters().begin(), database.encounters().end(),
        [](const swd2::BattleEncounter& encounter) {
            return encounter.definition_slots.size() > 1;
        });
    require(multi_encounter != database.encounters().end(),
            "ORC has no multi-monster selector test formation");
    auto multi_session = swd2::BattleSession::create(
        state, *multi_encounter, items);
    swd2::BattleCommandMenu multi_menu(multi_session, abilities, items);
    multi_menu.input(swd2::InputAction::confirm);
    multi_menu.input(swd2::InputAction::confirm);
    require(multi_menu.page() == swd2::BattleCommandMenuPage::monster_target &&
                multi_menu.entries().size() ==
                    multi_encounter->definition_slots.size(),
            "FIG multi-monster attack omitted its living-target name selector");
    multi_menu.input(swd2::InputAction::up);
    require(multi_menu.cursor() == 0,
            "FIG target selector wrapped above its first entry");
    multi_menu.input(swd2::InputAction::down);
    multi_menu.input(swd2::InputAction::confirm);
    require(multi_menu.complete() && multi_menu.commands()[0].target == 1,
            "FIG multi-monster selector did not commit the highlighted target");

    swd2::BattleCommandMenu target_return_menu(
        multi_session, abilities, items);
    target_return_menu.input(swd2::InputAction::confirm);
    target_return_menu.input(swd2::InputAction::right);
    target_return_menu.input(swd2::InputAction::confirm);
    require(target_return_menu.page() ==
                swd2::BattleCommandMenuPage::monster_target &&
                target_return_menu.target_return_page() ==
                    swd2::BattleCommandMenuPage::attack_modes &&
                target_return_menu.target_return_cursor() == 1 &&
                target_return_menu.target_return_entries().size() == 2,
            "FIG target selector did not retain its underlying attack page");
    target_return_menu.input(swd2::InputAction::cancel);
    require(target_return_menu.page() ==
                swd2::BattleCommandMenuPage::attack_modes &&
                target_return_menu.cursor() == 1,
            "FIG target cancel did not restore the previous submenu cursor");

    swd2::BattleCommandMenu ability_menu(session, abilities, items);
    ability_menu.input(swd2::InputAction::left);
    ability_menu.input(swd2::InputAction::confirm);
    require(ability_menu.page() == swd2::BattleCommandMenuPage::abilities &&
                ability_menu.entries().size() == 50 &&
                ability_menu.entries()[0].value == 76 &&
                ability_menu.entries()[1].value == 70 &&
                !ability_menu.entries()[2].enabled,
            "FIG command menu did not preserve all 50 learned-ability slots");
    ability_menu.input(swd2::InputAction::cancel);
    require(ability_menu.page() == swd2::BattleCommandMenuPage::commands,
            "FIG ability menu cancel did not return to commands");

    swd2::BattleCommandMenu item_menu(session, abilities, items);
    item_menu.input(swd2::InputAction::right);
    item_menu.input(swd2::InputAction::confirm);
    require(item_menu.page() == swd2::BattleCommandMenuPage::items &&
                item_menu.entries().size() == 50 &&
                item_menu.entries()[0].value == 0 &&
                item_menu.entries()[49].value == 49,
            "FIG inventory menu did not preserve its exact 50 physical slots");
    for (int move = 0; move < 60; ++move) {
        item_menu.input(swd2::InputAction::down);
    }
    require(item_menu.cursor() == 49,
            "FIG 50-slot selector wrapped instead of clamping at slot 49");

    std::optional<std::uint16_t> item_flag_mismatch;
    bool mismatch_battle_usable = false;
    for (std::size_t id = 1;
         id < 0x13a && id + 2U < items.entry_count(); ++id) {
        const auto record = items.entry(id + 2U);
        if (record.size() < 9) continue;
        const auto definition = swd2::BattleItemDefinition::parse(
            static_cast<std::uint16_t>(id), record);
        const auto type_bit = (definition.type & 2U) != 0;
        const auto use_bit = (definition.use_flags & 2U) != 0;
        if (type_bit != use_bit) {
            item_flag_mismatch = static_cast<std::uint16_t>(id);
            mismatch_battle_usable = use_bit;
            break;
        }
    }
    require(item_flag_mismatch.has_value(),
            "ITEM has no +0/+5 bit-1 discriminator regression record");
    auto item_flag_state = state;
    item_flag_state.set_u16(0x382, *item_flag_mismatch);
    auto item_flag_session = swd2::BattleSession::create(
        item_flag_state, selected->get(), items);
    swd2::BattleCommandMenu item_flag_menu(
        item_flag_session, abilities, items);
    item_flag_menu.input(swd2::InputAction::right);
    item_flag_menu.input(swd2::InputAction::confirm);
    require(item_flag_menu.entries()[0].enabled == mismatch_battle_usable,
            "FIG item menu tested type +0 instead of battle-use flags +5");

    auto reservation_state = state;
    reservation_state.set_u16(0x382, 51);
    auto reservation_session = swd2::BattleSession::create(
        reservation_state, selected->get(), items);
    swd2::BattleCommandMenu reservation_menu(
        reservation_session, abilities, items);
    reservation_menu.input(swd2::InputAction::right);
    reservation_menu.input(swd2::InputAction::confirm);
    require(reservation_menu.entries()[0].enabled,
            "FIG first actor could not reserve an available battle item");
    reservation_menu.input(swd2::InputAction::confirm);
    require(reservation_menu.page() == swd2::BattleCommandMenuPage::party_target,
            "FIG party-target item skipped its target selector");
    reservation_menu.input(swd2::InputAction::confirm);
    reservation_menu.input(swd2::InputAction::right);
    reservation_menu.input(swd2::InputAction::confirm);
    require(!reservation_menu.entries()[0].enabled &&
                reservation_menu.item_reserved(0),
            "FIG item reservation did not prevent a second actor reusing the slot");
    reservation_menu.input(swd2::InputAction::cancel);
    reservation_menu.input(swd2::InputAction::cancel);
    reservation_menu.input(swd2::InputAction::right);
    reservation_menu.input(swd2::InputAction::confirm);
    require(reservation_menu.entries()[0].enabled,
            "FIG command rewind did not clear the actor's item reservation bit");

    swd2::BattleCommandMenu automatic_menu(session, abilities, items);
    automatic_menu.input(swd2::InputAction::confirm);
    automatic_menu.input(swd2::InputAction::right);
    automatic_menu.input(swd2::InputAction::confirm);
    require(automatic_menu.complete() && automatic_menu.automatic_requested() &&
                automatic_menu.commands()[0].kind ==
                    swd2::PlayerCommandKind::basic_attack &&
                automatic_menu.commands()[1].kind ==
                    swd2::PlayerCommandKind::basic_attack &&
                automatic_menu.commands()[2].kind ==
                    swd2::PlayerCommandKind::basic_attack &&
                automatic_menu.commands()[0].target == 0 &&
                automatic_menu.commands()[1].target == 0 &&
                automatic_menu.commands()[2].target == 0,
            "FIG 1588 automatic mode did not fill all commandable party slots");

    swd2::BattleCommandMenu escape_menu(session, abilities, items);
    escape_menu.input(swd2::InputAction::down);
    escape_menu.input(swd2::InputAction::confirm);
    require(escape_menu.page() == swd2::BattleCommandMenuPage::tactics &&
                escape_menu.entries().size() == 2,
            "FIG fixed-battle tactics submenu exposed the wrong tiles");
    escape_menu.input(swd2::InputAction::right);
    escape_menu.input(swd2::InputAction::confirm);
    require(escape_menu.complete() &&
                escape_menu.commands()[0].kind == swd2::PlayerCommandKind::escape &&
                escape_menu.commands()[1].kind == swd2::PlayerCommandKind::escape &&
                escape_menu.commands()[2].kind == swd2::PlayerCommandKind::escape,
            "FIG flee tile did not override every commandable party slot");

    auto blocked_capture_session = swd2::BattleSession::create(
        state, selected->get(), items, true);
    swd2::BattleCommandMenu blocked_capture_menu(
        blocked_capture_session, abilities, items);
    blocked_capture_menu.input(swd2::InputAction::down);
    blocked_capture_menu.input(swd2::InputAction::confirm);
    require(blocked_capture_menu.entries().size() == 2,
            "FIG +3f0 bit 0100 did not suppress the random-battle capture tile");

    auto capture_menu_state = state;
    capture_menu_state.set_u8(0x3f1, 0);
    auto random_menu_session = swd2::BattleSession::create(
        capture_menu_state, selected->get(), items, true);
    swd2::BattleCommandMenu capture_menu(random_menu_session, abilities, items);
    capture_menu.input(swd2::InputAction::down);
    capture_menu.input(swd2::InputAction::confirm);
    require(capture_menu.page() == swd2::BattleCommandMenuPage::tactics &&
                capture_menu.entries().size() == 3 &&
                capture_menu.entries()[2].kind == swd2::PlayerCommandKind::capture,
            "FIG random-battle actor-zero tactics submenu omitted capture");
    capture_menu.input(swd2::InputAction::up);
    capture_menu.input(swd2::InputAction::confirm);
    require(capture_menu.page() == swd2::BattleCommandMenuPage::commands &&
                capture_menu.actor() == 1 &&
                capture_menu.commands()[0].kind ==
                    swd2::PlayerCommandKind::capture,
            "FIG one-monster capture did not bypass target selection");

    std::array<swd2::PlayerBattleCommand, 4> commands{};
    commands[0] = {swd2::PlayerCommandKind::ability, 76, 0};
    commands[1] = {swd2::PlayerCommandKind::basic_attack, 0, 0};
    commands[2] = {swd2::PlayerCommandKind::basic_attack, 0, 0};
    commands[3] = {swd2::PlayerCommandKind::skip, 0, 0};
    const auto stable_random = [](std::uint16_t modulus) {
        if (modulus == 0) throw std::runtime_error("zero deterministic modulus");
        return static_cast<std::uint16_t>(std::min<std::uint16_t>(2, modulus - 1));
    };

    const auto first = session.play_round(commands, abilities, stable_random);
    const auto ability_event = std::find_if(
        first.events.begin(), first.events.end(),
        [](const swd2::BattleSessionEvent& event) {
            return event.kind == swd2::BattleEventKind::player_ability &&
                   event.ability_id == 76;
        });
    require(first.outcome == swd2::BattleOutcome::ongoing &&
                ability_event != first.events.end() &&
                ability_event->effect_code == 0x38 &&
                session.monsters()[0].hit_points == 78 &&
                session.party()[2].hit_points == 3 &&
                session.party()[0].ability_points == 31,
            "portable battle round did not apply player magic and normal enemy AI");
    const auto second = session.play_round(commands, abilities, stable_random);
    require(second.outcome == swd2::BattleOutcome::ongoing &&
                session.monsters()[0].hit_points == 36 &&
                !session.party()[2].living() &&
                (session.party()[2].status_bits & 0x2000U) != 0 &&
                session.critical_countdown() == 1 &&
                std::none_of(second.events.begin(), second.events.end(),
                             [](const swd2::BattleSessionEvent& event) {
                                 return event.kind ==
                                        swd2::BattleEventKind::death_reaction;
                             }),
            "portable battle round did not persist death/remaining monster HP");
    const auto third = session.play_round(commands, abilities, stable_random);
    require(third.outcome == swd2::BattleOutcome::victory &&
                session.monsters()[0].hit_points == 0 &&
                session.party()[0].ability_points == 21 &&
                session.critical_countdown() == 20,
            "portable battle session did not reach a rule-driven victory");

    session.store(state);
    require(state.u16(0x106 + 0x55) == 21 &&
                state.u16(0x106 + 2 * 0x9f + 0x2d) == 0 &&
                (state.u16(0x106 + 2 * 0x9f + 8) & 0x2000U) != 0 &&
                state.u16(0x49e) == 20,
            "portable battle session did not commit exact shared-state fields");

    auto item_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    item_state.set_u16(0x382, 0x8000U | 51U);  // reservation bit + item id
    const auto actor_two = 0x106 + 2 * 0x9f;
    item_state.set_u16(actor_two + 0x2d, 0);
    item_state.set_u16(actor_two + 8, 0x2000);
    auto item_session = swd2::BattleSession::create(item_state, selected->get(), items);
    std::array<swd2::PlayerBattleCommand, 4> item_commands{};
    for (auto& command : item_commands) {
        command = {swd2::PlayerCommandKind::skip, 0, 0, 0};
    }
    item_commands[1] = {swd2::PlayerCommandKind::item, 0, 2, 0};
    const auto zero_random = [](std::uint16_t modulus) {
        if (modulus == 0) throw std::runtime_error("zero item-test modulus");
        return std::uint16_t{};
    };
    require(session.try_grant_encounter_capture(83, zero_random) &&
                session.inventory().back() == 83 &&
                !session.try_grant_encounter_capture(84, zero_random),
            "FIG ORC ## one-in-three capture reward was not reproduced");
    const auto item_round =
        item_session.play_round(item_commands, abilities, zero_random);
    const auto item_event = std::find_if(
        item_round.events.begin(), item_round.events.end(),
        [](const swd2::BattleSessionEvent& event) {
            return event.kind == swd2::BattleEventKind::player_ability &&
                   event.ability_id == 51;
        });
    require(item_session.inventory()[0] == 0 &&
                item_event != item_round.events.end() &&
                item_event->effect_code == 0x1c &&
                item_event->resulting_player_support_state &&
                item_event->resulting_player_support_state->hit_points == 26 &&
                item_event->resulting_player_support_state->status_bits == 0 &&
                item_session.party()[2].living() &&
                item_session.party()[2].hit_points == 26,
            "FIG consumable item did not dispatch support effect/clear its slot");
    item_session.store(item_state);
    require(item_state.u16(0x382) == 0 && item_state.u16(actor_two + 0x2d) == 26,
            "FIG battle-item/session changes did not persist to shared state");

    std::optional<std::uint16_t> resource_item_id;
    std::uint16_t resource_item_cost = 0;
    for (std::uint16_t id = 0x8c; id < 0x13a; ++id) {
        if (static_cast<std::size_t>(id) + 2U >= items.entry_count()) break;
        const auto record = items.entry(static_cast<std::size_t>(id) + 2U);
        if (record.size() < 9) continue;
        const auto item = swd2::BattleItemDefinition::parse(id, record);
        const auto ability_id = static_cast<std::size_t>(id - 0x8cU);
        if (item.type != 0x10 || (item.use_flags & 2U) == 0 ||
            ability_id >= abilities.abilities().size() ||
            abilities.ability(ability_id).cost == 0 || item.effect_code == 0x47) {
            continue;
        }
        resource_item_id = id;
        resource_item_cost = abilities.ability(ability_id).cost;
        break;
    }
    require(resource_item_id.has_value(),
            "ITEM has no type-10 FIG resource-cost regression item");
    auto resource_item_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    resource_item_state.set_u16(0x382, *resource_item_id);
    resource_item_state.set_u16(0x106 + 0x55, 1000);
    resource_item_state.set_u16(0x106 + 0x57, 1000);
    resource_item_state.set_u16(0x106 + 0x5d, 1000);
    auto resource_item_session = swd2::BattleSession::create(
        resource_item_state, selected->get(), items);
    std::array<swd2::PlayerBattleCommand, 4> resource_item_commands{};
    for (auto& command : resource_item_commands) {
        command = {swd2::PlayerCommandKind::skip, 0, 0, 0};
    }
    resource_item_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto resource_item_round = resource_item_session.play_round(
        resource_item_commands, abilities, zero_random);
    require(resource_item_session.party()[0].ability_points ==
                static_cast<std::uint16_t>(1000U - resource_item_cost) &&
                std::any_of(resource_item_round.events.begin(),
                            resource_item_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                       swd2::BattleEventKind::player_ability;
                            }),
            "FIG type-10 battle item did not charge its embedded ability cost");
    auto poor_item_state = resource_item_state;
    poor_item_state.set_u16(
        0x106 + 0x55,
        static_cast<std::uint16_t>(resource_item_cost - 1U));
    auto poor_item_session = swd2::BattleSession::create(
        poor_item_state, selected->get(), items);
    swd2::BattleCommandMenu poor_item_menu(
        poor_item_session, abilities, items);
    poor_item_menu.input(swd2::InputAction::right);
    poor_item_menu.input(swd2::InputAction::confirm);
    require(!poor_item_menu.entries()[0].enabled,
            "FIG type-10 item menu ignored the embedded ability affordability check");

    // FIG resource class five treats the ability "cost" as a mask over the
    // five shared counters at +3e6 rather than subtracting a numeric pool.
    auto class_five_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    class_five_state.set_u16(0x10, 4);
    const auto actor_three = 0x106 + 3 * 0x9f;
    class_five_state.set_u16(actor_three + 8, 0);
    class_five_state.set_u16(actor_three + 0x2d, 100);
    class_five_state.set_u16(actor_three + 0x2f, 100);
    class_five_state.set_u8(actor_three + 0x6d, 48);
    for (std::size_t slot = 1; slot <= 3; ++slot) {
        class_five_state.set_u16(0x3e6 + slot * 2, 1);
    }
    const swd2::BattleEncounter* class_five_encounter = nullptr;
    for (const auto& candidate : database.encounters()) {
        if (candidate.definition_slots.size() != 1) continue;
        const auto definition_id =
            candidate.monster_definition_ids[candidate.definition_slots[0]];
        const auto definition = swd2::MonsterDefinition::parse(
            definition_id, items.entry(static_cast<std::size_t>(definition_id) + 2));
        if (definition.resistance_flags[3] != 1) {
            class_five_encounter = &candidate;
            break;
        }
    }
    require(class_five_encounter != nullptr,
            "ORC data has no single-monster class-five status test formation");
    auto class_five_session = swd2::BattleSession::create(
        class_five_state, *class_five_encounter, items);
    std::array<swd2::PlayerBattleCommand, 4> class_five_commands{};
    for (auto& command : class_five_commands) {
        command = {swd2::PlayerCommandKind::skip, 0, 0, 0};
    }
    class_five_commands[3] = {swd2::PlayerCommandKind::ability, 48, 0, 0};
    class_five_session.play_round(class_five_commands, abilities, zero_random);
    require(class_five_session.monsters()[0].status_turns[2] == 2 &&
                class_five_session.special_item_counts()[1] == 0 &&
                class_five_session.special_item_counts()[2] == 0 &&
                class_five_session.special_item_counts()[3] == 0 &&
                class_five_session.party()[3].abilities[0] == 0,
            "FIG class-five masked consumable counters were not reproduced");
    class_five_session.store(class_five_state);
    require(class_five_state.u16(0x3e8) == 0 &&
                class_five_state.u16(0x3ea) == 0 &&
                class_five_state.u16(0x3ec) == 0 &&
                class_five_state.u8(actor_three + 0x6d) == 0,
            "FIG class-five counters/learned-slot exhaustion did not persist");

    auto escape_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto actor_zero = 0x106U;
    escape_state.set_u8(actor_zero + 0x6d, 7);
    escape_state.set_u16(actor_zero + 0x35, 200);
    escape_state.set_u16(actor_zero + 0x5d, 1000);
    auto escape_session = swd2::BattleSession::create(
        escape_state, selected->get(), items, true);
    std::array<swd2::PlayerBattleCommand, 4> escape_commands{};
    for (auto& command : escape_commands) {
        command = {swd2::PlayerCommandKind::skip, 0, 0, 0};
    }
    escape_commands[0] = {swd2::PlayerCommandKind::ability, 7, 0, 0};
    const auto escaped = escape_session.play_round(
        escape_commands, abilities, zero_random);
    require(escaped.outcome == swd2::BattleOutcome::escaped &&
                escape_session.party()[0].secondary_points == 200 &&
                std::any_of(escaped.events.begin(), escaped.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                           swd2::BattleEventKind::player_escaped &&
                                       event.effect_code == 0x47;
                            }),
            "FIG effect 47 did not escape a random battle without charging MP");

    auto fixed_escape_session = swd2::BattleSession::create(
        escape_state, selected->get(), items, false);
    const auto fixed_escape = fixed_escape_session.play_round(
        escape_commands, abilities, zero_random);
    require(fixed_escape.outcome != swd2::BattleOutcome::escaped &&
                fixed_escape_session.party()[0].secondary_points == 200 &&
                std::any_of(fixed_escape.events.begin(), fixed_escape.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                           swd2::BattleEventKind::escape_failed &&
                                       event.effect_code == 0x47;
                            }),
            "FIG effect 47 did not reject fixed-battle escape without charging MP");

    auto escape_item_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    escape_item_state.set_u16(0x382, 237);  // type-10 effect 47 wrapper
    escape_item_state.set_u16(actor_zero + 0x55, 1000);
    escape_item_state.set_u16(actor_zero + 0x57, 1000);
    escape_item_state.set_u16(actor_zero + 0x5d, 1000);
    auto escape_item_session = swd2::BattleSession::create(
        escape_item_state, selected->get(), items, true);
    auto escape_item_commands = escape_commands;
    escape_item_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto escaped_by_item = escape_item_session.play_round(
        escape_item_commands, abilities, zero_random);
    require(escaped_by_item.outcome == swd2::BattleOutcome::escaped &&
                escape_item_session.inventory()[0] == 237 &&
                escape_item_session.party()[0].ability_points == 1000,
            "FIG item 237 effect-47 stack exit consumed AP/item or failed to escape");

    auto fixed_escape_item_session = swd2::BattleSession::create(
        escape_item_state, selected->get(), items, false);
    const auto fixed_escape_item = fixed_escape_item_session.play_round(
        escape_item_commands, abilities, zero_random);
    require(fixed_escape_item.outcome != swd2::BattleOutcome::escaped &&
                fixed_escape_item_session.inventory()[0] == 237 &&
                fixed_escape_item_session.party()[0].ability_points == 1000 &&
                std::any_of(fixed_escape_item.events.begin(),
                            fixed_escape_item.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                       swd2::BattleEventKind::escape_failed;
                            }),
            "FIG item 237 did not reject fixed-battle escape without consumption");

    // The ordinary flee tile is global: it replaces every commandable
    // party slot, then each actor attempts escape in sorted initiative order.
    auto flee_rules_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    for (std::size_t index = 0; index < 3; ++index) {
        const auto base = actor_zero + index * 0x9f;
        flee_rules_state.set_u16(base + 8, 0);
        flee_rules_state.set_u16(base + 0x0e, 1000);
        flee_rules_state.set_u16(base + 0x2d, 1000);
        flee_rules_state.set_u16(base + 0x2f, 1000);
        flee_rules_state.set_u16(base + 0x31, 1);
        flee_rules_state.set_u16(base + 0x5d, 1000);
    }
    std::array<swd2::PlayerBattleCommand, 4> flee_commands{};
    for (std::size_t index = 0; index < flee_commands.size(); ++index) {
        flee_commands[index] = index < 3
                                   ? swd2::PlayerBattleCommand{
                                         swd2::PlayerCommandKind::escape, 0, index, 0}
                                   : swd2::PlayerBattleCommand{
                                         swd2::PlayerCommandKind::skip, 0, 0, 0};
    }

    auto low_card_state = flee_rules_state;
    low_card_state.set_u16(actor_zero + 8, 0);
    low_card_state.set_u16(actor_zero + 0x2d, 250);
    low_card_state.set_u16(actor_zero + 0x2f, 1000);
    auto low_card_session = swd2::BattleSession::create(
        low_card_state, selected->get(), items, false);
    std::array<swd2::PlayerBattleCommand, 4> skip_commands{};
    for (auto& command : skip_commands) {
        command = {swd2::PlayerCommandKind::skip, 0, 0, 0};
    }
    static_cast<void>(low_card_session.play_round(
        skip_commands, abilities, zero_random));
    require((low_card_session.party()[0].status_bits & 0x1000U) != 0,
            "FIG 2bd9 did not set its gameplay low-HP bit at one quarter");

    auto healthy_card_state = flee_rules_state;
    healthy_card_state.set_u16(actor_zero + 8, 0x1000);
    auto healthy_card_session = swd2::BattleSession::create(
        healthy_card_state, selected->get(), items, false);
    static_cast<void>(healthy_card_session.play_round(
        skip_commands, abilities, zero_random));
    require((healthy_card_session.party()[0].status_bits & 0x1000U) == 0,
            "FIG 2bd9 did not clear a stale low-HP bit above one quarter");

    auto fixed_flee_session = swd2::BattleSession::create(
        flee_rules_state, selected->get(), items, false);
    const auto fixed_flee = fixed_flee_session.play_round(
        flee_commands, abilities, zero_random);
    require(fixed_flee.outcome != swd2::BattleOutcome::escaped &&
                std::count_if(fixed_flee.events.begin(), fixed_flee.events.end(),
                              [](const swd2::BattleSessionEvent& event) {
                                  return event.kind ==
                                         swd2::BattleEventKind::escape_failed;
                              }) == 3,
            "FIG ordinary flee did not fail once per actor in a fixed battle");

    auto high_level_flee_state = flee_rules_state;
    high_level_flee_state.set_u16(
        actor_zero + 0x31,
        static_cast<std::uint16_t>(session.monster_definitions()[0].level + 4U));
    auto high_level_flee_session = swd2::BattleSession::create(
        high_level_flee_state, selected->get(), items, true);
    const auto high_level_flee = high_level_flee_session.play_round(
        flee_commands, abilities,
        [](std::uint16_t modulus) {
            if (modulus == 0) throw std::runtime_error("zero flee-test modulus");
            return static_cast<std::uint16_t>(modulus - 1U);
        });
    require(high_level_flee.outcome == swd2::BattleOutcome::escaped,
            "FIG ordinary flee did not grant the four-level advantage shortcut");

    auto countdown_flee_session = swd2::BattleSession::create(
        flee_rules_state, selected->get(), items, true);
    const auto fail_random = [](std::uint16_t modulus) {
        if (modulus == 0) throw std::runtime_error("zero flee-test modulus");
        return static_cast<std::uint16_t>(modulus - 1U);
    };
    const auto countdown_first = countdown_flee_session.play_round(
        flee_commands, abilities, fail_random);
    const auto countdown_second = countdown_flee_session.play_round(
        flee_commands, abilities, fail_random);
    require(countdown_first.outcome == swd2::BattleOutcome::ongoing &&
                countdown_second.outcome == swd2::BattleOutcome::escaped &&
                std::any_of(countdown_second.events.begin(),
                            countdown_second.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                       swd2::BattleEventKind::player_escaped;
                            }),
            "FIG failed-flee party_count*2 countdown did not guarantee escape");

    auto composite_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    composite_state.set_u8(actor_zero + 0x6d, 90);
    composite_state.set_u16(actor_zero + 0x55, 100);
    composite_state.set_u16(actor_zero + 0x5d, 1000);
    auto composite_session = swd2::BattleSession::create(
        composite_state, selected->get(), items);
    auto composite_commands = escape_commands;
    composite_commands[0] = {
        swd2::PlayerCommandKind::ability, 90, 0, 0,
    };
    const auto composite_round = composite_session.play_round(
        composite_commands, abilities, zero_random);
    std::vector<std::uint16_t> nested_damage;
    for (const auto& event : composite_round.events) {
        if (event.kind == swd2::BattleEventKind::player_ability &&
            event.source == 0 && event.ability_id == 90) {
            nested_damage.push_back(event.damage);
        }
    }
    require(composite_session.monsters()[0].hit_points == 20 &&
                composite_session.party()[0].ability_points == 60 &&
                nested_damage == std::vector<std::uint16_t>{40, 60},
            "FIG learned composite ability did not dispatch/pay its two effects");

    // FIG item 230 is the type-10 wrapper around the same ability-90
    // composite.  1138 derives 90 from 230-0x8c before entering 57f2, but the
    // presentation/consumption namespace remains ITEM id 230.
    auto composite_item_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    composite_item_state.set_u16(0x382, 230);
    composite_item_state.set_u16(actor_zero + 0x55, 100);
    composite_item_state.set_u16(actor_zero + 0x57, 100);
    composite_item_state.set_u16(actor_zero + 0x5d, 1000);
    auto composite_item_session = swd2::BattleSession::create(
        composite_item_state, selected->get(), items);
    auto composite_item_commands = escape_commands;
    composite_item_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto composite_item_round = composite_item_session.play_round(
        composite_item_commands, abilities, zero_random);
    std::vector<std::pair<std::uint16_t, std::uint16_t>> item_nested_damage;
    for (const auto& event : composite_item_round.events) {
        if (event.kind == swd2::BattleEventKind::player_ability &&
            event.source == 0 && event.ability_id == 230) {
            item_nested_damage.emplace_back(event.effect_code, event.damage);
        }
    }
    require(composite_item_session.monsters()[0].hit_points == 20 &&
                composite_item_session.party()[0].ability_points == 60 &&
                composite_item_session.inventory()[0] == 0 &&
                item_nested_damage ==
                    std::vector<std::pair<std::uint16_t, std::uint16_t>>{
                        {0x38, 40}, {0x3a, 60}},
            "FIG type-10 composite item did not derive/dispatch/pay/consume exactly");

    // The item target bits, not the derived ability's flags, suppress target
    // selection for item 219.  Its nested 66/69 handlers still apply both
    // tactical self buffs and must retain item id 219 in presentation events.
    auto tactical_item_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    tactical_item_state.set_u16(0x382, 219);
    tactical_item_state.set_u16(actor_zero + 0x55, 100);
    tactical_item_state.set_u16(actor_zero + 0x57, 100);
    tactical_item_state.set_u16(actor_zero + 0x5d, 1000);
    auto tactical_item_session = swd2::BattleSession::create(
        tactical_item_state, selected->get(), items);
    const auto tactical_defense_before =
        tactical_item_session.party()[0].physical_defense;
    auto tactical_item_commands = escape_commands;
    tactical_item_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto tactical_item_round = tactical_item_session.play_round(
        tactical_item_commands, abilities, zero_random);
    std::vector<std::uint16_t> tactical_nested;
    for (const auto& event : tactical_item_round.events) {
        if (event.kind == swd2::BattleEventKind::player_ability &&
            event.source == 0 && event.ability_id == 219) {
            require(!event.target_is_monster,
                    "FIG composite self-buff event was marked as an enemy hit");
            tactical_nested.push_back(event.effect_code);
        }
    }
    require(tactical_item_session.party()[0].physical_defense >
                tactical_defense_before &&
                tactical_item_session.inventory()[0] == 0 &&
                tactical_nested == std::vector<std::uint16_t>{0x66, 0x69},
            "FIG targetless composite item did not dispatch both tactical effects");

    // Direct ITEM selectors enter the same tactical handlers as learned and
    // nested abilities. Cover both the player/self side (62/63/69) and the
    // monster-buff removal side (61) using the shipped records.
    struct DirectTacticalItemCase {
        std::uint16_t item;
        std::uint16_t effect;
        bool targets_monster;
        bool consumed;
    };
    const std::array<DirectTacticalItemCase, 4> direct_tactical_items{{
        {186, 0x69, false, false}, // 辟邪戒指
        {204, 0x62, false, true},  // 代形符
        {211, 0x61, true, true},   // 破法符
        {226, 0x63, false, true},  // 踏風符
    }};
    for (const auto& test : direct_tactical_items) {
        auto direct_state = swd2::SharedState::load(game_root / "SAVE.DA1");
        direct_state.set_u16(0x10, 1);
        direct_state.set_u16(0x382, test.item);
        direct_state.set_u16(actor_zero + 0x55, 1000);
        direct_state.set_u16(actor_zero + 0x57, 1000);
        direct_state.set_u16(actor_zero + 0x2d, 60000);
        direct_state.set_u16(actor_zero + 0x2f, 60000);
        direct_state.set_u16(actor_zero + 0x5d, 1);
        auto direct_session = swd2::BattleSession::create(
            direct_state, selected->get(), items);
        const auto speed_before = direct_session.party()[0].speed;
        auto direct_commands = skip_commands;
        direct_commands[0] = {
            swd2::PlayerCommandKind::item, 0, 0, 0,
        };
        const auto direct_round = direct_session.play_round(
            direct_commands, abilities, zero_random);
        const auto event = std::find_if(
            direct_round.events.begin(), direct_round.events.end(),
            [&](const swd2::BattleSessionEvent& candidate) {
                return candidate.kind ==
                           swd2::BattleEventKind::player_ability &&
                       candidate.source == 0 &&
                       candidate.ability_id == test.item &&
                       candidate.effect_code == test.effect;
            });
        require(event != direct_round.events.end() &&
                    event->target_is_monster == test.targets_monster &&
                    direct_session.inventory()[0] ==
                        (test.consumed ? 0 : test.item),
                "FIG direct tactical item remained an invalid command");
        if (test.item == 226) {
            require(direct_session.party()[0].speed > speed_before,
                    "FIG direct effect-63 item did not apply its speed buff");
        }
    }

    // 1138 subtracts 8ch from every direct 6b item and 57f2 adds it back.
    // Item 54 proves the 16-bit underflow case still resolves its own record,
    // rather than indexing only the learned-ability 140..290 table.
    auto wrapped_composite_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    wrapped_composite_state.set_u16(0x10, 1);
    wrapped_composite_state.set_u16(0x382, 54);
    wrapped_composite_state.set_u16(actor_zero + 0x2d, 60000);
    wrapped_composite_state.set_u16(actor_zero + 0x2f, 60000);
    wrapped_composite_state.set_u16(actor_zero + 0x5d, 1000);
    auto wrapped_composite_session = swd2::BattleSession::create(
        wrapped_composite_state, selected->get(), items);
    auto wrapped_composite_commands = skip_commands;
    wrapped_composite_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto wrapped_composite_round = wrapped_composite_session.play_round(
        wrapped_composite_commands, abilities, zero_random);
    std::vector<std::uint16_t> wrapped_effects;
    for (const auto& event : wrapped_composite_round.events) {
        if (event.kind == swd2::BattleEventKind::player_ability &&
            event.source == 0 && event.ability_id == 54) {
            wrapped_effects.push_back(event.effect_code);
        }
    }
    require(wrapped_composite_session.inventory()[0] == 0 &&
                wrapped_effects == std::vector<std::uint16_t>{0x31, 0x3b},
            "FIG direct composite item below 8ch did not wrap to its own record");

    // Item 248's two nested selectors target a party member. Composite
    // dispatch therefore also has to route 01..30 through the support state
    // adapter instead of assuming every nested selector hits a monster.
    auto support_composite_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    support_composite_state.set_u16(0x10, 3);
    support_composite_state.set_u16(0x382, 248);
    support_composite_state.set_u16(actor_zero + 0x2d, 60000);
    support_composite_state.set_u16(actor_zero + 0x2f, 60000);
    support_composite_state.set_u16(actor_zero + 0x5d, 1000);
    const auto support_target = actor_zero + 2U * 0x9fU;
    support_composite_state.set_u16(support_target + 8, 0x0100);
    support_composite_state.set_u16(support_target + 0x2d, 1000);
    support_composite_state.set_u16(support_target + 0x2f, 1000);
    support_composite_state.set_u16(support_target + 0x55, 1);
    support_composite_state.set_u16(support_target + 0x57, 100);
    auto support_composite_session = swd2::BattleSession::create(
        support_composite_state, selected->get(), items);
    auto support_composite_commands = skip_commands;
    support_composite_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 2, 0,
    };
    const auto support_composite_round = support_composite_session.play_round(
        support_composite_commands, abilities, zero_random);
    std::vector<std::uint16_t> support_effects;
    for (const auto& event : support_composite_round.events) {
        if (event.kind == swd2::BattleEventKind::player_ability &&
            event.source == 0 && event.ability_id == 248) {
            require(!event.target_is_monster && event.target == 2,
                    "FIG support composite item used a monster target");
            support_effects.push_back(event.effect_code);
        }
    }
    require(support_composite_session.inventory()[0] == 0 &&
                support_composite_session.party()[2].ability_points == 100 &&
                support_composite_session.party()[2].status_bits == 0 &&
                support_effects == std::vector<std::uint16_t>{0x08, 0x0f},
            "FIG direct support composite did not restore/cleanse its party target");

    // Execute every shipped, non-summon ITEM record whose +5 battle bit is
    // set. This catches holes between the support, damage/status, tactical,
    // escape, medium and composite adapters instead of relying on a handpicked
    // subset of the 1138 input domain.
    std::size_t shipped_battle_item_count = 0;
    std::vector<std::uint16_t> invalid_shipped_battle_items;
    for (std::uint16_t item_id = 0; item_id < 0x13aU; ++item_id) {
        const auto record_index = static_cast<std::size_t>(item_id) + 2U;
        if (record_index >= items.entry_count()) break;
        const auto record = items.entry(record_index);
        if (record.size() < 9U || (record[5] & 0x02U) == 0U) continue;
        ++shipped_battle_item_count;

        auto exhaustive_state =
            swd2::SharedState::load(game_root / "SAVE.DA1");
        exhaustive_state.set_u16(0x10, 1);
        exhaustive_state.set_u16(0x382, item_id);
        exhaustive_state.set_u16(actor_zero + 8, 0);
        exhaustive_state.set_u16(actor_zero + 0x2d, 60000);
        exhaustive_state.set_u16(actor_zero + 0x2f, 60000);
        exhaustive_state.set_u16(actor_zero + 0x35, 60000);
        exhaustive_state.set_u16(actor_zero + 0x37, 60000);
        exhaustive_state.set_u16(actor_zero + 0x55, 60000);
        exhaustive_state.set_u16(actor_zero + 0x57, 60000);
        exhaustive_state.set_u16(actor_zero + 0x5d, 1000);
        auto exhaustive_session = swd2::BattleSession::create(
            exhaustive_state, selected->get(), items, false);
        auto exhaustive_commands = skip_commands;
        exhaustive_commands[0] = {
            swd2::PlayerCommandKind::item, 0, 0, 0,
        };
        const auto exhaustive_round = exhaustive_session.play_round(
            exhaustive_commands, abilities, zero_random);
        if (std::any_of(
                exhaustive_round.events.begin(), exhaustive_round.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind ==
                           swd2::BattleEventKind::invalid_command;
                })) {
            invalid_shipped_battle_items.push_back(item_id);
        }
    }
    require(shipped_battle_item_count == 124 &&
                invalid_shipped_battle_items.empty(),
            "FIG shipped +5-bit1 battle-item domain still contains an invalid dispatch");

    // Do the same for every player ability reachable from the shipped SAVE
    // slots or ORC growth tables. Some lower-table ids are enemy/captured-ally
    // data (for example 105) despite having no 4000h bit, so treating the
    // entire numeric 1..114 interval as learnable invents an invalid route.
    std::set<std::uint16_t> reachable_player_abilities;
    for (const auto& table : database.growth_tables()) {
        for (const auto& row : table) {
            if (row.fields[8] != 0) {
                reachable_player_abilities.insert(row.fields[8]);
            }
        }
    }
    for (std::size_t party_index = 0; party_index < 4; ++party_index) {
        for (std::size_t slot = 0; slot < 50; ++slot) {
            const auto ability_id = state.u8(
                actor_zero + party_index * 0x9fU + 0x6dU + slot);
            if (ability_id != 0) reachable_player_abilities.insert(ability_id);
        }
    }
    std::size_t shipped_player_ability_count = 0;
    std::vector<std::uint16_t> invalid_shipped_player_abilities;
    for (const auto ability_id : reachable_player_abilities) {
        const auto& ability = abilities.ability(ability_id);
        const auto resource_class =
            static_cast<std::uint8_t>((ability.target_flags >> 8U) & 0x0fU);
        if (ability.effect_code == 0 ||
            (ability.target_flags & 0x4000U) != 0 ||
            resource_class < 1U || resource_class > 5U) {
            continue;
        }
        ++shipped_player_ability_count;

        auto exhaustive_state =
            swd2::SharedState::load(game_root / "SAVE.DA1");
        exhaustive_state.set_u16(0x10, 1);
        exhaustive_state.set_u16(actor_zero + 8, 0);
        exhaustive_state.set_u16(actor_zero + 0x2d, 60000);
        exhaustive_state.set_u16(actor_zero + 0x2f, 60000);
        exhaustive_state.set_u16(actor_zero + 0x35, 60000);
        exhaustive_state.set_u16(actor_zero + 0x37, 60000);
        exhaustive_state.set_u16(actor_zero + 0x55, 60000);
        exhaustive_state.set_u16(actor_zero + 0x57, 60000);
        exhaustive_state.set_u16(actor_zero + 0x5d, 1000);
        for (std::size_t slot = 0; slot < 50; ++slot) {
            exhaustive_state.set_u8(actor_zero + 0x6d + slot, 0);
        }
        exhaustive_state.set_u8(
            actor_zero + 0x6d, static_cast<std::uint8_t>(ability_id));
        for (std::size_t material = 0; material < 5; ++material) {
            exhaustive_state.set_u16(0x3e6 + material * 2U, 20);
        }
        auto exhaustive_session = swd2::BattleSession::create(
            exhaustive_state, selected->get(), items, false);
        auto exhaustive_commands = skip_commands;
        exhaustive_commands[0] = {
            swd2::PlayerCommandKind::ability, ability_id, 0, 0,
        };
        const auto exhaustive_round = exhaustive_session.play_round(
            exhaustive_commands, abilities, zero_random);
        if (std::any_of(
                exhaustive_round.events.begin(), exhaustive_round.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind ==
                           swd2::BattleEventKind::invalid_command;
                })) {
            invalid_shipped_player_abilities.push_back(ability_id);
        }
    }
    require(reachable_player_abilities.size() == 75 &&
                shipped_player_ability_count == 71 &&
                invalid_shipped_player_abilities.empty(),
            "FIG reachable shipped player-ability domain contains an invalid dispatch");

    // 58fa skips a medium-dependent effect body but returns to the ordinary
    // payment/consumption path. The monster is untouched and the exact failed
    // selector remains available to every presentation frontend.
    auto missing_medium_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    missing_medium_state.set_u16(0x10, 1);
    missing_medium_state.set_u8(actor_zero + 0x6d, 54); // effect 32 / MENU AE
    missing_medium_state.set_u16(actor_zero + 0x55, 100);
    missing_medium_state.set_u16(actor_zero + 0x57, 100);
    missing_medium_state.set_u16(actor_zero + 0x2d, 5000);
    missing_medium_state.set_u16(actor_zero + 0x2f, 5000);
    missing_medium_state.set_u16(actor_zero + 0x5d, 1000);
    auto missing_medium_session = swd2::BattleSession::create(
        missing_medium_state, selected->get(), items);
    auto missing_medium_commands = escape_commands;
    missing_medium_commands[0] = {
        swd2::PlayerCommandKind::ability, 54, 0, 0,
    };
    const auto missing_medium_round = missing_medium_session.play_round(
        missing_medium_commands, abilities, zero_random);
    require(missing_medium_session.monsters()[0].hit_points == 120 &&
                missing_medium_session.party()[0].ability_points == 91 &&
                std::any_of(
                    missing_medium_round.events.begin(),
                    missing_medium_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::missing_medium &&
                               event.source == 0 && event.ability_id == 54 &&
                               event.effect_code == 0x32;
                    }),
            "FIG 58fa learned ability did not fail/pay without mediator AE");

    auto missing_medium_item_state = missing_medium_state;
    missing_medium_item_state.set_u16(0x382, 194); // type-10 effect 32
    auto missing_medium_item_session = swd2::BattleSession::create(
        missing_medium_item_state, selected->get(), items);
    auto missing_medium_item_commands = escape_commands;
    missing_medium_item_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto missing_medium_item_round =
        missing_medium_item_session.play_round(
            missing_medium_item_commands, abilities, zero_random);
    require(missing_medium_item_session.monsters()[0].hit_points == 120 &&
                missing_medium_item_session.party()[0].ability_points == 91 &&
                missing_medium_item_session.inventory()[0] == 0 &&
                std::any_of(
                    missing_medium_item_round.events.begin(),
                    missing_medium_item_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::missing_medium &&
                               event.ability_id == 194 &&
                               event.effect_code == 0x32;
                    }),
            "FIG 1138 type-10 medium failure did not pay/consume the item");

    auto missing_composite_state = missing_medium_state;
    missing_composite_state.set_u8(actor_zero + 0x6d, 85);
    missing_composite_state.set_u16(actor_zero + 0x55, 200);
    auto missing_composite_session = swd2::BattleSession::create(
        missing_composite_state, selected->get(), items);
    auto missing_composite_commands = escape_commands;
    missing_composite_commands[0] = {
        swd2::PlayerCommandKind::ability, 85, 0, 0,
    };
    const auto missing_composite_round = missing_composite_session.play_round(
        missing_composite_commands, abilities, zero_random);
    std::vector<std::uint16_t> missing_nested_effects;
    for (const auto& event : missing_composite_round.events) {
        if (event.kind == swd2::BattleEventKind::missing_medium &&
            event.source == 0 && event.ability_id == 85) {
            missing_nested_effects.push_back(event.effect_code);
        }
    }
    require(missing_composite_session.monsters()[0].hit_points == 120 &&
                missing_composite_session.party()[0].ability_points == 100 &&
                missing_nested_effects ==
                    std::vector<std::uint16_t>{0x43, 0x36},
            "FIG 57f2 did not continue/pay two failed nested medium effects");

    // Encounter data offset 1252 contains monster 338, whose generic ability
    // 127 requests medium AE and has exactly three 50-AP casts in its 150 pool.
    // 23b1 refunds the first prepaid cast when it summons the missing medium.
    const auto medium_encounter = std::find_if(
        database.encounters().begin(), database.encounters().end(),
        [](const swd2::BattleEncounter& encounter) {
            return encounter.data_offset == 1252;
        });
    require(medium_encounter != database.encounters().end(),
            "ORC medium-summon regression encounter 1252 is absent");
    auto medium_monster_state = missing_medium_state;
    medium_monster_state.set_u8(actor_zero + 0x6d, 0);
    medium_monster_state.set_u16(actor_zero + 0x2d, 60000);
    medium_monster_state.set_u16(actor_zero + 0x2f, 60000);
    medium_monster_state.set_u16(actor_zero + 0x0e, 1000);
    auto medium_monster_session = swd2::BattleSession::create(
        medium_monster_state, *medium_encounter, items);
    const auto medium_summon_round = medium_monster_session.play_round(
        skip_commands, abilities, zero_random);
    require(medium_monster_session.battle_media() ==
                    std::array<bool, 3>{true, false, false} &&
                std::any_of(
                    medium_summon_round.events.begin(),
                    medium_summon_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::medium_summoned &&
                               event.source_is_monster && event.source == 0 &&
                               event.target == 0 && event.ability_id == 127 &&
                               event.effect_code == 0x32;
                    }),
            "FIG 23b1 did not summon persistent MENU AE for generic ability 127");
    std::size_t paid_medium_casts = 0;
    bool paid_medium_casts_use_generic_path = true;
    for (std::size_t round = 0; round < 4; ++round) {
        const auto resolved = medium_monster_session.play_round(
            skip_commands, abilities, zero_random);
        paid_medium_casts += static_cast<std::size_t>(std::count_if(
            resolved.events.begin(), resolved.events.end(),
            [](const swd2::BattleSessionEvent& event) {
                return event.kind == swd2::BattleEventKind::monster_ability &&
                       event.source == 0 && event.ability_id == 127;
            }));
        paid_medium_casts_use_generic_path =
            paid_medium_casts_use_generic_path &&
            std::all_of(
                resolved.events.begin(), resolved.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind !=
                               swd2::BattleEventKind::monster_ability ||
                           event.source != 0 || event.ability_id != 127 ||
                           event.monster_generic_path;
                });
    }
    require(paid_medium_casts == 3 && paid_medium_casts_use_generic_path &&
                medium_monster_session.battle_media()[0],
            "FIG 23b1 generic-path marker/refunded prepaid casts differ");

    // Captured ally 374 selects generic ability 12 with zero_random.  Its
    // effect 57 itself needs no mediator, but the ability record's low-byte
    // 40h flag makes 1048 install MENU AF before dispatch.  This is distinct
    // from both enemy 23b1 (which refunds AP) and player 58fa (which displays
    // the missing-medium modal).
    auto ally_medium_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    ally_medium_state.set_u16(0x10, 1);
    for (std::size_t slot = 0; slot < 50; ++slot) {
        ally_medium_state.set_u16(0x382 + slot * 2U, 0);
    }
    ally_medium_state.set_u16(0x382, 374);
    ally_medium_state.set_u16(actor_zero + 8, 0);
    ally_medium_state.set_u16(actor_zero + 0x0e, 60000);
    ally_medium_state.set_u16(actor_zero + 0x2d, 60000);
    ally_medium_state.set_u16(actor_zero + 0x2f, 60000);
    ally_medium_state.set_u16(actor_zero + 0x35, 1000);
    ally_medium_state.set_u16(actor_zero + 0x37, 1000);
    ally_medium_state.set_u16(actor_zero + 0x5d, 1000);
    auto ally_medium_session = swd2::BattleSession::create(
        ally_medium_state, selected->get(), items);
    auto ally_summon_commands = skip_commands;
    ally_summon_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    static_cast<void>(ally_medium_session.play_round(
        ally_summon_commands, abilities, zero_random));
    require(ally_medium_session.summoned_allies().size() == 1,
            "FIG captured-ally mediator regression summon failed");
    const auto ally_initial_points =
        ally_medium_session.summoned_allies()[0].ai.ability_points;
    const auto ally_medium_round = ally_medium_session.play_round(
        skip_commands, abilities, zero_random);
    require(ally_medium_session.battle_media() ==
                    std::array<bool, 3>{false, true, false} &&
                ally_medium_session.summoned_allies()[0].ai.ability_points ==
                    static_cast<std::uint16_t>(
                        ally_initial_points - abilities.ability(12).cost) &&
                std::any_of(
                    ally_medium_round.events.begin(),
                    ally_medium_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::medium_summoned &&
                               event.source_is_summoned_ally &&
                               !event.source_is_monster && event.source == 0 &&
                               event.target == 1 && event.ability_id == 12 &&
                               event.effect_code == 0x57;
                    }) &&
                std::none_of(
                    ally_medium_round.events.begin(),
                    ally_medium_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::missing_medium &&
                               event.source_is_summoned_ally;
                    }),
            "FIG 1048 captured ally did not pay and summon its flagged mediator");
    const auto ally_medium_success = ally_medium_session.play_round(
        skip_commands, abilities, zero_random);
    require(std::any_of(
                ally_medium_success.events.begin(),
                ally_medium_success.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind == swd2::BattleEventKind::ally_ability &&
                           event.source == 0 && event.ability_id == 12 &&
                           event.effect_code == 0x57;
                }),
            "FIG 1048 did not dispatch the captured-ally effect after mediator install");

    auto medium_success_state = medium_monster_state;
    medium_success_state.set_u8(actor_zero + 0x6d, 54);
    medium_success_state.set_u16(actor_zero + 0x55, 100);
    medium_success_state.set_u16(actor_zero + 0x5d, 1000);
    auto medium_success_session = swd2::BattleSession::create(
        medium_success_state, *medium_encounter, items);
    static_cast<void>(medium_success_session.play_round(
        skip_commands, abilities, zero_random));
    auto medium_success_commands = skip_commands;
    medium_success_commands[0] = {
        swd2::PlayerCommandKind::ability, 54, 0, 0,
    };
    const auto medium_success_round = medium_success_session.play_round(
        medium_success_commands, abilities, zero_random);
    require(medium_success_session.party()[0].ability_points == 91 &&
                std::any_of(
                    medium_success_round.events.begin(),
                    medium_success_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::player_ability &&
                               event.source == 0 && event.ability_id == 54 &&
                               event.effect_code == 0x32;
                    }) &&
                std::none_of(
                    medium_success_round.events.begin(),
                    medium_success_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::missing_medium &&
                               event.source == 0;
                    }),
            "FIG 58fa rejected effect 32 after monster-installed medium AE");

    // Effect 5e writes DS:33fd only after its archive animation. Preserve the
    // rolled duration in the event so 2deb can add the icon on the following
    // clean composition, then silently remove it when 0af5 reaches zero.
    auto status_icon_state = missing_medium_state;
    status_icon_state.set_u8(actor_zero + 0x6d, 6);
    status_icon_state.set_u16(actor_zero + 0x55, 100);
    status_icon_state.set_u16(actor_zero + 0x5d, 0xffff);
    auto status_icon_session = swd2::BattleSession::create(
        status_icon_state, *medium_encounter, items);
    auto status_icon_commands = skip_commands;
    status_icon_commands[0] = {
        swd2::PlayerCommandKind::ability, 6, 0, 0,
    };
    const auto status_icon_round = status_icon_session.play_round(
        status_icon_commands, abilities, zero_random);
    require(std::any_of(
                status_icon_round.events.begin(), status_icon_round.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind ==
                               swd2::BattleEventKind::player_ability &&
                           event.ability_id == 6 &&
                           event.effect_code == 0x5e &&
                           event.status_duration == 2;
                }) &&
                status_icon_session.monsters()[0].status_turns[0] == 1,
            "FIG 5a91 status duration was not retained for 2deb presentation");
    const auto status_icon_expiry = status_icon_session.play_round(
        skip_commands, abilities, zero_random);
    require(std::any_of(
                status_icon_expiry.events.begin(),
                status_icon_expiry.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind == swd2::BattleEventKind::status_expired &&
                           event.target_is_monster &&
                           event.expired_monster_status_mask == 0x01U;
                }) &&
                status_icon_session.monsters()[0].status_turns[0] == 0,
            "FIG 0af5 status expiry did not remove the persistent 2deb icon");

    auto periodic_zero_state = status_icon_state;
    periodic_zero_state.set_u8(actor_zero + 0x6d, 48); // effect 60 / slot two
    for (std::size_t slot = 0; slot < 5; ++slot) {
        periodic_zero_state.set_u16(0x3e6 + slot * 2U, 1);
    }
    const auto periodic_encounter = std::find_if(
        database.encounters().begin(), database.encounters().end(),
        [](const swd2::BattleEncounter& encounter) {
            return encounter.data_offset == 1328;
        });
    require(periodic_encounter != database.encounters().end(),
            "ORC periodic-status regression encounter 1328 is absent");
    auto periodic_zero_session = swd2::BattleSession::create(
        periodic_zero_state, *periodic_encounter, items);
    auto periodic_zero_commands = skip_commands;
    periodic_zero_commands[0] = {
        swd2::PlayerCommandKind::ability, 48, 0, 0,
    };
    const auto periodic_zero_round = periodic_zero_session.play_round(
        periodic_zero_commands, abilities, zero_random);
    require(std::any_of(
                periodic_zero_round.events.begin(),
                periodic_zero_round.events.end(),
                [](const swd2::BattleSessionEvent& event) {
                    return event.kind == swd2::BattleEventKind::status_damage &&
                           event.target_is_monster && event.damage == 0;
                }),
            "FIG 0b7b zero periodic roll did not emit its literal 144e event");

    auto disabled_state = composite_state;
    disabled_state.set_u16(actor_zero + 8, 0x0002);
    auto disabled_session = swd2::BattleSession::create(
        disabled_state, selected->get(), items);
    const auto disabled_round = disabled_session.play_round(
        composite_commands, abilities, zero_random);
    require(disabled_session.monsters()[0].hit_points == 120 &&
                disabled_session.party()[0].ability_points == 100 &&
                std::any_of(disabled_round.events.begin(),
                            disabled_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return !event.source_is_monster &&
                                       event.source == 0 &&
                                       event.kind == swd2::BattleEventKind::skipped;
                            }),
            "FIG player incapacitation mask 0x2c7e did not suppress the action");

    auto capture_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    capture_state.set_u16(actor_zero + 0x31, 30);
    capture_state.set_u16(actor_zero + 0x5d, 1000);
    capture_state.set_u16(0x3e4, 0);
    auto capture_session = swd2::BattleSession::create(
        capture_state, selected->get(), items, true);
    auto capture_commands = escape_commands;
    capture_commands[0] = {
        swd2::PlayerCommandKind::capture, 0, 0, 0,
    };
    const auto capture_round = capture_session.play_round(
        capture_commands, abilities, zero_random);
    require(capture_round.outcome == swd2::BattleOutcome::victory &&
                capture_session.inventory().back() == 500 &&
                capture_session.monsters()[0].hit_points == 0 &&
                std::any_of(capture_round.events.begin(),
                            capture_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                           swd2::BattleEventKind::monster_captured &&
                                       event.defeated;
                            }),
            "FIG random-battle capture level/empty-slot rules were not reproduced");
    auto occupied_capture_state = swd2::SharedState::load(
        game_root / "SAVE.DA1");
    occupied_capture_state.set_u16(actor_zero + 0x31, 30);
    occupied_capture_state.set_u16(actor_zero + 0x35, 53);
    occupied_capture_state.set_u16(actor_zero + 0x5d, 1000);
    occupied_capture_state.set_u16(0x382, 319);
    occupied_capture_state.set_u16(0x3e4, 0);
    auto occupied_capture_session = swd2::BattleSession::create(
        occupied_capture_state, selected->get(), items, true);
    const auto occupied_capture_random = [](std::uint16_t modulus) {
        if (modulus == 0) {
            throw std::runtime_error("zero occupied-capture-test modulus");
        }
        return static_cast<std::uint16_t>(modulus == 1 ? 0 : 1);
    };
    auto summon_before_capture = escape_commands;
    summon_before_capture[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    static_cast<void>(occupied_capture_session.play_round(
        summon_before_capture, abilities, occupied_capture_random));
    require(!occupied_capture_session.summoned_allies().empty(),
            "FIG capture regression setup failed to summon the first ally");
    const auto occupied_capture_round = occupied_capture_session.play_round(
        capture_commands, abilities, occupied_capture_random);
    require(occupied_capture_session.inventory().back() == 0 &&
                std::any_of(occupied_capture_round.events.begin(),
                            occupied_capture_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                       swd2::BattleEventKind::capture_failed;
                            }),
            "FIG 0e10 capture did not reject a nonzero summoned-ally slot");
    capture_session.store(capture_state);
    require(capture_state.u16(0x3e4) == 500,
            "FIG captured monster definition did not persist in inventory slot 49");

    // ITEM ids >=314 are captured-monster summons rather than ordinary battle
    // items. The ally is pending for the current initiative roll, joins the
    // next round, spends its own AP, and is returned after battle.
    auto summon_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    summon_state.set_u16(0x382, 319);
    summon_state.set_u16(actor_zero + 0x35, 53);
    summon_state.set_u16(actor_zero + 0x5d, 1000);
    for (std::size_t index = 0; index < 3; ++index) {
        const auto base = actor_zero + index * 0x9f;
        summon_state.set_u16(base + 0x2d, 1000);
        summon_state.set_u16(base + 0x2f, 1000);
    }
    auto summon_commands = escape_commands;
    summon_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    const auto one_random = [](std::uint16_t modulus) {
        if (modulus == 0) throw std::runtime_error("zero summon-test modulus");
        return static_cast<std::uint16_t>(modulus == 1 ? 0 : 1);
    };
    auto summon_boundary_state = summon_state;
    summon_boundary_state.set_u16(actor_zero + 0x35, 52);
    auto summon_boundary_session = swd2::BattleSession::create(
        summon_boundary_state, selected->get(), items);
    const auto summon_boundary_round = summon_boundary_session.play_round(
        summon_commands, abilities, one_random);
    require(summon_boundary_session.summoned_allies().empty() &&
                summon_boundary_session.inventory()[0] == 319 &&
                std::any_of(summon_boundary_round.events.begin(),
                            summon_boundary_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                       swd2::BattleEventKind::invalid_command;
                            }),
            "FIG summon selector accepted actor +35 equal to level*2 despite JBE");
    auto summon_session = swd2::BattleSession::create(
        summon_state, selected->get(), items);
    const auto summon_round = summon_session.play_round(
        summon_commands, abilities, one_random);
    require(summon_session.summoned_allies().size() == 1 &&
                summon_session.summoned_allies()[0].item_id == 319 &&
                summon_session.inventory()[0] == 0 &&
                summon_session.party()[0].secondary_points == 1 &&
                std::any_of(summon_round.events.begin(), summon_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                       swd2::BattleEventKind::ally_summoned;
                            }),
            "FIG captured-monster item did not create/pay for a pending ally");
    std::array<swd2::PlayerBattleCommand, 4> summon_wait{};
    for (auto& command : summon_wait) {
        command = {swd2::PlayerCommandKind::skip, 0, 0, 0};
    }
    const auto ally_round = summon_session.play_round(
        summon_wait, abilities, one_random);
    require(summon_session.monsters()[0].hit_points == 59 &&
                summon_session.summoned_allies()[0].ai.ability_points == 138 &&
                std::any_of(ally_round.events.begin(), ally_round.events.end(),
                            [](const swd2::BattleSessionEvent& event) {
                                return event.kind ==
                                           swd2::BattleEventKind::ally_ability &&
                                       event.ability_id == 77 && event.damage == 61;
                            }),
            "FIG summoned ally did not join next-round initiative/use its ability");
    summon_session.store(summon_state);
    require(summon_state.u16(0x382) == 319,
            "FIG surviving summoned ally was not returned to inventory");

    // FIG 5d24 does not disable captured-monster items when both packed ally
    // slots are occupied. It asks which slot to replace, installs the newcomer
    // in that slot, and writes the displaced item back into the inventory slot
    // from which the newcomer came.
    auto replace_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    replace_state.set_u16(0x10, 1);
    replace_state.set_u16(0x382 + 0U * 2U, 319);
    replace_state.set_u16(0x382 + 1U * 2U, 320);
    replace_state.set_u16(0x382 + 2U * 2U, 321);
    replace_state.set_u16(actor_zero + 0x35, 2000);
    replace_state.set_u16(actor_zero + 0x2d, 60000);
    replace_state.set_u16(actor_zero + 0x2f, 60000);
    replace_state.set_u16(actor_zero + 0x5d, 1000);
    auto replace_session = swd2::BattleSession::create(
        replace_state, *medium_encounter, items);
    auto replace_commands = skip_commands;
    replace_commands[0] = {
        swd2::PlayerCommandKind::item, 0, 0, 0,
    };
    static_cast<void>(replace_session.play_round(
        replace_commands, abilities, one_random));
    replace_commands[0].item_slot = 1;
    static_cast<void>(replace_session.play_round(
        replace_commands, abilities, one_random));
    require(replace_session.summoned_allies().size() == 2 &&
                replace_session.summoned_allies()[0].item_id == 319 &&
                replace_session.summoned_allies()[1].item_id == 320 &&
                replace_session.inventory()[0] == 0 &&
                replace_session.inventory()[1] == 0 &&
                replace_session.inventory()[2] == 321,
            "FIG full-slot summon regression setup did not pack two allies");

    swd2::BattleCommandMenu replace_cancel_menu(
        replace_session, abilities, items);
    replace_cancel_menu.input(swd2::InputAction::right);
    replace_cancel_menu.input(swd2::InputAction::confirm);
    replace_cancel_menu.input(swd2::InputAction::down);
    replace_cancel_menu.input(swd2::InputAction::down);
    replace_cancel_menu.input(swd2::InputAction::confirm);
    require(replace_cancel_menu.page() ==
                swd2::BattleCommandMenuPage::summon_replace &&
                replace_cancel_menu.entries().size() == 2,
            "FIG 5d24 full-slot summon did not enter the two-slot selector");
    replace_cancel_menu.input(swd2::InputAction::cancel);
    require(replace_cancel_menu.page() == swd2::BattleCommandMenuPage::items &&
                replace_cancel_menu.cursor() == 2,
            "FIG 5d24 cancel did not restore the selected inventory cursor");

    swd2::BattleCommandMenu replace_menu(replace_session, abilities, items);
    replace_menu.input(swd2::InputAction::right);
    replace_menu.input(swd2::InputAction::confirm);
    replace_menu.input(swd2::InputAction::down);
    replace_menu.input(swd2::InputAction::down);
    replace_menu.input(swd2::InputAction::confirm);
    replace_menu.input(swd2::InputAction::right);
    replace_menu.input(swd2::InputAction::confirm);
    require(replace_menu.complete() &&
                replace_menu.commands()[0].kind ==
                    swd2::PlayerCommandKind::item &&
                replace_menu.commands()[0].item_slot == 2 &&
                replace_menu.commands()[0].target == 1,
            "FIG 5d24 replacement choice was not retained in the command");
    const auto resource_before_replace =
        replace_session.party()[0].secondary_points;
    const auto replace_round = replace_session.play_round(
        replace_menu.commands(), abilities, one_random);
    require(replace_session.summoned_allies().size() == 2 &&
                replace_session.summoned_allies()[0].item_id == 319 &&
                replace_session.summoned_allies()[1].item_id == 321 &&
                replace_session.inventory()[2] == 320 &&
                replace_session.party()[0].secondary_points ==
                    resource_before_replace - 72 &&
                std::any_of(
                    replace_round.events.begin(), replace_round.events.end(),
                    [](const swd2::BattleSessionEvent& event) {
                        return event.kind ==
                                   swd2::BattleEventKind::ally_summoned &&
                               event.target == 1 && event.ability_id == 321;
                    }),
            "FIG 5d24 did not replace slot one/return the displaced ally item");
}

void test_battle_ai(const std::filesystem::path& game_root) {
    const auto sequence = [](std::vector<std::uint16_t> values) {
        return [values = std::move(values), cursor = std::size_t{}]
               (std::uint16_t modulus) mutable {
            if (cursor >= values.size() || values[cursor] >= modulus) {
                throw std::runtime_error("invalid deterministic AI-random sequence");
            }
            return values[cursor++];
        };
    };
    const auto abilities = swd2::BattleAbilityDatabase::load(game_root / "FIG.EXE");
    const std::array<bool, 3> living = {true, false, true};

    swd2::MonsterAiState generic;
    generic.hit_points = generic.maximum_hit_points = 120;
    generic.level = 22;
    generic.primary_chance = 1;
    generic.generic_ability = 10;
    generic.secondary_chance = 0;
    generic.ability_points = 55;
    const auto generic_decision = swd2::choose_monster_action(
        generic, living, abilities, sequence({1, 2, 0, 1, 4}));
    require(generic_decision.action == swd2::MonsterAiAction::generic_ability &&
                generic_decision.target == 2 && generic_decision.ability_id == 10 &&
                generic_decision.power == 105 && generic.ability_points == 0,
            "FIG enemy target retry/generic-ability selection was not reproduced");

    swd2::MonsterAiState special;
    special.hit_points = special.maximum_hit_points = 120;
    special.level = 22;
    special.primary_chance = 9;
    special.secondary_chance = 9;
    special.special_ability_a = 54;
    special.ability_points = 100;
    const auto special_decision = swd2::choose_monster_action(
        special, living, abilities, sequence({0, 0, 0, 1, 2}));
    require(special_decision.action == swd2::MonsterAiAction::special_ability &&
                special_decision.ability_id == 54 && special_decision.power == 42 &&
                special.ability_points == 91,
            "FIG enemy special-ability selection was not reproduced");

    swd2::MonsterAiState healer;
    healer.hit_points = 20;
    healer.maximum_hit_points = 100;
    healer.level = 22;
    healer.healing_ability = 6;
    healer.ability_points = 100;
    const auto heal = swd2::choose_monster_action(
        healer, living, abilities, sequence({3}));
    require(heal.action == swd2::MonsterAiAction::heal_self && heal.power == 9 &&
                healer.hit_points == 29 && healer.ability_points == 55,
            "FIG enemy quarter-HP self-heal was not reproduced");

    swd2::MonsterAiState basic;
    basic.hit_points = basic.maximum_hit_points = 100;
    basic.level = 10;
    basic.primary_chance = 1;
    const auto attack = swd2::choose_monster_action(
        basic, living, abilities, sequence({0, 9}));
    require(attack.action == swd2::MonsterAiAction::basic_attack && attack.target == 0,
            "FIG enemy basic-attack fallback was not reproduced");

    swd2::MonsterAiState forced_flee;
    forced_flee.hit_points = forced_flee.maximum_hit_points = 100;
    forced_flee.level = 10;
    forced_flee.ai_type = 2;
    const auto type_two = swd2::choose_monster_action(
        forced_flee, living, abilities, sequence({}), true, 10);
    require(type_two.action == swd2::MonsterAiAction::flee,
            "FIG random-encounter AI type two did not flee immediately");

    swd2::MonsterAiState intimidated;
    intimidated.hit_points = intimidated.maximum_hit_points = 100;
    intimidated.level = 10;
    const auto intimidation = swd2::choose_monster_action(
        intimidated, living, abilities, sequence({2}), true, 17);
    require(intimidation.action == swd2::MonsterAiAction::flee,
            "FIG seven-level random-encounter intimidation roll was not reproduced");
    auto intimidated_failure = intimidated;
    const auto intimidation_failure = swd2::choose_monster_action(
        intimidated_failure, living, abilities, sequence({1}), true, 17);
    require(intimidation_failure.action ==
                swd2::MonsterAiAction::flee_failed,
            "FIG intimidation escape-failure branch was collapsed into skip");

    swd2::MonsterAiState desperate;
    desperate.hit_points = 20;
    desperate.maximum_hit_points = 100;
    desperate.level = 10;
    desperate.ai_type = 1;
    const auto desperation = swd2::choose_monster_action(
        desperate, living, abilities, sequence({0}), true, 10);
    require(desperation.action == swd2::MonsterAiAction::flee,
            "FIG quarter-HP AI type-one desperation flee was not reproduced");
    auto desperate_failure = desperate;
    const auto desperation_failure = swd2::choose_monster_action(
        desperate_failure, living, abilities, sequence({2}), true, 10);
    require(desperation_failure.action ==
                swd2::MonsterAiAction::flee_failed,
            "FIG quarter-HP escape-failure branch was collapsed into skip");

    swd2::MonsterAiState ally;
    ally.hit_points = ally.maximum_hit_points = 420;
    ally.level = 26;
    ally.primary_chance = 5;
    ally.secondary_chance = 0;
    ally.generic_ability = 77;
    ally.ability_points = 160;
    const auto ally_spell = swd2::choose_summoned_ally_action(
        ally, living, abilities, sequence({0, 2, 5, 1, 3}), true);
    require(ally_spell.action == swd2::MonsterAiAction::generic_ability &&
                ally_spell.target == 2 && ally_spell.ability_id == 77 &&
                ally_spell.power == 63 && ally.ability_points == 138,
            "FIG captured-ally target/ability/AP decision tree was not reproduced");

    const auto ally_flee = swd2::choose_summoned_ally_action(
        ally, living, abilities, sequence({7}), true);
    require(ally_flee.action == swd2::MonsterAiAction::flee,
            "FIG captured ally random-seven leave branch was not reproduced");
}

void test_legacy_event_resources(const std::filesystem::path& game_root) {
    const auto archive = swd2::ScriptArchive::load(game_root / "CHNA1.EXE");
    require(archive.entry_count() == 231, "unexpected CHNA1 script directory size");
    require(archive.sentinel_offset() == 0x8c4a, "unexpected CHNA1 script sentinel");
    const auto name = archive.entry(1);
    require(std::string(name.begin(), name.end()) == std::string("CHNA1.DSK\0", 10),
            "CHNA1 script did not reference its glyph font");
    const auto first_dialogue = swd2::decode_event_record(archive.entry(10));
    require(first_dialogue.commands.size() == 1 && first_dialogue.commands[0].opcode == 0,
            "CHNA1 first event was not decoded as dialogue");
    require(first_dialogue.commands[0].text.size() == 12,
            "CHNA1 first dialogue byte count is unexpected");
    const auto first_page = swd2::render_dialogue_page(
        swd2::LegacyFont::load(game_root / "CHNA1.DSK"), first_dialogue.commands[0].text);
    require(std::count_if(first_page.pixels.begin(), first_page.pixels.end(),
                          [](std::uint8_t pixel) { return pixel != 0; }) > 100,
            "CHNA1 dialogue did not render through its embedded font");
    require(first_page.cursor_x == 96U && first_page.cursor_y == 0U &&
                !first_page.page_break && !first_page.has_more,
            "CHNA1 dialogue did not retain its final 49d0 cursor");
    require(first_dialogue.consumed_bytes == archive.entry(10).size(),
            "CHNA1 first event was not consumed exactly");
    const auto scripted_dialogue = swd2::decode_event_record(archive.entry(15));
    require(scripted_dialogue.commands.size() == 4 &&
                scripted_dialogue.commands[0].opcode == 0 &&
                scripted_dialogue.commands[1].opcode == 1 &&
                scripted_dialogue.commands[2].opcode == 3 &&
                scripted_dialogue.commands[3].opcode == 34,
            "CHNA1 mixed dialogue/event command stream was not decoded");

    // RPG 53b1 copies through the first word-aligned ffff instead of using
    // the next directory pointer as an end bound. Several shipped, reachable
    // events rely on that fall-through or put unreachable padding after a
    // terminal module transition.
    const auto chapter_zero = swd2::ScriptArchive::load(game_root / "CHNA0.EXE");
    const auto adjacent_dialogues =
        swd2::decode_event_record(chapter_zero.event_stream(344));
    require(chapter_zero.entry(344).size() == 152 &&
                chapter_zero.event_stream(344).size() == 338 &&
                adjacent_dialogues.consumed_bytes == 338 &&
                adjacent_dialogues.commands.size() == 4 &&
                adjacent_dialogues.commands[0].opcode == 0 &&
                adjacent_dialogues.commands[1].opcode == 0 &&
                adjacent_dialogues.commands[2].opcode == 2 &&
                adjacent_dialogues.commands[3].opcode == 3,
            "CHNA0 adjacent event fall-through was cut at a directory pointer");
    const auto chapter_zero_terminal =
        swd2::decode_event_record(chapter_zero.event_stream(369));
    require(chapter_zero_terminal.commands.size() == 296 &&
                chapter_zero_terminal.commands.back().opcode == 28 &&
                chapter_zero_terminal.consumed_bytes == 1444,
            "CHNA0 terminal battle tail was decoded as unrelated trailing data");

    const auto battle_tail = swd2::decode_event_record(archive.event_stream(132));
    const auto ending_tail = swd2::decode_event_record(archive.event_stream(228));
    require(battle_tail.commands.size() == 3 &&
                battle_tail.commands.back().opcode == 28 &&
                battle_tail.consumed_bytes == 70 &&
                ending_tail.commands.size() == 228 &&
                ending_tail.commands.back().opcode == 52 &&
                ending_tail.consumed_bytes == 2276,
            "CHNA1 terminal event tails were not bounded by native control flow");
    const auto chapter_five = swd2::ScriptArchive::load(game_root / "CHNA5.EXE");
    const auto chapter_five_tail =
        swd2::decode_event_record(chapter_five.event_stream(161));
    require(chapter_five_tail.commands.size() == 9 &&
                chapter_five_tail.commands.back().opcode == 58 &&
                chapter_five_tail.consumed_bytes == 90,
            "CHNA5 battle transition did not terminate its event stream");
    const auto chapter_six = swd2::ScriptArchive::load(game_root / "CHNA6.EXE");
    const auto empty_tail = swd2::decode_event_record(chapter_six.event_stream(24));
    require(empty_tail.commands.size() == 5 && empty_tail.commands.back().opcode == 12 &&
                empty_tail.consumed_bytes == 28,
            "CHNA6 empty trailing event slot was treated as dialogue");
    const auto recovered_interaction =
        swd2::decode_event_record(chapter_six.event_stream(45));
    require(recovered_interaction.commands.size() == 5 &&
                recovered_interaction.commands[0].opcode == 2 &&
                recovered_interaction.commands[1].opcode == 3 &&
                recovered_interaction.commands[2].opcode == 0 &&
                recovered_interaction.commands[3].opcode == 32 &&
                recovered_interaction.commands[3].arguments ==
                    std::vector<std::uint16_t>({3}) &&
                recovered_interaction.commands[4].opcode == 0 &&
                recovered_interaction.consumed_bytes ==
                    chapter_six.event_stream(45).size(),
            "CHNA6 packed west-step story command was not recovered");

    // Start with every MAPA entity event, then conservatively follow static
    // branches, current-area rewrites and opcode 34 writes into MAPZ field 9.
    // The latter cross CHNA boundaries and install the released opcode 49/51
    // scenes, so a separate per-archive walk would silently miss them.
    const auto world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    std::map<std::string, std::set<std::uint16_t>> roots;
    for (const auto& location : world.locations()) {
        for (const auto target : location.area.entity_fields[9]) {
            if (target != 0) roots[location.area.event_archive_path].insert(target);
        }
    }

    struct Coverage {
        explicit Coverage(swd2::ScriptArchive loaded)
            : archive(std::move(loaded)) {}
        swd2::ScriptArchive archive;
        std::set<std::uint16_t> visited;
        std::size_t commands{};
    };
    struct TargetState {
        std::string archive_name;
        std::uint16_t target{};
        std::string active_area_archive;
    };
    std::queue<TargetState> pending;
    std::size_t root_count = 0;
    for (const auto& [name, archive_roots] : roots) {
        root_count += archive_roots.size();
        for (const auto target : archive_roots) {
            pending.push({name, target, name});
        }
    }
    std::map<std::string, std::unique_ptr<Coverage>> coverage;
    const auto coverage_for = [&](const std::string& name) -> Coverage& {
        auto found = coverage.find(name);
        if (found == coverage.end()) {
            found = coverage.emplace(
                name, std::make_unique<Coverage>(
                          swd2::ScriptArchive::load(game_root / name))).first;
        }
        return *found->second;
    };
    std::set<std::uint16_t> reachable_opcodes;
    std::set<std::tuple<std::string, std::uint16_t, std::string>> visited_states;
    std::size_t inert_odd_targets = 0;
    std::size_t map_mutations = 0;
    std::size_t event_pointer_mutations = 0;
    while (!pending.empty()) {
        auto target_state = std::move(pending.front());
        pending.pop();
        if (!visited_states.emplace(target_state.archive_name,
                                    target_state.target,
                                    target_state.active_area_archive).second) {
            continue;
        }
        const auto& name = target_state.archive_name;
        const auto target = target_state.target;
        auto& current = coverage_for(name);
        const auto first_record_visit = current.visited.insert(target).second;
        require((target & 1U) == 0 && target / 2 < current.archive.entry_count(),
                "reachable event target is outside its CHNA directory");
        const auto record = swd2::decode_event_record(
            current.archive.event_stream(target / 2));
        if (first_record_visit) current.commands += record.commands.size();
        auto active_area_archive = target_state.active_area_archive;
        for (const auto& command : record.commands) {
            if (first_record_visit) reachable_opcodes.insert(command.opcode);
            const auto branch = [&](std::uint16_t next) {
                if ((next & 1U) != 0) {
                    if (first_record_visit) ++inert_odd_targets;
                } else {
                    pending.push({name, next, active_area_archive});
                }
            };
            if (command.opcode == 2 && !command.arguments.empty()) {
                branch(command.arguments[0]);
                if ((command.arguments[0] & 1U) == 0) {
                    pending.push({active_area_archive, command.arguments[0],
                                  active_area_archive});
                }
            } else if (command.opcode == 3 && command.arguments.size() >= 2 &&
                       command.arguments[0] == 9) {
                pending.push({active_area_archive, command.arguments[1],
                              active_area_archive});
            } else if ((command.opcode == 4 || command.opcode == 15 ||
                        command.opcode == 21 || command.opcode == 40) &&
                       command.arguments.size() >= 2) {
                branch(command.arguments[1]);
            } else if (command.opcode == 13 && !command.arguments.empty()) {
                branch(command.arguments[0]);
            }

            if (command.opcode == 37 && !command.arguments.empty() &&
                (command.arguments[0] & 0x8000U) == 0U) {
                active_area_archive = world.location_at_directory_offset(
                    static_cast<std::uint16_t>(command.arguments[0] & 0x1fffU))
                                          .area.event_archive_path;
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
                require(cursor + 3U <= command.arguments.size(),
                        "reachable opcode 34 mutation is truncated");
                const auto field = command.arguments[cursor++];
                const auto byte_offset = static_cast<std::int16_t>(
                    command.arguments[cursor++]);
                const auto operation = command.arguments[cursor++];
                const auto additive = operation == 0x4144U;
                auto value = operation;
                if (additive) {
                    require(cursor < command.arguments.size(),
                            "reachable additive opcode 34 mutation is truncated");
                    value = command.arguments[cursor++];
                }
                if (first_record_visit) ++map_mutations;
                const auto& destination =
                    world.location_at_directory_offset(location_offset);
                const auto count = static_cast<std::int64_t>(
                    destination.area.entity_count());
                const auto relative = static_cast<std::int64_t>(6) +
                    static_cast<std::int64_t>(field) * count * 2 + byte_offset;
                const auto event_archive_pointer =
                    static_cast<std::int64_t>(6) + 11 * count * 2 + 3 * 2;
                require(relative >= event_archive_pointer + 2 ||
                            relative + 2 <= event_archive_pointer,
                        "opcode 34 dynamically changed a CHNA path");
                const auto event_field_begin =
                    static_cast<std::int64_t>(6) + 9 * count * 2;
                const auto event_field_end = event_field_begin + count * 2;
                if (relative >= event_field_end || relative + 2 <= event_field_begin) {
                    continue;
                }
                require(count != 0 && relative >= event_field_begin &&
                            relative + 2 <= event_field_end &&
                            ((relative - event_field_begin) & 1) == 0,
                        "opcode 34 partially overwrote an event pointer");
                if (first_record_visit) ++event_pointer_mutations;
                require(!additive && (value & 1U) == 0,
                        "dynamic MAPZ event-pointer mutation is not statically auditable");
                for (const auto& alias : world.locations()) {
                    if (alias.area_offset == destination.area_offset) {
                        pending.push({alias.area.event_archive_path, value,
                                      alias.area.event_archive_path});
                    }
                }
            }
            require(terminated,
                    "reachable opcode 34 mutation has no f800 terminator");
        }
    }

    std::size_t reachable_count = 0;
    std::size_t reachable_commands = 0;
    for (const auto& [name, current] : coverage) {
        reachable_count += current->visited.size();
        reachable_commands += current->commands;
    }
    std::set<std::uint16_t> expected_opcodes;
    for (std::uint16_t opcode = 0; opcode < 62U; ++opcode) {
        if (opcode != 10U && opcode != 11U) expected_opcodes.insert(opcode);
    }
    require(root_count == 684 && reachable_count == 1065 &&
                reachable_commands == 6380 &&
                reachable_opcodes == expected_opcodes &&
                map_mutations == 235 && event_pointer_mutations == 37 &&
                inert_odd_targets == 1,
            "reachable CHNA event graph coverage changed");
    require(!swd2::ScriptArchive::probe(game_root / "RPG.EXE"),
            "native RPG code was misclassified as a script archive");

    const auto font = swd2::LegacyFont::load(game_root / "CHNA1.DSK");
    require(font.glyph_count() == 1510, "unexpected CHNA1 DSK glyph count");
    require(font.codes().front() == 0xa140 && font.contains(0xbaf2),
            "CHNA1 DSK Big5 code table was not decoded");
    const auto glyph = font.rasterize(0xbaf2);  // Big5 緊
    require(std::count(glyph.begin(), glyph.end(), 1) > 20,
            "CHNA1 DSK glyph bitmap was not decoded");

    const auto entity_dialogue = swd2::decode_event_record(archive.entry(0x12c / 2));
    const auto name_font = swd2::LegacyFont::load(game_root / "NAME.DSK");
    const auto named_page = swd2::render_dialogue_page(
        font, entity_dialogue.commands[0].text, 0, 288, 64, 15,
        &name_font);
    require(std::count(named_page.pixels.begin(), named_page.pixels.end(), 15) > 100,
            "NAME.DSK substitution glyphs were not used by dialogue rendering");
    // CHNA1 has one shipped B6F2 reference absent from both its main table
    // and NAME.DSK. 70a6 falls back to glyph index zero (A140, blank) while
    // still advancing the cursor; it does not throw or collapse the spacing.
    const std::array<std::uint8_t, 2> absent_code{{0xb6, 0xf2}};
    const auto absent_page = swd2::render_dialogue_page(
        font, absent_code, 0, 32, 16, 15, &name_font);
    require(!font.contains(0xb6f2U) && !name_font.contains(0xb6f2U) &&
                font.rasterize_or_first(0xb6f2U) == font.rasterize(0xa140U) &&
                std::count(absent_page.pixels.begin(),
                           absent_page.pixels.end(), 15) == 0 &&
                absent_page.cursor_x == 16U,
            "RPG missing Big5 code did not use 70a6 glyph-zero fallback");

    // 49d0 advances DATA:60d5 by four Mode-X bytes after every glyph and
    // changes DATA:60d7 only for a literal ##. It has no right-edge test.
    // Preserve that distinction from a modern word-wrapping text widget:
    // the third glyph is clipped by this narrow host mask, but the cursor
    // remains three glyphs into the same DOS row.
    const std::array<std::uint8_t, 6> overlong_row{{
        0xba, 0xf2, 0xba, 0xf2, 0xba, 0xf2,
    }};
    const auto overlong_page = swd2::render_dialogue_page(
        font, overlong_row, 0, 32, 32, 15, &name_font);
    require(overlong_page.cursor_x == 48U && overlong_page.cursor_y == 0U &&
                std::none_of(overlong_page.pixels.begin() + 32U * 16U,
                             overlong_page.pixels.end(),
                             [](std::uint8_t pixel) { return pixel != 0; }),
            "RPG 49d0 invented an automatic line wrap without ##");
}

class TestEventHost final : public swd2::EventVmHost {
public:
    void show_dialogue(std::uint16_t opcode,
                       std::span<const std::uint8_t> text) override {
        last_text_opcode = opcode;
        dialogue_bytes += text.size();
        ++dialogues;
        if (abort_after_dialogue) abort = true;
    }
    bool abort_requested() const override { return abort; }
    void delay(std::uint16_t ticks) override { delayed_ticks += ticks; }
    bool present_event_command(std::uint16_t,
                               std::span<const std::uint16_t>) override {
        if (observed_state != nullptr) {
            observed_layout_frames.push_back(observed_state->u16(0x411));
        }
        ++presentations;
        return accept_presentations;
    }
    bool present_battle_transition(std::uint16_t opcode) override {
        ++battle_transitions;
        battle_transition_opcodes.push_back(opcode);
        return accept_presentations;
    }
    bool show_positioned_text(std::uint16_t x, std::uint16_t y,
                              std::span<const std::uint8_t> text) override {
        positioned_x = x;
        positioned_y = y;
        positioned_text.assign(text.begin(), text.end());
        ++positioned_calls;
        return accept_presentations;
    }
    bool run_shop(std::span<const std::uint16_t> items,
                  swd2::SharedState&) override {
        shop_items.assign(items.begin(), items.end());
        ++shops;
        return accept_shops;
    }
    std::optional<bool> run_combined_shop(
        std::span<const std::uint16_t> items,
        swd2::SharedState&) override {
        shop_items.assign(items.begin(), items.end());
        ++combined_shops;
        return combined_shop_result;
    }
    std::optional<bool> confirm_event_branch(swd2::SharedState&) override {
        ++confirmations;
        return confirmation_result;
    }
    std::optional<swd2::InventoryUiResult> run_inventory(swd2::SharedState&) override {
        ++inventories;
        return inventory_result;
    }
    swd2::MapRelocationOutcome map_relocated(
        swd2::MapAreaRecord& area) override {
        ++map_relocations;
        relocated_entity_count = area.entity_count();
        return {};
    }

    std::size_t dialogues{};
    std::size_t dialogue_bytes{};
    std::uint16_t last_text_opcode{};
    std::uint64_t delayed_ticks{};
    std::size_t presentations{};
    std::size_t battle_transitions{};
    std::vector<std::uint16_t> battle_transition_opcodes;
    std::size_t positioned_calls{};
    std::uint16_t positioned_x{};
    std::uint16_t positioned_y{};
    std::vector<std::uint8_t> positioned_text;
    bool accept_presentations{true};
    std::vector<std::uint16_t> shop_items;
    std::size_t shops{};
    bool accept_shops{true};
    std::size_t combined_shops{};
    std::optional<bool> combined_shop_result{false};
    std::size_t confirmations{};
    std::optional<bool> confirmation_result{false};
    std::size_t inventories{};
    std::size_t map_relocations{};
    std::size_t relocated_entity_count{};
    bool abort_after_dialogue{};
    bool abort{};
    const swd2::SharedState* observed_state{};
    std::vector<std::uint16_t> observed_layout_frames;
    std::optional<swd2::InventoryUiResult> inventory_result{
        swd2::InventoryUiResult::cancelled};
};

std::vector<std::uint8_t> event_words(std::initializer_list<std::uint16_t> words) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2);
    for (const auto word : words) {
        bytes.push_back(static_cast<std::uint8_t>(word));
        bytes.push_back(static_cast<std::uint8_t>(word >> 8U));
    }
    return bytes;
}

void test_stateful_event_opcodes(const std::filesystem::path& game_root) {
    auto aborted_record = event_words({0});
    aborted_record.insert(aborted_record.end(),
                          {0xba, 0xf2, '$', '$'});
    const auto aborted_tail = event_words({41, 500, 0xffff});
    aborted_record.insert(aborted_record.end(),
                          aborted_tail.begin(), aborted_tail.end());
    const std::vector<std::vector<std::uint8_t>> aborted_records = {
        aborted_record,
    };
    const auto aborted_archive = swd2::ScriptArchive::from_records(
        aborted_records);
    TestEventHost aborting_host;
    aborting_host.abort_after_dialogue = true;
    auto aborted_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto aborted_money = aborted_state.u16(0x104);
    const auto aborted = swd2::execute_event(
        aborted_archive, 2, aborted_state, nullptr, 0, aborting_host);
    require(aborted.status == swd2::EventVmStatus::host_abort &&
                aborted.commands_executed == 1U &&
                aborted_state.u16(0x104) == aborted_money,
            "event VM continued mutating state after a frontend text abort");

    const std::vector<std::vector<std::uint8_t>> presentation_records = {
        event_words({5, 0xffff}),
    };
    const auto presentation_archive =
        swd2::ScriptArchive::from_records(presentation_records);
    TestEventHost presentation_abort_host;
    presentation_abort_host.abort = true;
    presentation_abort_host.accept_presentations = false;
    auto presentation_abort_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto presentation_abort = swd2::execute_event(
        presentation_archive, 2, presentation_abort_state, nullptr, 0,
        presentation_abort_host);
    require(presentation_abort.status == swd2::EventVmStatus::host_abort,
            "event VM misreported a presentation frontend abort as unsupported");

    TestEventHost unsupported_presentation_host;
    unsupported_presentation_host.accept_presentations = false;
    auto unsupported_presentation_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto unsupported_presentation = swd2::execute_event(
        presentation_archive, 2, unsupported_presentation_state, nullptr, 0,
        unsupported_presentation_host);
    require(unsupported_presentation.status ==
                swd2::EventVmStatus::unsupported_opcode,
            "event VM confused an unimplemented presentation with frontend abort");

    // The first generated record exercises the state-only handlers in their
    // native word-stream representation. Record two is the missing-item
    // branch target of opcode 40.
    const std::vector<std::vector<std::uint8_t>> records = {
        event_words({10, 0x2d, 10,
                     40, 123, 4, 257,
                     51, 6,
                     42, 1,
                     47, 2,
                     58, 222,
                     0xffff}),
        event_words({61, 0xffff}),
    };
    const auto archive = swd2::ScriptArchive::from_records(records);
    TestEventHost host;

    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x106 + 0x2d, 95);
    state.set_u16(0x106 + 0x2f, 100);
    for (std::size_t i = 0; i < 50; ++i) state.set_u16(0x382 + i * 2, 0);
    state.set_u16(0x382, 123);
    state.set_u16(0x384, 400);
    state.set_u16(0x388, 88);
    state.set_u16(0x38a, 314);
    state.set_u16(0x38c, 99);
    const auto result = swd2::execute_event(archive, 2, state, nullptr, 0, host);
    require(result.status == swd2::EventVmStatus::completed &&
                result.requested_marker == swd2::Marker::open_figure &&
                state.u16(0x4a0) == 222 &&
                host.battle_transition_opcodes ==
                    std::vector<std::uint16_t>({58}),
            "event opcode 58 did not request the FIG module");
    require(state.u16(0x106 + 0x2d) == 100,
            "event opcode 10 did not saturate at the adjacent maximum");
    require(state.u16(0x382) == 257 && state.u16(0x384) == 88 &&
                state.u16(0x386) == 99 && state.u16(0x388) == 0 &&
                state.u8(0x3f1) == 1,
            "event opcodes 40/42 did not update, filter and compact the inventory");
    require(state.u16(0x10) == 2 && state.u16(0x106 + 2 * 0x9f + 8) == 0x2000 &&
                state.u16(0x106 + 3 * 0x9f + 8) == 0x2000,
            "event opcode 47 did not mark inactive party records");
    for (std::size_t i = 0; i < 12; ++i) {
        require(state.u16(0xa2 + i * 2) == 6,
                "event opcode 51 did not set every actor direction");
    }

    // A full inventory makes opcode 40 branch to record two instead of
    // executing the following commands in record one.
    auto missing = swd2::SharedState::load(game_root / "SAVE.DA1");
    for (std::size_t i = 0; i < 50; ++i) missing.set_u16(0x382 + i * 2, 1);
    missing.set_u8(0x112, 0x10);
    missing.set_u8(0x163, 0xf8);
    missing.set_u8(0x114, 0xfe);
    const auto branch = swd2::execute_event(archive, 2, missing, nullptr, 0, host);
    require(branch.status == swd2::EventVmStatus::completed &&
                branch.requested_marker == swd2::Marker::none &&
                missing.u8(0x11a) == 0xa4 && missing.u8(0x11c) == 0xa4 &&
                missing.u8(0x132) == 1 && missing.u8(0x112) == 0xb5 &&
                missing.u8(0x163) == 7 && missing.u8(0x114) == 13,
            "event opcodes 40/61 did not take and execute the missing-item branch");

    const std::vector<std::vector<std::uint8_t>> removal_records = {
        event_words({40, 75, 4, 0, 0xffff}),
        event_words({61, 0xffff}),
    };
    const auto removal_archive =
        swd2::ScriptArchive::from_records(removal_records);
    auto removal_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    for (std::size_t i = 0; i < 50; ++i) {
        removal_state.set_u16(0x382 + i * 2U, 0);
    }
    removal_state.set_u16(0x382, 1);
    removal_state.set_u16(0x384, 75);
    removal_state.set_u16(0x386, 2);
    const auto removal = swd2::execute_event(
        removal_archive, 2, removal_state, nullptr, 0, host);
    require(removal.status == swd2::EventVmStatus::completed &&
                removal.commands_executed == 1 && removal.last_opcode == 40 &&
                removal_state.u16(0x382) == 1 &&
                removal_state.u16(0x384) == 2 &&
                removal_state.u16(0x386) == 0,
            "event opcode 40 removal did not run its 3ced stable compaction");

    for (const auto opcode : {std::uint16_t{59}, std::uint16_t{60}}) {
        const std::vector<std::vector<std::uint8_t>> battle_records = {
            event_words({opcode, 7, 444, 0xffff}),
        };
        const auto battle_archive = swd2::ScriptArchive::from_records(battle_records);
        auto battle_state = swd2::SharedState::load(game_root / "SAVE.DA1");
        const auto battle =
            swd2::execute_event(battle_archive, 2, battle_state, nullptr, 0, host);
        require(battle.requested_marker == swd2::Marker::open_figure &&
                    battle_state.u16(0x51c) == 7 && battle_state.u16(0x4a0) == 444,
                "event opcode 59/60 did not preserve the FIG launch fields");
    }
    require(host.battle_transition_opcodes ==
                std::vector<std::uint16_t>({58, 59, 60}),
            "event battle transitions lost their FI00/FI02/F sentinel opcode");

    TestEventHost battle_abort_host;
    battle_abort_host.abort = true;
    battle_abort_host.accept_presentations = false;
    auto battle_abort_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const std::vector<std::vector<std::uint8_t>> battle_abort_records = {
        event_words({58, 444, 0xffff}),
    };
    const auto battle_abort_archive =
        swd2::ScriptArchive::from_records(battle_abort_records);
    const auto battle_abort = swd2::execute_event(
        battle_abort_archive, 2, battle_abort_state, nullptr, 0,
        battle_abort_host);
    require(battle_abort.status == swd2::EventVmStatus::host_abort,
            "event battle transition misreported a frontend abort as unsupported");

    const std::vector<std::vector<std::uint8_t>> movement_records = {
        event_words({5, 22, 30, 2, 31, 1, 32, 3, 33, 1, 45, 0xffff}),
    };
    const auto movement_archive = swd2::ScriptArchive::from_records(movement_records);
    auto moving = swd2::SharedState::load(game_root / "SAVE.DA1");
    moving.set_actor_screen_x(0x26);
    moving.set_actor_screen_y(0x50);
    moving.set_viewport_x(5);
    moving.set_viewport_y(6);
    moving.set_u16(0x413, 40);
    moving.set_u16(0x415, 25);
    moving.set_u16(0x417, 100);
    moving.set_u16(0x419, 100);
    moving.set_u16(0x40d, 1000);
    const auto before_presentations = host.presentations;
    const auto movement =
        swd2::execute_event(movement_archive, 2, moving, nullptr, 0, host);
    require(movement.status == swd2::EventVmStatus::completed &&
                moving.actor_screen_x() == 0x26 && moving.actor_screen_y() == 0x50 &&
                moving.viewport_x() == 3 && moving.viewport_y() == 5 &&
                moving.u16(0x40d) == 796 && moving.actor_direction() == 9 &&
                host.presentations - before_presentations == 10,
            "event presentation and scripted movement opcodes did not execute");

    const std::vector<std::vector<std::uint8_t>> layout_records = {
        event_words({36, 3, 0xffff}),
    };
    const auto layout_archive = swd2::ScriptArchive::from_records(layout_records);
    auto layout_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    layout_state.set_u16(0x40d, 0x7777);
    layout_state.set_u16(0x40f, 0x1234);
    layout_state.set_u16(0x411, 0);
    host.observed_state = &layout_state;
    host.observed_layout_frames.clear();
    const auto layout = swd2::execute_event(
        layout_archive, 2, layout_state, nullptr, 0, host);
    host.observed_state = nullptr;
    require(layout.status == swd2::EventVmStatus::completed &&
                layout_state.u16(0x40d) == 0x1234 &&
                layout_state.u16(0x411) == 3 &&
                host.observed_layout_frames ==
                    std::vector<std::uint16_t>({0, 1, 2}),
            "event opcode 36 skipped the current RAP frame before presenting it");

    const std::vector<std::vector<std::uint8_t>> entity_frame_records = {
        event_words({39, 0, 7, 0xffff}),
    };
    const auto entity_frame_archive =
        swd2::ScriptArchive::from_records(entity_frame_records);
    swd2::MapAreaRecord entity_frame_area;
    for (auto& field : entity_frame_area.entity_fields) field.resize(1);
    entity_frame_area.entity_fields[0][0] = 0x4402U;
    swd2::prepare_runtime_map_area(entity_frame_area);
    swd2::prepare_runtime_map_area(entity_frame_area);  // Runtime reload guard.
    auto entity_frame_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto before_entity_frames = host.presentations;
    const auto entity_frame = swd2::execute_event(
        entity_frame_archive, 2, entity_frame_state,
        &entity_frame_area, 0, host);
    require(entity_frame.status == swd2::EventVmStatus::completed &&
                entity_frame_area.entity_fields[0][0] == 7 &&
                entity_frame_area.entity_sprite_resources ==
                    std::vector<std::uint8_t>({0x44U}) &&
                host.presentations == before_entity_frames + 1U,
            "event opcode 39 changed the loaded SA resource with its frame");

    const std::vector<std::vector<std::uint8_t>> persistent_frame_records = {
        event_words({3, 0, 0x31, 0xffff}),
    };
    const auto persistent_frame_archive =
        swd2::ScriptArchive::from_records(persistent_frame_records);
    const auto persistent_frame = swd2::execute_event(
        persistent_frame_archive, 2, entity_frame_state,
        &entity_frame_area, 0, host);
    require(persistent_frame.status == swd2::EventVmStatus::completed &&
                entity_frame_area.entity_fields[0][0] == 0x31U &&
                entity_frame_area.entity_sprite_resources ==
                    std::vector<std::uint8_t>({0x44U}),
            "event opcode 3 changed the loaded SA resource with its frame");

    const std::vector<std::vector<std::uint8_t>> byte_slot_records = {
        event_words({54, 0x20, 0x1234, 0xffff}),
    };
    const auto byte_slot_archive =
        swd2::ScriptArchive::from_records(byte_slot_records);
    auto byte_slots = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto slot_base = std::size_t{0x126};
    byte_slots.set_u8(slot_base, 0);
    byte_slots.set_u8(slot_base + 1, 1);
    byte_slots.set_u8(slot_base + 2, 0);
    byte_slots.set_u8(slot_base + 3, 0);
    const auto byte_slot_result = swd2::execute_event(
        byte_slot_archive, 2, byte_slots, nullptr, 0, host);
    require(byte_slot_result.status == swd2::EventVmStatus::completed &&
                byte_slots.u8(slot_base) == 0 &&
                byte_slots.u8(slot_base + 2) == 0x34,
            "event opcode 54 did not use its overlapping word-empty scan");

    auto positioned_record = event_words({53, 7, 9});
    positioned_record.insert(positioned_record.end(),
                             {0xa4, 0x40, ' ', 0xa4, 0x41, '$', '$', 0xff, 0xff});
    const std::vector<std::vector<std::uint8_t>> positioned_records = {
        positioned_record,
    };
    const auto positioned_archive =
        swd2::ScriptArchive::from_records(positioned_records);
    const auto decoded_positioned =
        swd2::decode_event_record(positioned_archive.entry(1));
    require(decoded_positioned.commands.size() == 1 &&
                decoded_positioned.commands[0].opcode == 53 &&
                decoded_positioned.commands[0].arguments ==
                    std::vector<std::uint16_t>({7, 9}) &&
                decoded_positioned.commands[0].text ==
                    std::vector<std::uint8_t>({0xa4, 0x40, ' ', 0xa4, 0x41}),
            "event opcode 53 did not decode its coordinate-prefixed inline text");
    auto positioned_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto positioned = swd2::execute_event(
        positioned_archive, 2, positioned_state, nullptr, 0, host);
    require(positioned.status == swd2::EventVmStatus::completed &&
                host.positioned_calls == 1 && host.positioned_x == 7 &&
                host.positioned_y == 9 &&
                host.positioned_text ==
                    std::vector<std::uint8_t>({0xa4, 0x40, ' ', 0xa4, 0x41}),
            "event opcode 53 did not route its inline text to the RPG host");

    const std::vector<std::vector<std::uint8_t>> shop_records = {
        event_words({19, 3, 117, 118, 120, 0xffff}),
    };
    const auto shop_archive = swd2::ScriptArchive::from_records(shop_records);
    auto shop_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto shop = swd2::execute_event(shop_archive, 2, shop_state, nullptr, 0, host);
    require(shop.status == swd2::EventVmStatus::completed && host.shops == 1 &&
                host.shop_items == std::vector<std::uint16_t>({117, 118, 120}),
            "event opcode 19 did not pass its variable ITEM list to the shop host");

    const std::vector<std::vector<std::uint8_t>> confirmation_records = {
        event_words({13, 4, 58, 111, 0xffff}),
        event_words({58, 222, 0xffff}),
    };
    const auto confirmation_archive =
        swd2::ScriptArchive::from_records(confirmation_records);
    auto confirmation_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    host.confirmation_result = false;
    const auto declined = swd2::execute_event(
        confirmation_archive, 2, confirmation_state, nullptr, 0, host);
    require(declined.requested_marker == swd2::Marker::open_figure &&
                confirmation_state.u16(0x4a0) == 111 &&
                host.confirmations == 1 && host.inventories == 0,
            "event opcode 13 did not continue after the declined No choice");

    confirmation_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    host.confirmation_result = true;
    const auto confirmation = swd2::execute_event(
        confirmation_archive, 2, confirmation_state, nullptr, 0, host);
    require(confirmation.requested_marker == swd2::Marker::open_figure &&
                confirmation_state.u16(0x4a0) == 222 &&
                host.confirmations == 2 && host.inventories == 0,
            "event opcode 13 did not take the accepted Yes branch");

    const std::vector<std::vector<std::uint8_t>> combined_shop_records = {
        event_words({17, 2, 117, 118, 0xffff}),
        event_words({58, 333, 0xffff}),
    };
    const auto combined_shop_archive =
        swd2::ScriptArchive::from_records(combined_shop_records);
    swd2::MapAreaRecord shop_area;
    for (auto& field : shop_area.entity_fields) field.resize(1);
    shop_area.entity_fields[9][0] = 4;
    auto combined_shop_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    host.combined_shop_result = false;
    const auto combined_cancelled = swd2::execute_event(
        combined_shop_archive, 2, combined_shop_state, &shop_area, 0, host);
    require(combined_cancelled.status == swd2::EventVmStatus::completed &&
                combined_cancelled.requested_marker == swd2::Marker::none &&
                host.combined_shops == 1 && host.shops == 1 &&
                host.inventories == 0,
            "event opcode 17 did not stop after its initial selector was cancelled");

    host.combined_shop_result = true;
    const auto combined_shop = swd2::execute_event(
        combined_shop_archive, 2, combined_shop_state, &shop_area, 0, host);
    require(combined_shop.requested_marker == swd2::Marker::open_figure &&
                combined_shop_state.u16(0x4a0) == 333 &&
                host.combined_shops == 2 && host.shops == 1 &&
                host.inventories == 0 &&
                host.shop_items == std::vector<std::uint16_t>({117, 118}),
            "event opcode 17 did not run its shop list and reload the entity event");

    // RPG 569b passes field 9 straight to 53b1 after either shop closes.
    // Offset zero selects CHNA's terminating empty record; it is not a null
    // event pointer. Reloading it must skip the remainder of this record.
    const std::vector<std::vector<std::uint8_t>> zero_shop_records = {
        event_words({17, 2, 117, 118, 58, 444, 0xffff}),
    };
    const auto zero_shop_archive =
        swd2::ScriptArchive::from_records(zero_shop_records);
    shop_area.entity_fields[9][0] = 0;
    auto zero_shop_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto zero_shop = swd2::execute_event(
        zero_shop_archive, 2, zero_shop_state, &shop_area, 0, host);
    require(zero_shop.status == swd2::EventVmStatus::completed &&
                zero_shop.requested_marker == swd2::Marker::none &&
                zero_shop.commands_executed == 1 &&
                zero_shop_archive.event_stream(0).size() == 2 &&
                zero_shop_archive.event_stream(0)[0] == 0xff &&
                zero_shop_archive.event_stream(0)[1] == 0xff,
            "event opcode 17 treated the zero-offset CHNA record as no reload");
}

void test_event_vm(const std::filesystem::path& game_root) {
    const auto archive = swd2::ScriptArchive::load(game_root / "CHNA1.EXE");
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    auto world = swd2::MapDatabase::load(game_root / "MAPZ.DA1");
    auto& area = world.location_at_directory_offset(8).area;
    TestEventHost host;
    const auto dialogue = swd2::execute_event(archive, 0x12c, state, &area, 1, host);
    require(dialogue.status == swd2::EventVmStatus::completed &&
                dialogue.commands_executed == 1 && host.dialogues == 1 &&
                host.dialogue_bytes == 88,
            "CHNA1 map-entity dialogue did not execute in the event VM");

    // Event 204 ends in opcode 28, the one-argument battle transition.
    const auto battle = swd2::execute_event(archive, 204 * 2, state, &area, 1, host);
    require(battle.status == swd2::EventVmStatus::completed &&
                battle.requested_marker == swd2::Marker::open_figure &&
                battle.last_opcode == 28 && state.u16(0x4a0) == 392,
            "event opcode 28 did not request the in-process FIG module");

    // Opcode 48 carries both the auxiliary battle field and encounter id.
    const auto chapter_five = swd2::ScriptArchive::load(game_root / "CHNA5.EXE");
    const auto special_battle =
        swd2::execute_event(chapter_five, 189 * 2, state, &area, 1, host);
    require(special_battle.requested_marker == swd2::Marker::open_figure &&
                special_battle.last_opcode == 48 && state.u16(0x51c) == 36 &&
                state.u16(0x4a0) == 68,
            "event opcode 48 did not preserve both FIG launch arguments");

    const std::vector<std::vector<std::uint8_t>> exit_records = {
        event_words({52, 41, 99, 0xffff}),
    };
    const auto exit_archive = swd2::ScriptArchive::from_records(exit_records);
    const auto money_before_exit = state.u16(0x104);
    const auto program_exit = swd2::execute_event(
        exit_archive, 2, state, &area, 1, host);
    require(program_exit.status == swd2::EventVmStatus::completed &&
                program_exit.requested_program_exit &&
                !program_exit.requested_map_reload &&
                program_exit.requested_marker == swd2::Marker::none &&
                program_exit.commands_executed == 1 &&
                program_exit.last_opcode == 52 &&
                state.u16(0x104) == money_before_exit,
            "event opcode 52 reloaded the map instead of exiting RPG.EXE");

    const auto mixed = swd2::execute_event(archive, 15 * 2, state, &area, 1, host);
    require(mixed.status == swd2::EventVmStatus::unsupported_opcode &&
                mixed.last_opcode == 34 && area.entity_fields[3][1] == 3,
            "event VM did not preserve effects before an unsupported opcode");

    // The same real record becomes fully executable when the VM is given the
    // MAPZ database. Its opcode 34 writes location-directory slot 14,
    // field 3, entity byte offset 76.
    const auto map_mutation = swd2::execute_event(
        archive, 15 * 2, state, &area, 1, host, 10'000, &world);
    require(map_mutation.status == swd2::EventVmStatus::completed &&
                world.location_at_directory_offset(
                    state.map_location_directory_offset())
                        .area.entity_fields[3][1] == 3 &&
                world.location_at_directory_offset(14).area.entity_fields[3][38] == 3,
            "event opcodes 3/34 did not apply the real CHNA1 MAPZ mutations");
    const auto shared_area_offset = world.location_at_directory_offset(14).area_offset;
    for (const auto& location : world.locations()) {
        if (location.area_offset == shared_area_offset) {
            require(location.area.entity_fields[3][38] == 3,
                    "MAPZ mutation was not propagated across a shared area pointer");
        }
    }

    // 5a1f deliberately has no alignment or eleven-field bounds check. The
    // released story uses both consequences: CHNA2 entry 23 adds one complete
    // string length to the two path-pointer words after field 10, changing
    // AREA2.RAP into AREA7.RAP, and CHNA5 entry 44 writes a word starting at
    // the high byte of one field-3 entity word.
    TestEventHost raw_mutation_host;
    auto raw_pointer_world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    auto raw_pointer_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    auto& raw_pointer_area =
        raw_pointer_world.location_at_directory_offset(430).area;
    const auto chapter_two =
        swd2::ScriptArchive::load(game_root / "CHNA2.EXE");
    const auto path_pointer_mutation = swd2::execute_event(
        chapter_two, 46, raw_pointer_state, &raw_pointer_area, 1,
        raw_mutation_host,
        10'000, &raw_pointer_world);
    const auto& changed_paths =
        raw_pointer_world.location_at_directory_offset(338).area;
    require(path_pointer_mutation.status == swd2::EventVmStatus::completed &&
                path_pointer_mutation.commands_executed == 22U &&
                changed_paths.graphics_path == "\\SWD2\\T4\\AREA7.RAP" &&
                changed_paths.layout_path == "\\SWD2\\T4\\AREA7.RAP" &&
                raw_pointer_world.location_at_directory_offset(430)
                        .area.entity_fields[9][1] == 48U,
            "real CHNA2 opcode 34 did not mutate trailing MAPZ path pointers");

    auto unaligned_world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    auto unaligned_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    auto& unaligned_area =
        unaligned_world.location_at_directory_offset(636).area;
    const auto unaligned_mutation = swd2::execute_event(
        chapter_five, 88, unaligned_state, &unaligned_area, 4,
        raw_mutation_host,
        10'000, &unaligned_world);
    const auto& chapter_six_area =
        unaligned_world.location_at_directory_offset(684).area;
    require(unaligned_mutation.status == swd2::EventVmStatus::completed &&
                unaligned_mutation.commands_executed == 2U &&
                chapter_six_area.entity_fields[2][0] == 1820U &&
                chapter_six_area.entity_fields[2][1] == 2206U &&
                chapter_six_area.entity_fields[3][2] == 0U &&
                chapter_six_area.entity_fields[9][0] == 72U &&
                chapter_six_area.entity_fields[9][1] == 78U &&
                unaligned_world.location_at_directory_offset(636)
                        .area.entity_fields[3][1] == 0x0800U,
            "real CHNA5 opcode 34 did not preserve its unaligned MAPZ word write");

    // The global MAPZ closure makes these two records live even though no
    // original MAPA entity points at them. Exercise the actual released
    // streams, not merely synthetic opcode 49/51 commands.
    TestEventHost installed_story_host;
    auto quake_world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    auto quake_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    auto& quake_area = quake_world.location_at_directory_offset(604).area;
    const auto chapter_four =
        swd2::ScriptArchive::load(game_root / "CHNA4.EXE");
    const auto quake_story = swd2::execute_event(
        chapter_four, 24, quake_state, &quake_area, 0,
        installed_story_host, 10'000, &quake_world);
    require(quake_story.status == swd2::EventVmStatus::completed &&
                quake_story.commands_executed == 38U &&
                installed_story_host.presentations == 32U &&
                installed_story_host.dialogues == 3U &&
                installed_story_host.map_relocations == 1U,
            "dynamically installed CHNA4 opcode-49 story did not execute fully");

    TestEventHost direction_story_host;
    auto direction_world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    auto direction_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    auto& direction_area =
        direction_world.location_at_directory_offset(758).area;
    const auto direction_story = swd2::execute_event(
        chapter_five, 30, direction_state, &direction_area, 0,
        direction_story_host, 10'000, &direction_world);
    bool all_facing_west = true;
    for (std::size_t index = 0; index < 12; ++index) {
        all_facing_west = all_facing_west &&
            direction_state.u16(0xa2 + index * 2U) == 6U;
    }
    require(direction_story.status == swd2::EventVmStatus::completed &&
                direction_story.commands_executed == 125U &&
                direction_story.last_opcode == 48U &&
                direction_story.requested_marker == swd2::Marker::open_figure &&
                direction_story_host.battle_transitions == 1U &&
                all_facing_west,
            "dynamically installed CHNA5 opcode-51 story did not execute fully");

    auto overflow_world = swd2::MapDatabase::load(game_root / "MAPA.EXE");
    auto overflow_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(overflow_state, overflow_world, 144);
    auto& overflow_area = overflow_world.location_at_directory_offset(144).area;
    const auto chapter_zero =
        swd2::ScriptArchive::load(game_root / "CHNA0.EXE");
    const auto overflow_story = swd2::execute_event(
        chapter_zero, 740, overflow_state, &overflow_area, 0,
        installed_story_host, 10'000, &overflow_world);
    require(overflow_story.status == swd2::EventVmStatus::completed &&
                overflow_story.commands_executed == 105U &&
                overflow_story.last_opcode == 34U &&
                overflow_world.location_at_directory_offset(144)
                        .area.entity_count() == 5U &&
                overflow_world.location_at_directory_offset(144)
                        .area.entity_fields[9][0] == 744U,
            "CHNA0 five-entity scene rejected its invisible fixed-BSS choreography slots");

    const std::vector<std::vector<std::uint8_t>> relocation_records = {
        event_words({37, 10, 3, 3, 7, 41, 9, 0xffff}),
    };
    const auto relocation_archive =
        swd2::ScriptArchive::from_records(relocation_records);
    const auto& destination = world.location_at_directory_offset(10);
    const auto destination_behavior = destination.area.entity_fields[3][0];
    const auto money_before_relocation = state.u16(0x104);
    const auto relocation = swd2::execute_event(
        relocation_archive, 2, state, &area, 0, host, 10'000, &world);
    require(relocation.status == swd2::EventVmStatus::completed &&
                relocation.requested_map_reload && relocation.commands_executed == 3 &&
                relocation.last_opcode == 41 && relocation.relocated_area &&
                relocation.relocated_area->entity_fields[3][0] == 7 &&
                world.location_at_directory_offset(10).area.entity_fields[3][0] ==
                    7 && destination_behavior != 7 &&
                host.map_relocations == 1 &&
                host.relocated_entity_count == destination.area.entity_count() &&
                state.u16(0x104) == money_before_relocation + 9U &&
                state.u16(0x424) == 10 &&
                state.u16(0x40d) == destination.map_position &&
                state.viewport_x() == destination.viewport_x &&
                state.viewport_y() == destination.viewport_y &&
                state.actor_screen_x() == destination.actor_screen_x &&
                state.actor_screen_y() == destination.actor_screen_y &&
                state.area_graphics_path() == destination.area.graphics_path &&
                state.area_collision_path() == destination.area.layout_path &&
                state.music_path() == destination.area.music_path &&
                state.event_executable_path() == destination.area.event_archive_path &&
                state.event_data_path() == destination.area.event_font_path,
            "event opcode 37 did not continue/persist opcode 3 on the destination area");
    for (std::size_t i = 0; i < 12; ++i) {
        require(state.u16(0x12 + i * 2) == destination.actor_screen_x &&
                    state.u16(0x2a + i * 2) == destination.actor_screen_y &&
                    state.u16(0xba + i * 2) == 7U &&
                    state.u16(0xa2 + i * 2) == destination.actor_direction,
                "event opcode 37 did not reset the twelve actor placements");
    }

    const std::vector<std::vector<std::uint8_t>> position_records = {
        event_words({37, 0x800a, 41, 3, 0xffff}),
    };
    const auto position_archive =
        swd2::ScriptArchive::from_records(position_records);
    auto position_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    for (std::size_t i = 0; i < 12; ++i) {
        position_state.set_u16(0x12 + i * 2,
                               static_cast<std::uint16_t>(20U + i * 2U));
        position_state.set_u16(0x2a + i * 2,
                               static_cast<std::uint16_t>(40U + i * 3U));
        position_state.set_u16(0xba + i * 2,
                               static_cast<std::uint16_t>(100U + i));
    }
    const auto& original_location = world.location_at_directory_offset(8);
    position_state.set_dos_string(
        0x42d, 22, original_location.area.graphics_path);
    position_state.set_dos_string(
        0x443, 22, original_location.area.layout_path);
    position_state.set_dos_string(
        0x459, 22, original_location.area.music_path);
    auto position_area = original_location.area;
    const auto position_money = position_state.u16(0x104);
    const auto relocation_calls = host.map_relocations;
    const auto positioned = swd2::execute_event(
        position_archive, 2, position_state, &position_area, 0, host,
        10'000, &world);
    require(positioned.status == swd2::EventVmStatus::completed &&
                !positioned.requested_map_reload &&
                !positioned.relocated_area &&
                positioned.commands_executed == 2U &&
                host.map_relocations == relocation_calls &&
                position_state.u16(0x424) == 10U &&
                position_state.u16(0x40d) == destination.map_position &&
                position_state.area_graphics_path() ==
                    original_location.area.graphics_path &&
                position_state.area_collision_path() ==
                    original_location.area.layout_path &&
                position_state.music_path().empty() &&
                position_state.u16(0x104) == position_money + 3U,
            "event opcode 37 position-only form did not preserve its music clear quirk");
    for (std::size_t i = 0; i < 12; ++i) {
        require(
            position_state.u16(0x12 + i * 2) ==
                    static_cast<std::uint16_t>(
                        20U + i * 2U + destination.actor_screen_x - 20U) &&
                position_state.u16(0x2a + i * 2) ==
                    static_cast<std::uint16_t>(
                        40U + i * 3U + destination.actor_screen_y - 40U) &&
                position_state.u16(0xba + i * 2) == 100U + i &&
                position_state.u16(0xa2 + i * 2) ==
                    destination.actor_direction,
            "event opcode 37 relative form did not preserve the follower formation");
    }
}

class ScriptedPlatform final : public swd2::PlatformBackend {
public:
    void present(const swd2::IndexedSurfaceView& surface) override {
        require(surface.width == 320 && surface.height == 200, "unexpected MEO surface size");
        if (track_monochrome && last_pixels.size() == surface.pixels.size() &&
            std::equal(last_palette.begin(), last_palette.end(), surface.palette.begin())) {
            auto filtered = last_pixels;
            swd2::apply_rpg_event_monochrome_filter(filtered, last_palette);
            if (filtered != last_pixels &&
                std::equal(filtered.begin(), filtered.end(), surface.pixels.begin())) {
                ++monochrome_transitions;
            }
        }
        if (track_monochrome) {
            last_pixels.assign(surface.pixels.begin(), surface.pixels.end());
            std::copy(surface.palette.begin(), surface.palette.end(), last_palette.begin());
        }
        std::uint64_t frame_hash = 1469598103934665603ULL;
        for (const auto pixel : surface.pixels) {
            frame_hash ^= pixel;
            frame_hash *= 1099511628211ULL;
        }
        frame_hashes.push_back(frame_hash);
        std::uint64_t compact_hash = 1469598103934665603ULL;
        for (std::size_t y = 8; y < 40; ++y) {
            for (std::size_t x = 216; x < 296; ++x) {
                compact_hash ^= surface.pixels[y * 320 + x];
                compact_hash *= 1099511628211ULL;
            }
        }
        compact_hashes.push_back(compact_hash);
        std::uint64_t bottom_hash = 1469598103934665603ULL;
        for (std::size_t y = 128; y < 192; ++y) {
            for (std::size_t x = 0; x < 320; ++x) {
                bottom_hash ^= surface.pixels[y * 320 + x];
                bottom_hash *= 1099511628211ULL;
            }
        }
        bottom_hashes.push_back(bottom_hash);
        std::uint64_t palette_hash = 1469598103934665603ULL;
        for (const auto component : surface.palette) {
            palette_hash ^= component;
            palette_hash *= 1099511628211ULL;
        }
        palette_hashes.push_back(palette_hash);
        ++presented;
    }
    void present_direct_update(const swd2::IndexedSurfaceView& surface) override {
        ++direct_updates;
        direct_update_pixels.assign(surface.pixels.begin(), surface.pixels.end());
    }
    swd2::InputAction wait_for_input() override {
        ++wait_calls;
        if (cursor < actions.size()) {
            return actions[cursor++];
        }
        return swd2::InputAction::quit;
    }
    swd2::InputAction poll_input() override {
        ++poll_calls;
        if (cursor < actions.size()) return actions[cursor++];
        return swd2::InputAction::none;
    }
    swd2::InputAction poll_text_input() override {
        ++text_poll_calls;
        if (text_cursor < text_actions.size()) {
            return text_actions[text_cursor++];
        }
        return swd2::InputAction::none;
    }
    bool poll_frontend_quit() override {
        ++frontend_quit_poll_calls;
        if (frontend_cursor < frontend_actions.size()) {
            return frontend_actions[frontend_cursor++] ==
                   swd2::InputAction::quit;
        }
        return false;
    }
    swd2::ClockTime clock_time() const override { return {0, 0}; }
    void play_music(std::span<const std::uint8_t>, bool) override { ++music_calls; }
    void play_voice(std::span<const std::uint8_t> data) override {
        ++voice_calls;
        voice_bytes += data.size();
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto byte : data) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        voice_hashes.push_back(hash);
    }
    void stop_audio() override { ++stop_calls; }
    void stop_music() override { ++music_stop_calls; }
    void delay_for(std::chrono::milliseconds duration) override {
        ++delay_calls;
        delayed_milliseconds += static_cast<std::uint64_t>(duration.count());
    }
    std::size_t presented{};
    std::size_t music_calls{};
    std::size_t voice_calls{};
    std::size_t voice_bytes{};
    std::size_t stop_calls{};
    std::size_t music_stop_calls{};
    std::size_t wait_calls{};
    std::size_t poll_calls{};
    std::size_t direct_updates{};
    std::size_t text_poll_calls{};
    std::size_t frontend_quit_poll_calls{};
    std::size_t delay_calls{};
    std::uint64_t delayed_milliseconds{};
    std::vector<std::uint64_t> frame_hashes;
    std::vector<std::uint64_t> compact_hashes;
    std::vector<std::uint64_t> bottom_hashes;
    std::vector<std::uint64_t> palette_hashes;
    std::vector<std::uint64_t> voice_hashes;
    bool track_monochrome{};
    std::size_t monochrome_transitions{};
    std::vector<std::uint8_t> last_pixels;
    std::vector<std::uint8_t> direct_update_pixels;
    std::array<std::uint8_t, 768> last_palette{};
    std::vector<swd2::InputAction> actions = {
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
    };
    std::vector<swd2::InputAction> text_actions;
    std::vector<swd2::InputAction> frontend_actions;
    std::size_t text_cursor{};
    std::size_t frontend_cursor{};
    std::size_t cursor{};
};

void test_meo_exit_fade(const std::filesystem::path& game_root) {
    {
        ScriptedPlatform platform;
        swd2::GameContext context{
            game_root, swd2::SharedState::load(game_root / "SAVE.DA1"),
            platform};
        require(swd2::MeoModule().run(context, swd2::Marker::none) ==
                    swd2::Marker::menu_ready &&
                    platform.presented == 66U && platform.wait_calls == 3U &&
                    platform.frontend_quit_poll_calls == 63U &&
                    platform.delay_calls == 63U &&
                    platform.delayed_milliseconds == 900U &&
                    platform.palette_hashes.back() ==
                        17828133145641756547ULL,
                "MEO accepted path did not perform its 63-tick DAC fade");
    }

    {
        ScriptedPlatform platform;
        platform.frontend_actions.assign(10U, swd2::InputAction::none);
        platform.frontend_actions.push_back(swd2::InputAction::quit);
        swd2::GameContext context{
            game_root, swd2::SharedState::load(game_root / "SAVE.DA1"),
            platform};
        require(swd2::MeoModule().run(context, swd2::Marker::none) ==
                    swd2::Marker::none &&
                    platform.presented == 13U && platform.wait_calls == 3U &&
                    platform.frontend_quit_poll_calls == 11U &&
                    platform.delay_calls == 10U &&
                    platform.delayed_milliseconds == 142U,
                "MEO fade ignored frontend quit or consumed a DOS action");
    }
}

void test_monolithic_runtime(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions.insert(platform.actions.end(), {
        swd2::InputAction::down,     // RPG title: Continue
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // slot one
        swd2::InputAction::confirm,  // default Yes
        swd2::InputAction::quit,     // first world poll
    });
    swd2::GameContext context{game_root, swd2::SharedState::load(game_root / "SAVE.DA1"), platform};
    swd2::ModuleRegistry modules;
    modules.add(std::make_unique<swd2::MeoModule>());
    modules.add(std::make_unique<swd2::RpgModule>());
    modules.add(std::make_unique<swd2::BattleModule>());
    modules.add(std::make_unique<swd2::DemoModule>());
    const auto result = swd2::MonolithicRuntime(std::move(modules)).run(context);
    require(result.transitions.size() == 2, "monolithic runtime did not call MEO then RPG");
    require(result.transitions[0].module == swd2::Module::menu,
            "monolithic runtime did not begin with MEO");
    require(result.transitions[1].module == swd2::Module::rpg,
            "monolithic runtime did not continue in-process to RPG");
    require(platform.presented == 113 && platform.music_calls == 2 &&
                platform.stop_calls == 2,
            "MEO, RPG title/load and world did not remain in one process");
}

void test_rpg_opening_menu(const std::filesystem::path& game_root) {
    {
        ScriptedPlatform platform;
        platform.actions = {swd2::InputAction::confirm};
        auto original = swd2::SharedState::load(game_root / "SAVE.DA1");
        swd2::GameContext context{game_root, original, platform};
        require(swd2::RpgModule().run(
                    context, swd2::Marker::menu_ready) ==
                    swd2::Marker::open_demo &&
                    context.shared_state.bytes() == original.bytes() &&
                    platform.presented == 43U && platform.wait_calls == 1U &&
                    platform.poll_calls == 0U &&
                    platform.frontend_quit_poll_calls == 42U &&
                    platform.delay_calls == 42U &&
                    platform.delayed_milliseconds == 600U &&
                    platform.music_calls == 1U && platform.stop_calls == 1U &&
                    platform.palette_hashes.front() !=
                        platform.palette_hashes.back() &&
                    platform.frame_hashes[21] ==
                        6322980293999423254ULL &&
                    platform.palette_hashes[21] ==
                        3016666169878880281ULL &&
                    platform.palette_hashes.back() ==
                        17828133145641756547ULL,
                "RPG MT path did not reproduce OP01 fade and default New Game");
    }

    {
        ScriptedPlatform platform;
        platform.actions = {
            swd2::InputAction::down,
            swd2::InputAction::confirm,
            swd2::InputAction::confirm,
            swd2::InputAction::confirm,
            swd2::InputAction::quit,
        };
        auto initial = swd2::SharedState::load(game_root / "SAVE.DAQ");
        auto selected = std::uint8_t{0};
        auto loads = std::size_t{0};
        swd2::GameContext context{game_root, initial, platform};
        context.load_slot = [&](std::uint8_t slot) {
            selected = slot;
            ++loads;
            return swd2::LoadedSaveSlot{
                swd2::SharedState::load(
                    game_root / ("SAVE.DA" + std::to_string(slot))),
                std::make_shared<swd2::MapDatabase>(
                    swd2::MapDatabase::load(
                        game_root / ("MAPZ.DA" + std::to_string(slot))))};
        };
        const auto result = swd2::RpgModule().run(
            context, swd2::Marker::menu_ready);
        const auto slot_one = swd2::SharedState::load(
            game_root / "SAVE.DA1");
        require(result == swd2::Marker::none &&
                    selected == 1U && loads == 1U &&
                    context.map_database != nullptr &&
                    context.shared_state.u16(0x2c) == slot_one.u16(0x2c) &&
                    context.shared_state.u16(0x2c) != initial.u16(0x2c) &&
                    platform.presented == 47U && platform.wait_calls == 4U &&
                    platform.poll_calls == 1U &&
                    platform.music_calls == 2U && platform.stop_calls == 2U &&
                    platform.frame_hashes[21] ==
                        6322980293999423254ULL &&
                    platform.frame_hashes[22] ==
                        17092816181454245754ULL &&
                    platform.frame_hashes[23] ==
                        10706129714087963303ULL &&
                    platform.frame_hashes[24] ==
                        14165905608805928121ULL &&
                    platform.palette_hashes[45] ==
                        17828133145641756547ULL &&
                    platform.frame_hashes[46] ==
                        5188932088196950066ULL &&
                    platform.palette_hashes[46] ==
                        17136998718566118143ULL,
                "RPG Continue did not select and atomically install SAVE/MAPZ");
    }

    {
        ScriptedPlatform platform;
        platform.actions = {swd2::InputAction::quit};
        auto initial = swd2::SharedState::load(game_root / "SAVE.DA1");
        auto wrong_map = std::make_shared<swd2::MapDatabase>(
            swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
        swd2::GameContext context{game_root, initial, platform, wrong_map};
        const auto expected_state = swd2::SharedState::load(
            game_root / "SAVE.DAQ");
        const auto expected_map = swd2::MapDatabase::load(
            game_root / "MAPZ.DAQ");
        const auto result = swd2::RpgModule().run(
            context, swd2::Marker::returned_from_demo);
        require(result == swd2::Marker::none &&
                    context.map_database != nullptr &&
                    context.shared_state.map_location_directory_offset() == 8U &&
                    context.shared_state.u16(0x2c) == expected_state.u16(0x2c) &&
                    std::equal(context.map_database->serialized_bytes().begin(),
                               context.map_database->serialized_bytes().end(),
                               expected_map.serialized_bytes().begin(),
                               expected_map.serialized_bytes().end()) &&
                    platform.presented == 83U && platform.direct_updates == 37U &&
                    platform.poll_calls == 1U && platform.stop_calls == 1U,
                "RPG OM path did not install DAQ and dispatch opening entity two");
    }
}

void test_rpg_entity_dialogue(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    // The first key skips 49d0's remaining glyph delays. It is deliberately
    // separate from the later key which dismisses the final cursor.
    platform.text_actions = {swd2::InputAction::confirm};
    platform.actions = {
        swd2::InputAction::confirm,  // interact with SA068 entity
        swd2::InputAction::confirm,  // close its CHNA1 dialogue
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    // MAPZ entity 1 is anchored at world (139,111). Put the player immediately
    // to its left while preserving RPG.EXE's screen-position convention.
    state.set_viewport_x(118);
    state.set_viewport_y(99);
    state.set_actor_direction(9);
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG entity-dialogue run did not terminate normally");
    require(platform.presented == 4 && platform.music_calls == 1 &&
                platform.stop_calls == 1 && platform.frame_hashes.size() == 4 &&
                platform.frame_hashes[2] == 15418186358338427649ULL &&
                platform.text_cursor == 1U && platform.text_poll_calls == 1U &&
                platform.direct_updates == 2U,
            "RPG did not present dialogue and manage map music in-process");
}

void test_rpg_event_program_exit(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(8);
    // CHNA1 entry 176 shows one timed dialogue and then executes opcode 52.
    // The trailing queued Quit must remain unread: 52 terminates RPG.EXE
    // immediately rather than restarting the map loop for another poll.
    location.area.entity_fields[9][1] = 176U * 2U;

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::confirm,  // interact with the entity
        swd2::InputAction::confirm,  // close opcode-20 dialogue
        swd2::InputAction::quit,     // must not reach the world loop
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_viewport_x(118);
    state.set_viewport_y(99);
    state.set_actor_direction(9);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none &&
                platform.cursor == 2U && platform.poll_calls == 2U &&
                platform.presented == 3U && platform.stop_calls == 1U,
            "RPG opcode 52 returned to the map loop instead of exiting the module");
}

void test_rpg_idle_world_ticks(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::none, swd2::InputAction::quit};
    swd2::GameContext context{
        game_root, swd2::SharedState::load(game_root / "SAVE.DA1"), platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none,
            "RPG idle world tick did not terminate normally");
    require(platform.poll_calls == 2U && platform.wait_calls == 0U &&
                platform.presented == 2U &&
                platform.palette_hashes.size() == 2U &&
                platform.palette_hashes[0] != platform.palette_hashes[1],
            "RPG world loop blocked for input or froze its RSK palette cycle");
}

void test_rpg_map_portal(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    const auto map = swd2::MapResource::load(game_root / "T1" / "AREA1");
    require(map.layout().width == 180U && map.layout().height == 180U,
            "AREA1 portal oracle dimensions changed");

    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::quit};
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    // AREA1's second MAP0 record spans 4748h..474ch and carries action 200ch.
    // Position the actor's centre over its first 1000h-marked RAP cell while
    // keeping the ordinary 38/80 screen anchor.
    state.set_viewport_x(100);
    state.set_viewport_y(38);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
                              state.u16(0x40f) +
                              (38U * map.layout().width + 100U) * 2U));
    state.set_u16(0x40a, 0xffffU);
    state.set_u8(0x51f, 0U);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto portal_marker = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(portal_marker == swd2::Marker::none &&
                platform.presented == 2U && platform.poll_calls == 1U &&
                platform.music_calls == 2U && platform.stop_calls == 1U &&
                platform.frame_hashes[1] == 15088074390453743917ULL &&
                context.shared_state.map_location_directory_offset() == 12U &&
                context.shared_state.u16(0x40a) == 1U &&
                context.shared_state.u8(0x51f) == 1U &&
                context.shared_state.area_graphics_path() ==
                    database->location_at_directory_offset(12)
                        .area.graphics_path,
            "RPG e94 did not enter the initial MAP0 portal before input");
    for (std::size_t actor = 0; actor < 12U; ++actor) {
        require(context.shared_state.u16(0x42U + actor * 2U) == 0U,
                "RPG 136c did not install the BMAN horizontal offsets");
    }
    static constexpr std::array<std::uint16_t, 4> bman_y_offsets = {
        0xfff1U, 0xfff5U, 0xfff1U, 0xfff0U};
    for (std::size_t group = 0; group < bman_y_offsets.size(); ++group) {
        for (std::size_t actor = 0; actor < 3U; ++actor) {
            require(context.shared_state.u16(
                        0x5aU + group * 6U + actor * 2U) ==
                        bman_y_offsets[group],
                    "RPG 136c did not install the BMAN vertical offsets");
        }
    }
}

void test_rpg_map_chained_spawn_trigger(
    const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(state, *database, 180U);
    const auto& source = database->location_at_directory_offset(180U);
    auto graphics = swd2::normalize_dos_asset_path(source.area.graphics_path);
    auto layout = swd2::normalize_dos_asset_path(source.area.layout_path);
    graphics.replace_extension();
    layout.replace_extension();
    const auto map = swd2::MapResource::load(game_root / graphics,
                                              game_root / layout);
    const auto map_base = static_cast<std::uint16_t>(
        source.map_position -
        (source.viewport_y * map.layout().width + source.viewport_x) * 2U);
    constexpr std::uint16_t portal_cell = 0xe16cU;
    const auto portal_index =
        static_cast<std::size_t>((portal_cell - map_base) / 2U);
    const auto portal_x = portal_index % map.layout().width;
    const auto portal_y = portal_index / map.layout().width;
    require(map.layout().width == 180U && map.layout().height == 180U &&
                portal_x == 50U && portal_y == 160U &&
                (map.cells()[portal_index] & 0x1000U) != 0U,
            "DAU2 chained MAP0 portal oracle changed");

    // DAU2's normal action 00b8h loads location 184. Its destination spawn
    // is itself on special action 4006h, so e94 must install BMAN2 before the
    // first DAU3 page flip rather than exposing one intermediate BMAN1 frame.
    state.set_u16(0x40f, map_base);
    state.set_viewport_x(static_cast<std::uint16_t>(portal_x - 20U));
    state.set_viewport_y(static_cast<std::uint16_t>(portal_y - 12U));
    state.set_actor_screen_x(38U);
    state.set_actor_screen_y(80U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        map_base +
        (state.viewport_y() * map.layout().width + state.viewport_x()) * 2U));
    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::quit};
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto marker = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(marker == swd2::Marker::none &&
                platform.presented == 2U && platform.poll_calls == 1U &&
                platform.frame_hashes.size() == 2U &&
                platform.frame_hashes[0] == 16514004362969091039ULL &&
                platform.frame_hashes[1] == 1559950284723583022ULL &&
                context.shared_state.map_location_directory_offset() == 184U,
            "RPG e94 exposed an intermediate frame before a spawn trigger");
}

void test_rpg_opcode37_chained_spawn_event(
    const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& source = database->location_at_directory_offset(736U);
    require(source.area.event_archive_path == "CHNA5.EXE" &&
                source.area.entity_count() == 20U,
            "CHNA5 opcode-37 chain oracle area changed");
    // Run released CHNA5 entry 16 through nearby entity one. It relocates to
    // location 756 whose spawn action 400fh immediately executes entity zero;
    // that nested released event ends in opcode 59 and must prevent the outer
    // entry's post-relocation fade/dialogue tail from running.
    // Entry 16 changes its current entity's future event pointer. Using entity
    // one preserves destination entity zero's released entry-14 battle event.
    source.area.entity_fields[9][1] = 32U;
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(state, *database, 736U);
    auto graphics = swd2::normalize_dos_asset_path(source.area.graphics_path);
    auto layout = swd2::normalize_dos_asset_path(source.area.layout_path);
    graphics.replace_extension();
    layout.replace_extension();
    const auto map = swd2::MapResource::load(game_root / graphics,
                                              game_root / layout);
    const auto map_base = static_cast<std::uint16_t>(
        source.map_position -
        (source.viewport_y * map.layout().width + source.viewport_x) * 2U);
    require(map.layout().width == 180U && map.layout().height == 180U &&
                map_base == 8U,
            "SD01 opcode-37 chain RAP oracle changed");
    state.set_u16(0x40f, map_base);
    state.set_viewport_x(0U);
    state.set_viewport_y(127U);
    state.set_actor_screen_x(74U);  // world (38,139), immediately left of entity 1
    state.set_actor_screen_y(80U);
    state.set_actor_direction(9U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        map_base +
        (state.viewport_y() * map.layout().width + state.viewport_x()) * 2U));
    ScriptedPlatform platform;
    platform.actions.assign(64U, swd2::InputAction::confirm);
    platform.actions.push_back(swd2::InputAction::quit);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto marker = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    // The last two nonblack pages bracket entry 23's opcode-39 SA082 frame
    // change before its battle transition turns the palette black.
    require(marker == swd2::Marker::open_figure &&
                platform.presented == 109U && platform.poll_calls == 33U &&
                platform.cursor == 33U &&
                context.shared_state.map_location_directory_offset() == 756U &&
                (context.shared_state.u16(0x51a) & 0x0008U) != 0U,
            "RPG opcode 37 did not dispatch its immediate MAP0 spawn event");
}

void test_rpg_sa_scripted_entity_frames(
    const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(828U);
    require(location.area.event_archive_path == "CHNA5.EXE" &&
                location.area.entity_count() == 8U &&
                location.area.entity_fields[0][0] == 0x5200U &&
                location.area.entity_fields[9][0] == 46U,
            "WEST12 SA082 scripted-animation oracle changed");

    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(state, *database, 828U);
    auto graphics = swd2::normalize_dos_asset_path(location.area.graphics_path);
    auto layout = swd2::normalize_dos_asset_path(location.area.layout_path);
    graphics.replace_extension();
    layout.replace_extension();
    const auto map = swd2::MapResource::load(game_root / graphics,
                                              game_root / layout);
    require(map.layout().width == 130U && map.layout().height == 230U &&
                map.cell_base() == 8U,
            "WEST12 scripted-animation RAP oracle changed");
    // Entity zero is at world (49,31); place the leader immediately west.
    state.set_u16(0x40f, map.cell_base());
    state.set_viewport_x(28U);
    state.set_viewport_y(19U);
    state.set_actor_screen_x(38U);
    state.set_actor_screen_y(80U);
    state.set_actor_direction(9U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        map.cell_base() + (19U * map.layout().width + 28U) * 2U));

    ScriptedPlatform platform;
    platform.text_actions.assign(64U, swd2::InputAction::confirm);
    platform.actions.assign(64U, swd2::InputAction::confirm);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto marker = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(marker == swd2::Marker::open_figure &&
                platform.presented == 52U && platform.poll_calls == 4U &&
                platform.cursor == 4U && platform.text_poll_calls == 0U &&
                platform.frame_hashes.size() == 52U &&
                platform.frame_hashes[30] == 11619654329426894177ULL &&
                platform.frame_hashes[31] == 18286921205049796719ULL,
            "RPG did not execute the visible SA082 opcode-39 sequence");
}

void test_rpg_map_special_event(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    const auto map = swd2::MapResource::load(game_root / "T1" / "AREA1");
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::confirm,  // close CHNA1 entry 160 dialogue
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    // MAP0 action 4002h at 1f50h calls 52b4 with BX=6, temporarily
    // faces AREA1 entity three toward the leader and executes its 13eh event.
    state.set_viewport_x(24);
    state.set_viewport_y(10);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
                              state.u16(0x40f) +
                              (10U * map.layout().width + 24U) * 2U));
    const auto original_y = state.world_y();
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto marker = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(marker == swd2::Marker::none &&
                platform.presented == 6U && platform.wait_calls == 0U &&
                platform.poll_calls == 2U &&
                context.shared_state.map_location_directory_offset() == 8U &&
                context.shared_state.world_y() == original_y + 3U &&
                database->location_at_directory_offset(8)
                        .area.entity_fields[1][3] == 0U,
            "RPG f19/52b4 did not dispatch and restore a map special event");
}

void test_rpg_map_actor_variant(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(state, *database, 188);
    const auto& location = database->location_at_directory_offset(188);
    auto graphics = swd2::normalize_dos_asset_path(location.area.graphics_path);
    auto layout = swd2::normalize_dos_asset_path(location.area.layout_path);
    graphics.replace_extension();
    layout.replace_extension();
    const auto map = swd2::MapResource::load(game_root / graphics,
                                              game_root / layout);
    require(map.layout().width == 180U && map.layout().height == 180U,
            "DAU4 BMAN trigger oracle dimensions changed");
    const auto map_base = static_cast<std::uint16_t>(
        location.map_position -
        (location.viewport_y * map.layout().width + location.viewport_x) * 2U);
    state.set_u16(0x40f, map_base);
    // DAU4's first 4007h rectangle begins at 1110h (world 20,12).
    state.set_viewport_x(0);
    state.set_viewport_y(0);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, map_base);
    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::none, swd2::InputAction::quit};
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto variant_marker = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(variant_marker == swd2::Marker::none &&
                platform.presented == 2U && platform.poll_calls == 2U &&
                platform.frame_hashes[0] == 5050928523494376350ULL &&
                platform.frame_hashes[1] == 18246363941339037203ULL &&
                context.shared_state.map_location_directory_offset() == 188U,
            "RPG f19 action 7 did not replace BMAN1 with BMAN3");
}

void test_rpg_top_dialogue_panel(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(684);
    require(location.area.event_archive_path == "CHNA6.EXE" &&
                location.area.entity_count() >= 1,
            "RPG top-dialogue oracle location changed");
    std::fill(location.area.entity_fields[3].begin(),
              location.area.entity_fields[3].end(), 3);
    location.area.entity_fields[3][0] = 0;
    location.area.entity_fields[9][0] = 250;  // CHNA6 entry 125: opcode 46

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto top_map = swd2::MapResource::load(game_root / "T2" / "T2ROA");
    require(top_map.layout().width == 71U && top_map.layout().height == 39U &&
                top_map.cell_base() == 8U,
            "T2ROA RAP pointer-directory oracle changed");
    const auto map_width = top_map.layout().width;
    const auto cell_base = top_map.cell_base();
    state.set_u16(0x424, 684);
    state.set_u16(0x40f, cell_base);
    state.set_viewport_x(16);
    state.set_viewport_y(14);
    state.set_actor_screen_x(32);  // world x 33, left of entity x 34
    state.set_actor_screen_y(40);  // world y 21
    state.set_actor_direction(9);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        cell_base + (21U * map_width + 33U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto top_result = swd2::RpgModule().run(context, swd2::Marker::continue_rpg);
    const auto top_archive = swd2::ScriptArchive::load(game_root / "CHNA6.EXE");
    const auto top_record = swd2::decode_event_record(
        top_archive.event_stream(125));
    const auto count_glyphs = [](std::span<const std::uint8_t> text) {
        auto count = std::size_t{};
        for (std::size_t offset = 0; offset < text.size();) {
            if (text[offset] == ' ') {
                ++offset;
            } else if (offset + 1U < text.size() &&
                       ((text[offset] == '#' && text[offset + 1U] == '#') ||
                        (text[offset] == '%' && text[offset + 1U] == '%'))) {
                offset += 2U;
            } else {
                ++count;
                offset += 2U;
            }
        }
        return count;
    };
    const auto ordinary_glyphs = count_glyphs(top_record.commands[0].text);
    const auto forced_glyphs = count_glyphs(top_record.commands[3].text);
    require(top_result ==
                swd2::Marker::none &&
                platform.cursor == platform.actions.size() &&
                platform.frame_hashes.size() == 12 &&
                platform.frame_hashes[10] == 8053114325420148260ULL &&
                platform.text_poll_calls == ordinary_glyphs &&
                platform.direct_updates == ordinary_glyphs + forced_glyphs,
            "RPG opcode-46 top-dialogue run did not terminate normally");
}

void test_rpg_field_menu_inventory(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,   // RPG second action key: open 2e63.
        swd2::InputAction::right,    // select Item on the diamond.
        swd2::InputAction::confirm,  // enter 39ed general inventory.
        swd2::InputAction::cancel,   // return to the diamond.
        swd2::InputAction::cancel,   // return to the map.
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG field-menu inventory run did not terminate normally");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 6U && platform.stop_calls == 1U,
            "RPG 2e63 diamond did not enter/return from 39ed inventory");
    require(platform.frame_hashes.size() == 6U &&
                platform.frame_hashes[1] != platform.frame_hashes[0] &&
                platform.frame_hashes[2] != platform.frame_hashes[1] &&
                platform.frame_hashes[3] != platform.frame_hashes[2] &&
                platform.frame_hashes[3] == 13340828503804055853ULL &&
                platform.frame_hashes[4] == platform.frame_hashes[2] &&
                platform.frame_hashes[5] == platform.frame_hashes[0],
            "RPG field-menu page selection/return frames were not stable");

    ScriptedPlatform reopen_platform;
    reopen_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::cancel,   // store 37fd/37ff
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,   // reopen field diamond
        swd2::InputAction::right,
        swd2::InputAction::confirm,  // inventory resumes on row two
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto reopen_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::GameContext reopen_context{
        game_root, reopen_state, reopen_platform};
    const auto reopen_result = swd2::RpgModule().run(
        reopen_context, swd2::Marker::continue_rpg);
    require(reopen_result == swd2::Marker::none &&
                reopen_platform.cursor == reopen_platform.actions.size() &&
                reopen_platform.frame_hashes.size() == 13U &&
                reopen_platform.frame_hashes[5] ==
                    reopen_platform.frame_hashes[10] &&
                reopen_platform.frame_hashes[5] == 17863590386344463468ULL,
            "RPG 39ed did not restore 37fd/37ff after reopening inventory");
}

void test_rpg_inventory_item_actions(const std::filesystem::path& game_root) {
    // RPG:39ed must copy the completed inventory page before 2d0f adds the
    // directional Use/Explain/Discard cards. Exercise ITEM2 explanation and
    // return through that action page without mutating the selected object.
    ScriptedPlatform explain_platform;
    explain_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,  // choose value two
        swd2::InputAction::confirm,  // 46cc default Yes
        swd2::InputAction::confirm,  // physical slot zero
        swd2::InputAction::right,    // Explain
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // acknowledge ITEM2 text
        swd2::InputAction::cancel,   // action page -> inventory
        swd2::InputAction::cancel,   // inventory -> diamond
        swd2::InputAction::cancel,   // diamond -> map
        swd2::InputAction::quit,
    };
    auto explain_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    explain_state.set_u16(0x382, 98);  // 人髮: descriptive and discardable
    swd2::GameContext explain_context{
        game_root, explain_state, explain_platform};
    require(swd2::RpgModule().run(
                explain_context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                explain_platform.cursor == explain_platform.actions.size() &&
                explain_context.shared_state.u16(0x382) == 98,
            "RPG item Explain action did not preserve/return to 2d0f");
    require(explain_platform.frame_hashes.size() >= 11U &&
                explain_platform.frame_hashes[4] !=
                    explain_platform.frame_hashes[3] &&
                explain_platform.frame_hashes[6] !=
                    explain_platform.frame_hashes[5],
            "RPG item action/ITEM2 message pages were not composed");

    // Seven ITEM2 records contain 49d0's literal %% page separator.  Item
    // 250 has two pages and exercises the intermediate MENU frame-91h wait
    // before the ordinary animated final acknowledgement.
    ScriptedPlatform paged_platform;
    paged_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // %% continuation
        swd2::InputAction::confirm,  // final page acknowledgement
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto paged_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    paged_state.set_u16(0x382, 250);
    swd2::GameContext paged_context{game_root, paged_state, paged_platform};
    require(swd2::RpgModule().run(
                paged_context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                paged_platform.cursor == paged_platform.actions.size() &&
                paged_context.shared_state.u16(0x382) == 250U,
            "RPG paged ITEM2 explanation did not return to 2d0f");
    require(paged_platform.frame_hashes.size() == 12U &&
                paged_platform.frame_hashes[6] !=
                    paged_platform.frame_hashes[7] &&
                paged_platform.frame_hashes[8] ==
                    paged_platform.frame_hashes[5],
            "RPG 49d0 %% continuation did not show both ITEM2 pages");

    // The upper 2d0f card is Discard.  DATA:3754 is followed by 46cc's
    // default-Yes selector; confirming clears the physical word and invokes
    // the original stable 3ced compaction before reconstructing 39ed.
    ScriptedPlatform discard_platform;
    discard_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Yes
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto discard_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    discard_state.set_u16(0x382, 98);
    discard_state.set_u16(0x384, 99);
    swd2::GameContext discard_context{
        game_root, discard_state, discard_platform};
    require(swd2::RpgModule().run(
                discard_context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                discard_platform.cursor == discard_platform.actions.size() &&
                discard_context.shared_state.u16(0x382) == 99 &&
                discard_context.shared_state.u16(0x384) == 0,
            "RPG item Discard action did not clear/compact the selected slot");

    ScriptedPlatform use_platform;
    use_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // first actor target
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto use_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    use_state.set_u16(0x382, 76);  // 絞心丸, effect 16h
    use_state.set_u16(0x106 + 8, 0);
    use_state.set_u16(0x106 + 0x35, 0);
    use_state.set_u16(0x106 + 0x37, 100);
    swd2::GameContext use_context{game_root, use_state, use_platform};
    require(swd2::RpgModule().run(
                use_context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                use_platform.cursor == use_platform.actions.size() &&
                use_context.shared_state.u16(0x382) == 0U &&
                use_context.shared_state.u16(0x106 + 0x35) == 50U,
            "RPG item Use action did not target/apply/consume from 2d0f");

    // Type-10 field talismans are ability_id+8ch. RPG:3857 charges the
    // selected target's +55 resource from the original ability record before
    // dispatch, even though the ITEM itself is also consumed by +05 bit 04h.
    const auto item_definitions = swd2::ItemDatabase::load(
        game_root / "ITEM.EXE");
    require(item_definitions.at(190).type == 0x10U &&
                item_definitions.at(190).effect_code == 1U &&
                item_definitions.at(190).consumed_on_use(),
            "fixture no longer maps field talisman 190 to ability 50");
    ScriptedPlatform talisman_platform;
    talisman_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // target actor zero
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto talisman_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    talisman_state.set_u16(0x382U, 190U);
    talisman_state.set_u16(0x106U + 8U, 0U);
    talisman_state.set_u16(0x106U + 0x2dU, 0U);
    talisman_state.set_u16(0x106U + 0x2fU, 100U);
    talisman_state.set_u16(0x106U + 0x55U, 10U);
    swd2::GameContext talisman_context{
        game_root, talisman_state, talisman_platform};
    const auto talisman_result = swd2::RpgModule().run(
        talisman_context, swd2::Marker::continue_rpg);
    require(talisman_result == swd2::Marker::none &&
                talisman_platform.cursor == talisman_platform.actions.size() &&
                talisman_platform.frame_hashes.size() == 9U &&
                talisman_platform.frame_hashes[2] == 17341303582624991656ULL &&
                talisman_context.shared_state.u16(0x382U) == 0U &&
                talisman_context.shared_state.u16(0x106U + 0x55U) == 3U &&
                talisman_context.shared_state.u16(0x106U + 0x2dU) != 0U,
            "RPG type-10 field talisman did not charge, apply and consume");

    ScriptedPlatform talisman_error_platform;
    talisman_error_platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // target actor zero
        swd2::InputAction::confirm,  // dismiss DATA:3630
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto talisman_error_state = swd2::SharedState::load(
        game_root / "SAVE.DA1");
    talisman_error_state.set_u16(0x382U, 190U);
    talisman_error_state.set_u16(0x106U + 8U, 0U);
    talisman_error_state.set_u16(0x106U + 0x2dU, 0U);
    talisman_error_state.set_u16(0x106U + 0x2fU, 100U);
    talisman_error_state.set_u16(0x106U + 0x55U, 6U);
    swd2::GameContext talisman_error_context{
        game_root, talisman_error_state, talisman_error_platform};
    require(swd2::RpgModule().run(
                talisman_error_context, swd2::Marker::continue_rpg) ==
                    swd2::Marker::none &&
                talisman_error_platform.cursor ==
                    talisman_error_platform.actions.size() &&
                talisman_error_context.shared_state.u16(0x382U) == 190U &&
                talisman_error_context.shared_state.u16(0x106U + 0x55U) == 6U &&
                talisman_error_context.shared_state.u16(0x106U + 0x2dU) == 0U &&
                talisman_error_platform.bottom_hashes[6] !=
                    talisman_error_platform.bottom_hashes[5],
            "RPG insufficient talisman resource did not preserve item/target state");
}

void test_rpg_inventory_alchemy(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // first ingredient, slot zero
        swd2::InputAction::down,     // 煉妖壺 bottom card
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // acknowledge DATA:36c8
        swd2::InputAction::down,     // temporary hole -> second ingredient
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // combine pair: Yes
        swd2::InputAction::confirm,  // accept resulting product: Yes
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u8(0x3f1, 0);  // enable 2d0f's fourth action card
    state.set_u16(0x382, 98);
    state.set_u16(0x384, 99);
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                platform.cursor == platform.actions.size() &&
                context.shared_state.u16(0x382) == 458U &&
                context.shared_state.u16(0x384) == 0U,
            "RPG 4397 alchemy flow did not consume two items/create product");
    require(platform.presented == platform.actions.size() &&
                platform.frame_hashes.size() == 14U &&
                platform.frame_hashes[7] == 11679310933776722775ULL &&
                platform.frame_hashes[8] == 2795229981088272918ULL &&
                platform.frame_hashes[9] == 12664240429231480625ULL &&
                platform.frame_hashes[10] == 14297608139487699870ULL &&
                platform.frame_hashes[11] == 10609575317381259566ULL &&
                platform.frame_hashes[12] == platform.frame_hashes[2] &&
                platform.frame_hashes[13] == platform.frame_hashes[0] &&
                std::set<std::uint64_t>(platform.frame_hashes.begin(),
                                        platform.frame_hashes.end()).size() >= 8U,
            "RPG alchemy ingredient/result pages were not presented");
}

void test_rpg_inventory_equipment_screen(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,   // map -> field diamond
        swd2::InputAction::right,    // Item
        swd2::InputAction::confirm,  // inventory
        swd2::InputAction::confirm,  // physical slot zero
        swd2::InputAction::confirm,  // Equip action card
        swd2::InputAction::confirm,  // actor zero
        swd2::InputAction::down,
        swd2::InputAction::down,     // original equipment cursor starts at row zero
        swd2::InputAction::confirm,  // replace the existing two-handed item
        swd2::InputAction::cancel,   // equipment page -> inventory
        swd2::InputAction::cancel,   // inventory -> field diamond
        swd2::InputAction::cancel,   // field diamond -> map
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x382U, 117U);  // category-nine two-handed equipment
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                platform.cursor == platform.actions.size(),
            "RPG equipment-page run did not terminate normally");
    require(context.shared_state.u16(0x382U) == 122U &&
                context.shared_state.u16(0x106U + 0x14U) == 117U &&
                context.shared_state.u16(0x106U + 0x16U) == 117U &&
                context.shared_state.u8(0x106U + 0x2cU) == 1U,
            "RPG 425b equipment page did not exchange the selected item");
    require(platform.frame_hashes.size() == 13U &&
                platform.frame_hashes[3] == 17346323509180310990ULL &&
                platform.frame_hashes[4] == 9580035020130697964ULL &&
                platform.frame_hashes[5] == 1193126455766717997ULL &&
                platform.frame_hashes[6] == 2557044951284384639ULL &&
                platform.frame_hashes[7] == 11490455812655608249ULL &&
                platform.frame_hashes[8] == 11095224185392137962ULL &&
                platform.frame_hashes[9] == 9630457108031417445ULL &&
                platform.frame_hashes[10] == 3578951147401443181ULL &&
                platform.frame_hashes[11] == platform.frame_hashes[2] &&
                platform.frame_hashes[12] == platform.frame_hashes[0],
            "RPG 3feb equipment redraw/return frames were not stable");
}

void test_rpg_inventory_empty_slot_unequip(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,   // map -> field diamond
        swd2::InputAction::right,    // Item
        swd2::InputAction::confirm,  // inventory, physical slot zero is empty
        swd2::InputAction::confirm,  // empty selection enters 3f53 directly
        swd2::InputAction::confirm,  // actor zero
        swd2::InputAction::down,     // equipment selector starts at row zero
        swd2::InputAction::confirm,  // unequip row one into the empty bag cell
        swd2::InputAction::cancel,   // equipment page -> inventory
        swd2::InputAction::cancel,   // inventory -> field diamond
        swd2::InputAction::cancel,   // field diamond -> map
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    require(state.u16(0x382U) == 0U &&
                state.u16(0x106U + 0x12U) == 165U,
            "empty-slot unequip fixture no longer matches SAVE.DA1");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                platform.cursor == platform.actions.size(),
            "RPG empty-slot unequip run did not terminate normally");
    require(context.shared_state.u16(0x382U) == 165U &&
                context.shared_state.u16(0x106U + 0x12U) == 0U,
            "RPG 3f53/425b did not move equipped item into an empty bag cell");
    require(platform.frame_hashes.size() == 11U &&
                platform.frame_hashes[3] == 13340828503804055853ULL &&
                platform.frame_hashes[4] == 11432918589084695794ULL &&
                platform.frame_hashes[5] == 6062538655916986583ULL &&
                platform.frame_hashes[6] == 14884106248727251905ULL &&
                platform.frame_hashes[7] == 16959856505073652608ULL &&
                platform.frame_hashes[8] == 14429707993458738632ULL &&
                platform.frame_hashes[9] == platform.frame_hashes[2] &&
                platform.frame_hashes[10] == platform.frame_hashes[0],
            "RPG empty-slot equipment/return frames were not stable");
}

void test_rpg_field_status_menu(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::left,     // Status
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // first actor
        swd2::InputAction::page_down,
        swd2::InputAction::page_down, // equipment rows
        swd2::InputAction::cancel,   // back to actor selector
        swd2::InputAction::cancel,   // back to field diamond
        swd2::InputAction::cancel,   // back to map
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x106U + 8U, 0x1ffeU);
    state.set_u16(0x106U + 0x2dU, 0U);
    state.set_u16(0x106U + 0x35U,
                  static_cast<std::uint16_t>(
                      state.u16(0x106U + 0x37U) >> 2U));
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG field Status run did not terminate normally");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 10U && platform.stop_calls == 1U,
            "RPG 2f64 actor selector did not enter/return from 26f3 Status");
    require(platform.frame_hashes[4] != platform.frame_hashes[3] &&
                platform.frame_hashes[4] == 10287332555232791096ULL &&
                platform.frame_hashes[5] != platform.frame_hashes[4] &&
                platform.frame_hashes[5] == 7309384108166588722ULL &&
                platform.frame_hashes[6] != platform.frame_hashes[5] &&
                platform.frame_hashes[6] == 6262036099231779612ULL &&
                platform.frame_hashes[7] == platform.frame_hashes[3] &&
                platform.frame_hashes[8] == platform.frame_hashes[2] &&
                platform.frame_hashes[9] == platform.frame_hashes[0],
            "RPG Status paging/return frames were not stable");
}

void test_rpg_field_magic_menu(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,       // Magic
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // first actor
        swd2::InputAction::page_down,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG field Magic run did not terminate normally");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 9U && platform.stop_calls == 1U,
            "RPG 2fb7 actor selector/list did not return through the field menu");
    require(platform.frame_hashes[4] != platform.frame_hashes[3] &&
                platform.frame_hashes[4] == 5845923941079099482ULL &&
                platform.frame_hashes[5] != platform.frame_hashes[4] &&
                platform.frame_hashes[5] == 5574429281769751476ULL &&
                platform.frame_hashes[6] == platform.frame_hashes[3] &&
                platform.frame_hashes[7] == platform.frame_hashes[2] &&
                platform.frame_hashes[8] == platform.frame_hashes[0],
            "RPG Magic paging/return frames were not stable");
}

void test_rpg_field_magic_cast(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::right,    // actor one: ability 50 / action 01h
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // target actor zero
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto actor_base = 0x106U + 0x9fU;
    const auto before = state.u16(actor_base + 0x55U);
    require(state.u8(actor_base + 0x6dU) == 50U && before >= 7U,
            "fixture no longer exposes actor-one field ability 50");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(actor_base + 0x55U) == before - 7U,
            "RPG field ability did not dispatch/deduct DATA:1dce cost");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 12U && platform.stop_calls == 1U,
            "RPG field ability target/cast did not return to its source list");
}

void test_rpg_field_magic_value_error(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::right,    // actor one: ability 50 / action 01h
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // dismiss DATA:3630
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto actor_base = 0x106U + 0x9fU;
    require(state.u8(actor_base + 0x6dU) == 50U,
            "fixture no longer exposes actor-one field ability 50");
    state.set_u16(actor_base + 0x55U, 0);
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(actor_base + 0x55U) == 0U,
            "RPG insufficient ability resource changed actor state");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 12U && platform.stop_calls == 1U &&
                platform.bottom_hashes[7] != platform.bottom_hashes[6],
            "RPG DATA:3630 resource feedback did not preserve the ability list");
}

void test_rpg_field_magic_description(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::right,    // actor one / ability 50
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::right,    // Explain
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // dismiss DATE2 description
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG DATE2 field-ability description run did not terminate");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 14U && platform.stop_calls == 1U &&
                platform.frame_hashes[6] != platform.frame_hashes[7] &&
                platform.bottom_hashes[8] != platform.bottom_hashes[7] &&
                platform.frame_hashes[9] == platform.frame_hashes[7],
            "RPG 2c0b Explain card did not restore its selected source page");
}

void test_rpg_field_magic_refine(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::right,    // actor one / type-four ability 50
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::up,       // Refine talisman
        swd2::InputAction::confirm,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto actor_base = 0x106U + 0x9fU;
    const auto before = state.u16(actor_base + 0x55U);
    require(state.u8(actor_base + 0x6dU) == 50U && state.u16(0x3e4U) == 0U,
            "fixture no longer exposes an empty type-four refine slot");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(0x382U) == 50U + 0x8cU &&
                context.shared_state.u16(actor_base + 0x55U) == before,
            "RPG 333d Refine did not create/compact the type-10 talisman");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 13U && platform.stop_calls == 1U &&
                platform.frame_hashes[7] == platform.frame_hashes[8],
            "RPG Refine did not retain the upper 2c0b action card");
}

void test_rpg_field_magic_refine_full(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // dismiss DATA:36e0
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x3e4U, 1U);
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(0x3e4U) == 1U,
            "RPG full-inventory Refine changed physical slot 49");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 14U && platform.stop_calls == 1U &&
                platform.bottom_hashes[8] != platform.bottom_hashes[7] &&
                platform.frame_hashes[9] == platform.frame_hashes[7],
            "RPG DATA:36e0 Refine capacity feedback did not preserve 2c0b");
}

void test_rpg_field_magic_material_cast(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // ability 41 / five materials
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // target actor zero
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u8(0x106U + 0x6dU, 41U);
    for (std::size_t material = 0; material < 5U; ++material) {
        state.set_u16(0x3e6U + material * 2U, 1U);
        state.set_u16(0x382U + material * 2U,
                      static_cast<std::uint16_t>(0x44U + material));
    }
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG type-five field ability run did not terminate");
    for (std::size_t material = 0; material < 5U; ++material) {
        require(context.shared_state.u16(0x3e6U + material * 2U) == 0U,
                "RPG type-five ability did not consume a selected material");
    }
    for (std::size_t slot = 0; slot < 5U; ++slot) {
        require(context.shared_state.u16(0x382U + slot * 2U) == 0U,
                "RPG zero-count material item was not removed/compacted");
    }
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 11U && platform.stop_calls == 1U,
            "RPG five-material cast did not return through 2fb7");
}

void test_rpg_field_magic_material_error(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // dismiss DATA:364a
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u8(0x106U + 0x6dU, 41U);
    for (std::size_t material = 0; material < 5U; ++material) {
        state.set_u16(0x3e6U + material * 2U,
                      static_cast<std::uint16_t>(material == 2U ? 0U : 1U));
    }
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(0x3e6U) == 1U &&
                context.shared_state.u16(0x3e8U) == 1U &&
                context.shared_state.u16(0x3eaU) == 0U,
            "RPG insufficient materials changed counters before dispatch");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 11U && platform.stop_calls == 1U &&
                platform.bottom_hashes[6] != platform.bottom_hashes[5],
            "RPG DATA:364a material feedback did not preserve the Use page");
}

void test_rpg_field_magic_travel(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // actor zero
        swd2::InputAction::confirm,  // ability 99 / action 29h
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::confirm,  // first unlocked destination
        swd2::InputAction::quit,     // reloaded map
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    constexpr auto actor_base = 0x106U;
    state.set_u8(actor_base + 0x6dU, 99U);
    state.set_u16(actor_base + 0x55U, 100U);
    state.set_u8(0x51eU, 1U);
    require((state.u16(0x408U) & 0x8000U) != 0U,
            "fixture no longer permits action-29h travel");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(actor_base + 0x55U) == 85U &&
                context.shared_state.map_location_directory_offset() == 0x0046U,
            "RPG ability 99 did not deduct/reload the selected MAPZ destination");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 8U && platform.stop_calls == 1U &&
                platform.frame_hashes[6] != platform.frame_hashes[5] &&
                platform.frame_hashes[6] == 536414092186444491ULL &&
                platform.frame_hashes[7] != platform.frame_hashes[0],
            "RPG action-29h travel list/map reload frames were not stable");
}

void test_rpg_field_magic_travel_current(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // ability 98 / action 28h
        swd2::InputAction::confirm,  // Use
        swd2::InputAction::quit,     // reloaded map
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    constexpr auto actor_base = 0x106U;
    state.set_u8(actor_base + 0x6dU, 98U);
    state.set_u16(actor_base + 0x55U, 100U);
    state.set_u16(0x408U,
                  static_cast<std::uint16_t>(state.u16(0x408U) & ~0x4000U));
    state.set_u16(0x40aU, 0U);
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(actor_base + 0x55U) == 93U &&
                context.shared_state.map_location_directory_offset() == 0x0046U,
            "RPG ability 98 did not reload SAVE+40a at its seven-point cost");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 7U && platform.stop_calls == 1U,
            "RPG action-28h direct travel did not return on the reloaded map");
}

void test_rpg_field_magic_travel_restricted(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::up,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Use action 28h
        swd2::InputAction::confirm,  // dismiss DATA:3620
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    constexpr auto actor_base = 0x106U;
    state.set_u8(actor_base + 0x6dU, 98U);
    state.set_u16(actor_base + 0x55U, 100U);
    state.set_u16(0x408U,
                  static_cast<std::uint16_t>(state.u16(0x408U) | 0x4000U));
    const auto before_location = state.map_location_directory_offset();
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(actor_base + 0x55U) == 100U &&
                context.shared_state.map_location_directory_offset() ==
                    before_location,
            "RPG restricted action-28h changed cost or map location");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 11U && platform.stop_calls == 1U &&
                platform.bottom_hashes[6] != platform.bottom_hashes[5],
            "RPG restricted Magic travel did not show DATA:3620 on Use");
}

void test_rpg_system_menu_speed_and_exit(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,   // open the field diamond (System default)
        swd2::InputAction::confirm,  // enter 4b76
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,     // message speed
        swd2::InputAction::confirm,  // enter 4e4b five-value selector
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,     // jump back to DOS
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // default Yes in 46cc
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    require(state.u16(0x3f2) == 1U,
            "fixture no longer has RPG message speed two");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(0x3f2) == 1U,
            "RPG system menu did not commit the selected message speed");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 14U && platform.music_calls == 1U &&
                platform.stop_calls == 1U,
            "RPG 4b76/4e4b/46cc system-menu sequence was not exact");
    require(platform.frame_hashes[2] != platform.frame_hashes[1] &&
                platform.frame_hashes[2] == 9900629663170054402ULL &&
                platform.frame_hashes[7] == 9871370903072498198ULL &&
                platform.frame_hashes[8] == 6692007422499836438ULL &&
                platform.frame_hashes[8] != platform.frame_hashes[7] &&
                platform.frame_hashes[13] != platform.frame_hashes[12],
            "RPG system menu/value/exit selection frames did not change");
}

void test_rpg_system_value_confirmation_escape(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,     // message speed
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // first value
        swd2::InputAction::cancel,   // 46cc Escape commits default Yes
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    require(state.u16(0x3f2) != 0U,
            "fixture no longer exposes a nonzero message speed");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(0x3f2) == 0U &&
                platform.cursor == platform.actions.size() &&
                platform.presented == 12U &&
                platform.stop_calls == 1U,
            "RPG 4e4b lost nested 46cc Escape/default-Yes commit behavior");
}

void test_rpg_system_value_left_wrap(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,     // message speed
        swd2::InputAction::confirm,
        swd2::InputAction::left,     // first value wraps to the fifth
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // default Yes
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.u16(0x3f2) == 4U &&
                platform.cursor == platform.actions.size() &&
                platform.presented == 13U &&
                platform.stop_calls == 1U,
            "RPG 4e9d did not wrap Left from the first system value to the fifth");
}

void test_rpg_system_menu_save(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,     // Record
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // choose slot one
        swd2::InputAction::confirm,  // default Yes
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    std::size_t saves = 0;
    std::uint8_t saved_slot = 0;
    swd2::GameContext context{game_root, state, platform};
    context.save_slot = [&](std::uint8_t slot,
                            const swd2::SharedState& saved_state,
                            const swd2::MapDatabase&) {
        ++saves;
        saved_slot = slot;
        require(saved_state.bytes() == context.shared_state.bytes(),
                "RPG system save supplied a stale SharedState snapshot");
    };
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                saves == 1U && saved_slot == 1U,
            "RPG system Record did not persist the confirmed slot pair");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 11U && platform.stop_calls == 1U &&
                platform.frame_hashes[6] == 4590673835455086092ULL,
            "RPG system Record did not return through 4b76 to the field");
}

void test_rpg_system_menu_save_restricted(
    const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,
        swd2::InputAction::down,     // Record
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // dismiss DATA:3620
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x408U,
                  static_cast<std::uint16_t>(state.u16(0x408U) & ~0x2000U));
    std::size_t saves = 0;
    swd2::GameContext context{game_root, state, platform};
    context.save_slot = [&](std::uint8_t, const swd2::SharedState&,
                            const swd2::MapDatabase&) { ++saves; };
    const auto restricted_result = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(restricted_result == swd2::Marker::none &&
                saves == 0U &&
                platform.cursor == platform.actions.size() &&
                platform.direct_updates == 7U &&
                platform.presented == 10U &&
                platform.stop_calls == 1U,
            "RPG map-restricted Record did not show and dismiss DATA:3620");
}

void test_rpg_system_menu_load(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::confirm,
        swd2::InputAction::down,
        swd2::InputAction::down,     // Read
        swd2::InputAction::confirm,
        swd2::InputAction::right,    // slot two
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // 46cc default Yes
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto loaded_money = static_cast<std::uint16_t>(state.u16(0x104) + 7U);
    std::size_t loads = 0;
    std::uint8_t loaded_slot = 0;
    swd2::GameContext context{game_root, state, platform};
    context.load_slot = [&](std::uint8_t slot) {
        ++loads;
        loaded_slot = slot;
        auto loaded_state = swd2::SharedState::load(game_root / "SAVE.DA1");
        loaded_state.set_u16(0x104, loaded_money);
        return swd2::LoadedSaveSlot{
            std::move(loaded_state),
            std::make_shared<swd2::MapDatabase>(
                swd2::MapDatabase::load(game_root / "MAPZ.DA1"))};
    };
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                loads == 1U && loaded_slot == 2U &&
                context.shared_state.u16(0x104) == loaded_money &&
                context.map_database != nullptr,
            "RPG system Read did not atomically install the selected SAVE/MAPZ pair");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 9U && platform.music_calls == 1U &&
                platform.stop_calls == 1U,
            "RPG system Read did not reload map resources before resuming");
}

void test_rpg_system_audio_toggle(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::cancel,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Music off
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,   // reopen through a new RpgEventHost
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // Music on
        swd2::InputAction::cancel,
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG system audio-toggle run did not terminate normally");
    require(platform.cursor == platform.actions.size() &&
                platform.presented == 11U && platform.music_calls == 2U &&
                platform.music_stop_calls == 1U && platform.stop_calls == 1U &&
                context.shared_state.u8(0x3f4) == 0U,
            "RPG system Music toggle did not stop/restart the current RIX");
    require(platform.frame_hashes[3] == platform.frame_hashes[7] &&
                platform.frame_hashes[2] == platform.frame_hashes[8] &&
                platform.frame_hashes[2] != platform.frame_hashes[3],
            "RPG system Music state did not survive reconstruction of the menu host");

    ScriptedPlatform disabled_platform;
    disabled_platform.actions = {swd2::InputAction::quit};
    auto disabled_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    disabled_state.set_u8(0x3f4, 1U);
    disabled_state.set_u8(0x3f5, 1U);
    swd2::GameContext disabled_context{
        game_root, disabled_state, disabled_platform};
    require(swd2::RpgModule().run(
                disabled_context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none &&
                disabled_platform.music_calls == 0U,
            "RPG ignored the saved music/sound disable bytes on startup");
}

void test_rpg_entity_collision(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::right,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    // The same SA068 entity occupies three consecutive RAP cells beginning at
    // world (139,111). Its automatic-event flag is clear, so walking right
    // must turn the actor without moving or opening the dialogue.
    state.set_viewport_x(118);
    state.set_viewport_y(99);
    state.set_actor_direction(0);
    const auto original_x = state.world_x();
    const auto original_y = state.world_y();
    swd2::GameContext context{game_root, state, platform};
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG entity-collision run did not terminate normally");
    require(context.shared_state.world_x() == original_x &&
                context.shared_state.world_y() == original_y &&
                context.shared_state.actor_direction() == 9,
            "RPG did not reproduce the three-cell entity collision footprint");
    require(platform.presented == 2 && platform.music_calls == 1 &&
                platform.stop_calls == 1,
            "blocked RPG movement unexpectedly opened an entity event");
    require(platform.bottom_hashes.size() == 2 &&
                platform.bottom_hashes[0] == platform.bottom_hashes[1],
            "ordinary entity collision unexpectedly drew a dialogue panel");
}

void test_rpg_behavior_six_collision(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(10);
    require(location.area.entity_count() > 8U &&
                location.area.entity_fields[3][8] == 6U &&
                location.area.entity_fields[2][8] == 26734U &&
                (location.area.entity_fields[8][8] & 0x8000U) == 0U,
            "MA-DE behavior-six collision oracle changed");
    for (std::size_t entity = 0; entity < location.area.entity_count(); ++entity) {
        if (entity != 8U) location.area.entity_fields[3][entity] = 3U;
    }

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::right,  // collide: 5298 hides entity eight
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(state, *database, 10U);
    state.set_u16(0x10, 0U);
    state.set_u16(0x102, 0U);
    // Keep this isolated collision fixture out of MA-DE's MAP0 portal; the
    // real area flag behavior is covered independently by the portal tests.
    state.set_u16(0x408, 0x0ffeU);
    state.set_viewport_x(22U);
    state.set_viewport_y(62U);
    state.set_actor_screen_x(38U);  // world (42,74), immediately left
    state.set_actor_screen_y(80U);  // of entity eight's (43..45,74) cells
    state.set_actor_direction(9U);
    state.set_u16(0x40f, 8U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (62U * 180U + 22U) * 2U));
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto result = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(result == swd2::Marker::none &&
                context.shared_state.world_x() == 42U &&
                context.shared_state.world_y() == 74U,
            "RPG behavior-six collision unexpectedly moved the actor");
    require(platform.cursor == platform.actions.size() &&
                platform.poll_calls == 2U && platform.wait_calls == 0U &&
                platform.presented == 2U && platform.stop_calls == 1U &&
                platform.frame_hashes[0] != platform.frame_hashes[1],
            "RPG 5298 did not hide behavior six without dispatching its event");
}

void test_rpg_interaction_rays(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    require(location.area.entity_count() > 5U &&
                location.area.entity_fields[2][5] == 25008U,
            "SBOUT four-cell interaction oracle changed");
    for (std::size_t entity = 0; entity < location.area.entity_count(); ++entity) {
        location.area.entity_fields[3][entity] = entity == 5U ? 1U : 3U;
    }

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::confirm,  // entity begins four cells east
        swd2::InputAction::quit,     // consumed by its first dialogue
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    swd2::install_map_location(state, *database, 12U);
    state.set_viewport_x(56U);
    state.set_viewport_y(57U);
    state.set_actor_screen_x(38U);  // centre (76,69), entity anchor (80,69)
    state.set_actor_screen_y(80U);
    state.set_actor_direction(9U);
    state.set_u16(0x40f, 8U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 56U) * 2U));
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto ray_result = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(ray_result == swd2::Marker::none &&
                platform.cursor == platform.actions.size() &&
                platform.poll_calls == 2U && platform.wait_calls == 0U &&
                platform.presented == 3U,
            "RPG 523d/52fd did not find an entity on the fourth forward probe");
}

void test_rpg_corner_slide(const std::filesystem::path& game_root) {
    const auto map = swd2::MapResource::load(game_root / "T1" / "AREA1");
    std::size_t center_x = 0;
    std::size_t center_y = 0;
    const auto clear = [&](std::size_t x, std::size_t y) {
        return (map.cells()[y * map.layout().width + x] & 0x8000U) == 0;
    };
    bool found = false;
    for (std::size_t y = 12; y + 2 < map.layout().height && !found; ++y) {
        for (std::size_t x = 20; x + 2 < map.layout().width; ++x) {
            const auto leading = map.cells()[y * map.layout().width + x + 2];
            const auto current_clear = clear(x - 1, y) && clear(x, y) && clear(x + 1, y);
            const auto south_footprint =
                clear(x - 1, y + 1) && clear(x, y + 1) && clear(x + 1, y + 1);
            const auto original_corner_probe =
                clear(x + 1, y + 1) &&
                (clear(x + 2, y + 1) || clear(x + 2, y + 2));
            if ((leading & 0x8000U) != 0 && (leading & 0x0800U) == 0 &&
                current_clear && south_footprint && original_corner_probe) {
                center_x = x;
                center_y = y;
                found = true;
                break;
            }
        }
    }
    require(found, "AREA1 has no right-wall corner-slide oracle");

    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(8);
    for (auto& behavior : location.area.entity_fields[3]) behavior = 3;

    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::right, swd2::InputAction::quit};
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_viewport_x(static_cast<std::uint16_t>(center_x - 20));
    state.set_viewport_y(static_cast<std::uint16_t>(center_y - 12));
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_actor_direction(3);
    state.set_u16(0x40f, 8);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + ((center_y - 12) * map.layout().width + center_x - 20) * 2U));
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG corner-slide run did not terminate normally");
    require(context.shared_state.world_x() == center_x &&
                context.shared_state.world_y() == center_y + 1 &&
                context.shared_state.actor_direction() == 0,
            "RPG right-wall collision did not take its south-first corner slide");
}

void test_rpg_overworld_poison(const std::filesystem::path& game_root) {
    const auto map = swd2::MapResource::load(game_root / "T1" / "AREA1");
    std::size_t start_x = 0;
    std::size_t start_y = 0;
    bool found = false;
    for (std::size_t y = 12; y + 1 < map.layout().height && !found; ++y) {
        for (std::size_t x = 20; x + 12 < map.layout().width; ++x) {
            bool corridor = true;
            for (std::size_t center = x; center <= x + 10U; ++center) {
                corridor = corridor &&
                    (map.cells()[y * map.layout().width + center] & 0x1000U) == 0U;
                for (int dx = -1; dx <= 1; ++dx) {
                    corridor = corridor &&
                        (map.cells()[y * map.layout().width +
                                     static_cast<std::size_t>(
                                         static_cast<int>(center) + dx)] &
                         0x8000U) == 0U;
                }
            }
            if (corridor) {
                start_x = x;
                start_y = y;
                found = true;
                break;
            }
        }
    }
    require(found, "AREA1 has no ten-step poison-test corridor");

    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(8);
    require(location.area.auxiliary != 0U,
            "AREA1 no longer enables RPG 1fa8 field steps");
    for (auto& behavior : location.area.entity_fields[3]) behavior = 3U;

    ScriptedPlatform platform;
    platform.actions.assign(10U, swd2::InputAction::right);
    platform.actions.push_back(swd2::InputAction::confirm);
    platform.actions.push_back(swd2::InputAction::quit);
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x10, 1U);
    state.set_u16(0x102, 0U);
    const auto actor = 0x106U;
    state.set_u16(actor + 8U, 0x0200U);
    state.set_u16(actor + 0x2dU, 1U);
    state.set_viewport_x(static_cast<std::uint16_t>(start_x - 20U));
    state.set_viewport_y(static_cast<std::uint16_t>(start_y - 12U));
    state.set_actor_screen_x(38U);
    state.set_actor_screen_y(80U);
    state.set_actor_direction(9U);
    state.set_u16(0x40f, 8U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + ((start_y - 12U) * map.layout().width + start_x - 20U) * 2U));
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(
                context, swd2::Marker::continue_rpg) == swd2::Marker::none &&
                context.shared_state.world_x() == start_x + 10U &&
                context.shared_state.world_y() == start_y &&
                context.shared_state.u16(actor + 0x2dU) == 0U &&
                context.shared_state.u16(actor + 8U) == 0x2000U,
            "RPG ten-step overworld poison pulse did not defeat the actor");
    std::uint64_t solid_hash = 1469598103934665603ULL;
    for (std::size_t pixel = 0; pixel < 320U * 200U; ++pixel) {
        solid_hash ^= 0x6bU;
        solid_hash *= 1099511628211ULL;
    }
    require(platform.cursor == platform.actions.size() &&
                platform.wait_calls == 0U && platform.poll_calls == 12U &&
                platform.presented == 14U && platform.frame_hashes[11] == solid_hash &&
                platform.frame_hashes[12] == platform.frame_hashes[13],
            "RPG poison dialogue/6b flash did not preserve the 1ffb/200f sequence");

    ScriptedPlatform quit_platform;
    quit_platform.actions.assign(10U, swd2::InputAction::right);
    quit_platform.frontend_actions = {swd2::InputAction::quit};
    auto quit_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    quit_state.set_u16(0x10, 1U);
    quit_state.set_u16(0x102, 0U);
    quit_state.set_u16(actor + 8U, 0x0200U);
    quit_state.set_u16(actor + 0x2dU, 1U);
    quit_state.set_viewport_x(static_cast<std::uint16_t>(start_x - 20U));
    quit_state.set_viewport_y(static_cast<std::uint16_t>(start_y - 12U));
    quit_state.set_actor_screen_x(38U);
    quit_state.set_actor_screen_y(80U);
    quit_state.set_actor_direction(9U);
    quit_state.set_u16(0x40f, 8U);
    quit_state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + ((start_y - 12U) * map.layout().width + start_x - 20U) * 2U));
    swd2::GameContext quit_context{game_root, quit_state, quit_platform};
    quit_context.map_database = database;
    require(swd2::RpgModule().run(
                quit_context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none &&
                quit_platform.cursor == quit_platform.actions.size() &&
                quit_platform.frontend_cursor == 1U &&
                quit_platform.frontend_quit_poll_calls == 1U &&
                quit_platform.stop_calls == 1U &&
                quit_context.shared_state.u16(actor + 0x2dU) == 0U &&
                std::find(quit_platform.frame_hashes.begin(),
                          quit_platform.frame_hashes.end(), solid_hash) ==
                    quit_platform.frame_hashes.end(),
            "RPG forced opcode-20 text ignored frontend quit before poison flash");
}

void test_rpg_random_encounter(const std::filesystem::path& game_root) {
    const auto map = swd2::MapResource::load(game_root / "T1" / "AREA1");
    std::size_t start_x = 0;
    std::size_t start_y = 0;
    bool found = false;
    for (std::size_t y = 12; y + 1 < map.layout().height && !found; ++y) {
        for (std::size_t x = 20; x + 2 < map.layout().width; ++x) {
            bool clear = true;
            for (int dx = -1; dx <= 2; ++dx) {
                clear = clear &&
                    (map.cells()[y * map.layout().width +
                                 static_cast<std::size_t>(
                                     static_cast<int>(x) + dx)] &
                     0x8000U) == 0U;
            }
            clear = clear &&
                (map.cells()[y * map.layout().width + x] & 0x1000U) == 0U &&
                (map.cells()[y * map.layout().width + x + 1U] & 0x1000U) == 0U;
            if (clear) {
                start_x = x;
                start_y = y;
                found = true;
                break;
            }
        }
    }
    require(found, "AREA1 has no two-cell random-encounter test path");

    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(8);
    for (auto& behavior : location.area.entity_fields[3]) behavior = 3U;
    ScriptedPlatform platform;
    platform.actions.clear();
    for (std::size_t step = 0; step < 75U; ++step) {
        platform.actions.push_back((step & 1U) == 0U
            ? swd2::InputAction::right : swd2::InputAction::left);
    }
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x10, 1U);
    state.set_u16(0x102, 0U);
    state.set_viewport_x(static_cast<std::uint16_t>(start_x - 20U));
    state.set_viewport_y(static_cast<std::uint16_t>(start_y - 12U));
    state.set_actor_screen_x(38U);
    state.set_actor_screen_y(80U);
    state.set_actor_direction(9U);
    state.set_u16(0x40f, 8U);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + ((start_y - 12U) * map.layout().width + start_x - 20U) * 2U));
    state.set_u16(0x49c, 0x1000U);
    state.set_u16(0x4a0, 0x1234U);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto encounter_result = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    std::uint64_t black_hash = 1469598103934665603ULL;
    for (std::size_t pixel = 0; pixel < 320U * 200U; ++pixel) {
        black_hash *= 1099511628211ULL;
    }
    require(encounter_result ==
                swd2::Marker::open_figure &&
                platform.cursor == platform.actions.size() &&
                platform.poll_calls == 75U && platform.presented == 115U &&
                platform.frontend_quit_poll_calls == 42U &&
                platform.stop_calls == 1U && platform.music_calls == 2U &&
                platform.frame_hashes[75] != platform.frame_hashes[74] &&
                platform.frame_hashes[114] == black_hash &&
                context.shared_state.world_x() == start_x + 1U &&
                context.shared_state.world_y() == start_y &&
                context.shared_state.u16(0x49c) == 0x1018U &&
                context.shared_state.u16(0x4a0) == 0U,
            "RPG 1fa8 code-window gate did not enter an in-process random FIG battle");

    ScriptedPlatform quit_platform;
    quit_platform.actions = platform.actions;
    quit_platform.frontend_actions = {swd2::InputAction::quit};
    auto quit_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    quit_state.set_u16(0x10, 1U);
    quit_state.set_u16(0x102, 0U);
    quit_state.set_viewport_x(static_cast<std::uint16_t>(start_x - 20U));
    quit_state.set_viewport_y(static_cast<std::uint16_t>(start_y - 12U));
    quit_state.set_actor_screen_x(38U);
    quit_state.set_actor_screen_y(80U);
    quit_state.set_actor_direction(9U);
    quit_state.set_u16(0x40f, 8U);
    quit_state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + ((start_y - 12U) * map.layout().width + start_x - 20U) * 2U));
    quit_state.set_u16(0x49c, 0x1000U);
    quit_state.set_u16(0x4a0, 0x1234U);
    swd2::GameContext quit_context{game_root, quit_state, quit_platform};
    quit_context.map_database = database;
    require(swd2::RpgModule().run(
                quit_context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none &&
                quit_platform.cursor == quit_platform.actions.size() &&
                quit_platform.presented == 75U &&
                quit_platform.frontend_quit_poll_calls == 1U &&
                quit_platform.music_calls == 1U &&
                quit_platform.stop_calls == 1U,
            "RPG random battle wipe ignored frontend quit and launched FIG");
}

void test_rpg_automatic_entity_event(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    const auto entity = swd2::map_entity(location.area, 5);
    require(entity.cell_offset == 25008 && entity.event_directory_offset == 220 &&
                (entity.flags & 0x8000U) != 0,
            "automatic SBOUT entity oracle changed");

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::right,
        // This quit must be consumed by the collision-triggered dialogue, not
        // by a later ordinary map iteration.
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x424, 12);
    state.set_u16(0x40f, 8);
    state.set_viewport_x(59);
    state.set_viewport_y(57);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_actor_direction(0);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG automatic entity-event run did not terminate normally");
    require(context.shared_state.world_x() == 79 &&
                context.shared_state.world_y() == 69 &&
                context.shared_state.actor_direction() == 9,
            "automatic entity event moved the blocked player");
    require(platform.presented == 3 && platform.bottom_hashes.size() == 3 &&
                platform.bottom_hashes[0] == platform.bottom_hashes[1] &&
                platform.bottom_hashes[1] != platform.bottom_hashes[2] &&
                platform.music_calls == 1 && platform.stop_calls == 1,
            "field-8 8000h collision did not open the entity dialogue immediately");
}

void test_rpg_event_voice(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    // CHNA1 directory 298 is dialogue, opcode-57 voice 53, then dialogue.
    location.area.entity_fields[9][5] = 298;

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::right,
        swd2::InputAction::confirm,  // interact with the entity
        swd2::InputAction::confirm,  // close dialogue before opcode 57
        swd2::InputAction::quit,     // abort in the dialogue after the voice
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x424, 12);
    state.set_u16(0x40f, 8);
    state.set_viewport_x(59);
    state.set_viewport_y(57);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG voiced event did not terminate normally");
    require(platform.voice_calls == 1 &&
                platform.voice_bytes == std::filesystem::file_size(
                    game_root / "VC" / "SP053.VOC"),
            "RPG opcode 57 did not route the original VOC through PlatformBackend");
}

void test_rpg_compact_money_overlay(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    // CHNA1 directory word 198 (entry 99) starts with opcode 14, conditionally
    // spends 50 coins, and otherwise opens a dialogue. Zero money keeps the
    // execution on that record and makes its exact one-digit rendering stable.
    location.area.entity_fields[9][5] = 198;

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::quit,
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x104, 0);
    state.set_u16(0x424, 12);
    state.set_u16(0x40f, 8);
    state.set_viewport_x(59);
    state.set_viewport_y(57);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none,
            "RPG compact money-overlay event did not terminate normally");
    require(platform.presented == 5 && platform.compact_hashes.size() == 5 &&
                platform.compact_hashes[2] == 6584855232119388833ULL &&
                platform.compact_hashes[2] == platform.compact_hashes[3] &&
                platform.compact_hashes[2] != platform.compact_hashes[1] &&
                platform.compact_hashes[4] != platform.compact_hashes[2],
            "RPG opcode 14 did not persist its overlay through dialogue and clear afterward");
}

void test_rpg_dialogue_then_money_overlay(
    const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    // CHNA0 entry 202 starts with opcode 18 immediately followed by opcode
    // 14. 22cf must draw the compact money card into the dialogue VGA page;
    // rebuilding the map here erases the still-visible bottom panel.
    location.area.event_archive_path = "CHNA0.EXE";
    location.area.event_font_path = "CHNA0.DSK";
    location.area.entity_fields[9][5] = 202U * 2U;

    ScriptedPlatform platform;
    platform.actions = {
        swd2::InputAction::right,    // opcode 13: select No
        swd2::InputAction::confirm,  // decline its conditional branch
        swd2::InputAction::cancel,   // close the following dialogue
    };
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x424, 12);
    state.set_u16(0x40f, 8);
    state.set_viewport_x(59);
    state.set_viewport_y(57);
    state.set_actor_screen_x(38);  // world x 79, immediately left of entity five
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    const auto result = swd2::RpgModule().run(
        context, swd2::Marker::continue_rpg);
    require(result == swd2::Marker::open_figure &&
                platform.cursor == platform.actions.size() &&
                platform.music_calls == 2U,
            "RPG dialogue-to-money event did not terminate normally");
    std::uint64_t black_hash = 1469598103934665603ULL;
    for (std::size_t pixel = 0; pixel < 320U * 200U; ++pixel) {
        black_hash *= 1099511628211ULL;
    }
    require(platform.frame_hashes.size() == 47U &&
                platform.frame_hashes[2] == 1522698264658253850ULL &&
                platform.frame_hashes[3] == 6615315919581820857ULL &&
                platform.frame_hashes[4] == 13660947412412094554ULL &&
                platform.frame_hashes[5] == platform.frame_hashes[3] &&
                platform.frame_hashes[6] == 6624625476138752861ULL &&
                platform.frame_hashes[7] != platform.frame_hashes[6] &&
                platform.frame_hashes[46] == black_hash &&
                platform.bottom_hashes[2] == platform.bottom_hashes[5] &&
                platform.compact_hashes[2] != platform.compact_hashes[3],
            "RPG opcode 13/14 did not preserve and restore the dialogue VGA page");
}

void test_rpg_shop_confirmation(const std::filesystem::path& game_root) {
    const auto prepare = [&]() {
        auto database = std::make_shared<swd2::MapDatabase>(
            swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
        const auto& location = database->location_at_directory_offset(46);
        require(location.area.event_archive_path == "CHNA1.EXE" &&
                    swd2::map_entity(location.area, 0).event_directory_offset == 58,
                "RPG shop confirmation oracle no longer points at CHNA1 entry 29");
        auto graphics = game_root / swd2::normalize_dos_asset_path(
            location.area.graphics_path);
        auto layout = game_root / swd2::normalize_dos_asset_path(
            location.area.layout_path);
        graphics.replace_extension();
        layout.replace_extension();
        const auto shop_map = swd2::MapResource::load(graphics, layout);
        require(shop_map.layout().width == 64U &&
                    shop_map.layout().height == 38U &&
                    shop_map.cell_base() == 8U,
                "SWRO7 RAP pointer-directory oracle changed");
        auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
        for (std::size_t slot = 0; slot < 50; ++slot) {
            state.set_u16(0x382 + slot * 2U, 0);
        }
        for (std::size_t counter = 0; counter < 5; ++counter) {
            state.set_u16(0x3e6 + counter * 2U, 0);
        }
        state.set_u16(0x104, 100);
        state.set_u16(0x424, 46);
        state.set_u16(0x40f, shop_map.cell_base());
        state.set_viewport_x(20);
        state.set_viewport_y(0);
        state.set_u16(0x40d, static_cast<std::uint16_t>(
            shop_map.cell_base() + 20U * 2U));
        state.set_actor_screen_x(50);  // world x 46, left of entity x 47
        state.set_actor_screen_y(72);  // world y 11
        state.set_actor_direction(9);
        state.set_dos_string(0x42d, 22, location.area.graphics_path);
        state.set_dos_string(0x443, 22, location.area.layout_path);
        state.set_dos_string(0x459, 22, location.area.music_path);
        state.set_dos_string(0x46f, 22, location.area.event_archive_path);
        state.set_dos_string(0x485, 24, location.area.event_font_path);
        return std::pair{std::move(database), std::move(state)};
    };

    auto [no_database, no_state] = prepare();
    ScriptedPlatform no_platform;
    no_platform.text_actions = {
        swd2::InputAction::confirm,  // skip opcode-18 greeting delay
        swd2::InputAction::confirm,  // skip 5884 purchase-prompt delay
    };
    no_platform.actions = {
        swd2::InputAction::confirm,  // interact with the shop entity
        // Opcode 18's greeting returns at $$ without a confirmation.
        swd2::InputAction::confirm,  // select ITEM 117
        swd2::InputAction::right,    // choose No
        swd2::InputAction::confirm,
        swd2::InputAction::cancel,   // close the shop
        swd2::InputAction::quit,
    };
    swd2::GameContext no_context{game_root, no_state, no_platform};
    no_context.map_database = no_database;
    const auto no_result = swd2::RpgModule().run(
        no_context, swd2::Marker::continue_rpg);
    require(no_result == swd2::Marker::none &&
                no_context.shared_state.u16(0x104) == 100U &&
                no_context.shared_state.u16(0x382) == 0U &&
                no_platform.cursor == no_platform.actions.size() &&
                no_platform.text_cursor == no_platform.text_actions.size() &&
                no_platform.direct_updates == 4U &&
                no_platform.presented == 10U &&
                no_platform.frame_hashes[4] == 6062648533265157386ULL &&
                no_platform.frame_hashes[7] == 7826475264314421950ULL &&
                no_platform.frame_hashes[8] == no_platform.frame_hashes[4] &&
                no_platform.frame_hashes[9] == 3445820599633288345ULL,
            "RPG 5884 purchase confirmation did not preserve state on No");

    auto [yes_database, yes_state] = prepare();
    ScriptedPlatform yes_platform;
    yes_platform.actions = {
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // default Yes
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    swd2::GameContext yes_context{game_root, yes_state, yes_platform};
    yes_context.map_database = yes_database;
    const auto yes_result =
        swd2::RpgModule().run(yes_context, swd2::Marker::continue_rpg);
    require(yes_result == swd2::Marker::none &&
                yes_context.shared_state.u16(0x104) == 75U &&
                yes_context.shared_state.u16(0x382) == 117U &&
                yes_platform.cursor == yes_platform.actions.size() &&
                yes_platform.presented == 9U &&
                yes_platform.frame_hashes[4] == 6062648533265157386ULL &&
                yes_platform.frame_hashes[6] == 7826475264314421950ULL &&
                yes_platform.frame_hashes[7] == 9841584038327432507ULL &&
                yes_platform.frame_hashes[8] == 3445820599633288345ULL,
            "RPG 5884 purchase confirmation did not commit the default Yes");

    auto [error_database, error_state] = prepare();
    error_state.set_u16(0x104, 0);
    ScriptedPlatform error_platform;
    error_platform.actions = {
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // dismiss DATA:3c44/MENU 149
        swd2::InputAction::cancel,
        swd2::InputAction::quit,
    };
    swd2::GameContext error_context{game_root, error_state, error_platform};
    error_context.map_database = error_database;
    require(swd2::RpgModule().run(
                error_context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none &&
                error_context.shared_state.u16(0x104) == 0U &&
                error_context.shared_state.u16(0x382) == 0U &&
                error_platform.cursor == error_platform.actions.size() &&
                error_platform.presented >= 6U,
            "RPG 49d0 shop error prompt did not animate/consume acknowledgement");

    auto [sell_database, sell_state] = prepare();
    // CHNA1 entry 28 is the opcode-17 combined Buy/Sell form. Its entity
    // event is intentionally reloaded after leaving either sub-loop, so the
    // second initial selector cancellation below is what exits the shop.
    sell_database->location_at_directory_offset(46)
        .area.entity_fields[9][0] = 56U;
    sell_state.set_u16(0x382U, 117U);
    ScriptedPlatform sell_platform;
    sell_platform.actions = {
        swd2::InputAction::confirm,  // interact with the shop entity
        swd2::InputAction::right,    // initial selector: Sell
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // select ITEM 117
        swd2::InputAction::confirm,  // accept three-quarter sale value
        swd2::InputAction::confirm,  // select the now-empty physical cell
        swd2::InputAction::confirm,  // dismiss DATA:3c0a unsellable feedback
        swd2::InputAction::cancel,   // leave the repeated sale selector
        swd2::InputAction::cancel,   // leave reloaded Buy/Sell selector
        swd2::InputAction::quit,
    };
    swd2::GameContext sell_context{
        game_root, sell_state, sell_platform};
    sell_context.map_database = sell_database;
    require(swd2::RpgModule().run(
                sell_context, swd2::Marker::continue_rpg) ==
                    swd2::Marker::none &&
                sell_platform.cursor == sell_platform.actions.size() &&
                sell_context.shared_state.u16(0x104U) == 119U &&
                sell_context.shared_state.u16(0x382U) == 0U,
            "RPG opcode 17 did not select Sell, repeat its inventory or reload");
    require(sell_platform.frame_hashes.size() == 19U &&
                sell_platform.frame_hashes[4] == 15055269731696797642ULL &&
                sell_platform.frame_hashes[5] == 6024300895331846812ULL &&
                sell_platform.frame_hashes[6] == sell_platform.frame_hashes[3] &&
                sell_platform.frame_hashes[8] == 9437141809607963950ULL &&
                sell_platform.frame_hashes[9] == 10342911885711284032ULL &&
                sell_platform.frame_hashes[10] ==
                    sell_platform.frame_hashes[12] &&
                sell_platform.bottom_hashes[10] == 18064545685687686801ULL &&
                sell_platform.bottom_hashes[11] == 15293909858239181191ULL &&
                sell_platform.bottom_hashes[12] ==
                    sell_platform.bottom_hashes[10] &&
                sell_platform.frame_hashes[13] == sell_platform.frame_hashes[0] &&
                sell_platform.frame_hashes[16] == 6581974554223330126ULL &&
                sell_platform.frame_hashes[17] == sell_platform.frame_hashes[15] &&
                sell_platform.frame_hashes[18] == 3445820599633288345ULL,
            "RPG opcode-17 Buy/Sell, empty feedback or reload frames changed");

    auto [combined_quit_database, combined_quit_state] = prepare();
    combined_quit_database->location_at_directory_offset(46)
        .area.entity_fields[9][0] = 56U;
    ScriptedPlatform combined_quit_platform;
    combined_quit_platform.actions = {
        swd2::InputAction::confirm,  // interact with opcode 17
        swd2::InputAction::confirm,  // choose Buy
        swd2::InputAction::quit,     // close from the purchase list
    };
    swd2::GameContext combined_quit_context{
        game_root, combined_quit_state, combined_quit_platform};
    combined_quit_context.map_database = combined_quit_database;
    const auto combined_quit_result = swd2::RpgModule().run(
        combined_quit_context, swd2::Marker::continue_rpg);
    require(combined_quit_result ==
                swd2::Marker::none &&
                combined_quit_platform.cursor ==
                    combined_quit_platform.actions.size() &&
                combined_quit_platform.presented == 7U &&
                combined_quit_platform.stop_calls == 1U,
            "RPG opcode-17 quit redrew/reloaded the shop event after frontend close");
}

void test_rpg_cutscene_presentation(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    // CHNA1 directory 32 exercises DE001/2/3, RI000/001 event music,
    // palette fades, frame delay, and opcode-36 animation.
    location.area.entity_fields[9][5] = 32;

    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::right, swd2::InputAction::quit};
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x424, 12);
    state.set_u16(0x40f, 8);
    state.set_viewport_x(59);
    state.set_viewport_y(57);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) == swd2::Marker::none,
            "RPG cutscene event did not terminate normally");
    const std::set<std::uint64_t> palettes(platform.palette_hashes.begin(),
                                           platform.palette_hashes.end());
    const std::set<std::uint64_t> frames(platform.bottom_hashes.begin(),
                                         platform.bottom_hashes.end());
    require(platform.presented >= 80 && platform.music_calls >= 3 &&
                palettes.size() > 10 && frames.size() > 2,
            "RPG cutscene opcodes did not render DE frames, RI music, and palette ramps");

    auto quit_database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& quit_location = quit_database->location_at_directory_offset(12);
    quit_location.area.entity_fields[9][5] = 32;
    ScriptedPlatform quit_platform;
    quit_platform.actions = {swd2::InputAction::right};
    quit_platform.frontend_actions = {swd2::InputAction::quit};
    auto quit_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    quit_state.set_u16(0x424, 12);
    quit_state.set_u16(0x40f, 8);
    quit_state.set_viewport_x(59);
    quit_state.set_viewport_y(57);
    quit_state.set_actor_screen_x(38);
    quit_state.set_actor_screen_y(80);
    quit_state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    quit_state.set_dos_string(0x42d, 22, quit_location.area.graphics_path);
    quit_state.set_dos_string(0x443, 22, quit_location.area.layout_path);
    quit_state.set_dos_string(0x459, 22, quit_location.area.music_path);
    quit_state.set_dos_string(
        0x46f, 22, quit_location.area.event_archive_path);
    quit_state.set_dos_string(0x485, 24, quit_location.area.event_font_path);
    swd2::GameContext quit_context{game_root, quit_state, quit_platform};
    quit_context.map_database = quit_database;
    require(swd2::RpgModule().run(
                quit_context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none &&
                quit_platform.cursor == quit_platform.actions.size() &&
                quit_platform.frontend_cursor == 1U &&
                quit_platform.frontend_quit_poll_calls == 1U &&
                quit_platform.presented == 2U &&
                quit_platform.stop_calls == 1U,
            "RPG cutscene fade ignored frontend quit and continued the event");

    // Entry 32 performs two complete 21-step fades before its first opcode-36
    // frame, with opcode 44 having selected a four-tick hold. Deliver the
    // close after the 44 fade polls: the frame itself remains observable, but
    // its replacement timer must abort before sleeping or executing opcode 8.
    auto timed_quit_database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& timed_quit_location =
        timed_quit_database->location_at_directory_offset(12);
    timed_quit_location.area.entity_fields[9][5] = 32;
    ScriptedPlatform timed_quit_platform;
    timed_quit_platform.actions = {swd2::InputAction::right};
    timed_quit_platform.frontend_actions.assign(
        44U, swd2::InputAction::none);
    timed_quit_platform.frontend_actions.push_back(
        swd2::InputAction::quit);
    auto timed_quit_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    timed_quit_state.set_u16(0x424, 12);
    timed_quit_state.set_u16(0x40f, 8);
    timed_quit_state.set_viewport_x(59);
    timed_quit_state.set_viewport_y(57);
    timed_quit_state.set_actor_screen_x(38);
    timed_quit_state.set_actor_screen_y(80);
    timed_quit_state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    timed_quit_state.set_dos_string(
        0x42d, 22, timed_quit_location.area.graphics_path);
    timed_quit_state.set_dos_string(
        0x443, 22, timed_quit_location.area.layout_path);
    timed_quit_state.set_dos_string(
        0x459, 22, timed_quit_location.area.music_path);
    timed_quit_state.set_dos_string(
        0x46f, 22, timed_quit_location.area.event_archive_path);
    timed_quit_state.set_dos_string(
        0x485, 24, timed_quit_location.area.event_font_path);
    swd2::GameContext timed_quit_context{
        game_root, timed_quit_state, timed_quit_platform};
    timed_quit_context.map_database = timed_quit_database;
    const auto timed_quit_result = swd2::RpgModule().run(
        timed_quit_context, swd2::Marker::continue_rpg);
    require(timed_quit_result == swd2::Marker::none &&
                timed_quit_platform.cursor ==
                    timed_quit_platform.actions.size() &&
                timed_quit_platform.frontend_cursor == 45U &&
                timed_quit_platform.frontend_quit_poll_calls == 45U &&
                timed_quit_platform.presented == 45U &&
                timed_quit_platform.delay_calls == 43U &&
                timed_quit_platform.delayed_milliseconds == 659U &&
                timed_quit_platform.stop_calls == 1U,
            "RPG opcode-36 fixed hold ignored frontend quit or ran opcode 8");
}

void test_rpg_opcode55_cutscene(const std::filesystem::path& game_root) {
    auto database = std::make_shared<swd2::MapDatabase>(
        swd2::MapDatabase::load(game_root / "MAPZ.DA1"));
    auto& location = database->location_at_directory_offset(12);
    // CHNA1 entry 229 (directory byte offset 458) is the long DE ending
    // sequence. Its 15 opcode-55 commands immediately follow DE animation
    // frames, providing end-to-end oracles for the in-place VGA conversion.
    location.area.entity_fields[9][5] = 458;

    ScriptedPlatform platform;
    platform.track_monochrome = true;
    platform.actions = {swd2::InputAction::right, swd2::InputAction::quit};
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    state.set_u16(0x424, 12);
    state.set_u16(0x40f, 8);
    state.set_viewport_x(59);
    state.set_viewport_y(57);
    state.set_actor_screen_x(38);
    state.set_actor_screen_y(80);
    state.set_u16(0x40d, static_cast<std::uint16_t>(
        8U + (57U * 180U + 59U) * 2U));
    state.set_dos_string(0x42d, 22, location.area.graphics_path);
    state.set_dos_string(0x443, 22, location.area.layout_path);
    state.set_dos_string(0x459, 22, location.area.music_path);
    state.set_dos_string(0x46f, 22, location.area.event_archive_path);
    state.set_dos_string(0x485, 24, location.area.event_font_path);
    swd2::GameContext context{game_root, state, platform};
    context.map_database = database;
    require(swd2::RpgModule().run(context, swd2::Marker::continue_rpg) ==
                swd2::Marker::none,
            "RPG opcode-55 ending cutscene did not terminate normally");
    require(platform.monochrome_transitions == 15,
            "RPG opcode-55 cutscene did not transform every preceding DE page");
}

void test_demo_module(const std::filesystem::path& game_root) {
    ScriptedPlatform platform;
    platform.actions = {swd2::InputAction::confirm};
    swd2::GameContext context{game_root, swd2::SharedState::load(game_root / "SAVE.DA1"),
                              platform};
    require(swd2::DemoModule().run(context, swd2::Marker::open_demo) == swd2::Marker::none,
            "in-process DEMO module returned an invalid marker");
    require(platform.presented == 1 && platform.music_calls == 0 && platform.stop_calls == 1,
            "DEMO module did not render and manage audio through PlatformBackend");

    ScriptedPlatform full_platform;
    full_platform.actions.assign(3'000, swd2::InputAction::none);
    swd2::GameContext full_context{
        game_root, swd2::SharedState::load(game_root / "SAVE.DA1"), full_platform};
    require(swd2::DemoModule().run(full_context, swd2::Marker::open_demo) ==
                swd2::Marker::none &&
                full_platform.presented > 2'400 &&
                full_platform.music_calls == 1 && full_platform.stop_calls == 1 &&
                full_platform.cursor == full_platform.presented,
            "DEMO full 960-frame timeline/epilogue did not start SWORD.RIX exactly once");

    ScriptedPlatform muted_platform;
    muted_platform.actions.assign(201, swd2::InputAction::none);
    muted_platform.actions.push_back(swd2::InputAction::confirm);
    auto muted_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    muted_state.set_u8(0x3f4, 1U);
    swd2::GameContext muted_context{game_root, muted_state, muted_platform};
    require(swd2::DemoModule().run(
                muted_context, swd2::Marker::open_demo) == swd2::Marker::none &&
                muted_platform.presented == 202U &&
                muted_platform.music_calls == 0U,
            "merged DEMO ignored the shared saved music-disable byte");
}

void test_demo_timeline(const std::filesystem::path& game_root) {
    auto sword6 = swd2::decode_demo_rle(
        swd2::decode_rsk_block(read_file(game_root / "SWORD6.RSK")).data);
    auto sword7 = swd2::decode_demo_rle(
        swd2::decode_rsk_block(read_file(game_root / "SWORD7.RSK")).data);
    require(sword6.size() == 54'233 && sword7.size() == 31'400,
            "DEMO nested run decoder produced unexpected sizes");
    const auto expanded6 = swd2::SpriteArchive::parse(std::move(sword6));
    const auto expanded7 = swd2::SpriteArchive::parse(std::move(sword7));
    require(expanded6.sprites().size() == 15 &&
                expanded6.sprites()[0].width == 204 &&
                expanded6.sprites()[0].height == 190 &&
                expanded7.sprites().size() == 2 &&
                expanded7.sprites()[0].width == 200 &&
                expanded7.sprites()[0].height == 25,
            "DEMO nested archives were not reconstructed");

    swd2::DemoTimeline timeline;
    std::size_t music_starts = 0;
    for (std::size_t tick = 0; tick < swd2::DemoTimeline::frame_count; ++tick) {
        const auto frame = timeline.next();
        require(frame && frame->tick == tick, "DEMO timeline ended early");
        music_starts += frame->start_music ? 1U : 0U;
        if (tick == 0) {
            require(frame->draws.size() == 11 &&
                        frame->draws[0] == swd2::DemoDrawCommand{
                            swd2::DemoSpriteSource::sword1, 0, 320, 53, false} &&
                        frame->draws[3] == swd2::DemoDrawCommand{
                            swd2::DemoSpriteSource::sword2, 1, 1045, 41, false},
                    "DEMO initial road positions/blitter modes differ from EXE data");
        }
        if (tick == 200) {
            require(frame->start_music, "DEMO did not start SWORD.RIX at CX=02f8");
        }
        if (tick == 540) {
            require(timeline.procession_state() == 1,
                    "DEMO SWORD5 animation did not start at x=100");
        }
        if (tick == 699) {
            require(timeline.final_group_started() && timeline.procession_state() == 8 &&
                        timeline.actor_state() == 10 &&
                        std::find(frame->draws.begin(), frame->draws.end(),
                                  swd2::DemoDrawCommand{
                                      swd2::DemoSpriteSource::sword6_expanded,
                                      0, 119, 112, true}) != frame->draws.end(),
                    "DEMO late archive replacement started on the wrong frame");
        }
        if (tick == 727) {
            require(timeline.final_group_started() &&
                        timeline.procession_state() == 8 &&
                        std::find(frame->draws.begin(), frame->draws.end(),
                                  swd2::DemoDrawCommand{
                                      swd2::DemoSpriteSource::sword7_expanded,
                                      1, 29, 18, true}) != frame->draws.end(),
                    "DEMO final SWORD7 composition did not reach state 29");
        }
    }
    require(!timeline.next() && timeline.tick() == 960 && timeline.first_x() == -640 &&
                timeline.actor_x() == -720 && timeline.procession_x() == -740 &&
                music_starts == 1,
            "DEMO 03c0-frame state machine did not terminate at original values");

    std::array<std::uint8_t, swd2::DemoByteMaskReveal::mask_size> full_mask{};
    full_mask.fill(0xff);
    swd2::DemoByteMaskReveal reveal(full_mask);
    std::vector<std::uint8_t> source(320 * 200, 1);
    std::vector<std::uint8_t> destination(320 * 200, 0);
    require(reveal.step(source, destination) && reveal.frame() == 1 &&
                reveal.selector() == 3 &&
                std::count(destination.begin(), destination.end(), 1) == 10'667 &&
                std::all_of(destination.begin(),
                            destination.begin() +
                                static_cast<std::ptrdiff_t>(
                                    swd2::DemoByteMaskReveal::region_offset),
                            [](std::uint8_t value) { return value == 0; }),
            "DEMO VGA reveal did not copy every third matching low-memory bit");
    for (std::size_t frame = 1; frame < swd2::DemoByteMaskReveal::frame_count; ++frame) {
        require(reveal.step(source, destination), "DEMO VGA reveal ended before 20 IRQs");
    }
    require(!reveal.step(source, destination) && reveal.selector() == 0xff &&
                swd2::portable_demo_reveal_mask() == swd2::portable_demo_reveal_mask(),
            "DEMO VGA reveal selector/twentieth-frame contract differs from 1000:0278");
}

void test_battle_module(const std::filesystem::path& game_root) {
    ScriptedPlatform round_quit_platform;
    round_quit_platform.actions.assign(2U, swd2::InputAction::confirm);
    round_quit_platform.frontend_actions = {swd2::InputAction::quit};
    auto round_quit_state = swd2::SharedState::load(
        game_root / "SAVE.DA1");
    const auto round_quit_money = round_quit_state.u16(0x104);
    round_quit_state.set_u16(0x4a0, 392);
    round_quit_state.set_u16(0x106 + 0x0c, 1234U);
    swd2::GameContext round_quit_context{
        game_root, round_quit_state, round_quit_platform};
    require(swd2::BattleModule().run(
                round_quit_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                round_quit_platform.cursor == 2U &&
                round_quit_platform.frontend_cursor == 1U &&
                round_quit_platform.stop_calls == 1U &&
                round_quit_context.shared_state.u16(0x4a0) == 0U &&
                round_quit_context.shared_state.u16(0x104) ==
                    round_quit_money,
            "FIG round presentation ignored frontend quit or awarded an unseen win");

    ScriptedPlatform defeat_quit_platform;
    defeat_quit_platform.actions.clear();
    defeat_quit_platform.frontend_actions = {swd2::InputAction::quit};
    auto defeat_quit_state = swd2::SharedState::load(
        game_root / "SAVE.DA1");
    defeat_quit_state.set_u16(0x4a0, 392);
    defeat_quit_state.set_u16(0x10, 1);
    defeat_quit_state.set_u16(0x106 + 0x2d, 0);
    defeat_quit_state.set_u16(0x106 + 8, 0x2000);
    swd2::GameContext defeat_quit_context{
        game_root, defeat_quit_state, defeat_quit_platform};
    require(swd2::BattleModule().run(
                defeat_quit_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                defeat_quit_platform.cursor == 0U &&
                defeat_quit_platform.frontend_cursor == 1U &&
                defeat_quit_platform.frontend_quit_poll_calls == 1U &&
                defeat_quit_platform.presented == 2U &&
                defeat_quit_platform.music_calls == 2U &&
                defeat_quit_platform.stop_calls == 1U &&
                defeat_quit_context.shared_state.u16(0x4a0) == 0U,
            "FIG defeat timer ignored frontend quit or returned to RPG");

    ScriptedPlatform settlement_quit_platform;
    settlement_quit_platform.actions.assign(2U, swd2::InputAction::confirm);
    settlement_quit_platform.actions.push_back(swd2::InputAction::quit);
    auto settlement_quit_state = swd2::SharedState::load(
        game_root / "SAVE.DA1");
    const auto settlement_money = settlement_quit_state.u16(0x104);
    settlement_quit_state.set_u16(0x4a0, 392);
    settlement_quit_state.set_u16(0x106 + 0x0c, 1234U);
    swd2::GameContext settlement_quit_context{
        game_root, settlement_quit_state, settlement_quit_platform};
    const auto settlement_quit_result = swd2::BattleModule().run(
        settlement_quit_context, swd2::Marker::open_figure);
    require(settlement_quit_result ==
                swd2::Marker::none &&
                settlement_quit_platform.cursor ==
                    settlement_quit_platform.actions.size() &&
                settlement_quit_platform.stop_calls == 1U &&
                settlement_quit_context.shared_state.u16(0x4a0) == 0U &&
                settlement_quit_context.shared_state.u16(0x104) ==
                    settlement_money + 20U,
            "FIG victory-page quit continued into RPG or lost committed rewards");

    ScriptedPlatform introduction_cursor_platform;
    introduction_cursor_platform.actions = {swd2::InputAction::quit};
    introduction_cursor_platform.text_actions = {swd2::InputAction::confirm};
    auto introduction_cursor_state = swd2::SharedState::load(
        game_root / "SAVE.DA1");
    introduction_cursor_state.set_u16(0x4a0, 20);  // text, no YN/NY prompt
    swd2::GameContext introduction_cursor_context{
        game_root, introduction_cursor_state, introduction_cursor_platform};
    require(swd2::BattleModule().run(
                introduction_cursor_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                introduction_cursor_platform.presented == 2U &&
                introduction_cursor_platform.direct_updates == 2U &&
                introduction_cursor_platform.text_cursor == 1U &&
                introduction_cursor_platform.cursor == 1U &&
                introduction_cursor_platform.frame_hashes[0] !=
                    introduction_cursor_platform.frame_hashes[1],
            "FIG 3ed3 did not animate the final 95h..98h text cursor");

    ScriptedPlatform immediate_battle_platform;
    immediate_battle_platform.actions = {swd2::InputAction::quit};
    auto immediate_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    immediate_state.set_u16(0x4a0, 392);  // introduction is literal empty/!!
    swd2::GameContext immediate_context{
        game_root, immediate_state, immediate_battle_platform};
    require(swd2::BattleModule().run(
                immediate_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                immediate_battle_platform.cursor == 1 &&
                immediate_battle_platform.presented == 2 &&
                immediate_context.shared_state.u16(0x4a0) == 0,
            "FIG empty ORC introduction still consumed a false confirmation gate");

    struct FigPartyOverlayCase {
        std::uint16_t hit_points;
        std::uint16_t status_bits;
        std::uint64_t expected_bottom_hash;
    };
    for (const auto& overlay_case :
         std::array<FigPartyOverlayCase, 2>{
             FigPartyOverlayCase{1, 0, 8017911571611297859ULL},
             FigPartyOverlayCase{0, 0x2000, 7538110103735948165ULL}}) {
        ScriptedPlatform overlay_platform;
        overlay_platform.actions = {swd2::InputAction::quit};
        auto overlay_state = swd2::SharedState::load(game_root / "SAVE.DA1");
        overlay_state.set_u16(0x4a0, 392);
        overlay_state.set_u16(0x106 + 0x2d, overlay_case.hit_points);
        overlay_state.set_u16(0x106 + 8, overlay_case.status_bits);
        swd2::GameContext overlay_context{
            game_root, overlay_state, overlay_platform};
        require(swd2::BattleModule().run(
                    overlay_context, swd2::Marker::open_figure) ==
                    swd2::Marker::none &&
                    overlay_platform.bottom_hashes.size() == 2 &&
                    overlay_platform.bottom_hashes.back() ==
                        overlay_case.expected_bottom_hash,
                "FIG low-HP/death overlay regression run failed");
    }

    ScriptedPlatform story_setup_platform;
    story_setup_platform.actions = {swd2::InputAction::quit};
    auto story_setup_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    story_setup_state.set_u16(0x4a0, 0x30);
    swd2::GameContext story_setup_context{
        game_root, story_setup_state, story_setup_platform};
    require(swd2::BattleModule().run(
                story_setup_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                story_setup_platform.cursor == 1 &&
                story_setup_platform.presented == 8 &&
                story_setup_context.shared_state.u16(0x4a0) == 0,
            "FIG directory-30 CD348/CD521/CD352 six-frame setup was not replayed");

    ScriptedPlatform story_setup_quit_platform;
    story_setup_quit_platform.actions = {swd2::InputAction::quit};
    story_setup_quit_platform.frontend_actions = {
        swd2::InputAction::none,
        swd2::InputAction::quit,
    };
    auto story_setup_quit_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    story_setup_quit_state.set_u16(0x4a0, 0x30);
    swd2::GameContext story_setup_quit_context{
        game_root, story_setup_quit_state, story_setup_quit_platform};
    require(swd2::BattleModule().run(
                story_setup_quit_context, swd2::Marker::open_figure) ==
                    swd2::Marker::none &&
                story_setup_quit_platform.cursor == 0U &&
                story_setup_quit_platform.frontend_cursor == 2U &&
                story_setup_quit_platform.presented == 1U &&
                story_setup_quit_platform.delay_calls == 1U &&
                story_setup_quit_platform.delayed_milliseconds == 20U &&
                story_setup_quit_platform.music_calls == 0U &&
                story_setup_quit_platform.stop_calls == 1U &&
                story_setup_quit_context.shared_state.u16(0x4a0) == 0U,
            "FIG directory-30 fixed setup ignored frontend quit");

    ScriptedPlatform story_boss_platform;
    story_boss_platform.actions = {swd2::InputAction::quit};
    auto story_boss_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    story_boss_state.set_u16(0x4a0, 0x42);
    swd2::GameContext story_boss_context{
        game_root, story_boss_state, story_boss_platform};
    require(swd2::BattleModule().run(
                story_boss_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                story_boss_platform.cursor == 1 &&
                story_boss_platform.presented == 2 &&
                story_boss_context.shared_state.u16(0x4a0) == 0,
            "FIG directory-42 did not preload its CD523 boss-action archive");

    ScriptedPlatform notice_platform;
    notice_platform.actions = {
        swd2::InputAction::left,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,  // insufficient ability resource
        swd2::InputAction::quit,
    };
    auto notice_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    notice_state.set_u16(0x4a0, 392);
    notice_state.set_u16(0x106 + 0x55, 0);
    swd2::GameContext notice_context{game_root, notice_state, notice_platform};
    const auto notice_result = swd2::BattleModule().run(
        notice_context, swd2::Marker::open_figure);
    require(notice_result ==
                swd2::Marker::none && notice_platform.presented == 5 &&
                notice_platform.direct_updates == 12U &&
                notice_platform.text_poll_calls == 12U &&
                notice_platform.frame_hashes.back() ==
                    15354309854190851288ULL,
            "FIG 3e19 modal battle notice did not consume its closing key");

    // 3f3b has a distinct icon pair/resource label for all five classes.
    // Pin the final composed page for one shipped descriptor from each class,
    // including class five's five-element glyph mask.
    static constexpr std::array<std::uint8_t, 5> ability_card_ids = {
        1, 4, 33, 3, 41,
    };
    static constexpr std::array<std::uint64_t, 5> ability_card_hashes = {
        13015473527361863690ULL,
        11277564899619558452ULL,
        18363502700070130146ULL,
        10216347769889622229ULL,
        2119409900340458190ULL,
    };
    for (std::size_t resource = 0; resource < ability_card_ids.size();
         ++resource) {
        ScriptedPlatform ability_card_platform;
        ability_card_platform.actions = {
            swd2::InputAction::left,
            swd2::InputAction::confirm,
            swd2::InputAction::quit,
        };
        auto ability_card_state =
            swd2::SharedState::load(game_root / "SAVE.DA1");
        ability_card_state.set_u16(0x4a0, 392);
        ability_card_state.set_u16(0x106 + 0x35, 1000);
        ability_card_state.set_u16(0x106 + 0x55, 1000);
        for (std::size_t slot = 0; slot < 50; ++slot) {
            ability_card_state.set_u8(0x106 + 0x6d + slot, 0);
        }
        ability_card_state.set_u8(0x106 + 0x6d,
                                  ability_card_ids[resource]);
        for (std::size_t slot = 0; slot < 5; ++slot) {
            ability_card_state.set_u16(0x3e6 + slot * 2U, 1);
        }
        swd2::GameContext ability_card_context{
            game_root, ability_card_state, ability_card_platform};
        require(swd2::BattleModule().run(
                    ability_card_context, swd2::Marker::open_figure) ==
                    swd2::Marker::none &&
                    ability_card_platform.presented == 4,
                "FIG 41a1 ability-resource card run failed");
        require(ability_card_platform.frame_hashes.back() ==
                    ability_card_hashes[resource],
                "FIG 3f3b ability-resource card pixel hash changed");
    }

    ScriptedPlatform item_card_platform;
    item_card_platform.actions = {
        swd2::InputAction::right,    // item tile
        swd2::InputAction::confirm,
        swd2::InputAction::quit,
    };
    auto item_card_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    item_card_state.set_u16(0x4a0, 392);
    item_card_state.set_u16(0x382, 51);
    swd2::GameContext item_card_context{
        game_root, item_card_state, item_card_platform};
    require(swd2::BattleModule().run(
                item_card_context, swd2::Marker::open_figure) ==
                swd2::Marker::none && item_card_platform.presented == 4 &&
                item_card_platform.frame_hashes.back() ==
                    15441783918533631957ULL,
            "FIG 1ac2 item-category card did not render");

    struct StatusCardCase {
        std::uint8_t ability_id;
        std::size_t card_frame;
        std::uint64_t card_hash;
    };
    // 57d6 is reached by five distinct effect selectors.  Ability 86 first
    // installs its required mediator, hence its card occurs eight presents
    // later than the four direct class-three abilities.
    static constexpr std::array<StatusCardCase, 5> status_card_cases = {{
        {86, 14, 12944436973162848075ULL},  // effect 63, speed
        {35, 6, 2498598045570496996ULL},    // effect 66, defence
        {38, 6, 4157160127829814172ULL},    // effect 67, attack
        {33, 6, 2315349818278800257ULL},    // effect 68, evasion
        {37, 6, 17274552217982981682ULL},   // effect 69, ward
    }};
    for (const auto& test : status_card_cases) {
        ScriptedPlatform status_card_platform;
        status_card_platform.actions = {
            swd2::InputAction::left,
            swd2::InputAction::confirm,
            swd2::InputAction::confirm,
            swd2::InputAction::quit,
        };
        auto status_card_state = swd2::SharedState::load(game_root / "SAVE.DA1");
        status_card_state.set_u16(0x4a0, 392);
        status_card_state.set_u16(0x10, 1);
        status_card_state.set_u8(0x106 + 0x6d, test.ability_id);
        status_card_state.set_u16(0x106 + 0x55, 1000);
        if (test.ability_id != 86) {
            status_card_state.set_u16(0x106 + 0x35, 1000);
        }
        status_card_state.set_u16(0x106 + 0x57, 1000);
        status_card_state.set_u16(0x106 + 0x2d, 1000);
        status_card_state.set_u16(0x106 + 0x2f, 1000);
        status_card_state.set_u16(0x106 + 0x5d, 1000);
        swd2::GameContext status_card_context{
            game_root, status_card_state, status_card_platform};
        require(swd2::BattleModule().run(
                    status_card_context, swd2::Marker::open_figure) ==
                    swd2::Marker::none &&
                    status_card_platform.frame_hashes.size() > test.card_frame &&
                    status_card_platform.frame_hashes[test.card_frame] ==
                        test.card_hash,
                "FIG 57d6 player-status information card run failed");
    }

    // A frontend close is distinct from a DOS acknowledgement and may arrive
    // during 57d6's fixed 18-tick information-card hold. Keep the queued Quit
    // untouched, stop on the already-presented defence card, and still let
    // BattleModule perform its normal shared-state cleanup.
    ScriptedPlatform status_card_quit_platform;
    status_card_quit_platform.actions = {
        swd2::InputAction::left,
        swd2::InputAction::confirm,
        swd2::InputAction::confirm,
        swd2::InputAction::quit,
    };
    status_card_quit_platform.frontend_actions.assign(
        9U, swd2::InputAction::none);
    status_card_quit_platform.frontend_actions.push_back(
        swd2::InputAction::quit);
    auto status_card_quit_state =
        swd2::SharedState::load(game_root / "SAVE.DA1");
    status_card_quit_state.set_u16(0x4a0, 392);
    status_card_quit_state.set_u16(0x10, 1);
    status_card_quit_state.set_u8(0x106 + 0x6d, 35);
    status_card_quit_state.set_u16(0x106 + 0x35, 1000);
    status_card_quit_state.set_u16(0x106 + 0x55, 1000);
    status_card_quit_state.set_u16(0x106 + 0x57, 1000);
    status_card_quit_state.set_u16(0x106 + 0x2d, 1000);
    status_card_quit_state.set_u16(0x106 + 0x2f, 1000);
    status_card_quit_state.set_u16(0x106 + 0x5d, 1000);
    swd2::GameContext status_card_quit_context{
        game_root, status_card_quit_state, status_card_quit_platform};
    const auto status_card_quit_result = swd2::BattleModule().run(
        status_card_quit_context, swd2::Marker::open_figure);
    require(status_card_quit_result == swd2::Marker::none &&
                status_card_quit_platform.cursor == 3U &&
                status_card_quit_platform.frontend_cursor == 10U &&
                status_card_quit_platform.frontend_quit_poll_calls == 10U &&
                status_card_quit_platform.frame_hashes.size() == 7U &&
                status_card_quit_platform.frame_hashes.back() ==
                    2498598045570496996ULL &&
                status_card_quit_platform.delay_calls == 6U &&
                status_card_quit_platform.delayed_milliseconds == 86U &&
                status_card_quit_platform.stop_calls == 1U &&
                status_card_quit_context.shared_state.u16(0x4a0) == 0U,
            "FIG 57d6 fixed status-card hold ignored frontend quit");

    ScriptedPlatform target_overlay_platform;
    target_overlay_platform.actions = {
        swd2::InputAction::confirm,  // attack tile
        swd2::InputAction::confirm,  // normal/group attack
        swd2::InputAction::quit,     // stop on the multi-monster list
    };
    auto target_overlay_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    target_overlay_state.set_u16(0x4a0, 12);  // empty intro, two monsters
    swd2::GameContext target_overlay_context{
        game_root, target_overlay_state, target_overlay_platform};
    require(swd2::BattleModule().run(
                target_overlay_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                target_overlay_platform.presented == 4 &&
                target_overlay_platform.frame_hashes.back() ==
                    9077867568305503402ULL,
            "FIG 178c target list did not preserve its dimmed attack menus");

    ScriptedPlatform platform;
    // Empty introductions enter immediately; pairs of confirmations choose
    // Attack and the highlighted target for each commandable actor.
    platform.actions.assign(10'000, swd2::InputAction::confirm);
    auto state = swd2::SharedState::load(game_root / "SAVE.DA1");
    const auto money_before = state.u16(0x104);
    const auto experience_before = state.u16(0x106 + 0x39);
    const auto experience_one_before = state.u16(0x106 + 0x9f + 0x39);
    const auto experience_two_before = state.u16(0x106 + 2 * 0x9f + 0x39);
    state.set_u16(0x4a0, 392);
    state.set_u16(0x106 + 0x0c, 1234);
    // Exercise FIG's end-of-battle status mask without setting any bit in the
    // exact 0x2c7e incapacitation mask (which would correctly skip actor zero).
    state.set_u16(0x106 + 8, 0x0381);
    state.set_u16(0x382, 5);
    state.set_u16(0x384, 0);
    state.set_u16(0x386, 6);
    swd2::GameContext context{game_root, state, platform};
    require(swd2::BattleModule().run(context, swd2::Marker::open_figure) ==
                swd2::Marker::continue_rpg,
            "in-process FIG module did not return the OC marker");
    require(platform.presented > 1 && platform.music_calls == 2 && platform.stop_calls == 1,
            "FIG module did not present interactive battle frames and manage audio");
    require(context.shared_state.u16(0x4a0) == 0 &&
                context.shared_state.u16(0x106 + 0x41) == 1234 &&
                context.shared_state.u16(0x106 + 0x0c) == 1234 &&
                context.shared_state.u16(0x106 + 8) == 0x0200 &&
                context.shared_state.u16(0x382) == 5 &&
                context.shared_state.u16(0x384) == 6 &&
                context.shared_state.u16(0x386) == 0,
            "FIG module did not apply its exact shared-state return contract");
    require(context.shared_state.u16(0x104) == money_before + 20 &&
                context.shared_state.u16(0x106 + 0x39) == experience_before + 44 &&
                context.shared_state.u16(0x106 + 0x9f + 0x39) ==
                    experience_one_before + 44 &&
                context.shared_state.u16(0x106 + 2 * 0x9f + 0x39) ==
                    experience_two_before + 44,
            "FIG victory did not distribute ITEM reward fields like the original");

    ScriptedPlatform level_up_platform;
    level_up_platform.actions.assign(10'000, swd2::InputAction::confirm);
    auto level_up_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    level_up_state.set_u16(0x4a0, 392);
    const auto old_level = level_up_state.u16(0x106 + 0x31);
    const auto next_threshold = level_up_state.u16(0x106 + 0x3b);
    require(next_threshold >= 44,
            "FIG level-up regression threshold is smaller than reward share");
    level_up_state.set_u16(0x106 + 0x39,
                           static_cast<std::uint16_t>(next_threshold - 44U));
    for (std::size_t actor = 0; actor < 3; ++actor) {
        const auto base = 0x106 + actor * 0x9f;
        level_up_state.set_u16(base + 0x0c, 1234);
        level_up_state.set_u16(base + 0x2d, 1000);
        level_up_state.set_u16(base + 0x2f, 1000);
        level_up_state.set_u16(base + 0x5d, 1000);
    }
    swd2::GameContext level_up_context{
        game_root, level_up_state, level_up_platform};
    require(swd2::BattleModule().run(
                level_up_context, swd2::Marker::open_figure) ==
                swd2::Marker::continue_rpg &&
                level_up_context.shared_state.u16(0x106 + 0x31) ==
                    old_level + 1U &&
                level_up_context.shared_state.u16(0x106 + 0x39) == 0 &&
                level_up_platform.music_calls == 3,
            "FIG 07d2 growth step/WI02 level-up presentation did not run");

    ScriptedPlatform automatic_platform;
    automatic_platform.actions = {
        swd2::InputAction::confirm,  // attack tile
        swd2::InputAction::right,    // automatic mode
        swd2::InputAction::confirm,
    };
    auto automatic_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    automatic_state.set_u16(0x4a0, 392);
    for (std::size_t actor = 0; actor < 3; ++actor) {
        const auto base = 0x106 + actor * 0x9f;
        automatic_state.set_u16(base + 0x0c, 25);
        automatic_state.set_u16(base + 0x0e, 1000);
        automatic_state.set_u16(base + 0x2d, 1000);
        automatic_state.set_u16(base + 0x2f, 1000);
        automatic_state.set_u16(base + 0x5d, 1000);
    }
    swd2::GameContext automatic_context{
        game_root, automatic_state, automatic_platform};
    require(swd2::BattleModule().run(
                automatic_context, swd2::Marker::open_figure) ==
                swd2::Marker::continue_rpg &&
                automatic_platform.cursor == automatic_platform.actions.size(),
            "FIG automatic mode did not persist without reopening later-round menus");

    ScriptedPlatform interrupted_automatic_platform;
    interrupted_automatic_platform.actions = {
        swd2::InputAction::confirm,
        swd2::InputAction::right,
        swd2::InputAction::confirm,
        swd2::InputAction::cancel,  // timer-polled automatic interruption
    };
    swd2::GameContext interrupted_automatic_context{
        game_root, automatic_state, interrupted_automatic_platform};
    require(swd2::BattleModule().run(
                interrupted_automatic_context, swd2::Marker::open_figure) ==
                swd2::Marker::none &&
                interrupted_automatic_platform.cursor ==
                    interrupted_automatic_platform.actions.size(),
            "FIG automatic mode did not consume a key and reopen command collection");

    ScriptedPlatform effect_voice_platform;
    effect_voice_platform.actions.clear();
    for (std::size_t actor = 0; actor < 3; ++actor) {
        effect_voice_platform.actions.insert(
            effect_voice_platform.actions.end(),
            {swd2::InputAction::left, swd2::InputAction::confirm,
             swd2::InputAction::confirm});
    }
    effect_voice_platform.actions.push_back(swd2::InputAction::quit);
    auto effect_voice_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    effect_voice_state.set_u16(0x4a0, 392);
    for (std::size_t actor = 0; actor < 3; ++actor) {
        const auto base = 0x106 + actor * 0x9f;
        effect_voice_state.set_u16(base + 0x55, 0xffff);
        effect_voice_state.set_u16(base + 0x57, 0xffff);
        for (std::size_t slot = 0; slot < 50; ++slot) {
            effect_voice_state.set_u8(base + 0x6d + slot, 0);
        }
        effect_voice_state.set_u8(base + 0x6d, 1);  // effect 4e / SP078.VOC
    }
    swd2::GameContext effect_voice_context{
        game_root, effect_voice_state, effect_voice_platform};
    const auto effect_voice_result = swd2::BattleModule().run(
        effect_voice_context, swd2::Marker::open_figure);
    const auto expected_voice = read_file(game_root / "VC" / "SP078.VOC");
    std::uint64_t expected_voice_hash = 1469598103934665603ULL;
    for (const auto byte : expected_voice) {
        expected_voice_hash ^= byte;
        expected_voice_hash *= 1099511628211ULL;
    }
    require((effect_voice_result == swd2::Marker::continue_rpg ||
             effect_voice_result == swd2::Marker::none) &&
                effect_voice_platform.voice_calls != 0 &&
                std::find(effect_voice_platform.voice_hashes.begin(),
                          effect_voice_platform.voice_hashes.end(),
                          expected_voice_hash) !=
                    effect_voice_platform.voice_hashes.end(),
            "FIG effect 4e did not stream the original SP078.VOC payload");

    ScriptedPlatform prompt_platform;
    prompt_platform.actions = {swd2::InputAction::confirm};
    prompt_platform.text_actions = {swd2::InputAction::confirm};
    auto prompt_state = swd2::SharedState::load(game_root / "SAVE.DA1");
    prompt_state.set_u8(0x3f4, 1U);
    prompt_state.set_u8(0x3f5, 1U);
    const auto prompt_money = prompt_state.u16(0x104);
    prompt_state.set_u16(0x4a0, 150);  // ORC trailing "NY": default No
    swd2::GameContext prompt_context{game_root, prompt_state, prompt_platform};
    const auto prompt_result = swd2::BattleModule().run(
        prompt_context, swd2::Marker::open_figure);
    require(prompt_result == swd2::Marker::continue_rpg &&
                prompt_platform.presented == 2 &&
                prompt_platform.frame_hashes.size() == 2 &&
                prompt_platform.frame_hashes[1] == 16443477943331878383ULL &&
                prompt_platform.text_cursor == 1U &&
                prompt_platform.text_poll_calls == 1U &&
                prompt_platform.direct_updates == 2U &&
                prompt_platform.music_calls == 0U &&
                prompt_platform.voice_calls == 0U &&
                prompt_context.shared_state.u16(0x4a0) == 0 &&
                prompt_context.shared_state.u16(0x104) == prompt_money,
            "FIG ORC NY prompt/audio flags differ from exact 3de8/5c98/default No");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "test requires the game directory argument");
        test_launcher();
        test_paths();
        test_replay_input();
        test_rpg_mode_x_event_offset();
        test_rpg_opcode55_monochrome(argv[1]);
        test_rpg_save_slot_selector(argv[1]);
        test_original_launcher(argv[1]);
        test_resource_decoder(argv[1]);
        test_voc_decoder(argv[1]);
        test_rix_decoder(argv[1]);
        test_shared_state(argv[1]);
        test_item_inventory(argv[1]);
        test_field_actions(argv[1]);
        test_map_resource(argv[1]);
        test_map_database(argv[1]);
        test_map_transition_database(argv[1]);
        test_rpg_entity_system(argv[1]);
        test_save_slot(argv[1]);
        test_planar_sprite_set(argv[1]);
        test_battle_database(argv[1]);
        test_mon_database(argv[1]);
        test_battle_rules();
        test_battle_random(argv[1]);
        test_battle_party(argv[1]);
        test_fig_effect_timeline(argv[1]);
        test_battle_effects(argv[1]);
        test_battle_session(argv[1]);
        test_battle_ai(argv[1]);
        test_legacy_event_resources(argv[1]);
        test_event_vm(argv[1]);
        test_stateful_event_opcodes(argv[1]);
        test_meo_exit_fade(argv[1]);
        test_monolithic_runtime(argv[1]);
        test_rpg_opening_menu(argv[1]);
        test_rpg_entity_dialogue(argv[1]);
        test_rpg_event_program_exit(argv[1]);
        test_rpg_idle_world_ticks(argv[1]);
        test_rpg_map_portal(argv[1]);
        test_rpg_map_chained_spawn_trigger(argv[1]);
        test_rpg_opcode37_chained_spawn_event(argv[1]);
        test_rpg_sa_scripted_entity_frames(argv[1]);
        test_rpg_map_special_event(argv[1]);
        test_rpg_map_actor_variant(argv[1]);
        test_rpg_top_dialogue_panel(argv[1]);
        test_rpg_field_menu_inventory(argv[1]);
        test_rpg_inventory_item_actions(argv[1]);
        test_rpg_inventory_alchemy(argv[1]);
        test_rpg_inventory_equipment_screen(argv[1]);
        test_rpg_inventory_empty_slot_unequip(argv[1]);
        test_rpg_field_status_menu(argv[1]);
        test_rpg_field_magic_menu(argv[1]);
        test_rpg_field_magic_cast(argv[1]);
        test_rpg_field_magic_value_error(argv[1]);
        test_rpg_field_magic_description(argv[1]);
        test_rpg_field_magic_refine(argv[1]);
        test_rpg_field_magic_refine_full(argv[1]);
        test_rpg_field_magic_material_cast(argv[1]);
        test_rpg_field_magic_material_error(argv[1]);
        test_rpg_field_magic_travel(argv[1]);
        test_rpg_field_magic_travel_current(argv[1]);
        test_rpg_field_magic_travel_restricted(argv[1]);
        test_rpg_system_menu_speed_and_exit(argv[1]);
        test_rpg_system_value_confirmation_escape(argv[1]);
        test_rpg_system_value_left_wrap(argv[1]);
        test_rpg_system_menu_save(argv[1]);
        test_rpg_system_menu_save_restricted(argv[1]);
        test_rpg_system_menu_load(argv[1]);
        test_rpg_system_audio_toggle(argv[1]);
        test_rpg_entity_collision(argv[1]);
        test_rpg_behavior_six_collision(argv[1]);
        test_rpg_interaction_rays(argv[1]);
        test_rpg_corner_slide(argv[1]);
        test_rpg_overworld_poison(argv[1]);
        test_rpg_random_encounter(argv[1]);
        test_rpg_automatic_entity_event(argv[1]);
        test_rpg_event_voice(argv[1]);
        test_rpg_compact_money_overlay(argv[1]);
        test_rpg_dialogue_then_money_overlay(argv[1]);
        test_rpg_shop_confirmation(argv[1]);
        test_rpg_cutscene_presentation(argv[1]);
        test_rpg_opcode55_cutscene(argv[1]);
        test_demo_module(argv[1]);
        test_demo_timeline(argv[1]);
        test_battle_module(argv[1]);
        std::cout << "all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
