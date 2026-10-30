cmake_minimum_required(VERSION 3.20)
project(VinewoodHook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vh_core STATIC src/core/pattern.cpp src/core/natives.cpp src/core/hashmap.cpp)
target_include_directories(vh_core PUBLIC src)
target_compile_options(vh_core PRIVATE -Wall -Wextra)

if(CMAKE_CROSSCOMPILING)
    set(VH_WIN_FLAGS -Wall -Wextra)

    # The hook itself. .asi plugins import it by name, so it must be called ScriptHookV.dll.
    add_library(ScriptHookV SHARED
        src/win/shv_main.cpp
        src/win/shv_api.cpp
        src/win/scripts.cpp
        src/win/selftest.cpp
        src/win/log.cpp
        src/win/diagnostics.cpp
        src/win/explore.cpp
        src/win/ScriptHookV.def)
    target_link_libraries(ScriptHookV PRIVATE vh_core -static -static-libgcc -static-libstdc++)
    target_compile_options(ScriptHookV PRIVATE ${VH_WIN_FLAGS})
    target_compile_definitions(ScriptHookV PRIVATE __USE_MINGW_ANSI_STDIO=1)
    set_target_properties(ScriptHookV PROPERTIES PREFIX "")

    # Thin proxy the game loads as dinput8.dll; it only loads ScriptHookV.dll (and optionally .asi files).
    add_library(dinput8 SHARED src/win/proxy_main.cpp src/win/dinput8.def)
    target_link_libraries(dinput8 PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(dinput8 PRIVATE ${VH_WIN_FLAGS})
    set_target_properties(dinput8 PROPERTIES PREFIX "")
else()
    # The portable core builds and tests natively.
    enable_testing()
    add_executable(test_pattern tests/test_pattern.cpp)
    target_link_libraries(test_pattern PRIVATE vh_core)
    add_test(NAME pattern COMMAND test_pattern)
    add_executable(test_natives tests/test_natives.cpp)
    target_link_libraries(test_natives PRIVATE vh_core)
    add_test(NAME natives COMMAND test_natives)
    add_executable(test_hashmap tests/test_hashmap.cpp)
    target_link_libraries(test_hashmap PRIVATE vh_core)
    add_test(NAME hashmap COMMAND test_hashmap ${CMAKE_SOURCE_DIR}/data/native_hashes.txt)

    # Dev tool: apply the pattern table to a memory dump of the game.
    add_executable(scan_image tools/scan_image.cpp)
    target_link_libraries(scan_image PRIVATE vh_core)
endif()
