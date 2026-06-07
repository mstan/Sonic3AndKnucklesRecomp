cmake_minimum_required(VERSION 3.16)
project(Sonic3AndKnucklesRecomp C)

set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# Sonic 3 & Knuckles — native static recompilation.
#
# Sonic 3 and Sonic & Knuckles are two separate ~2 MB ROMs; "Sonic 3 &
# Knuckles" is the 4 MB lock-on UNION of the two (S&K boots at $000000, Sonic 3
# maps at $200000). They share this engine, so one repo hosts all three as
# build modes — each a native target + a paired _oracle (interpreter) target:
#
#   Target            ROM                      Ports (native/oracle)  Status
#   Sonic3Recomp      Sonic 3 alone, 2 MB      4384 / 4385            playable bring-up
#   Sonic3KRecomp     S3+S&K combined, 4 MB    4386 / 4387            scaffold / bring-up
#   (Sonic&Knuckles alone)                     —                      planned (no sandk/ data yet)
#
# Engine (runner + recompiler + clownmdemu fork) comes from the canonical
# segagenesisrecomp checkout at the workspace root, reached here through a local
# directory junction (./segagenesisrecomp -> ../segagenesisrecomp); per-game data
# lives in segagenesisrecomp/<mode>/. See README.md / RELEASING.md.
# ---------------------------------------------------------------------------

# Engine root. Public layout: the committed `segagenesisrecomp` submodule
# (clone --recursive is self-contained). Local-dev override: if a gitignored
# `engine-local` symlink -> ../segagenesisrecomp is present, prefer it so every
# game repo shares ONE engine checkout. Create it with scripts/link-engine.*.
if(EXISTS "${CMAKE_SOURCE_DIR}/engine-local")
    set(RECOMP_ROOT "${CMAKE_SOURCE_DIR}/engine-local")
    message(STATUS "engine: local-dev override (engine-local/ -> shared checkout)")
else()
    set(RECOMP_ROOT "${CMAKE_SOURCE_DIR}/segagenesisrecomp")
    message(STATUS "engine: committed segagenesisrecomp/ submodule")
endif()
set(RUNNER_ROOT "${RECOMP_ROOT}/runner")

# clownmdemu-core is the mstan fork; our changes are committed in it directly
# (no build-time patch step — see THIRD-PARTY-LICENSES.md).
add_subdirectory("${RECOMP_ROOT}/clownmdemu-core" clownmdemu-core)

set(SDL2_BUNDLED "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5")
# SDL2 discovery. Windows pins the bundled VC devel package (NO_DEFAULT_PATH
# keeps a stray MSYS2 SDL2 from leaking msys64 headers onto cl.exe). macOS/Linux
# use the system/Homebrew SDL2 (Apple Silicon: /opt/homebrew).
if(WIN32)
    set(SDL2_DIR "${SDL2_BUNDLED}/cmake" CACHE PATH "" FORCE)
    find_package(SDL2 REQUIRED CONFIG NO_DEFAULT_PATH PATHS "${SDL2_DIR}")
else()
    if(APPLE)
        find_program(BREW_EXECUTABLE brew)
        if(BREW_EXECUTABLE)
            execute_process(COMMAND "${BREW_EXECUTABLE}" --prefix sdl2
                OUTPUT_VARIABLE _brew_sdl2_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET RESULT_VARIABLE _brew_sdl2_rc)
            if(_brew_sdl2_rc EQUAL 0 AND _brew_sdl2_prefix)
                list(APPEND CMAKE_PREFIX_PATH "${_brew_sdl2_prefix}")
            endif()
        endif()
    endif()
    find_package(SDL2 REQUIRED)
endif()

# The runner includes SDL as <SDL2/SDL.h> (the bundled Windows devel layout).
# Homebrew's SDL2::SDL2 points the include dir *at* .../include/SDL2, so add
# its parent so <SDL2/SDL.h> resolves the same way on every platform.
set(SDL2_PARENT_INCLUDE_DIRS "")
foreach(_inc IN LISTS SDL2_INCLUDE_DIRS)
    get_filename_component(_inc_name "${_inc}" NAME)
    if(_inc_name STREQUAL "SDL2")
        get_filename_component(_inc_parent "${_inc}" DIRECTORY)
        list(APPEND SDL2_PARENT_INCLUDE_DIRS "${_inc_parent}")
    endif()
endforeach()

set(RUNNER_SOURCES
    "${RUNNER_ROOT}/main.c"
    "${RUNNER_ROOT}/audio.c"
    "${RUNNER_ROOT}/cmd_server.c"
    "${RUNNER_ROOT}/frame_snapshots.c"
    "${RUNNER_ROOT}/hybrid_global.c"
    "${RUNNER_ROOT}/glue.c"
    "${RUNNER_ROOT}/fiber_compat.c"
    "${RUNNER_ROOT}/crash_report.c"
    "${RUNNER_ROOT}/input_script.c"
    "${RUNNER_ROOT}/reverse_debug.c"
    "${RUNNER_ROOT}/gamepad.c"
    "${RUNNER_ROOT}/audio/event_queue.c"
    "${RUNNER_ROOT}/audio/ym2612.c"
    "${RUNNER_ROOT}/audio/sn76489.c"
    "${RUNNER_ROOT}/audio/mixer.c"
    "${RUNNER_ROOT}/audio/observability.c"
)

