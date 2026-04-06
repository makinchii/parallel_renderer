cmake_minimum_required(VERSION 3.16)
project(parallel_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(IMGUI REQUIRED IMPORTED_TARGET imgui)

add_library(renderer_core INTERFACE)
target_include_directories(renderer_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(IMGUI_SOURCES
  /usr/share/doc/libimgui-dev/examples/backends/imgui_impl_sdl3.cpp
  /usr/share/doc/libimgui-dev/examples/backends/imgui_impl_sdlrenderer3.cpp
)

add_library(render_scene OBJECT
  src/scene_registry.cpp
)
target_include_directories(render_scene PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_scene PRIVATE renderer_core)

add_library(render_camera OBJECT
  src/camera.cpp
)
target_include_directories(render_camera PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_camera PRIVATE renderer_core)

add_library(render_engine OBJECT
  src/render_engine.cpp
)
target_include_directories(render_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_engine PRIVATE renderer_core Threads::Threads)

add_library(render_io_utils OBJECT
  src/png_writer.cpp
  src/csv_writer.cpp
)
target_include_directories(render_io_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_io_utils PRIVATE renderer_core)

add_library(render_benchmark OBJECT
  src/benchmark_runner.cpp
)
target_include_directories(render_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_benchmark PRIVATE renderer_core Threads::Threads)

add_library(renderer_cli OBJECT
  src/cli.cpp
  src/app.cpp
)
target_include_directories(renderer_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(renderer_cli PRIVATE renderer_core Threads::Threads PkgConfig::SDL3)

add_library(render_io OBJECT
  src/render_job.cpp
  src/viewer.cpp
  src/viewer_state.cpp
  src/viewer_sdl.cpp
  src/viewer_terminal.cpp
)
target_include_directories(render_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_io PRIVATE renderer_core Threads::Threads PkgConfig::SDL3)

add_library(render_ui OBJECT
  src/gui_app.cpp
  ${IMGUI_SOURCES}
)
target_include_directories(render_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src /usr/include/imgui /usr/include/imgui/backends)
target_link_libraries(render_ui PRIVATE renderer_core Threads::Threads PkgConfig::SDL3 PkgConfig::IMGUI)

add_executable(renderer
  src/main.cpp
)
target_include_directories(renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(renderer PRIVATE $<TARGET_OBJECTS:render_scene> $<TARGET_OBJECTS:render_camera> $<TARGET_OBJECTS:render_engine> $<TARGET_OBJECTS:render_io_utils> $<TARGET_OBJECTS:render_benchmark> $<TARGET_OBJECTS:renderer_cli> $<TARGET_OBJECTS:render_io> $<TARGET_OBJECTS:render_ui>)
target_link_libraries(renderer PRIVATE renderer_core Threads::Threads PkgConfig::SDL3)

enable_testing()

add_executable(renderer_tests
  tests/test_renderer.cpp
  src/viewer_state.cpp
  src/render_job.cpp
)
target_include_directories(renderer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(renderer_tests PRIVATE $<TARGET_OBJECTS:render_scene> $<TARGET_OBJECTS:render_camera> $<TARGET_OBJECTS:render_engine> $<TARGET_OBJECTS:render_io_utils> $<TARGET_OBJECTS:render_benchmark>)
target_link_libraries(renderer_tests PRIVATE renderer_core Threads::Threads PkgConfig::SDL3)
add_test(NAME renderer_tests COMMAND renderer_tests)
