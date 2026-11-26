cmake_minimum_required(VERSION 3.22)
project(nebel LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(EMSCRIPTEN)
    set(CMAKE_C_EXTENSIONS ON)
else()
    set(CMAKE_C_EXTENSIONS OFF)
endif()

option(NEBEL_BUILD_TESTS "Build unit and E2E tests (native only)" ON)
option(NEBEL_COVERAGE "Enable gcov coverage flags on core and script targets" OFF)
option(NEBEL_SANITIZERS "Enable ASan + UBSan on test builds" OFF)

include(FetchContent)

# --- Lua 5.4.8 (tiny static C library, works identically on Win/Linux/Mac/Emscripten) ---
FetchContent_Declare(
    lua_src
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lua_src)

set(LUA_LIB_SOURCES
    ${lua_src_SOURCE_DIR}/lapi.c     ${lua_src_SOURCE_DIR}/lauxlib.c
    ${lua_src_SOURCE_DIR}/lbaselib.c ${lua_src_SOURCE_DIR}/lcode.c
    ${lua_src_SOURCE_DIR}/lcorolib.c ${lua_src_SOURCE_DIR}/lctype.c
    ${lua_src_SOURCE_DIR}/ldblib.c   ${lua_src_SOURCE_DIR}/ldebug.c
    ${lua_src_SOURCE_DIR}/ldo.c      ${lua_src_SOURCE_DIR}/ldump.c
    ${lua_src_SOURCE_DIR}/lfunc.c    ${lua_src_SOURCE_DIR}/lgc.c
    ${lua_src_SOURCE_DIR}/linit.c    ${lua_src_SOURCE_DIR}/liolib.c
    ${lua_src_SOURCE_DIR}/llex.c     ${lua_src_SOURCE_DIR}/lmathlib.c
    ${lua_src_SOURCE_DIR}/lmem.c     ${lua_src_SOURCE_DIR}/loadlib.c
    ${lua_src_SOURCE_DIR}/lobject.c  ${lua_src_SOURCE_DIR}/lopcodes.c
    ${lua_src_SOURCE_DIR}/loslib.c   ${lua_src_SOURCE_DIR}/lparser.c
    ${lua_src_SOURCE_DIR}/lstate.c   ${lua_src_SOURCE_DIR}/lstring.c
    ${lua_src_SOURCE_DIR}/lstrlib.c  ${lua_src_SOURCE_DIR}/ltable.c
    ${lua_src_SOURCE_DIR}/ltablib.c  ${lua_src_SOURCE_DIR}/ltm.c
    ${lua_src_SOURCE_DIR}/lundump.c  ${lua_src_SOURCE_DIR}/lutf8lib.c
    ${lua_src_SOURCE_DIR}/lvm.c      ${lua_src_SOURCE_DIR}/lzio.c
)
add_library(lua_static STATIC ${LUA_LIB_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_src_SOURCE_DIR})
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
endif()
if(NOT MSVC)
    target_link_libraries(lua_static PUBLIC m)
endif()

# --- Pure C11 Core Engine + Lua Bridge (zero SDL/OpenGL deps) ---
add_library(nebel_core STATIC
    src/core/image.c
    src/core/cv_depth.c
    src/core/brush.c
    src/core/fog_layer.c
    src/core/undo.c
    src/core/touch.c
    src/core/ui_state.c
    src/core/manifest.c
    src/core/app_state.c
    src/script/lua_bridge.c
)
target_include_directories(nebel_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(nebel_core PUBLIC lua_static)
if(NOT MSVC)
    target_compile_options(nebel_core PRIVATE -Wall -Wextra)
    target_link_libraries(nebel_core PUBLIC m)
else()
    target_compile_options(nebel_core PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
endif()

if(NEBEL_COVERAGE AND NOT MSVC AND NOT EMSCRIPTEN)
    target_compile_options(nebel_core PRIVATE --coverage -O0 -g)
    target_link_options(nebel_core PUBLIC --coverage)
endif()
if(NEBEL_SANITIZERS AND NOT MSVC AND NOT EMSCRIPTEN)
    target_compile_options(nebel_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(nebel_core PUBLIC -fsanitize=address,undefined)
endif()

# --- SDL3 (Desktop + Emscripten) ---
option(NEBEL_BUILD_APP "Build SDL3 + OpenGL interactive application" ON)
if(NEBEL_BUILD_APP)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.30
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    add_executable(nebel
        src/main.c
        src/render/gl_api.c
        src/render/font.c
        src/render/renderer.c
        src/platform/audio.c
        src/platform/file_dialog.c
        src/platform/depth_onnx.c
    )
    target_link_libraries(nebel PRIVATE nebel_core SDL3::SDL3)
    if(NOT MSVC)
        target_compile_options(nebel PRIVATE -Wall -Wextra)
    else()
        target_compile_options(nebel PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
    endif()

    if(EMSCRIPTEN)
        # Never bake assets into WASM: emit loose dist/ folder alongside nebel.wasm
        set(NEBEL_WEB_DIST "${CMAKE_BINARY_DIR}/dist")
        set_target_properties(nebel PROPERTIES
            OUTPUT_NAME "nebel"
            SUFFIX ".js"
            RUNTIME_OUTPUT_DIRECTORY "${NEBEL_WEB_DIST}"
        )
        target_link_options(nebel PRIVATE
            "-sUSE_WEBGL2=1"
            "-sFULL_ES3=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=67108864"
            "-sFORCE_FILESYSTEM=1"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_nebel_web_on_file_loaded','_nebel_web_set_env','_nebel_web_on_depth_progress','_nebel_web_on_depth_ready','_nebel_web_on_depth_fused','_nebel_web_on_depth_failed']"
            "-sEXPORTED_RUNTIME_METHODS=['FS','HEAPU8','HEAPF32','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
        )
        add_custom_command(TARGET nebel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/web/index.html" "${NEBEL_WEB_DIST}/index.html"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/web/help.html" "${NEBEL_WEB_DIST}/help.html"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/web/nebel_web.js" "${NEBEL_WEB_DIST}/nebel_web.js"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${NEBEL_WEB_DIST}/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/scripts" "${NEBEL_WEB_DIST}/scripts"
            COMMENT "Packaging loose external assets, help, and Lua scripts beside nebel.wasm"
        )
    else()
        if(APPLE)
            find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
            target_link_libraries(nebel PRIVATE ${OPENGL_FRAMEWORK})
        elseif(WIN32)
            target_link_libraries(nebel PRIVATE opengl32)
        else()
            find_package(OpenGL REQUIRED)
            target_link_libraries(nebel PRIVATE OpenGL::GL)
        endif()

        # Keep assets/ and scripts/ as loose folders right beside the native binary
        add_custom_command(TARGET nebel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:nebel>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/scripts" "$<TARGET_FILE_DIR:nebel>/scripts"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:nebel>/assets/help"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/web/help.html" "$<TARGET_FILE_DIR:nebel>/assets/help/index.html"
            COMMENT "Copying loose external assets/, help, and scripts/ beside binary"
        )
    endif()
endif()

# --- Unit Tests (>=95% coverage) & Automated Headless E2E Simulation Test ---
if(NEBEL_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()

    add_executable(nebel_unit_tests tests/unit/test_main.c)
    target_link_libraries(nebel_unit_tests PRIVATE nebel_core)
    target_compile_definitions(nebel_unit_tests PRIVATE NEBEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(NOT MSVC)
        target_compile_options(nebel_unit_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME nebel_unit_tests COMMAND nebel_unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(nebel_e2e_test tests/e2e/test_app_simulation.c)
    target_link_libraries(nebel_e2e_test PRIVATE nebel_core)
    target_compile_definitions(nebel_e2e_test PRIVATE NEBEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(NOT MSVC)
        target_compile_options(nebel_e2e_test PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME nebel_e2e_test COMMAND nebel_e2e_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
