cmake_minimum_required(VERSION 3.16)
project(parallel_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

option(USE_SYSTEM_SDL3 "Prefer a system-installed SDL3 before using the vendored copy" ON)

# Unified SDL3 dependency target used everywhere in this project.
add_library(renderer_sdl3 INTERFACE)

set(HAVE_SYSTEM_SDL3 OFF)

if(USE_SYSTEM_SDL3)
  # Try a CMake package first.
  find_package(SDL3 CONFIG QUIET)

  if(SDL3_FOUND)
    set(HAVE_SYSTEM_SDL3 ON)
    target_link_libraries(renderer_sdl3 INTERFACE SDL3::SDL3)
    message(STATUS "Using system SDL3 via CMake package")
  else()
    # Fall back to pkg-config if a system SDL3 exists but only ships .pc metadata.
    pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
    if(TARGET PkgConfig::SDL3)
      set(HAVE_SYSTEM_SDL3 ON)
      target_link_libraries(renderer_sdl3 INTERFACE PkgConfig::SDL3)
      message(STATUS "Using system SDL3 via pkg-config")
    endif()
  endif()
endif()

if(NOT HAVE_SYSTEM_SDL3)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
    message(FATAL_ERROR
      "SDL3 was not found on the system and the vendored copy is missing.\n"
      "Add the SDL3 submodule at third_party/SDL, or install a system SDL3 package."
    )
  endif()

  add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
  target_link_libraries(renderer_sdl3 INTERFACE SDL3::SDL3)
  message(STATUS "Using vendored SDL3 from third_party/SDL")
endif()

# Dear ImGui is still expected from the system package.
pkg_check_modules(IMGUI REQUIRED IMPORTED_TARGET imgui)

# Make these overridable in case your distro installs them somewhere else.
set(IMGUI_INCLUDE_DIR "/usr/include/imgui" CACHE PATH
  "Directory containing imgui.h")
set(IMGUI_BACKENDS_INCLUDE_DIR "/usr/include/imgui/backends" CACHE PATH
  "Directory containing Dear ImGui backend headers")
set(IMGUI_BACKEND_SOURCE_DIR "/usr/share/doc/libimgui-dev/examples/backends" CACHE PATH
  "Directory containing Dear ImGui backend source files")

set(IMGUI_SOURCES
  "${IMGUI_BACKEND_SOURCE_DIR}/imgui_impl_sdl3.cpp"
  "${IMGUI_BACKEND_SOURCE_DIR}/imgui_impl_sdlrenderer3.cpp"
)

foreach(imgui_src IN LISTS IMGUI_SOURCES)
  if(NOT EXISTS "${imgui_src}")
    message(FATAL_ERROR
      "Missing Dear ImGui backend source: ${imgui_src}\n"
      "Install libimgui-dev or set IMGUI_BACKEND_SOURCE_DIR to the correct location."
    )
  endif()
endforeach()

add_library(renderer_core INTERFACE)
target_include_directories(renderer_core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(render_scene OBJECT
  src/scene_registry.cpp
)
target_include_directories(render_scene PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(render_scene PRIVATE
  renderer_core
)

add_library(render_camera OBJECT
  src/camera.cpp
)
target_include_directories(render_camera PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(render_camera PRIVATE
  renderer_core
)

add_library(render_engine OBJECT
  src/render_engine.cpp
)
target_include_directories(render_engine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(render_engine PRIVATE
  renderer_core
  Threads::Threads
)

add_library(render_io_utils OBJECT
  src/png_writer.cpp
  src/csv_writer.cpp
)
target_include_directories(render_io_utils PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(render_io_utils PRIVATE
  renderer_core
)

add_library(render_benchmark OBJECT
  src/benchmark_runner.cpp
)
target_include_directories(render_benchmark PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(render_benchmark PRIVATE
  renderer_core
  Threads::Threads
)

add_library(renderer_cli OBJECT
  src/cli.cpp
  src/app.cpp
)
target_include_directories(renderer_cli PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(renderer_cli PRIVATE
  renderer_core
  Threads::Threads
  renderer_sdl3
)

add_library(render_io OBJECT
  src/render_job.cpp
  src/viewer.cpp
  src/viewer_state.cpp
  src/viewer_sdl.cpp
  src/viewer_terminal.cpp
)
target_include_directories(render_io PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(render_io PRIVATE
  renderer_core
  Threads::Threads
  renderer_sdl3
)

add_library(render_ui OBJECT
  src/gui_app.cpp
  ${IMGUI_SOURCES}
)
target_include_directories(render_ui PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${IMGUI_INCLUDE_DIR}"
  "${IMGUI_BACKENDS_INCLUDE_DIR}"
)
target_link_libraries(render_ui PRIVATE
  renderer_core
  Threads::Threads
  renderer_sdl3
  PkgConfig::IMGUI
)

add_executable(renderer
  src/main.cpp
)
target_include_directories(renderer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_sources(renderer PRIVATE
  $<TARGET_OBJECTS:render_scene>
  $<TARGET_OBJECTS:render_camera>
  $<TARGET_OBJECTS:render_engine>
  $<TARGET_OBJECTS:render_io_utils>
  $<TARGET_OBJECTS:render_benchmark>
  $<TARGET_OBJECTS:renderer_cli>
  $<TARGET_OBJECTS:render_io>
  $<TARGET_OBJECTS:render_ui>
)
target_link_libraries(renderer PRIVATE
  renderer_core
  Threads::Threads
  renderer_sdl3
  PkgConfig::IMGUI
)

enable_testing()

add_executable(renderer_tests
  tests/test_renderer.cpp
  src/viewer_state.cpp
  src/render_job.cpp
)
target_include_directories(renderer_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_sources(renderer_tests PRIVATE
  $<TARGET_OBJECTS:render_scene>
  $<TARGET_OBJECTS:render_camera>
  $<TARGET_OBJECTS:render_engine>
  $<TARGET_OBJECTS:render_io_utils>
  $<TARGET_OBJECTS:render_benchmark>
)
target_link_libraries(renderer_tests PRIVATE
  renderer_core
  Threads::Threads
  renderer_sdl3
)

add_test(NAME renderer_tests COMMAND renderer_tests)