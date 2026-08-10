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
                "${PROJECT_SOURCE_DIR}/verification/long_run/checkpoint-2026-08-10-macos"
    )
endif()
