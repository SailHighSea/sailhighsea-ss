cmake_minimum_required(VERSION 3.16)
project(SailHighSeaSimpleSync VERSION 0.0.1 LANGUAGES C)

# Shown in the UI. CI overrides it with the release version (e.g. -DSHS_VERSION_STRING=0.0.3).
set(SHS_VERSION_STRING "${PROJECT_VERSION}" CACHE STRING "Version string shown in the app")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---- Output: <repo>/test/SailHighSea-SimpleSync.exe (local dev testing) ----
# Multi-config generators (Visual Studio) would otherwise append Debug/Release.
set(SHS_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test" CACHE PATH "Where the built exe is written")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SHS_TEST_DIR}")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg}" "${SHS_TEST_DIR}")
endforeach()

# ---- raylib: prefer external/raylib, else system package, else fetch ----
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/raylib/CMakeLists.txt")
    add_subdirectory(external/raylib)
else()
    find_package(raylib 5.0 QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        FetchContent_Declare(raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(raylib)
    endif()
endif()

# ---- Executable ----
file(GLOB_RECURSE SHS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

find_package(Threads REQUIRED)

# Windows: embed assets/icon.ico as the exe icon (shown in Explorer and the taskbar).
if(WIN32)
    enable_language(RC)
    set(SHS_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    configure_file(assets/app.rc.in "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
    list(APPEND SHS_SOURCES "${CMAKE_BINARY_DIR}/app.rc")
endif()

add_executable(SailHighSea-SimpleSync ${SHS_SOURCES})
set_target_properties(SailHighSea-SimpleSync PROPERTIES OUTPUT_NAME "SailHighSea-SimpleSync")

target_include_directories(SailHighSea-SimpleSync PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine")

target_compile_definitions(SailHighSea-SimpleSync PRIVATE SHS_VERSION="${SHS_VERSION_STRING}")
target_link_libraries(SailHighSea-SimpleSync PRIVATE raylib Threads::Threads)
if(NOT WIN32)
    target_link_libraries(SailHighSea-SimpleSync PRIVATE m)
endif()

# ---- Engine test (no window): ctest / cmake --build build --target engine_test ----
enable_testing()
add_executable(engine_test
    tests/engine_test.c
    src/engine/sync_engine.c
    src/engine/task_store.c
    src/platform/updater.c
    src/platform/drives.c)
target_include_directories(engine_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/engine" "${CMAKE_CURRENT_SOURCE_DIR}/src/platform")
if(WIN32)
    target_link_libraries(engine_test PRIVATE winhttp)
endif()
target_link_libraries(engine_test PRIVATE Threads::Threads)
set_target_properties(engine_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(engine_test PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${cfg}" "${CMAKE_BINARY_DIR}/tests")
endforeach()
add_test(NAME engine_test COMMAND engine_test "${CMAKE_BINARY_DIR}/tests/sandbox")

if(WIN32)
    target_link_libraries(SailHighSea-SimpleSync PRIVATE ole32 shell32 uuid advapi32 winhttp)
    # GUI subsystem (no console window) in non-Debug builds, keeping plain main().
    if(MSVC)
        target_link_options(SailHighSea-SimpleSync PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>
            $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
    else()
        # -static: no libwinpthread/libgcc/libraylib DLLs needed next to the exe.
        target_link_options(SailHighSea-SimpleSync PRIVATE -static
            $<$<NOT:$<CONFIG:Debug>>:-mwindows>)
    endif()
endif()

if(MSVC)
    target_compile_options(SailHighSea-SimpleSync PRIVATE /W4)
else()
    target_compile_options(SailHighSea-SimpleSync PRIVATE -Wall -Wextra)
endif()
