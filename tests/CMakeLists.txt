# Sonic 3 family implementation tests belong to the game, never the shared engine.
add_executable(campaign_save_test runtime/campaign_save_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/campaign_save.c")
target_include_directories(campaign_save_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME campaign_save COMMAND campaign_save_test)
add_executable(scd_geometry_test runtime/scd_geometry_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_geometry.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_resource.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_pack.c")
target_include_directories(scd_geometry_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME scd_geometry COMMAND scd_geometry_test)
add_executable(scd_background_test runtime/scd_background_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_background.c")
target_include_directories(scd_background_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME scd_background COMMAND scd_background_test)
add_executable(scd_pack_graph_test runtime/scd_pack_graph_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_resource.c")
target_include_directories(scd_pack_graph_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME scd_pack_graph COMMAND scd_pack_graph_test)
add_executable(scd_audio_control_test runtime/scd_audio_control_test.c)
target_include_directories(scd_audio_control_test PRIVATE "${RUNNER_ROOT}")
if(NOT WIN32)
    target_link_libraries(scd_audio_control_test PRIVATE m)
endif()
add_test(NAME scd_audio_control COMMAND scd_audio_control_test)
add_executable(scd_resource_test runtime/scd_resource_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_resource.c")
target_include_directories(scd_resource_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_executable(campaign_shadow_test runtime/campaign_shadow_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/campaign_save.c")
target_include_directories(campaign_shadow_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME campaign_shadow COMMAND campaign_shadow_test)
add_executable(campaign_mighty_test runtime/campaign_mighty_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/campaign_mighty.c")
target_include_directories(campaign_mighty_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME campaign_mighty COMMAND campaign_mighty_test)
set(S3_GAME_COMMON "${CMAKE_CURRENT_LIST_DIR}/../game/common")
add_executable(megamix_geometry_test runtime/megamix_geometry_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/megamix_geometry.c")
target_include_directories(megamix_geometry_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME megamix_geometry COMMAND megamix_geometry_test)
add_executable(megamix_background_test runtime/megamix_background_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/megamix_background.c")
add_test(NAME megamix_background COMMAND megamix_background_test)
add_executable(megamix_mighty_test runtime/megamix_mighty_test.c)
target_include_directories(megamix_mighty_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
add_test(NAME megamix_mighty COMMAND megamix_mighty_test)
add_executable(megamix_shadow_test runtime/megamix_shadow_test.c)
target_include_directories(megamix_shadow_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
add_test(NAME megamix_shadow COMMAND megamix_shadow_test)
add_executable(megamix_presentation_test runtime/megamix_presentation_test.c)
target_include_directories(megamix_presentation_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${S3_GAME_COMMON}" "${RUNNER_ROOT}" "${RUNNER_ROOT}/include" "${RUNNER_ROOT}/external/superzazu")
add_test(NAME megamix_presentation COMMAND megamix_presentation_test)
add_executable(megamix_sat_test runtime/megamix_sat_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/trilogy_megamix_sprites.c")
target_include_directories(megamix_sat_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
add_test(NAME megamix_sat COMMAND megamix_sat_test)
add_executable(megamix_hooks_test runtime/megamix_hooks_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/megamix_geometry.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/trilogy_megamix_geometry.c")
target_include_directories(megamix_hooks_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${S3_GAME_COMMON}" "${RUNNER_ROOT}" "${RUNNER_ROOT}/include")
add_test(NAME megamix_hooks COMMAND megamix_hooks_test)
add_executable(stage_pack_test runtime/stage_pack_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/stage_pack_mods.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_import.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_resource.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/scd_pack.c"
    "${CMAKE_SOURCE_DIR}/game/trilogy/stage_pack_bank.c")
target_include_directories(stage_pack_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy"
    "${CMAKE_SOURCE_DIR}/recomp-ui/src")
target_compile_definitions(stage_pack_test PRIVATE RECOMP_LAUNCHER=1
    S3_CD_IMPORT_SOURCE="${CMAKE_SOURCE_DIR}/tools/import_sonic_cd.py"
    S3_CD_IMPORT_VALIDATOR="$<TARGET_FILE:scd_pack_validate>")
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
    add_executable(scd_import_process_test runtime/scd_import_process_test.c
        "${CMAKE_SOURCE_DIR}/game/trilogy/scd_import.c")
    target_include_directories(scd_import_process_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
    target_compile_definitions(scd_import_process_test PRIVATE
        S3_CD_IMPORT_SOURCE="${CMAKE_CURRENT_LIST_DIR}/runtime/scd_import_process_fixture.py"
        S3_CD_IMPORT_VALIDATOR="$<TARGET_FILE:scd_pack_validate>")
    add_dependencies(scd_import_process_test scd_pack_validate)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/import-process")
    add_test(NAME scd_import_process COMMAND scd_import_process_test
        "${CMAKE_CURRENT_BINARY_DIR}/import-process/settings.ini")
    set_tests_properties(scd_import_process PROPERTIES TIMEOUT 15
        ENVIRONMENT "SONIC3K_PYTHON=${Python3_EXECUTABLE}")
    add_test(NAME trilogy_hook_ownership COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/check_trilogy_hook_ownership.py")
    add_test(NAME scd_resource COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/scd_resource_test.py" --validator "$<TARGET_FILE:scd_resource_test>")
    add_test(NAME scd_disc COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/scd_disc_test.py")
    add_test(NAME scd_import_transaction COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/scd_import_test.py")
    # The Python media reader supports the same explicit engine override as
    # the C/C++ build. Isolated worktrees need not initialize a second engine.
    set_property(TEST scd_resource scd_disc scd_import_transaction APPEND
        PROPERTY ENVIRONMENT "GENESIS_RECOMP_ROOT=${RECOMP_ROOT}")
    add_test(NAME megamix_sprite_formats COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/megamix_sprite_test.py")
    if(EXISTS "${CMAKE_SOURCE_DIR}/game/sonic3k/sonic3k.bin")
        add_test(NAME megamix_geometry_sites COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_megamix_geometry_sites.py"
            --rom "${CMAKE_SOURCE_DIR}/game/sonic3k/sonic3k.bin" --check)
        add_test(NAME sonic_cd_sites COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_sonic_cd_sites.py"
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

add_executable(campaign_combined_test runtime/campaign_combined_test.c
    "${CMAKE_SOURCE_DIR}/game/trilogy/campaign_save.c")
target_include_directories(campaign_combined_test PRIVATE "${CMAKE_SOURCE_DIR}/game/trilogy")
add_test(NAME campaign_combined COMMAND campaign_combined_test)
