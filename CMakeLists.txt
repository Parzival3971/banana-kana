cmake_minimum_required(VERSION 3.16)
project(banana_kana CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

add_library(ktcore src/core.cpp)
target_include_directories(ktcore PUBLIC src)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core ktcore)
add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/app/data)

find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
add_executable(banana_kana src/main.cpp src/app.cpp src/gfx.cpp src/fox.cpp src/selftest.cpp src/sound.cpp)
target_link_libraries(banana_kana ktcore SDL2::SDL2 PkgConfig::SDL2_TTF)
# Device build: SDL2/SDL2_ttf/FreeType/ALSA come from BananaOS, so link them dynamically but
# carry libstdc++/libgcc inside the binary to avoid depending on the device's C++ runtime.
option(KT_STATIC_STDCPP "Statically link libstdc++ and libgcc" OFF)
if(KT_STATIC_STDCPP)
  target_link_options(banana_kana PRIVATE -static-libstdc++ -static-libgcc)
endif()
