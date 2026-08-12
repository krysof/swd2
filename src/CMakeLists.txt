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
        NAME fig_type10_composite_coverage
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/audit-fig-type10-composites.py"
                "${PROJECT_SOURCE_DIR}/game"
    )
    add_test(
        NAME fig_learned_composite_coverage
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/audit-fig-learned-composites.py"
                "${PROJECT_SOURCE_DIR}/game"
    )
    add_test(
        NAME fig_rgb_checkpoint_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-rgb-checkpoint.py"
                "${PROJECT_SOURCE_DIR}/verification/pixel_diffs/checkpoint-2026-08-11-fig-rgb.json"
    )
    add_test(
        NAME wasm_safari_idbfs_checkpoint
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-wasm-safari-idbfs-reference.py"
                "${PROJECT_SOURCE_DIR}/scripts/wasm-safari-idbfs-reference.json"
    )
    add_test(
        NAME wasm_firefox_idbfs_checkpoint
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-wasm-firefox-idbfs-reference.py"
                "${PROJECT_SOURCE_DIR}/scripts/wasm-firefox-idbfs-reference.json"
    )
    add_test(
        NAME rix_audio_clock_soak_checkpoint
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rix-audio-clock-soak.py"
                "${PROJECT_SOURCE_DIR}/scripts/rix-audio-clock-soak-reference.json"
                "${PROJECT_SOURCE_DIR}/game"
    )
    add_test(
        NAME fig_wall_clock_checkpoint
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/build-fig-wall-clock-checkpoint.py"
                "${PROJECT_SOURCE_DIR}/verification/audio/checkpoint-2026-08-12-fig-wall-clock.json"
                --verify
    )
    add_test(
        NAME fig_timer_machine_code_audit
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-timer-machine-code.py"
                "${PROJECT_SOURCE_DIR}/game"
    )
    add_test(
        NAME fig_story_visual_branch_coverage
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/audit-fig-story-visual-branches.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${PROJECT_SOURCE_DIR}/src/battle_module.cpp"
                "${PROJECT_SOURCE_DIR}/scripts"
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
        NAME rpg_opening_pixel_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-opening.py"
                "${CMAKE_BINARY_DIR}/replay-smoke-trace.json"
                "${CMAKE_BINARY_DIR}/replay-smoke.swd2frames"
    )
    set_tests_properties(rpg_opening_pixel_validation PROPERTIES
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
                "${CMAKE_BINARY_DIR}/rpg-system-toggles-frames.bin"
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
                "${CMAKE_BINARY_DIR}/rpg-system-speed-wrap-frames.bin"
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
                "${CMAKE_BINARY_DIR}/rpg-system-exit-frames.bin"
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
                "${CMAKE_BINARY_DIR}/rpg-system-record-frames.bin"
    )
    set_tests_properties(rpg_system_record_validation PROPERTIES
        DEPENDS rpg_system_record_replay)
    add_test(
        NAME rpg_system_record_commit_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-record-commit-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-system-record-commit-saves"
    )
    add_test(
        NAME rpg_system_record_commit_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-system-record-commit-saves"
                --slot 1
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-system-record-commit.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-system-record-commit-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-system-record-commit-frames.bin"
    )
    set_tests_properties(rpg_system_record_commit_replay PROPERTIES
        DEPENDS rpg_system_record_commit_prepare)
    add_test(
        NAME rpg_system_record_commit_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-system-record-commit.py"
                "${CMAKE_BINARY_DIR}/rpg-system-record-commit-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-system-record-commit-frames.bin"
                "${CMAKE_BINARY_DIR}/rpg-system-record-commit-saves"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-system-record-commit-rgb-reference.json"
    )
    set_tests_properties(rpg_system_record_commit_validation PROPERTIES
        DEPENDS rpg_system_record_commit_replay)
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
                "${CMAKE_BINARY_DIR}/rpg-system-read-frames.bin"
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
                "${CMAKE_BINARY_DIR}/rpg-field-diamond-frames.bin"
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
        NAME rpg_travel_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-travel-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-travel-saves"
    )
    add_test(
        NAME rpg_travel_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-travel-saves"
                --slot 1
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-travel.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-travel-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-travel-frames.bin"
    )
    set_tests_properties(rpg_travel_replay PROPERTIES
        DEPENDS rpg_travel_prepare)
    add_test(
        NAME rpg_travel_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-travel.py"
                "${CMAKE_BINARY_DIR}/rpg-travel-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-travel-frames.bin"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-travel-rgb-reference.json"
    )
    set_tests_properties(rpg_travel_validation PROPERTIES
        DEPENDS rpg_travel_replay)
    add_test(
        NAME rpg_shop_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-shop-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-shop-saves"
    )
    add_test(
        NAME rpg_shop_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-shop-saves"
                --slot 1
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-shop.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-shop-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-shop-frames.bin"
    )
    set_tests_properties(rpg_shop_replay PROPERTIES
        DEPENDS rpg_shop_prepare)
    add_test(
        NAME rpg_shop_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-shop.py"
                "${CMAKE_BINARY_DIR}/rpg-shop-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-shop-frames.bin"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-shop-rgb-reference.json"
    )
    set_tests_properties(rpg_shop_validation PROPERTIES
        DEPENDS rpg_shop_replay)
    add_test(
        NAME rpg_sell_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-sell-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-sell-saves"
    )
    add_test(
        NAME rpg_sell_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-sell-saves"
                --slot 1
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-sell.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-sell-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-sell-frames.bin"
    )
    set_tests_properties(rpg_sell_replay PROPERTIES
        DEPENDS rpg_sell_prepare)
    add_test(
        NAME rpg_sell_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-sell.py"
                "${CMAKE_BINARY_DIR}/rpg-sell-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-sell-frames.bin"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-sell-rgb-reference.json"
    )
    set_tests_properties(rpg_sell_validation PROPERTIES
        DEPENDS rpg_sell_replay)
    add_test(
        NAME rpg_item_action_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-item-action-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-item-action-saves"
    )
    add_test(
        NAME rpg_item_action_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-item-action-saves"
                --slot 1
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-item-action.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-item-action-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-item-action-frames.bin"
    )
    set_tests_properties(rpg_item_action_replay PROPERTIES
        DEPENDS rpg_item_action_prepare)
    add_test(
        NAME rpg_item_action_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-item-action.py"
                "${CMAKE_BINARY_DIR}/rpg-item-action-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-item-action-frames.bin"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-item-action-rgb-reference.json"
    )
    set_tests_properties(rpg_item_action_validation PROPERTIES
        DEPENDS rpg_item_action_replay)
    add_test(
        NAME rpg_party_target_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-party-target-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-party-target-saves"
    )
    add_test(
        NAME rpg_party_target_replay
        COMMAND swd2_rewrite
                --game "${PROJECT_SOURCE_DIR}/game"
                --save-dir "${CMAKE_BINARY_DIR}/rpg-party-target-saves"
                --slot 1
                --no-save
                --run-replay
                    "${PROJECT_SOURCE_DIR}/scripts/replay-rpg-party-target.txt"
                --trace-output
                    "${CMAKE_BINARY_DIR}/rpg-party-target-trace.json"
                --frame-output
                    "${CMAKE_BINARY_DIR}/rpg-party-target-frames.bin"
    )
    set_tests_properties(rpg_party_target_replay PROPERTIES
        DEPENDS rpg_party_target_prepare)
    add_test(
        NAME rpg_party_target_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-party-target.py"
                "${CMAKE_BINARY_DIR}/rpg-party-target-trace.json"
                "${CMAKE_BINARY_DIR}/rpg-party-target-frames.bin"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-party-target-rgb-reference.json"
    )
    set_tests_properties(rpg_party_target_validation PROPERTIES
        DEPENDS rpg_party_target_replay)
    add_test(
        NAME rpg_equipment_actors_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-party-target-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-equipment-actors-saves"
    )
    add_test(
        NAME rpg_equipment_actors_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-equipment-actors.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-equipment-actors-saves"
                "${CMAKE_BINARY_DIR}/rpg-equipment-actors"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-equipment-actors-rgb-reference.json"
    )
    set_tests_properties(rpg_equipment_actors_validation PROPERTIES
        DEPENDS rpg_equipment_actors_prepare)
    add_test(
        NAME rpg_equipment_errors_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-equipment-errors-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-equipment-errors-saves"
    )
    add_test(
        NAME rpg_equipment_errors_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-equipment-errors.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-equipment-errors-saves"
                "${CMAKE_BINARY_DIR}/rpg-equipment-errors"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-equipment-errors-rgb-reference.json"
    )
    set_tests_properties(rpg_equipment_errors_validation PROPERTIES
        DEPENDS rpg_equipment_errors_prepare)
    add_test(
        NAME rpg_magic_actors_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-party-target-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-magic-actors-saves"
    )
    add_test(
        NAME rpg_magic_actors_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-magic-actors.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-magic-actors-saves"
                "${CMAKE_BINARY_DIR}/rpg-magic-actors"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-magic-actors-rgb-reference.json"
    )
    set_tests_properties(rpg_magic_actors_validation PROPERTIES
        DEPENDS rpg_magic_actors_prepare)
    add_test(
        NAME rpg_status_actors_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-rpg-party-target-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-status-actors-saves"
    )
    add_test(
        NAME rpg_status_actors_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-rpg-status-actors.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/rpg-status-actors-saves"
                "${CMAKE_BINARY_DIR}/rpg-status-actors"
                "${PROJECT_SOURCE_DIR}/scripts/rpg-status-actors-rgb-reference.json"
    )
    set_tests_properties(rpg_status_actors_validation PROPERTIES
        DEPENDS rpg_status_actors_prepare)
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
    add_test(
        NAME fig_ability_pages_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ability-pages-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ability-pages-saves"
    )
    add_test(
        NAME fig_ability_pages_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ability-pages.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ability-pages-saves"
                "${CMAKE_BINARY_DIR}/fig-ability-pages"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ability-pages-rgb-reference.json"
    )
    set_tests_properties(fig_ability_pages_validation PROPERTIES
        DEPENDS fig_ability_pages_prepare)
    add_test(
        NAME fig_item_page_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-page-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-page-save"
    )
    add_test(
        NAME fig_item_page_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-page.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-page-save"
                "${CMAKE_BINARY_DIR}/fig-item-page"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-page-rgb-reference.json"
    )
    set_tests_properties(fig_item_page_validation PROPERTIES
        DEPENDS fig_item_page_prepare)
    add_test(
        NAME fig_monster_target_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-target-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-target-save"
    )
    add_test(
        NAME fig_monster_target_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-target.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-target-save"
                "${CMAKE_BINARY_DIR}/fig-monster-target"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-target-rgb-reference.json"
    )
    set_tests_properties(fig_monster_target_validation PROPERTIES
        DEPENDS fig_monster_target_prepare)
    add_test(
        NAME fig_player_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-status-save"
    )
    add_test(
        NAME fig_player_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-status-save"
                "${CMAKE_BINARY_DIR}/fig-player-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-status-rgb-reference.json"
    )
    set_tests_properties(fig_player_status_validation PROPERTIES
        DEPENDS fig_player_status_prepare)
    add_test(
        NAME fig_player_support_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-save"
    )
    add_test(
        NAME fig_player_support_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-save"
                "${CMAKE_BINARY_DIR}/fig-player-support"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_validation PROPERTIES
        DEPENDS fig_player_support_prepare)
    add_test(
        NAME fig_player_support_all_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-all-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-all-save"
    )
    add_test(
        NAME fig_player_support_all_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-all-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-all"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-all-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_all_validation PROPERTIES
        DEPENDS fig_player_support_all_prepare)
    add_test(
        NAME fig_player_support_effect2_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect2-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect2-save"
    )
    add_test(
        NAME fig_player_support_effect2_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect2-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect2"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect2-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect2_validation PROPERTIES
        DEPENDS fig_player_support_effect2_prepare)
    add_test(
        NAME fig_player_support_effect3_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect3-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect3-save"
    )
    add_test(
        NAME fig_player_support_effect3_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect3-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect3"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect3-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect3_validation PROPERTIES
        DEPENDS fig_player_support_effect3_prepare)
    add_test(
        NAME fig_player_support_effect4_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect4-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect4-save"
    )
    add_test(
        NAME fig_player_support_effect4_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect4-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect4"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect4-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect4_validation PROPERTIES
        DEPENDS fig_player_support_effect4_prepare)
    add_test(
        NAME fig_player_support_effect9_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect9-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect9-save"
    )
    add_test(
        NAME fig_player_support_effect9_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect9-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect9"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect9-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect9_validation PROPERTIES
        DEPENDS fig_player_support_effect9_prepare)
    add_test(
        NAME fig_player_support_effect0a_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect0a-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0a-save"
    )
    add_test(
        NAME fig_player_support_effect0a_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0a-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0a"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect0a-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect0a_validation PROPERTIES
        DEPENDS fig_player_support_effect0a_prepare)
    add_test(
        NAME fig_player_support_effect0b_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect0b-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0b-save"
    )
    add_test(
        NAME fig_player_support_effect0b_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0b-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0b"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect0b-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect0b_validation PROPERTIES
        DEPENDS fig_player_support_effect0b_prepare)
    add_test(
        NAME fig_player_support_effect0d_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect0d-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0d-save"
    )
    add_test(
        NAME fig_player_support_effect0d_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0d-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0d"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect0d-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect0d_validation PROPERTIES
        DEPENDS fig_player_support_effect0d_prepare)
    add_test(
        NAME fig_player_support_effect0e_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect0e-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0e-save"
    )
    add_test(
        NAME fig_player_support_effect0e_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0e-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0e"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect0e-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect0e_validation PROPERTIES
        DEPENDS fig_player_support_effect0e_prepare)
    add_test(
        NAME fig_player_support_effect0f_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect0f-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0f-save"
    )
    add_test(
        NAME fig_player_support_effect0f_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0f-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect0f"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect0f-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect0f_validation PROPERTIES
        DEPENDS fig_player_support_effect0f_prepare)
    add_test(
        NAME fig_player_support_effect19_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect19-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect19-save"
    )
    add_test(
        NAME fig_player_support_effect19_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect19-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect19"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect19-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect19_validation PROPERTIES
        DEPENDS fig_player_support_effect19_prepare)
    add_test(
        NAME fig_player_support_effect1a_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect1a-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect1a-save"
    )
    add_test(
        NAME fig_player_support_effect1a_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect1a-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect1a"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect1a-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect1a_validation PROPERTIES
        DEPENDS fig_player_support_effect1a_prepare)
    add_test(
        NAME fig_player_support_effect1e_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect1e-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect1e-save"
    )
    add_test(
        NAME fig_player_support_effect1e_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect1e-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect1e"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect1e-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect1e_validation PROPERTIES
        DEPENDS fig_player_support_effect1e_prepare)
    add_test(
        NAME fig_player_support_effect01_class5_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect01-class5-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect01-class5-save"
    )
    add_test(
        NAME fig_player_support_effect01_class5_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect01-class5-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect01-class5"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect01-class5-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect01_class5_validation PROPERTIES
        DEPENDS fig_player_support_effect01_class5_prepare)
    add_test(
        NAME fig_player_support_effect03_reachable_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect03-reachable-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect03-reachable-save"
    )
    add_test(
        NAME fig_player_support_effect03_reachable_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect03-reachable-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect03-reachable"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect03-reachable-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect03_reachable_validation PROPERTIES
        DEPENDS fig_player_support_effect03_reachable_prepare)
    add_test(
        NAME fig_player_support_effect02_class1_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect02-class1-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect02-class1-save"
    )
    add_test(
        NAME fig_player_support_effect02_class1_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect02-class1-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect02-class1"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect02-class1-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect02_class1_validation PROPERTIES
        DEPENDS fig_player_support_effect02_class1_prepare)
    add_test(
        NAME fig_player_support_effect19_class2_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect19-class2-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect19-class2-save"
    )
    add_test(
        NAME fig_player_support_effect19_class2_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect19-class2-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect19-class2"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect19-class2-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect19_class2_validation PROPERTIES
        DEPENDS fig_player_support_effect19_class2_prepare)
    add_test(
        NAME fig_reachable_support_coverage
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-reachable-support-coverage.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${PROJECT_SOURCE_DIR}/scripts"
    )
    add_test(
        NAME fig_player_effect32_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect32-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect32-save"
    )
    add_test(
        NAME fig_player_effect32_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect32.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect32-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect32"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect32-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect32_validation PROPERTIES
        DEPENDS fig_player_effect32_prepare)
    add_test(
        NAME fig_player_effect34_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect34-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect34-save"
    )
    add_test(
        NAME fig_player_effect34_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect34.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect34-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect34"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect34-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect34_validation PROPERTIES
        DEPENDS fig_player_effect34_prepare)
    add_test(
        NAME fig_player_effect35_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect35-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect35-save"
    )
    add_test(
        NAME fig_player_effect35_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect35.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect35-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect35"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect35-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect35_validation PROPERTIES
        DEPENDS fig_player_effect35_prepare)
    add_test(
        NAME fig_player_effect36_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect36-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect36-save"
    )
    add_test(
        NAME fig_player_effect36_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect36.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect36-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect36"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect36-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect36_validation PROPERTIES
        DEPENDS fig_player_effect36_prepare)
    add_test(
        NAME fig_player_effect37_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect37-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect37-save"
    )
    add_test(
        NAME fig_player_effect37_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect37.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect37-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect37"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect37-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect37_validation PROPERTIES
        DEPENDS fig_player_effect37_prepare)
    add_test(
        NAME fig_player_effect38_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect38-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect38-save"
    )
    add_test(
        NAME fig_player_effect38_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect38.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect38-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect38"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect38-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect38_validation PROPERTIES
        DEPENDS fig_player_effect38_prepare)
    add_test(
        NAME fig_player_effect39_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect39-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect39-save"
    )
    add_test(
        NAME fig_player_effect39_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect39.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect39-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect39"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect39-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect39_validation PROPERTIES
        DEPENDS fig_player_effect39_prepare)
    add_test(
        NAME fig_player_effect3a_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect3a-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect3a-save"
    )
    add_test(
        NAME fig_player_effect3a_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect3a.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect3a-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect3a"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect3a-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect3a_validation PROPERTIES
        DEPENDS fig_player_effect3a_prepare)
    add_test(
        NAME fig_player_effect40_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect40-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect40-save"
    )
    add_test(
        NAME fig_player_effect40_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect40.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect40-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect40"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect40-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect40_validation PROPERTIES
        DEPENDS fig_player_effect40_prepare)
    add_test(
        NAME fig_player_effect41_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect41-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect41-save"
    )
    add_test(
        NAME fig_player_effect41_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect41.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect41-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect41"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect41-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect41_validation PROPERTIES
        DEPENDS fig_player_effect41_prepare)
    add_test(
        NAME fig_player_effect42_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect42-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect42-save"
    )
    add_test(
        NAME fig_player_effect42_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect42.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect42-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect42"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect42-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect42_validation PROPERTIES
        DEPENDS fig_player_effect42_prepare)
    add_test(
        NAME fig_player_effect43_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect43-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect43-save"
    )
    add_test(
        NAME fig_player_effect43_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect43.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect43-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect43"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect43-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect43_validation PROPERTIES
        DEPENDS fig_player_effect43_prepare)
    add_test(
        NAME fig_player_effect44_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect44-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect44-save"
    )
    add_test(
        NAME fig_player_effect44_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect44.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect44-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect44"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect44-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect44_validation PROPERTIES
        DEPENDS fig_player_effect44_prepare)
    add_test(
        NAME fig_player_effect45_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect45-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect45-save"
    )
    add_test(
        NAME fig_player_effect45_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect45.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect45-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect45"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect45-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect45_validation PROPERTIES
        DEPENDS fig_player_effect45_prepare)
    add_test(
        NAME fig_player_effect46_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect46-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect46-save"
    )
    add_test(
        NAME fig_player_effect46_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect46.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect46-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect46"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect46-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect46_validation PROPERTIES
        DEPENDS fig_player_effect46_prepare)
    add_test(
        NAME fig_player_effect47_escape_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect47-escape-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect47-escape-game"
    )
    add_test(
        NAME fig_player_effect47_escape_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect47-escape.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-effect47-escape-game"
                "${CMAKE_BINARY_DIR}/fig-player-effect47-escape"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect47-escape-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect47_escape_validation PROPERTIES
        DEPENDS fig_player_effect47_escape_prepare)
    add_test(
        NAME fig_player_effect47_fixed_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect47-escape-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect47-fixed-game"
                --fixed
    )
    add_test(
        NAME fig_player_effect47_fixed_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect47-fixed.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-effect47-fixed-game"
                "${CMAKE_BINARY_DIR}/fig-player-effect47-fixed"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect47-fixed-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect47_fixed_validation PROPERTIES
        DEPENDS fig_player_effect47_fixed_prepare)
    add_test(
        NAME fig_item_effect47_escape_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect47-escape-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-effect47-escape-game"
                --item
    )
    add_test(
        NAME fig_item_effect47_escape_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-effect47-escape.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-item-effect47-escape-game"
                "${CMAKE_BINARY_DIR}/fig-item-effect47-escape"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-effect47-escape-rgb-reference.json"
    )
    set_tests_properties(fig_item_effect47_escape_validation PROPERTIES
        DEPENDS fig_item_effect47_escape_prepare)
    add_test(
        NAME fig_item_effect47_fixed_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect47-escape-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-effect47-fixed-game"
                --fixed-item
    )
    add_test(
        NAME fig_item_effect47_fixed_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-effect47-fixed.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-item-effect47-fixed-game"
                "${CMAKE_BINARY_DIR}/fig-item-effect47-fixed"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-effect47-fixed-rgb-reference.json"
    )
    set_tests_properties(fig_item_effect47_fixed_validation PROPERTIES
        DEPENDS fig_item_effect47_fixed_prepare)
    add_test(
        NAME fig_player_effect48_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect48-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect48-save"
    )
    add_test(
        NAME fig_player_effect48_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect48.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect48-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect48"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect48-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect48_validation PROPERTIES
        DEPENDS fig_player_effect48_prepare)
    add_test(
        NAME fig_player_effect49_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect49-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect49-save"
    )
    add_test(
        NAME fig_player_effect49_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect49.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect49-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect49"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect49-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect49_validation PROPERTIES
        DEPENDS fig_player_effect49_prepare)
    add_test(
        NAME fig_player_effect4a_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect4a-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4a-save"
    )
    add_test(
        NAME fig_player_effect4a_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect4a.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4a-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect4a"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect4a-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect4a_validation PROPERTIES
        DEPENDS fig_player_effect4a_prepare)
    add_test(
        NAME fig_player_effect4c_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect4c-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4c-save"
    )
    add_test(
        NAME fig_player_effect4c_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect4c.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4c-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect4c"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect4c-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect4c_validation PROPERTIES
        DEPENDS fig_player_effect4c_prepare)
    add_test(
        NAME fig_player_effect4d_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect4d-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4d-save"
    )
    add_test(
        NAME fig_player_effect4d_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect4d.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4d-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect4d"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect4d-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect4d_validation PROPERTIES
        DEPENDS fig_player_effect4d_prepare)
    add_test(
        NAME fig_player_effect4e_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect4e-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4e-save"
    )
    add_test(
        NAME fig_player_effect4e_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect4e.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4e-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect4e"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect4e-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect4e_validation PROPERTIES
        DEPENDS fig_player_effect4e_prepare)
    add_test(
        NAME fig_player_effect4f_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect4f-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4f-save"
    )
    add_test(
        NAME fig_player_effect4f_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect4f.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4f-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect4f"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect4f-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect4f_validation PROPERTIES
        DEPENDS fig_player_effect4f_prepare)
    add_test(
        NAME fig_player_effect50_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect50-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect50-save"
    )
    add_test(
        NAME fig_player_effect50_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect50.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect50-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect50"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect50-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect50_validation PROPERTIES
        DEPENDS fig_player_effect50_prepare)
    add_test(
        NAME fig_player_effect5a_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect5a-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5a-save"
    )
    add_test(
        NAME fig_player_effect5a_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect5a.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5a-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect5a"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect5a-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect5a_validation PROPERTIES
        DEPENDS fig_player_effect5a_prepare)
    add_test(
        NAME fig_player_effect51_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect51-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect51-save"
    )
    add_test(
        NAME fig_player_effect51_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect51.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect51-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect51"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect51-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect51_validation PROPERTIES
        DEPENDS fig_player_effect51_prepare)
    add_test(
        NAME fig_player_effect52_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect52-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect52-kill-save"
    )
    add_test(
        NAME fig_player_effect52_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect52-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect52-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect52-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect52-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect52_kill_validation PROPERTIES
        DEPENDS fig_player_effect52_kill_prepare)
    add_test(
        NAME fig_player_effect53_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect53-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect53-save"
    )
    add_test(
        NAME fig_player_effect53_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect53.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect53-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect53"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect53-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect53_validation PROPERTIES
        DEPENDS fig_player_effect53_prepare)
    add_test(
        NAME fig_player_effect4b_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect4b-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4b-kill-save"
    )
    add_test(
        NAME fig_player_effect4b_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect4b-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect4b-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect4b-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect4b-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect4b_kill_validation PROPERTIES
        DEPENDS fig_player_effect4b_kill_prepare)
    add_test(
        NAME fig_player_effect54_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect54-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect54-kill-save"
    )
    add_test(
        NAME fig_player_effect54_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect54-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect54-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect54-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect54-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect54_kill_validation PROPERTIES
        DEPENDS fig_player_effect54_kill_prepare)
    add_test(
        NAME fig_player_effect55_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect55-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect55-save"
    )
    add_test(
        NAME fig_player_effect55_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect55.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect55-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect55"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect55-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect55_validation PROPERTIES
        DEPENDS fig_player_effect55_prepare)
    add_test(
        NAME fig_player_effect5b_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect5b-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5b-kill-save"
    )
    add_test(
        NAME fig_player_effect5b_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect5b-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5b-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect5b-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect5b-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect5b_kill_validation PROPERTIES
        DEPENDS fig_player_effect5b_kill_prepare)
    add_test(
        NAME fig_player_effect5c_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect5c-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5c-kill-save"
    )
    add_test(
        NAME fig_player_effect5c_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect5c-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5c-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect5c-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect5c-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect5c_kill_validation PROPERTIES
        DEPENDS fig_player_effect5c_kill_prepare)
    add_test(
        NAME fig_player_effect56_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect56-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect56-kill-save"
    )
    add_test(
        NAME fig_player_effect56_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect56-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect56-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect56-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect56-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect56_kill_validation PROPERTIES
        DEPENDS fig_player_effect56_kill_prepare)
    add_test(
        NAME fig_player_effect57_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect57-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect57-kill-save"
    )
    add_test(
        NAME fig_player_effect57_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect57-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect57-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect57-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect57-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect57_kill_validation PROPERTIES
        DEPENDS fig_player_effect57_kill_prepare)
    add_test(
        NAME fig_player_effect58_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect58-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect58-kill-save"
    )
    add_test(
        NAME fig_player_effect58_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect58-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect58-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect58-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect58-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect58_kill_validation PROPERTIES
        DEPENDS fig_player_effect58_kill_prepare)
    add_test(
        NAME fig_player_effect59_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect59-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect59-kill-save"
    )
    add_test(
        NAME fig_player_effect59_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect59-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect59-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect59-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect59-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect59_kill_validation PROPERTIES
        DEPENDS fig_player_effect59_kill_prepare)
    add_test(
        NAME fig_player_effect5d_kill_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect5d-kill-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5d-kill-save"
    )
    add_test(
        NAME fig_player_effect5d_kill_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect5d-kill.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5d-kill-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect5d-kill"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect5d-kill-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect5d_kill_validation PROPERTIES
        DEPENDS fig_player_effect5d_kill_prepare)
    add_test(
        NAME fig_player_effect5e_resistance_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect5e-resistance-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5e-resistance-save"
    )
    add_test(
        NAME fig_player_effect5e_resistance_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect5e-resistance.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5e-resistance-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect5e-resistance"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect5e-resistance-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect5e_resistance_validation PROPERTIES
        DEPENDS fig_player_effect5e_resistance_prepare)
    add_test(
        NAME fig_player_effect5f_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect5f-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5f-status-save"
    )
    add_test(
        NAME fig_player_effect5f_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect5f-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect5f-status-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect5f-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect5f-status-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect5f_status_validation PROPERTIES
        DEPENDS fig_player_effect5f_status_prepare)
    add_test(
        NAME fig_player_effect60_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect60-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect60-status-save"
    )
    add_test(
        NAME fig_player_effect60_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect60-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect60-status-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect60-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect60-status-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect60_status_validation PROPERTIES
        DEPENDS fig_player_effect60_status_prepare)
    add_test(
        NAME fig_player_effect64_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect64-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect64-status-save"
    )
    add_test(
        NAME fig_player_effect64_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect64-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect64-status-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect64-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect64-status-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect64_status_validation PROPERTIES
        DEPENDS fig_player_effect64_status_prepare)
    add_test(
        NAME fig_player_effect65_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-effect65-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect65-status-save"
    )
    add_test(
        NAME fig_player_effect65_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect65-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-effect65-status-save"
                "${CMAKE_BINARY_DIR}/fig-player-effect65-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-effect65-status-rgb-reference.json"
    )
    set_tests_properties(fig_player_effect65_status_validation PROPERTIES
        DEPENDS fig_player_effect65_status_prepare)
    add_test(
        NAME fig_player_effect_coverage
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-effect-coverage.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_CURRENT_SOURCE_DIR}/battle_presentation.cpp"
                "${PROJECT_SOURCE_DIR}/scripts"
    )
    add_test(
        NAME fig_player_support_effect07_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-support-effect07-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect07-save"
    )
    add_test(
        NAME fig_player_support_effect07_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect07-save"
                "${CMAKE_BINARY_DIR}/fig-player-support-effect07"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-support-effect07-rgb-reference.json"
    )
    set_tests_properties(fig_player_support_effect07_validation PROPERTIES
        DEPENDS fig_player_support_effect07_prepare)
    add_test(
        NAME fig_player_barrier_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-barrier-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-barrier-save"
    )
    add_test(
        NAME fig_player_barrier_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-barrier.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-barrier-save"
                "${CMAKE_BINARY_DIR}/fig-player-barrier"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-barrier-rgb-reference.json"
    )
    set_tests_properties(fig_player_barrier_validation PROPERTIES
        DEPENDS fig_player_barrier_prepare)
    add_test(
        NAME fig_player_dispel_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-dispel-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-dispel-save"
    )
    add_test(
        NAME fig_player_dispel_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-dispel.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-dispel-save"
                "${CMAKE_BINARY_DIR}/fig-player-dispel"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-dispel-rgb-reference.json"
    )
    set_tests_properties(fig_player_dispel_validation PROPERTIES
        DEPENDS fig_player_dispel_prepare)
    add_test(
        NAME fig_item_dispel_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-dispel-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-dispel-save"
                --item
    )
    add_test(
        NAME fig_item_dispel_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-dispel.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-dispel-save"
                "${CMAKE_BINARY_DIR}/fig-item-dispel"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-dispel-rgb-reference.json"
    )
    set_tests_properties(fig_item_dispel_validation PROPERTIES
        DEPENDS fig_item_dispel_prepare)
    add_test(
        NAME fig_item_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-status-save"
    )
    add_test(
        NAME fig_item_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-status-save"
                "${CMAKE_BINARY_DIR}/fig-item-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-status-rgb-reference.json"
    )
    set_tests_properties(fig_item_status_validation PROPERTIES
        DEPENDS fig_item_status_prepare)
    add_test(
        NAME fig_item_monster_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-monster-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-monster-status-save"
    )
    add_test(
        NAME fig_item_monster_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-monster-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-monster-status-save"
                "${CMAKE_BINARY_DIR}/fig-item-monster-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-monster-status-rgb-reference.json"
    )
    set_tests_properties(fig_item_monster_status_validation PROPERTIES
        DEPENDS fig_item_monster_status_prepare)
    add_test(
        NAME fig_item_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-damage-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-damage-save"
    )
    add_test(
        NAME fig_item_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-damage-save"
                "${CMAKE_BINARY_DIR}/fig-item-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-damage-rgb-reference.json"
    )
    set_tests_properties(fig_item_damage_validation PROPERTIES
        DEPENDS fig_item_damage_prepare)
    add_test(
        NAME fig_item_composite_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-composite-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-composite-save"
    )
    add_test(
        NAME fig_item_composite_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-composite.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-composite-save"
                "${CMAKE_BINARY_DIR}/fig-item-composite"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-composite-rgb-reference.json"
    )
    set_tests_properties(fig_item_composite_validation PROPERTIES
        DEPENDS fig_item_composite_prepare)
    add_test(
        NAME fig_item_composite_media_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-composite-media-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-composite-media-save"
    )
    add_test(
        NAME fig_item_composite_media_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-composite-media.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-composite-media-save"
                "${CMAKE_BINARY_DIR}/fig-item-composite-media"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-composite-media-rgb-reference.json"
    )
    set_tests_properties(fig_item_composite_media_validation PROPERTIES
        DEPENDS fig_item_composite_media_prepare)
    add_test(
        NAME fig_player_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-medium-save"
    )
    add_test(
        NAME fig_player_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-medium-save"
                "${CMAKE_BINARY_DIR}/fig-player-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-medium-rgb-reference.json"
    )
    set_tests_properties(fig_player_medium_validation PROPERTIES
        DEPENDS fig_player_medium_prepare)
    add_test(
        NAME fig_learned_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-learned-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-learned-medium-save"
    )
    add_test(
        NAME fig_learned_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-learned-medium-save"
                "${CMAKE_BINARY_DIR}/fig-learned-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-learned-medium-rgb-reference.json"
    )
    set_tests_properties(fig_learned_medium_validation PROPERTIES
        DEPENDS fig_learned_medium_prepare)
    add_test(
        NAME fig_player_empty_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-empty-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-empty-medium-save"
    )
    add_test(
        NAME fig_player_empty_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-empty-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-empty-medium-save"
                "${CMAKE_BINARY_DIR}/fig-player-empty-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-empty-medium-rgb-reference.json"
    )
    set_tests_properties(fig_player_empty_medium_validation PROPERTIES
        DEPENDS fig_player_empty_medium_prepare)
    add_test(
        NAME fig_learned_empty_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-learned-empty-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-learned-empty-medium-save"
    )
    add_test(
        NAME fig_learned_empty_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-empty-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-learned-empty-medium-save"
                "${CMAKE_BINARY_DIR}/fig-learned-empty-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-learned-empty-medium-rgb-reference.json"
    )
    set_tests_properties(fig_learned_empty_medium_validation PROPERTIES
        DEPENDS fig_learned_empty_medium_prepare)
    add_test(
        NAME fig_item_composite_support_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-composite-support-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-composite-support-save"
    )
    add_test(
        NAME fig_item_composite_support_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-composite-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-composite-support-save"
                "${CMAKE_BINARY_DIR}/fig-item-composite-support"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-composite-support-rgb-reference.json"
    )
    set_tests_properties(fig_item_composite_support_validation PROPERTIES
        DEPENDS fig_item_composite_support_prepare)
    add_test(
        NAME fig_item_support_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-support-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-save"
    )
    add_test(
        NAME fig_item_support_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-save"
                "${CMAKE_BINARY_DIR}/fig-item-support"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-support-rgb-reference.json"
    )
    set_tests_properties(fig_item_support_validation PROPERTIES
        DEPENDS fig_item_support_prepare)
    add_test(
        NAME fig_item_support_class5_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-support-class5-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-class5-save"
    )
    add_test(
        NAME fig_item_support_class5_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-class5-save"
                "${CMAKE_BINARY_DIR}/fig-item-support-class5"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-support-class5-rgb-reference.json"
    )
    set_tests_properties(fig_item_support_class5_validation PROPERTIES
        DEPENDS fig_item_support_class5_prepare)
    add_test(
        NAME fig_item_support_dispatch_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-support-dispatch-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-dispatch-save"
    )
    add_test(
        NAME fig_item_support_dispatch_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-dispatch-save"
                "${CMAKE_BINARY_DIR}/fig-item-support-dispatch"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-support-dispatch-rgb-reference.json"
    )
    set_tests_properties(fig_item_support_dispatch_validation PROPERTIES
        DEPENDS fig_item_support_dispatch_prepare)
    add_test(
        NAME fig_item_support_class5_selector_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-support-class5-selector-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-class5-selector-save"
    )
    add_test(
        NAME fig_item_support_class5_selector_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-support-class5-selector-save"
                "${CMAKE_BINARY_DIR}/fig-item-support-class5-selector"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-support-class5-selector-rgb-reference.json"
    )
    set_tests_properties(fig_item_support_class5_selector_validation PROPERTIES
        DEPENDS fig_item_support_class5_selector_prepare)
    add_test(
        NAME fig_item_barrier_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-item-barrier-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-barrier-save"
    )
    add_test(
        NAME fig_item_barrier_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-item-barrier.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-item-barrier-save"
                "${CMAKE_BINARY_DIR}/fig-item-barrier"
                "${PROJECT_SOURCE_DIR}/scripts/fig-item-barrier-rgb-reference.json"
    )
    set_tests_properties(fig_item_barrier_validation PROPERTIES
        DEPENDS fig_item_barrier_prepare)
    add_test(
        NAME fig_flagged_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-flagged-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-flagged-medium-save"
    )
    add_test(
        NAME fig_flagged_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-flagged-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-flagged-medium-save"
                "${CMAKE_BINARY_DIR}/fig-flagged-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-flagged-medium-rgb-reference.json"
    )
    set_tests_properties(fig_flagged_medium_validation PROPERTIES
        DEPENDS fig_flagged_medium_prepare)
    add_test(
        NAME fig_victory_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-victory-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-victory-save"
    )
    add_test(
        NAME fig_victory_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-victory.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-victory-save"
                "${CMAKE_BINARY_DIR}/fig-victory"
                "${PROJECT_SOURCE_DIR}/scripts/fig-victory-rgb-reference.json"
    )
    set_tests_properties(fig_victory_validation PROPERTIES
        DEPENDS fig_victory_prepare)
    add_test(
        NAME fig_player_critical_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-critical-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-critical-save"
    )
    add_test(
        NAME fig_player_critical_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-critical.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-critical-save"
                "${CMAKE_BINARY_DIR}/fig-player-critical"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-critical-rgb-reference.json"
    )
    set_tests_properties(fig_player_critical_validation PROPERTIES
        DEPENDS fig_player_critical_prepare)
    add_test(
        NAME fig_player_attack_evaded_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-evaded-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-evaded-save"
    )
    add_test(
        NAME fig_player_attack_evaded_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-evaded.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-evaded-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-evaded"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-evaded-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_evaded_validation PROPERTIES
        DEPENDS fig_player_attack_evaded_prepare)
    add_test(
        NAME fig_player_attack_zero_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-zero-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-zero-save"
    )
    add_test(
        NAME fig_player_attack_zero_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-zero.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-zero-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-zero"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-zero-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_zero_validation PROPERTIES
        DEPENDS fig_player_attack_zero_prepare)
    add_test(
        NAME fig_player_attack_hit_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-hit-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-hit-save"
    )
    add_test(
        NAME fig_player_attack_hit_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-hit.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-hit-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-hit"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-hit-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_hit_validation PROPERTIES
        DEPENDS fig_player_attack_hit_prepare)
    add_test(
        NAME fig_player_critical_hit_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-critical-hit-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-critical-hit-save"
    )
    add_test(
        NAME fig_player_critical_hit_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-critical-hit.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-critical-hit-save"
                "${CMAKE_BINARY_DIR}/fig-player-critical-hit"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-critical-hit-rgb-reference.json"
    )
    set_tests_properties(fig_player_critical_hit_validation PROPERTIES
        DEPENDS fig_player_critical_hit_prepare)
    add_test(
        NAME fig_player_attack_basic_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-basic-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-basic-save"
    )
    add_test(
        NAME fig_player_attack_basic_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-basic.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-basic-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-basic"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-basic-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_basic_validation PROPERTIES
        DEPENDS fig_player_attack_basic_prepare)
    add_test(
        NAME fig_player_attack_flee_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-flee-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-flee-save"
    )
    add_test(
        NAME fig_player_attack_flee_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-flee.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-flee-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-flee"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-flee-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_flee_validation PROPERTIES
        DEPENDS fig_player_attack_flee_prepare)
    add_test(
        NAME fig_player_attack_flee_failed_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-flee-failed-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-flee-failed-save"
    )
    add_test(
        NAME fig_player_attack_flee_failed_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-flee-failed.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-flee-failed-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-flee-failed"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-flee-failed-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_flee_failed_validation PROPERTIES
        DEPENDS fig_player_attack_flee_failed_prepare)
    add_test(
        NAME fig_player_attack_special_silent_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-special-silent-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-silent-save"
    )
    add_test(
        NAME fig_player_attack_special_silent_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-special-silent.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-silent-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-silent"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-special-silent-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_special_silent_validation PROPERTIES
        DEPENDS fig_player_attack_special_silent_prepare)
    add_test(
        NAME fig_player_attack_special_resisted_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-special-resisted-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-resisted-save"
    )
    add_test(
        NAME fig_player_attack_special_resisted_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-special-resisted.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-resisted-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-resisted"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-special-resisted-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_special_resisted_validation PROPERTIES
        DEPENDS fig_player_attack_special_resisted_prepare)
    add_test(
        NAME fig_player_attack_special_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-special-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-status-save"
    )
    add_test(
        NAME fig_player_attack_special_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-special-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-status-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-special-status-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_special_status_validation PROPERTIES
        DEPENDS fig_player_attack_special_status_prepare)
    add_test(
        NAME fig_player_attack_special_buff_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-special-buff-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-buff-save"
    )
    add_test(
        NAME fig_player_attack_special_buff_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-special-buff.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-buff-save"
                "${CMAKE_BINARY_DIR}/fig-player-attack-special-buff"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-special-buff-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_special_buff_validation PROPERTIES
        DEPENDS fig_player_attack_special_buff_prepare)
    add_test(
        NAME fig_player_attack_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-medium-game"
    )
    add_test(
        NAME fig_player_attack_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-medium-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_medium_validation PROPERTIES
        DEPENDS fig_player_attack_medium_prepare)
    add_test(
        NAME fig_player_attack_b0_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-b0-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-b0-medium-game"
    )
    add_test(
        NAME fig_player_attack_b0_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-b0-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-b0-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-b0-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-b0-medium-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_b0_medium_validation PROPERTIES
        DEPENDS fig_player_attack_b0_medium_prepare)
    add_test(
        NAME fig_player_attack_af_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-attack-af-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-af-medium-game"
    )
    add_test(
        NAME fig_player_attack_af_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-attack-af-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-attack-af-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-attack-af-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-attack-af-medium-rgb-reference.json"
    )
    set_tests_properties(fig_player_attack_af_medium_validation PROPERTIES
        DEPENDS fig_player_attack_af_medium_prepare)
    add_test(
        NAME fig_monster_slot_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-slot-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-slot-medium-game"
    )
    add_test(
        NAME fig_monster_slot_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-slot-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-monster-slot-medium-game"
                "${CMAKE_BINARY_DIR}/fig-monster-slot-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-slot-medium-rgb-reference.json"
    )
    set_tests_properties(fig_monster_slot_medium_validation PROPERTIES
        DEPENDS fig_monster_slot_medium_prepare)
    add_test(
        NAME fig_monster_third_slot_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-third-slot-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-third-slot-medium-game"
    )
    add_test(
        NAME fig_monster_third_slot_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-third-slot-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-monster-third-slot-medium-game"
                "${CMAKE_BINARY_DIR}/fig-monster-third-slot-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-third-slot-medium-rgb-reference.json"
    )
    set_tests_properties(fig_monster_third_slot_medium_validation PROPERTIES
        DEPENDS fig_monster_third_slot_medium_prepare)
    add_test(
        NAME fig_monster_first_slot_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-first-slot-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-first-slot-medium-game"
    )
    add_test(
        NAME fig_monster_first_slot_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-first-slot-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-monster-first-slot-medium-game"
                "${CMAKE_BINARY_DIR}/fig-monster-first-slot-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-first-slot-medium-rgb-reference.json"
    )
    set_tests_properties(fig_monster_first_slot_medium_validation PROPERTIES
        DEPENDS fig_monster_first_slot_medium_prepare)
    add_test(
        NAME fig_defeat_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-defeat-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-defeat-save"
    )
    add_test(
        NAME fig_defeat_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-defeat.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-defeat-save"
                "${CMAKE_BINARY_DIR}/fig-defeat"
                "${PROJECT_SOURCE_DIR}/scripts/fig-defeat-rgb-reference.json"
    )
    set_tests_properties(fig_defeat_validation PROPERTIES
        DEPENDS fig_defeat_prepare)
    add_test(
        NAME fig_level_up_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-level-up-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-level-up-save"
    )
    add_test(
        NAME fig_level_up_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-level-up.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-level-up-save"
                "${CMAKE_BINARY_DIR}/fig-level-up"
                "${PROJECT_SOURCE_DIR}/scripts/fig-level-up-rgb-reference.json"
    )
    set_tests_properties(fig_level_up_validation PROPERTIES
        DEPENDS fig_level_up_prepare)
    add_test(
        NAME fig_story_30_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-story-30-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-story-30-save"
    )
    add_test(
        NAME fig_story_30_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-story-30.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-story-30-save"
                "${CMAKE_BINARY_DIR}/fig-story-30"
                "${PROJECT_SOURCE_DIR}/scripts/fig-story-30-rgb-reference.json"
    )
    set_tests_properties(fig_story_30_validation PROPERTIES
        DEPENDS fig_story_30_prepare)
    add_test(
        NAME fig_story_42_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-story-42-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-story-42-save"
    )
    add_test(
        NAME fig_story_42_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-story-42.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-story-42-save"
                "${CMAKE_BINARY_DIR}/fig-story-42"
                "${PROJECT_SOURCE_DIR}/scripts/fig-story-42-rgb-reference.json"
    )
    set_tests_properties(fig_story_42_validation PROPERTIES
        DEPENDS fig_story_42_prepare)
    add_test(
        NAME fig_introduction_ny_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-introduction-ny-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-introduction-ny-save"
    )
    add_test(
        NAME fig_introduction_ny_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-introduction-ny.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-introduction-ny-save"
                "${CMAKE_BINARY_DIR}/fig-introduction-ny"
                "${PROJECT_SOURCE_DIR}/scripts/fig-introduction-ny-rgb-reference.json"
    )
    set_tests_properties(fig_introduction_ny_validation PROPERTIES
        DEPENDS fig_introduction_ny_prepare)
    add_test(
        NAME fig_introduction_yn_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-introduction-yn-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-introduction-yn-save"
    )
    add_test(
        NAME fig_introduction_yn_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-introduction-yn.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-introduction-yn-save"
                "${CMAKE_BINARY_DIR}/fig-introduction-yn"
                "${PROJECT_SOURCE_DIR}/scripts/fig-introduction-yn-rgb-reference.json"
    )
    set_tests_properties(fig_introduction_yn_validation PROPERTIES
        DEPENDS fig_introduction_yn_prepare)
    add_test(
        NAME fig_ordinary_attack_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ordinary-attack-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ordinary-attack-save"
    )
    add_test(
        NAME fig_ordinary_attack_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ordinary-attack.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ordinary-attack-save"
                "${CMAKE_BINARY_DIR}/fig-ordinary-attack"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ordinary-attack-rgb-reference.json"
    )
    set_tests_properties(fig_ordinary_attack_validation PROPERTIES
        DEPENDS fig_ordinary_attack_prepare)
    add_test(
        NAME fig_capture_reward_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ordinary-attack-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-capture-reward-save"
    )
    add_test(
        NAME fig_capture_reward_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-capture-reward.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-capture-reward-save"
                "${CMAKE_BINARY_DIR}/fig-capture-reward"
                "${PROJECT_SOURCE_DIR}/scripts/fig-capture-reward-rgb-reference.json"
    )
    set_tests_properties(fig_capture_reward_validation PROPERTIES
        DEPENDS fig_capture_reward_prepare)
    add_test(
        NAME fig_monster_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-status-save"
    )
    add_test(
        NAME fig_monster_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-status-save"
                "${CMAKE_BINARY_DIR}/fig-monster-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-status-rgb-reference.json"
    )
    set_tests_properties(fig_monster_status_validation PROPERTIES
        DEPENDS fig_monster_status_prepare)
    add_test(
        NAME fig_monster_immunity_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-immunity-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-immunity-save"
    )
    add_test(
        NAME fig_monster_immunity_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-immunity.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-immunity-save"
                "${CMAKE_BINARY_DIR}/fig-monster-immunity"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-immunity-rgb-reference.json"
    )
    set_tests_properties(fig_monster_immunity_validation PROPERTIES
        DEPENDS fig_monster_immunity_prepare)
    add_test(
        NAME fig_player_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-damage-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-damage-save"
    )
    add_test(
        NAME fig_player_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-damage-save"
                "${CMAKE_BINARY_DIR}/fig-player-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-damage-rgb-reference.json"
    )
    set_tests_properties(fig_player_damage_validation PROPERTIES
        DEPENDS fig_player_damage_prepare)
    add_test(
        NAME fig_implicit_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-implicit-damage-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-implicit-damage-save"
    )
    add_test(
        NAME fig_implicit_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-implicit-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-implicit-damage-save"
                "${CMAKE_BINARY_DIR}/fig-implicit-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-implicit-damage-rgb-reference.json"
    )
    set_tests_properties(fig_implicit_damage_validation PROPERTIES
        DEPENDS fig_implicit_damage_prepare)
    add_test(
        NAME fig_periodic_zero_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-periodic-zero-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-periodic-zero-save"
    )
    add_test(
        NAME fig_periodic_zero_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-periodic-zero.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-periodic-zero-save"
                "${CMAKE_BINARY_DIR}/fig-periodic-zero"
                "${PROJECT_SOURCE_DIR}/scripts/fig-periodic-zero-rgb-reference.json"
    )
    set_tests_properties(fig_periodic_zero_validation PROPERTIES
        DEPENDS fig_periodic_zero_prepare)
    add_test(
        NAME fig_ally_zero_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-zero-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-zero-save"
    )
    add_test(
        NAME fig_ally_zero_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-zero.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-zero-save"
                "${CMAKE_BINARY_DIR}/fig-ally-zero"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-zero-rgb-reference.json"
    )
    set_tests_properties(fig_ally_zero_validation PROPERTIES
        DEPENDS fig_ally_zero_prepare)
    add_test(
        NAME fig_ally_evasion_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-evasion-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-evasion-save"
    )
    add_test(
        NAME fig_ally_evasion_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-evasion.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-evasion-save"
                "${CMAKE_BINARY_DIR}/fig-ally-evasion"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-evasion-rgb-reference.json"
    )
    set_tests_properties(fig_ally_evasion_validation PROPERTIES
        DEPENDS fig_ally_evasion_prepare)
    add_test(
        NAME fig_ally_hit_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-hit-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-hit-save"
    )
    add_test(
        NAME fig_ally_hit_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-hit.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-hit-save"
                "${CMAKE_BINARY_DIR}/fig-ally-hit"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-hit-rgb-reference.json"
    )
    set_tests_properties(fig_ally_hit_validation PROPERTIES
        DEPENDS fig_ally_hit_prepare)
    add_test(
        NAME fig_ally_hit_survives_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-hit-survives-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-hit-survives-save"
    )
    add_test(
        NAME fig_ally_hit_survives_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-hit-survives.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-hit-survives-save"
                "${CMAKE_BINARY_DIR}/fig-ally-hit-survives"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-hit-survives-rgb-reference.json"
    )
    set_tests_properties(fig_ally_hit_survives_validation PROPERTIES
        DEPENDS fig_ally_hit_survives_prepare)
    add_test(
        NAME fig_ally_ability_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-ability-save"
    )
    add_test(
        NAME fig_ally_ability_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-ability.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-ability-save"
                "${CMAKE_BINARY_DIR}/fig-ally-ability"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-ability-rgb-reference.json"
    )
    set_tests_properties(fig_ally_ability_validation PROPERTIES
        DEPENDS fig_ally_ability_prepare)
    add_test(
        NAME fig_ally_multitarget_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-multitarget-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-multitarget-save"
    )
    add_test(
        NAME fig_ally_multitarget_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-multitarget.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-multitarget-save"
                "${CMAKE_BINARY_DIR}/fig-ally-multitarget"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-multitarget-rgb-reference.json"
    )
    set_tests_properties(fig_ally_multitarget_validation PROPERTIES
        DEPENDS fig_ally_multitarget_prepare)
    add_test(
        NAME fig_ally_special_ability_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-special-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-special-ability-save"
    )
    add_test(
        NAME fig_ally_special_ability_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-special-ability.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-special-ability-save"
                "${CMAKE_BINARY_DIR}/fig-ally-special-ability"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-special-ability-rgb-reference.json"
    )
    set_tests_properties(fig_ally_special_ability_validation PROPERTIES
        DEPENDS fig_ally_special_ability_prepare)
    add_test(
        NAME fig_ally_medium_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-medium-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-medium-dismiss-save"
    )
    add_test(
        NAME fig_ally_medium_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-medium-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-medium-dismiss-save"
                "${CMAKE_BINARY_DIR}/fig-ally-medium-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-medium-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_ally_medium_dismiss_validation PROPERTIES
        DEPENDS fig_ally_medium_dismiss_prepare)
    add_test(
        NAME fig_ally_empty_medium_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-empty-medium-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-medium-dismiss-save"
    )
    add_test(
        NAME fig_ally_empty_medium_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-empty-medium-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-medium-dismiss-save"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-medium-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-empty-medium-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_ally_empty_medium_dismiss_validation PROPERTIES
        DEPENDS fig_ally_empty_medium_dismiss_prepare)
    add_test(
        NAME fig_ally_empty_ae_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-empty-ae-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-ae-dismiss-game"
    )
    add_test(
        NAME fig_ally_empty_ae_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-empty-ae-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-ae-dismiss-game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-ae-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-empty-ae-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_ally_empty_ae_dismiss_validation PROPERTIES
        DEPENDS fig_ally_empty_ae_dismiss_prepare)
    add_test(
        NAME fig_ally_empty_b0_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-empty-b0-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-b0-dismiss-game"
    )
    add_test(
        NAME fig_ally_empty_b0_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-empty-b0-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-b0-dismiss-game"
                "${CMAKE_BINARY_DIR}/fig-ally-empty-b0-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-empty-b0-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_ally_empty_b0_dismiss_validation PROPERTIES
        DEPENDS fig_ally_empty_b0_dismiss_prepare)
    add_test(
        NAME fig_ally_ae_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-ae-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-ae-dismiss-game"
    )
    add_test(
        NAME fig_ally_ae_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-ae-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-ae-dismiss-game"
                "${CMAKE_BINARY_DIR}/fig-ally-ae-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-ae-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_ally_ae_dismiss_validation PROPERTIES
        DEPENDS fig_ally_ae_dismiss_prepare)
    add_test(
        NAME fig_ally_b0_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-b0-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-b0-dismiss-game"
    )
    add_test(
        NAME fig_ally_b0_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-b0-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-b0-dismiss-game"
                "${CMAKE_BINARY_DIR}/fig-ally-b0-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-b0-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_ally_b0_dismiss_validation PROPERTIES
        DEPENDS fig_ally_b0_dismiss_prepare)
    add_test(
        NAME fig_ally_flee_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-flee-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-flee-save"
    )
    add_test(
        NAME fig_ally_flee_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-flee.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-flee-save"
                "${CMAKE_BINARY_DIR}/fig-ally-flee"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-flee-rgb-reference.json"
    )
    set_tests_properties(fig_ally_flee_validation PROPERTIES
        DEPENDS fig_ally_flee_prepare)
    add_test(
        NAME fig_ally_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-ally-medium-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-medium-save"
    )
    add_test(
        NAME fig_ally_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-ally-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-ally-medium-save"
                "${CMAKE_BINARY_DIR}/fig-ally-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-ally-medium-rgb-reference.json"
    )
    set_tests_properties(fig_ally_medium_validation PROPERTIES
        DEPENDS fig_ally_medium_prepare)
    add_test(
        NAME fig_summon_replace_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-summon-replace-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-summon-replace-save"
    )
    add_test(
        NAME fig_summon_replace_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-summon-replace.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-summon-replace-save"
                "${CMAKE_BINARY_DIR}/fig-summon-replace"
                "${PROJECT_SOURCE_DIR}/scripts/fig-summon-replace-rgb-reference.json"
    )
    set_tests_properties(fig_summon_replace_validation PROPERTIES
        DEPENDS fig_summon_replace_prepare)
    add_test(
        NAME fig_monster_flee_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-flee-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-flee-save"
    )
    add_test(
        NAME fig_monster_flee_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-flee.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-flee-save"
                "${CMAKE_BINARY_DIR}/fig-monster-flee"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-flee-rgb-reference.json"
    )
    set_tests_properties(fig_monster_flee_validation PROPERTIES
        DEPENDS fig_monster_flee_prepare)
    add_test(
        NAME fig_monster_escape_failed_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-escape-failed-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-escape-failed-save"
    )
    add_test(
        NAME fig_monster_escape_failed_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-escape-failed.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-escape-failed-save"
                "${CMAKE_BINARY_DIR}/fig-monster-escape-failed"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-escape-failed-rgb-reference.json"
    )
    set_tests_properties(fig_monster_escape_failed_validation PROPERTIES
        DEPENDS fig_monster_escape_failed_prepare)
    add_test(
        NAME fig_monster_heal_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-heal-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-heal-save"
    )
    add_test(
        NAME fig_monster_heal_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-heal.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-heal-save"
                "${CMAKE_BINARY_DIR}/fig-monster-heal"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-heal-rgb-reference.json"
    )
    set_tests_properties(fig_monster_heal_validation PROPERTIES
        DEPENDS fig_monster_heal_prepare)
    add_test(
        NAME fig_monster_generic_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-generic-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-generic-save"
    )
    add_test(
        NAME fig_monster_generic_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-generic.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-generic-save"
                "${CMAKE_BINARY_DIR}/fig-monster-generic"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-generic-rgb-reference.json"
    )
    set_tests_properties(fig_monster_generic_validation PROPERTIES
        DEPENDS fig_monster_generic_prepare)
    add_test(
        NAME fig_monster_special_silent_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-special-silent-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-silent-save"
    )
    add_test(
        NAME fig_monster_special_silent_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-special-silent.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-silent-save"
                "${CMAKE_BINARY_DIR}/fig-monster-special-silent"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-special-silent-rgb-reference.json"
    )
    set_tests_properties(fig_monster_special_silent_validation PROPERTIES
        DEPENDS fig_monster_special_silent_prepare)
    add_test(
        NAME fig_monster_special_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-special-status-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-status-save"
    )
    add_test(
        NAME fig_monster_special_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-special-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-status-save"
                "${CMAKE_BINARY_DIR}/fig-monster-special-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-special-status-rgb-reference.json"
    )
    set_tests_properties(fig_monster_special_status_validation PROPERTIES
        DEPENDS fig_monster_special_status_prepare)
    add_test(
        NAME fig_monster_special_resistance_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-special-resistance-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-resistance-save"
    )
    add_test(
        NAME fig_monster_special_resistance_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-special-resistance.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-resistance-save"
                "${CMAKE_BINARY_DIR}/fig-monster-special-resistance"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-special-resistance-rgb-reference.json"
    )
    set_tests_properties(fig_monster_special_resistance_validation PROPERTIES
        DEPENDS fig_monster_special_resistance_prepare)
    add_test(
        NAME fig_monster_special_buff_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-special-buff-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-buff-save"
    )
    add_test(
        NAME fig_monster_special_buff_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-special-buff.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-special-buff-save"
                "${CMAKE_BINARY_DIR}/fig-monster-special-buff"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-special-buff-rgb-reference.json"
    )
    set_tests_properties(fig_monster_special_buff_validation PROPERTIES
        DEPENDS fig_monster_special_buff_prepare)
    add_test(
        NAME fig_monster_buff_expiry_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-monster-buff-expiry-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-monster-buff-expiry-game"
    )
    add_test(
        NAME fig_monster_buff_expiry_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-monster-buff-expiry.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-monster-buff-expiry-game"
                "${CMAKE_BINARY_DIR}/fig-monster-buff-expiry"
                "${PROJECT_SOURCE_DIR}/scripts/fig-monster-buff-expiry-rgb-reference.json"
    )
    set_tests_properties(fig_monster_buff_expiry_validation PROPERTIES
        DEPENDS fig_monster_buff_expiry_prepare)
    add_test(
        NAME fig_player_buff_expiry_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-game"
    )
    add_test(
        NAME fig_player_buff_expiry_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-game"
    )
    add_test(
        NAME fig_player_buff_expiry_ability_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_ability_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_composite_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-damage-game"
                --composite-damage
    )
    add_test(
        NAME fig_player_buff_expiry_ability_composite_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-composite-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-damage-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-composite-damage-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_composite_damage_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_composite_damage_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_composite_status_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-status-damage-game"
                --composite-status-damage
    )
    add_test(
        NAME fig_player_buff_expiry_ability_composite_status_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-composite-status-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-status-damage-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-status-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-composite-status-damage-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_composite_status_damage_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_ability_composite_status_damage_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_composite_missing_media_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-missing-media-game"
                --composite-missing-media
    )
    add_test(
        NAME fig_player_buff_expiry_ability_composite_missing_media_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-composite-missing-media.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-missing-media-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-composite-missing-media"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-composite-missing-media-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_composite_missing_media_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_ability_composite_missing_media_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-medium-game"
                --medium
    )
    add_test(
        NAME fig_player_buff_expiry_ability_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-af-game"
                --medium-af
    )
    add_test(
        NAME fig_player_buff_expiry_ability_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_af_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-b0-game"
                --medium-b0
    )
    add_test(
        NAME fig_player_buff_expiry_ability_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_b0_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_resisted_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-resisted-game"
                --resisted
    )
    add_test(
        NAME fig_player_buff_expiry_ability_resisted_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-resisted.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-resisted-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-resisted"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-resisted-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_resisted_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_resisted_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_dismiss_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-medium-game"
                --dismiss-medium
    )
    add_test(
        NAME fig_player_buff_expiry_ability_dismiss_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-dismiss-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-dismiss-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_dismiss_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_dismiss_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_dismiss_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-af-game"
                --dismiss-medium-af
    )
    add_test(
        NAME fig_player_buff_expiry_ability_dismiss_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-dismiss-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-dismiss-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_dismiss_af_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_dismiss_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_dismiss_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-b0-game"
                --dismiss-medium-b0
    )
    add_test(
        NAME fig_player_buff_expiry_ability_dismiss_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-dismiss-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-dismiss-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-dismiss-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_dismiss_b0_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_dismiss_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_empty_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-medium-game"
                --empty-medium
    )
    add_test(
        NAME fig_player_buff_expiry_ability_empty_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-empty-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-empty-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_empty_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_empty_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_empty_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-af-game"
                --empty-medium-af
    )
    add_test(
        NAME fig_player_buff_expiry_ability_empty_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-empty-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-empty-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_empty_af_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_empty_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_ability_empty_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-b0-game"
                --empty-medium-b0
    )
    add_test(
        NAME fig_player_buff_expiry_ability_empty_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-ability-empty-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-ability-empty-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-ability-empty-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_ability_empty_b0_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_ability_empty_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_missing_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-missing-medium-game"
                --missing-medium
    )
    add_test(
        NAME fig_player_buff_expiry_missing_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-missing-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-missing-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-missing-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-missing-medium-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_missing_medium_validation
        PROPERTIES DEPENDS fig_player_buff_expiry_missing_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-game"
    )
    add_test(
        NAME fig_player_buff_expiry_item_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_item_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_missing_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-missing-medium-game"
                --missing-medium
    )
    add_test(
        NAME fig_player_buff_expiry_item_missing_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-missing-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-missing-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-missing-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-missing-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_missing_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_missing_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-status-game"
                --status-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-status-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-status-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_status_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_status_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_support_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-support-game"
                --support-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_support_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-support-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-support"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-support-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_support_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_support_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_support_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-support-game"
                --composite-support-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_support_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-support.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-support-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-support"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-support-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_support_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_composite_support_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_dispel_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dispel-game"
                --dispel-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_dispel_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-dispel.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dispel-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dispel"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-dispel-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_dispel_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_dispel_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_barrier_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-barrier-game"
                --barrier-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_barrier_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-barrier.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-barrier-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-barrier"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-barrier-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_barrier_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_barrier_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-medium-game"
                --medium-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_media_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-media-game"
                --composite-media-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_media_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-media.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-media-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-media"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-media-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_media_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_composite_media_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_reversed_media_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-reversed-media-game"
                --composite-reversed-media-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_reversed_media_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-reversed-media.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-reversed-media-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-reversed-media"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-reversed-media-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_reversed_media_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_reversed_media_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_af_b0_media_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-af-b0-media-game"
                --composite-af-b0-media-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_af_b0_media_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-af-b0-media.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-af-b0-media-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-af-b0-media"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-af-b0-media-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_af_b0_media_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_af_b0_media_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-damage-game"
                --composite-damage-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-damage-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-damage-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_damage_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_composite_damage_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_status_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-status-damage-game"
                --composite-status-damage-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_status_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-status-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-status-damage-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-status-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-status-damage-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_status_damage_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_status_damage_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_targetless_damage_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-targetless-damage-game"
                --composite-targetless-damage-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_targetless_damage_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-targetless-damage.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-targetless-damage-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-targetless-damage"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-targetless-damage-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_targetless_damage_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_targetless_damage_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_targetless_status_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-targetless-status-game"
                --composite-targetless-status-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_targetless_status_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-targetless-status.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-targetless-status-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-targetless-status"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-targetless-status-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_targetless_status_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_targetless_status_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_damage_missing_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-damage-missing-af-game"
                --composite-damage-missing-af-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_damage_missing_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-damage-missing-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-damage-missing-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-damage-missing-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-damage-missing-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_damage_missing_af_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_damage_missing_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_missing_af_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-missing-af-b0-game"
                --composite-missing-af-b0-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_missing_af_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-missing-af-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-missing-af-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-missing-af-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-missing-af-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_missing_af_b0_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_missing_af_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_composite_missing_media_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-missing-media-game"
                --composite-missing-media-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_composite_missing_media_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-composite-missing-media.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-missing-media-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-composite-missing-media"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-composite-missing-media-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_composite_missing_media_validation
        PROPERTIES DEPENDS
        fig_player_buff_expiry_item_composite_missing_media_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-af-game"
                --medium-item-af
    )
    add_test(
        NAME fig_player_buff_expiry_item_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_af_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-b0-game"
                --medium-item-b0
    )
    add_test(
        NAME fig_player_buff_expiry_item_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_b0_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_empty_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-medium-game"
                --empty-medium-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_empty_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-empty-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-empty-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_empty_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_empty_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_empty_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-af-game"
                --empty-medium-item-af
    )
    add_test(
        NAME fig_player_buff_expiry_item_empty_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-empty-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-empty-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_empty_af_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_empty_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_empty_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-b0-game"
                --empty-medium-item-b0
    )
    add_test(
        NAME fig_player_buff_expiry_item_empty_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-empty-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-empty-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-empty-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_empty_b0_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_empty_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_dismiss_medium_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-medium-game"
                --dismiss-medium-item
    )
    add_test(
        NAME fig_player_buff_expiry_item_dismiss_medium_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-dismiss-medium.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-medium-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-medium"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-dismiss-medium-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_dismiss_medium_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_dismiss_medium_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_dismiss_af_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-af-game"
                --dismiss-medium-item-af
    )
    add_test(
        NAME fig_player_buff_expiry_item_dismiss_af_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-dismiss-af.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-af-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-af"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-dismiss-af-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_dismiss_af_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_dismiss_af_prepare)
    add_test(
        NAME fig_player_buff_expiry_item_dismiss_b0_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-item-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-b0-game"
                --dismiss-medium-item-b0
    )
    add_test(
        NAME fig_player_buff_expiry_item_dismiss_b0_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-item-dismiss-b0.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-b0-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-item-dismiss-b0"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-item-dismiss-b0-rgb-reference.json"
    )
    set_tests_properties(
        fig_player_buff_expiry_item_dismiss_b0_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_item_dismiss_b0_prepare)
    add_test(
        NAME fig_player_buff_expiry_escape_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-ability-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-escape-game"
    )
    add_test(
        NAME fig_player_buff_expiry_escape_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-escape.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-escape-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-escape"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-escape-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_escape_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_escape_prepare)
    add_test(
        NAME fig_player_buff_expiry_capture_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-capture-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-capture-game"
    )
    add_test(
        NAME fig_player_buff_expiry_capture_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-capture.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-capture-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-capture"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-capture-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_capture_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_capture_prepare)
    add_test(
        NAME fig_player_buff_expiry_capture_success_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-player-buff-expiry-capture-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-capture-success-game"
                --success
    )
    add_test(
        NAME fig_player_buff_expiry_capture_success_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-buff-expiry-capture-success.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-capture-success-game"
                "${CMAKE_BINARY_DIR}/fig-player-buff-expiry-capture-success"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-buff-expiry-capture-success-rgb-reference.json"
    )
    set_tests_properties(fig_player_buff_expiry_capture_success_validation PROPERTIES
        DEPENDS fig_player_buff_expiry_capture_success_prepare)
    add_test(
        NAME fig_capture_success_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-capture-success-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-capture-success-game"
    )
    add_test(
        NAME fig_capture_success_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-capture-success.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-capture-success-game"
                "${CMAKE_BINARY_DIR}/fig-capture-success-game"
                "${CMAKE_BINARY_DIR}/fig-capture-success"
                "${PROJECT_SOURCE_DIR}/scripts/fig-capture-success-rgb-reference.json"
    )
    set_tests_properties(fig_capture_success_validation PROPERTIES
        DEPENDS fig_capture_success_prepare)
    add_test(
        NAME fig_capture_failure_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-capture-success-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-capture-failure-game"
                --failure
    )
    add_test(
        NAME fig_capture_failure_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-capture-failure.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-capture-failure-game"
                "${CMAKE_BINARY_DIR}/fig-capture-failure-game"
                "${CMAKE_BINARY_DIR}/fig-capture-failure"
                "${PROJECT_SOURCE_DIR}/scripts/fig-capture-failure-rgb-reference.json"
    )
    set_tests_properties(fig_capture_failure_validation PROPERTIES
        DEPENDS fig_capture_failure_prepare)
    add_test(
        NAME fig_player_escape_success_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-capture-success-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-escape-success-game"
    )
    add_test(
        NAME fig_player_escape_success_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-escape-success.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-escape-success-game"
                "${CMAKE_BINARY_DIR}/fig-player-escape-success-game"
                "${CMAKE_BINARY_DIR}/fig-player-escape-success"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-escape-success-rgb-reference.json"
    )
    set_tests_properties(fig_player_escape_success_validation PROPERTIES
        DEPENDS fig_player_escape_success_prepare)
    add_test(
        NAME fig_player_escape_failure_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-capture-success-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-player-escape-failure-game"
                --escape-failure
    )
    add_test(
        NAME fig_player_escape_failure_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-player-escape-failure.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-player-escape-failure-game"
                "${CMAKE_BINARY_DIR}/fig-player-escape-failure-game"
                "${CMAKE_BINARY_DIR}/fig-player-escape-failure"
                "${PROJECT_SOURCE_DIR}/scripts/fig-player-escape-failure-rgb-reference.json"
    )
    set_tests_properties(fig_player_escape_failure_validation PROPERTIES
        DEPENDS fig_player_escape_failure_prepare)
    add_test(
        NAME fig_medium_dismiss_prepare
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/prepare-fig-medium-dismiss-fixture.py"
                "${PROJECT_SOURCE_DIR}/game"
                "${CMAKE_BINARY_DIR}/fig-medium-dismiss-game"
    )
    add_test(
        NAME fig_medium_dismiss_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-fig-medium-dismiss.py"
                "$<TARGET_FILE:swd2_rewrite>"
                "${CMAKE_BINARY_DIR}/fig-medium-dismiss-game"
                "${CMAKE_BINARY_DIR}/fig-medium-dismiss-game"
                "${CMAKE_BINARY_DIR}/fig-medium-dismiss"
                "${PROJECT_SOURCE_DIR}/scripts/fig-medium-dismiss-rgb-reference.json"
    )
    set_tests_properties(fig_medium_dismiss_validation PROPERTIES
        DEPENDS fig_medium_dismiss_prepare)
    add_test(
        NAME long_run_checkpoint_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-long-run-checkpoint.py"
                "${PROJECT_SOURCE_DIR}/verification/long_run/checkpoint-2026-08-12-macos-arm64"
    )
    add_test(
        NAME linux_arm64_checkpoint_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/verify-linux-arm64-checkpoint.py"
                "${PROJECT_SOURCE_DIR}/verification/long_run/checkpoint-2026-08-11-linux-arm64"
    )
endif()