set(COMMON_LIBS clownmdemu-core SDL2::SDL2 SDL2::SDL2main)
if(WIN32)
    list(APPEND COMMON_LIBS ws2_32 comdlg32)
endif()

# add_recomp_mode(<target> <prefix> <game_dir> <rom> <native_port> <oracle_port>)
#   Builds <target> (native) and <target>_oracle (interpreter) for one game
#   mode, pulling generated C + spec from segagenesisrecomp/<game_dir>/.
function(add_recomp_mode TARGET PREFIX GAME_DIR ROM PORT_NATIVE PORT_ORACLE)
    set(GAME "${RECOMP_ROOT}/${GAME_DIR}")
    set(GEN  "${GAME}/generated")
    set(SHARED_SRC
        ${RUNNER_SOURCES}
        "${GEN}/${PREFIX}_full.c"
        "${GEN}/${PREFIX}_dispatch.c"
        "${GEN}/${PREFIX}_layout.c"
        "${GAME}/${PREFIX}_spec.c"
    )
    set(INCLUDES
        "${RUNNER_ROOT}" "${RUNNER_ROOT}/include" "${GAME}"
        "${RECOMP_ROOT}/clownmdemu-core/source"
        "${RECOMP_ROOT}/clownmdemu-core/libraries/clowncommon"
        "${RECOMP_ROOT}/clownmdemu-core/libraries/clown68000/source/interpreter"
        ${SDL2_PARENT_INCLUDE_DIRS}
    )

    # Native — recompiled 68K drives the game.
    add_executable(${TARGET} ${SHARED_SRC} "${RUNNER_ROOT}/stub_clown68000.c")
    target_compile_definitions(${TARGET} PRIVATE
        INCLUDE_GENERATED_CODE=1 ENABLE_RECOMPILED_CODE=1
        DEFAULT_DEBUG_PORT=${PORT_NATIVE} SONIC_REVERSE_DEBUG=1)

    # Oracle — clown68000 interpreter executes everything (parity reference).
    add_executable(${TARGET}_oracle ${SHARED_SRC}
        "${RUNNER_ROOT}/hybrid.c" "${GAME}/${PREFIX}_hybrid_table.c"
        "${RUNNER_ROOT}/hybrid_jmp.c" "${RUNNER_ROOT}/oracle_trace.c")
    target_compile_definitions(${TARGET}_oracle PRIVATE
        INCLUDE_GENERATED_CODE=1 HYBRID_RECOMPILED_CODE=1 SONIC_ORACLE_BUILD=1
        DEFAULT_DEBUG_PORT=${PORT_ORACLE} SONIC_REVERSE_DEBUG=1)

    foreach(t ${TARGET} ${TARGET}_oracle)
        target_include_directories(${t} PRIVATE ${INCLUDES})
        target_link_libraries(${t} PRIVATE ${COMMON_LIBS})
        if(MSVC)
            target_compile_options(${t} PRIVATE /W3 /WX- /utf-8 /wd4102 /wd4996 /wd4013)
            target_link_options(${t} PRIVATE /MAP)
            set_source_files_properties("${GEN}/${PREFIX}_full.c"
                PROPERTIES COMPILE_FLAGS "/bigobj")
        endif()
        if(WIN32)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_BUNDLED}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:${t}>"
                COMMENT "Copying SDL2.dll for ${t}")
            # Bring-your-own ROM: copy next to the exe only if present. ROMs are
            # gitignored and never committed/distributed (see RELEASING.md).
            if(EXISTS "${GAME}/${ROM}")
                add_custom_command(TARGET ${t} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${GAME}/${ROM}" "$<TARGET_FILE_DIR:${t}>/${ROM}"
                    COMMENT "Copying ${ROM} for ${t}")
            endif()
        endif()
    endforeach()
endfunction()

# --- Mode: Sonic 3 alone (2 MB @ $000000) — playable bring-up ---------------
add_recomp_mode(Sonic3Recomp  sonic3  sonic3  sonic3.bin  4384 4385)

# --- Mode: Sonic 3 & Knuckles combined (4 MB lock-on) — scaffold ------------
add_recomp_mode(Sonic3KRecomp sonic3k sonic3k sonic3k.bin 4386 4387)

# --- Mode: Sonic & Knuckles alone — PLANNED ---------------------------------
# No segagenesisrecomp/sandk/ data yet. When it exists, mirror the calls above:
#   add_recomp_mode(SonicAndKnucklesRecomp sandk sandk sandk.bin 4388 4389)
