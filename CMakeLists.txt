cmake_minimum_required(VERSION 3.16)
project(Sonic3Recomp C)

set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# Sonic the Hedgehog 3 (USA) STANDALONE native build — "Sonic 3 mode".
#
# First of three planned modes (Sonic 3 alone / S&K alone / S3K locked-on).
# Clean single 2 MB ROM at $000000, identity-mapped, no lock-on, no offsets.
#
# Shares the engine (runner) + recompiler from the segagenesisrecomp submodule
# under Sonic 1's release repo (sibling SonicTheHedgehogRecomp). Per-game files
# live in segagenesisrecomp/sonic3/.
# ---------------------------------------------------------------------------

set(SHARED_REPO_ROOT "${CMAKE_SOURCE_DIR}/../SonicTheHedgehogRecomp")
set(RECOMP_ROOT      "${SHARED_REPO_ROOT}/segagenesisrecomp")
set(RUNNER_ROOT      "${RECOMP_ROOT}/runner")

include("${RUNNER_ROOT}/clownmdemu_patches.cmake")
add_subdirectory("${RECOMP_ROOT}/clownmdemu-core" clownmdemu-core)

set(SDL2_BUNDLED "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5")
set(SDL2_DIR "${SDL2_BUNDLED}/cmake" CACHE PATH "" FORCE)
find_package(SDL2 REQUIRED CONFIG NO_DEFAULT_PATH PATHS "${SDL2_DIR}")

# ---------------------------------------------------------------------------
# Source set shared by native and oracle targets.
# ---------------------------------------------------------------------------

set(S3_SHARED_SOURCES
    "${RUNNER_ROOT}/main.c"
    "${RUNNER_ROOT}/audio.c"
    "${RUNNER_ROOT}/cmd_server.c"
    "${RUNNER_ROOT}/frame_snapshots.c"
    "${RUNNER_ROOT}/hybrid_global.c"
    "${RUNNER_ROOT}/glue.c"
    "${RUNNER_ROOT}/crash_report.c"
    "${RUNNER_ROOT}/input_script.c"
    "${RUNNER_ROOT}/reverse_debug.c"
    "${RUNNER_ROOT}/gamepad.c"
    "${RUNNER_ROOT}/audio/event_queue.c"
    "${RUNNER_ROOT}/audio/ym2612.c"
    "${RUNNER_ROOT}/audio/sn76489.c"
    "${RUNNER_ROOT}/audio/mixer.c"
    "${RUNNER_ROOT}/audio/observability.c"
    "${RECOMP_ROOT}/sonic3/generated/sonic3_full.c"
    "${RECOMP_ROOT}/sonic3/generated/sonic3_dispatch.c"
    "${RECOMP_ROOT}/sonic3/generated/sonic3_layout.c"
    "${RECOMP_ROOT}/sonic3/sonic3_spec.c"
)

set(S3_INCLUDES
    "${RUNNER_ROOT}"
    "${RUNNER_ROOT}/include"
    "${RECOMP_ROOT}/sonic3"
    "${RECOMP_ROOT}/clownmdemu-core/source"
    "${RECOMP_ROOT}/clownmdemu-core/libraries/clowncommon"
    "${RECOMP_ROOT}/clownmdemu-core/libraries/clown68000/source/interpreter"
)

set(S3_LIBS clownmdemu-core SDL2::SDL2 SDL2::SDL2main)
if(WIN32)
    list(APPEND S3_LIBS ws2_32 comdlg32)
endif()

function(_s3_finalize_target tgt)
    target_include_directories(${tgt} PRIVATE ${S3_INCLUDES})
    target_link_libraries(${tgt} PRIVATE ${S3_LIBS})
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W3 /WX- /utf-8 /wd4102 /wd4996 /wd4013)
        target_link_options(${tgt} PRIVATE /MAP)
        set_source_files_properties(
            "${RECOMP_ROOT}/sonic3/generated/sonic3_full.c"
            PROPERTIES COMPILE_FLAGS "/bigobj"
        )
    endif()
    if(WIN32)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_BUNDLED}/lib/x64/SDL2.dll"
                "$<TARGET_FILE_DIR:${tgt}>"
            COMMENT "Copying SDL2.dll for ${tgt}")
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RECOMP_ROOT}/sonic3/sonic3.bin"
                "$<TARGET_FILE_DIR:${tgt}>/sonic3.bin"
            COMMENT "Copying Sonic 3 ROM for ${tgt}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Native target — recompiled 68K drives the game.
# ---------------------------------------------------------------------------

add_executable(Sonic3Recomp
    ${S3_SHARED_SOURCES}
    "${RUNNER_ROOT}/stub_clown68000.c"
)
target_compile_definitions(Sonic3Recomp PRIVATE
    INCLUDE_GENERATED_CODE=1
    ENABLE_RECOMPILED_CODE=1
    DEFAULT_DEBUG_PORT=4384
    SONIC_REVERSE_DEBUG=1
)
_s3_finalize_target(Sonic3Recomp)

# ---------------------------------------------------------------------------
# Oracle target — clown68000 interpreter executes everything.
# ---------------------------------------------------------------------------

add_executable(Sonic3Recomp_oracle
    ${S3_SHARED_SOURCES}
    "${RUNNER_ROOT}/hybrid.c"
    "${RECOMP_ROOT}/sonic3/sonic3_hybrid_table.c"
    "${RUNNER_ROOT}/hybrid_jmp.c"
    "${RUNNER_ROOT}/oracle_trace.c"
)
target_compile_definitions(Sonic3Recomp_oracle PRIVATE
    INCLUDE_GENERATED_CODE=1
    HYBRID_RECOMPILED_CODE=1
    SONIC_ORACLE_BUILD=1
    DEFAULT_DEBUG_PORT=4385
    SONIC_REVERSE_DEBUG=1
)
_s3_finalize_target(Sonic3Recomp_oracle)
