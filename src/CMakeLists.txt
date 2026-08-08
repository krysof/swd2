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
    endif()
endif()

if(BUILD_TESTING)
    add_executable(swd2_tests tests/test_main.cpp)
    target_link_libraries(swd2_tests PRIVATE swd2_core)
    add_test(NAME swd2_tests COMMAND swd2_tests "${PROJECT_SOURCE_DIR}/game")
endif()
