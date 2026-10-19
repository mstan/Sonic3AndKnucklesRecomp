# Sonic 3 family implementation tests belong to the game, never the shared engine.
set(S3_GAME_COMMON "${CMAKE_CURRENT_LIST_DIR}/../game/common")
add_executable(megamix_geometry_test runtime/megamix_geometry_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/megamix_geometry.c")
target_include_directories(megamix_geometry_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME megamix_geometry COMMAND megamix_geometry_test)
add_executable(megamix_hooks_test runtime/megamix_hooks_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/megamix_geometry.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/trilogy_megamix_geometry.c")
target_include_directories(megamix_hooks_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${S3_GAME_COMMON}" "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
add_test(NAME megamix_hooks COMMAND megamix_hooks_test)
add_executable(stage_pack_test runtime/stage_pack_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/stage_pack_mods.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/stage_pack_bank.c")
target_include_directories(stage_pack_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${CMAKE_SOURCE_DIR}/recomp-ui/src")
target_compile_definitions(stage_pack_test PRIVATE RECOMP_LAUNCHER=1)
add_test(NAME stage_pack COMMAND stage_pack_test)

add_executable(sonic3_mods_test runtime/sonic3_mods_test.c "${S3_GAME_COMMON}/sonic3_mods.c")
target_include_directories(sonic3_mods_test PRIVATE "${S3_GAME_COMMON}" "${CMAKE_SOURCE_DIR}/recomp-ui/src")
target_compile_definitions(sonic3_mods_test PRIVATE RECOMP_LAUNCHER=1)
add_test(NAME sonic3_mods COMMAND sonic3_mods_test)

# The renderer test #includes game/common/sonic3_video.c directly and supplies
# its own guest-memory/TCP stubs; it compiles once per cartridge address profile.
function(s3_video_test name)
    add_executable(${name}_test runtime/sonic3_video_test.c)
    target_include_directories(${name}_test PRIVATE
        "${S3_GAME_COMMON}" "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
    target_compile_definitions(${name}_test PRIVATE ${ARGN})
    # Blue Spheres' projection uses the C math library. Unlike the Windows
    # CRT, Unix toolchains require this dependency explicitly at link time.
    if(UNIX)
        target_link_libraries(${name}_test PRIVATE m)
    endif()
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()

add_executable(sonic3_knuckles_art_test runtime/sonic3_knuckles_art_test.c
    "${S3_GAME_COMMON}/sonic3_knuckles_art.c")
target_include_directories(sonic3_knuckles_art_test PRIVATE "${S3_GAME_COMMON}")
add_test(NAME sonic3_knuckles_art COMMAND sonic3_knuckles_art_test)

add_executable(sonic3_title_art_test runtime/sonic3_title_art_test.c)
target_include_directories(sonic3_title_art_test PRIVATE "${S3_GAME_COMMON}")
add_test(NAME sonic3_title_art COMMAND sonic3_title_art_test)
add_executable(sonic3_knuckles_menu_test runtime/sonic3_knuckles_menu_test.c)
target_include_directories(sonic3_knuckles_menu_test PRIVATE "${S3_GAME_COMMON}"
    "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
add_test(NAME sonic3_knuckles_menu COMMAND sonic3_knuckles_menu_test)
if(EXISTS "${S3_GAME_COMMON}/../sonic3k/sonic3k.bin")
    add_test(NAME sonic3_title_rom_art COMMAND sonic3_title_art_test
        "${S3_GAME_COMMON}/../sonic3k/sonic3k.bin")
endif()

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME megamix_sprite_formats COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/megamix_sprite_test.py")
    if(EXISTS "${CMAKE_SOURCE_DIR}/game/sonic3k/sonic3k.bin")
        add_test(NAME megamix_geometry_sites COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_megamix_geometry_sites.py"
            --rom "${CMAKE_SOURCE_DIR}/game/sonic3k/sonic3k.bin" --check)
    endif()
endif()
set(S3_SK_LISTING "" CACHE FILEPATH
    "Built skdisasm sonic3k.lst (tools/sonic3_disassembly.py); enables the hook-site drift check")
set(S3_S3_LISTING "" CACHE FILEPATH "Built skdisasm s3.lst; enables SpecialVInt discovery drift check")
if(Python3_Interpreter_FOUND AND EXISTS "${S3_SK_LISTING}" AND EXISTS "${S3_S3_LISTING}")
    add_test(NAME special_vint_sites COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_special_vint_sites.py"
        --sk-listing "${S3_SK_LISTING}" --s3-listing "${S3_S3_LISTING}" --check)
endif()
if(Python3_Interpreter_FOUND AND EXISTS "${S3_SK_LISTING}")
    add_test(NAME knuckles_army_sites COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_knuckles_army_sites.py"
        --listing "${S3_SK_LISTING}" --check)
endif()

s3_video_test(sonic3_video)
s3_video_test(sonic3_standalone_video SONIC3_STANDALONE=1)
