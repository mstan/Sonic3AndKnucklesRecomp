# Sonic 3 family implementation tests belong to the game, never the shared engine.
set(S3_GAME_COMMON "${CMAKE_CURRENT_LIST_DIR}/../game/common")

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

find_package(Python3 QUIET COMPONENTS Interpreter)
set(S3_SK_LISTING "" CACHE FILEPATH
    "Built skdisasm sonic3k.lst (tools/sonic3_disassembly.py); enables the hook-site drift check")
if(Python3_Interpreter_FOUND AND EXISTS "${S3_SK_LISTING}")
    add_test(NAME knuckles_army_sites COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_knuckles_army_sites.py"
        --listing "${S3_SK_LISTING}" --check)
endif()

s3_video_test(sonic3_video)
s3_video_test(sonic3_standalone_video SONIC3_STANDALONE=1)
