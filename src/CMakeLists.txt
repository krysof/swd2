add_library(ymfm_core STATIC
    third_party/ymfm/ymfm_adpcm.cpp
    third_party/ymfm/ymfm_opl.cpp
    third_party/ymfm/ymfm_pcm.cpp
)
target_include_directories(ymfm_core SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm")
target_compile_features(ymfm_core PUBLIC cxx_std_14)
if(MSVC)
    target_compile_options(ymfm_core PRIVATE /W0)
else()
    target_compile_options(ymfm_core PRIVATE -w)
endif()

add_library(swd2_core
    asset_catalog.cpp
    audio_loop_clock.cpp
    battle_ai.cpp
    battle_ability_database.cpp
    battle_composite_effect.cpp
    battle_command_menu.cpp
    battle_database.cpp
    battle_effects.cpp
    battle_item_definition.cpp
    battle_module.cpp
    battle_party.cpp
    battle_presentation.cpp
    battle_random.cpp
    battle_rules.cpp
    battle_session.cpp
    dialogue.cpp
    demo_module.cpp
    demo_timeline.cpp
    event_program.cpp
    event_vm.cpp
    field_action_system.cpp
    inventory_system.cpp
    item_database.cpp
    launcher.cpp
    legacy_font.cpp
    map_database.cpp
    map_resource.cpp
    map_transition_database.cpp
    meo.cpp
    meo_module.cpp
    mon_database.cpp
    monster_definition.cpp
    mz_executable.cpp
    planar_sprite_set.cpp
    rsk_decoder.cpp
    rpg_entity_system.cpp
    rpg_module.cpp
    rpg_presentation.cpp
    replay_input.cpp
    rix_decoder.cpp
    runtime.cpp
    save_slot.cpp
    script_archive.cpp
    shared_state.cpp
    sprite_archive.cpp
    voc_decoder.cpp
    ym3812_synth.cpp
)

target_include_directories(swd2_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(swd2_core PUBLIC cxx_std_20)
target_link_libraries(swd2_core PRIVATE ymfm_core)

if(MSVC)
    target_compile_options(swd2_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(swd2_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(swd2_rewrite main.cpp)
target_link_libraries(swd2_rewrite PRIVATE swd2_core)

# SDL is a shell around the platform-independent core. It is linked into the
# same swd2_rewrite program rather than producing another game executable.
# Emscripten supplies SDL2 as a port, while native builds discover an installed
# SDL2 package in the usual way.
if(EMSCRIPTEN)
    target_sources(swd2_rewrite PRIVATE sdl_platform.cpp)
    target_compile_definitions(swd2_rewrite PRIVATE SWD2_HAVE_SDL2=1)
    # The core uses C++ exceptions for parse/IO error propagation. Optimized
    # Emscripten builds disable catching by default. Use Emscripten exception
    # handling rather than native Wasm EH because the synchronous game loop
    # also relies on ASYNCIFY for cooperative browser waits.
    target_compile_options(swd2_rewrite PRIVATE "SHELL:-sUSE_SDL=2")

    set(SWD2_GAME_DIR "${PROJECT_SOURCE_DIR}/game" CACHE PATH
        "Original game data directory to preload into the WebAssembly build")
    option(SWD2_WEB_PRELOAD_GAME_DATA
        "Package the original game data into the browser build" ON)
    if(SWD2_WEB_PRELOAD_GAME_DATA AND NOT IS_DIRECTORY "${SWD2_GAME_DIR}")
        message(FATAL_ERROR "SWD2_GAME_DIR does not exist: ${SWD2_GAME_DIR}")
    endif()

    target_link_options(swd2_rewrite PRIVATE
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sSTACK_SIZE=1048576"
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-sENVIRONMENT=web"
        "SHELL:-lidbfs.js"
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )
    # CMake passes --shell-file to Emscripten but does not infer that the HTML
    # is a link input. Track it explicitly so UI-only edits cannot leave a
    # stale index.html in an otherwise up-to-date Ninja build or deployment.
    set_property(TARGET swd2_rewrite APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    if(SWD2_WEB_PRELOAD_GAME_DATA)
        target_link_options(swd2_rewrite PRIVATE
            "--preload-file" "${SWD2_GAME_DIR}@/game"
        )
    endif()

    # A project Pages site can serve these relative files from /swd2/ without
    # any hard-coded origin or path prefix.
    set_target_properties(swd2_rewrite PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/site"
    )
else()
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
        target_sources(swd2_rewrite PRIVATE sdl_platform.cpp)
        target_link_libraries(swd2_rewrite PRIVATE SDL2::SDL2)
        target_compile_definitions(swd2_rewrite PRIVATE SWD2_HAVE_SDL2=1)
        if(BUILD_TESTING)
            add_executable(swd2_sdl_tests
                tests/test_sdl_platform.cpp
                sdl_platform.cpp
            )
            target_link_libraries(swd2_sdl_tests PRIVATE swd2_core SDL2::SDL2)
            target_compile_definitions(swd2_sdl_tests PRIVATE SWD2_HAVE_SDL2=1)
            add_test(NAME sdl_portable_input COMMAND swd2_sdl_tests)
            set_tests_properties(sdl_portable_input PROPERTIES ENVIRONMENT
                "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
        endif()
    endif()
endif()

if(BUILD_TESTING)
    add_executable(swd2_tests tests/test_main.cpp)
    target_link_libraries(swd2_tests PRIVATE swd2_core)
    add_test(NAME swd2_tests COMMAND swd2_tests "${PROJECT_SOURCE_DIR}/game")
    add_test(
        NAME reachable_event_audit
        COMMAND swd2_rewrite --game "${PROJECT_SOURCE_DIR}/game" --verify-events
    )
    add_test(
        NAME resource_audit
        COMMAND swd2_rewrite --game "${PROJECT_SOURCE_DIR}/game" --verify-resources
    )
    add_test(
        NAME map_audit
        COMMAND swd2_rewrite --game "${PROJECT_SOURCE_DIR}/game" --verify-maps
    )
    add_test(
        NAME battle_audit
        COMMAND swd2_rewrite --game "${PROJECT_SOURCE_DIR}/game" --verify-battles
    )
    add_test(
        NAME legacy_program_data_audit
        COMMAND swd2_rewrite --game "${PROJECT_SOURCE_DIR}/game"
                --verify-legacy-program-data
    )
    add_test(
        NAME replay_trace_smoke
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/replay-smoke-saves"
                --no-save
                --run-replay "${PROJECT_SOURCE_DIR}/scripts/replay-smoke.txt"
                --trace-output "${CMAKE_BINARY_DIR}/replay-smoke-trace.json"
                --frame-output "${CMAKE_BINARY_DIR}/replay-smoke.swd2frames"
    )
    add_test(
        NAME replay_trace_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-replay-trace.py"
                "${CMAKE_BINARY_DIR}/replay-smoke-trace.json"
                --require-module MEO.EXE
                --require-module RPG.EXE
                --min-frames 4
    )
    set_tests_properties(replay_trace_validation PROPERTIES
        DEPENDS replay_trace_smoke)
    add_test(
        NAME replay_trace_comparison
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/compare-replay-traces.py"
                "${CMAKE_BINARY_DIR}/replay-smoke-trace.json"
                "${CMAKE_BINARY_DIR}/replay-smoke-trace.json"
                --output "${CMAKE_BINARY_DIR}/replay-smoke-comparison.json"
    )
    set_tests_properties(replay_trace_comparison PROPERTIES
        DEPENDS replay_trace_smoke)
    add_test(
        NAME frame_capture_comparison
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/compare-frame-captures.py"
                "${CMAKE_BINARY_DIR}/replay-smoke.swd2frames"
                "${CMAKE_BINARY_DIR}/replay-smoke.swd2frames"
                --input "${PROJECT_SOURCE_DIR}/scripts/replay-smoke.txt"
                --output "${CMAKE_BINARY_DIR}/replay-smoke-pixel-diff.json"
    )
    set_tests_properties(frame_capture_comparison PROPERTIES
        DEPENDS replay_trace_smoke)
    add_test(
        NAME frame_capture_report_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/compare-frame-captures.py"
                "${CMAKE_BINARY_DIR}/replay-smoke.swd2frames"
                "${CMAKE_BINARY_DIR}/replay-smoke.swd2frames"
                --input "${PROJECT_SOURCE_DIR}/scripts/replay-smoke.txt"
                --verify-report "${CMAKE_BINARY_DIR}/replay-smoke-pixel-diff.json"
    )
    set_tests_properties(frame_capture_report_validation PROPERTIES
        DEPENDS frame_capture_comparison)
    add_test(
        NAME new_game_startup_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/new-game-startup-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-new-game-startup.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/new-game-startup-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/new-game-startup-frames.bin"
    )
    add_test(
        NAME new_game_startup_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-replay-trace.py"
                "${CMAKE_BINARY_DIR}/new-game-startup-trace.json"
                --require-module MEO.EXE
                --require-module RPG.EXE
                --require-module DEMO.EXE
                --min-frames 220
    )
    set_tests_properties(new_game_startup_validation PROPERTIES
        DEPENDS new_game_startup_replay)
    add_test(
        NAME new_game_startup_checkpoint
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-new-game-startup.py"
                "${CMAKE_BINARY_DIR}/new-game-startup-trace.json"
    )
    set_tests_properties(new_game_startup_checkpoint PROPERTIES
        DEPENDS new_game_startup_validation)
    add_test(
        NAME rpg_name_bitmap_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/name-bitmap-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-name-bitmap.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/name-bitmap-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/name-bitmap-frames.bin"
    )
    add_test(
        NAME rpg_name_bitmap_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-name-bitmap.py"
                "${CMAKE_BINARY_DIR}/name-bitmap-trace.json"
    )
    set_tests_properties(rpg_name_bitmap_validation PROPERTIES
        DEPENDS rpg_name_bitmap_replay)
    add_test(
        NAME rpg_system_menu_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-menu-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-menu.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-menu-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-menu-frames.bin"
    )
    add_test(
        NAME rpg_system_menu_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-menu.py"
                "${CMAKE_BINARY_DIR}/rpg-system-menu-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-system-menu-frames.bin"
    )
    set_tests_properties(rpg_system_menu_validation PROPERTIES
        DEPENDS rpg_system_menu_replay)
    add_test(
        NAME rpg_system_toggles_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-toggles-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-toggles.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-toggles-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-toggles-frames.bin"
    )
    add_test(
        NAME rpg_system_toggles_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-toggles.py"
                "${CMAKE_BINARY_DIR}/rpg-system-toggles-trace.json"
    )
    set_tests_properties(rpg_system_toggles_validation PROPERTIES
        DEPENDS rpg_system_toggles_replay)
    add_test(
        NAME rpg_system_speed_wrap_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-speed-wrap-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-speed-wrap.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-speed-wrap-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-speed-wrap-frames.bin"
    )
    add_test(
        NAME rpg_system_speed_wrap_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-speed-wrap.py"
                "${CMAKE_BINARY_DIR}/rpg-system-speed-wrap-trace.json"
    )
    set_tests_properties(rpg_system_speed_wrap_validation PROPERTIES
        DEPENDS rpg_system_speed_wrap_replay)
    add_test(
        NAME rpg_system_exit_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-exit-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-exit.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-exit-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-exit-frames.bin"
    )
    add_test(
        NAME rpg_system_exit_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-exit.py"
                "${CMAKE_BINARY_DIR}/rpg-system-exit-trace.json"
    )
    set_tests_properties(rpg_system_exit_validation PROPERTIES
        DEPENDS rpg_system_exit_replay)
    add_test(
        NAME rpg_system_record_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-record-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-record.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-record-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-record-frames.bin"
    )
    add_test(
        NAME rpg_system_record_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-record.py"
                "${CMAKE_BINARY_DIR}/rpg-system-record-trace.json"
    )
    set_tests_properties(rpg_system_record_validation PROPERTIES
        DEPENDS rpg_system_record_replay)
    add_test(
        NAME rpg_system_read_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-read-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-read.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-read-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-read-frames.bin"
    )
    add_test(
        NAME rpg_system_read_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-read.py"
                "${CMAKE_BINARY_DIR}/rpg-system-read-trace.json"
    )
    set_tests_properties(rpg_system_read_validation PROPERTIES
        DEPENDS rpg_system_read_replay)
    add_test(
        NAME rpg_inventory_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-inventory-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-inventory.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-inventory-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-inventory-frames.bin"
    )
    add_test(
        NAME rpg_inventory_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-inventory.py"
                "${CMAKE_BINARY_DIR}/rpg-inventory-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-inventory-frames.bin"
    )
    set_tests_properties(rpg_inventory_validation PROPERTIES
        DEPENDS rpg_inventory_replay)
    add_test(
        NAME rpg_field_diamond_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-field-diamond-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-field-diamond.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-field-diamond-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-field-diamond-frames.bin"
    )
    add_test(
        NAME rpg_field_diamond_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-field-diamond.py"
                "${CMAKE_BINARY_DIR}/rpg-field-diamond-trace.json"
    )
    set_tests_properties(rpg_field_diamond_validation PROPERTIES
        DEPENDS rpg_field_diamond_replay)
    add_test(
        NAME rpg_magic_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-magic-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-magic.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-magic-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-magic-frames.bin"
    )
    add_test(
        NAME rpg_magic_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-magic.py"
                "${CMAKE_BINARY_DIR}/rpg-magic-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-magic-frames.bin"
    )
    set_tests_properties(rpg_magic_validation PROPERTIES
        DEPENDS rpg_magic_replay)
    add_test(
        NAME rpg_status_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-status-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-status.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-status-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-status-frames.bin"
    )
    add_test(
        NAME rpg_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-status.py"
                "${CMAKE_BINARY_DIR}/rpg-status-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-status-frames.bin"
    )
    set_tests_properties(rpg_status_validation PROPERTIES
        DEPENDS rpg_status_replay)
    add_test(
        NAME rpg_equipment_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-equipment-saves"
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-equipment.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-equipment-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-equipment-frames.bin"
    )
    add_test(
        NAME rpg_equipment_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-equipment.py"
                "${CMAKE_BINARY_DIR}/rpg-equipment-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-equipment-frames.bin"
    )
    set_tests_properties(rpg_equipment_validation PROPERTIES
        DEPENDS rpg_equipment_replay)
    add_test(
        NAME new_game_exit_checkpoint_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/new-game-exit-checkpoint-saves"
                --slot 3
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-new-game-startup.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/new-game-exit-checkpoint-trace.json"
    )
    add_test(
        NAME new_game_exit_checkpoint_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-live-save-pair.py"
                "${CMAKE_BINARY_DIR}/new-game-exit-checkpoint-trace.json"
                "${CMAKE_BINARY_DIR}/new-game-exit-checkpoint-saves/SAVE.DA3"
                "${CMAKE_BINARY_DIR}/new-game-exit-checkpoint-saves/MAPZ.DA3"
                "${CMAKE_BINARY_DIR}/new-game-exit-checkpoint-saves/NAME3.DSK"
    )
    set_tests_properties(new_game_exit_checkpoint_validation PROPERTIES
        DEPENDS new_game_exit_checkpoint_replay)
    add_test(
        NAME explicit_save_routing_prepare
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "${CMAKE_BINARY_DIR}/explicit-save-routing-saves"
    )
    add_test(
        NAME explicit_save_routing_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/explicit-save-routing-saves"
                --slot 1
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-explicit-save.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/explicit-save-routing-trace.json"
    )
    set_tests_properties(explicit_save_routing_replay PROPERTIES
        DEPENDS explicit_save_routing_prepare)
    add_test(
        NAME explicit_save_routing_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-explicit-save-routing.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/explicit-save-routing-saves"
                "${CMAKE_BINARY_DIR}/explicit-save-routing-trace.json"
    )
    set_tests_properties(explicit_save_routing_validation PROPERTIES
        DEPENDS explicit_save_routing_replay)
    add_test(
        NAME no_save_mode_prepare
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "${CMAKE_BINARY_DIR}/no-save-mode-saves"
    )
    add_test(
        NAME no_save_mode_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/no-save-mode-saves"
                --slot 1
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-explicit-save.txt"
    )
    set_tests_properties(no_save_mode_replay PROPERTIES
        DEPENDS no_save_mode_prepare)
    add_test(
        NAME no_save_mode_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-no-save-mode.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/no-save-mode-saves"
    )
    set_tests_properties(no_save_mode_validation PROPERTIES
        DEPENDS no_save_mode_replay)
endif()
