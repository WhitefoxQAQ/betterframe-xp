cmake_minimum_required(VERSION 3.16)
project(betterframe-xp-panel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SteamVR に同梱の OpenVR ライブラリ（Frame の SteamOS 上のパス）
set(OPENVR_LIB_DIR "/opt/steamvr/bin/linuxarm64" CACHE PATH "Directory containing libopenvr_api.so")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)

add_executable(betterframe-xp-panel
  src/panel_main.cpp
  src/params.cpp
  src/draw.cpp
  src/gpu_usage.cpp
  src/vk_texture.cpp
  src/vr_overlay.cpp
  src/layer_state.cpp
  src/update_check.cpp
)
target_include_directories(betterframe-xp-panel PRIVATE third_party/openvr)
target_compile_options(betterframe-xp-panel PRIVATE -Wall -Wextra)
target_link_libraries(betterframe-xp-panel PRIVATE
  PkgConfig::CAIRO
  PkgConfig::FREETYPE
  PkgConfig::VULKAN
  Threads::Threads
  "${OPENVR_LIB_DIR}/libopenvr_api.so"
)
set_target_properties(betterframe-xp-panel PROPERTIES
  INSTALL_RPATH "${OPENVR_LIB_DIR}"
  BUILD_WITH_INSTALL_RPATH TRUE
)

install(TARGETS betterframe-xp-panel RUNTIME DESTINATION bin)
