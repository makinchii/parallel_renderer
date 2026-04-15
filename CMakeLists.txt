cmake_minimum_required(VERSION 3.16)
project(parallel_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(renderer_core INTERFACE)
target_include_directories(renderer_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(external/SDL EXCLUDE_FROM_ALL)

add_library(imgui STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/misc/cpp/imgui_stdlib.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_sdl3.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/misc/cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(implot STATIC
  external/implot/implot.cpp
  external/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui ${CMAKE_CURRENT_SOURCE_DIR}/external/implot)
target_link_libraries(implot PUBLIC imgui)

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
  src/benchmark_analysis.cpp
)
target_include_directories(render_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_benchmark PRIVATE renderer_core Threads::Threads)

add_library(renderer_cli_common OBJECT
  src/cli.cpp
)
target_include_directories(renderer_cli_common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(renderer_cli_common PRIVATE renderer_core Threads::Threads)

add_library(renderer_cli_app OBJECT
  src/app_cli.cpp
)
target_include_directories(renderer_cli_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(renderer_cli_app PRIVATE renderer_core Threads::Threads)

add_library(render_io OBJECT
  src/render_job.cpp
  src/viewer.cpp
  src/viewer_state.cpp
  src/viewer_sdl.cpp
  src/viewer_terminal.cpp
)
target_include_directories(render_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(render_io PRIVATE renderer_core Threads::Threads)

add_library(render_ui OBJECT
  src/gui_app.cpp
)
target_include_directories(render_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/misc/cpp ${CMAKE_CURRENT_SOURCE_DIR}/external/implot)
target_link_libraries(render_ui PRIVATE renderer_core Threads::Threads SDL3::SDL3 imgui implot)

add_executable(renderer
  src/main.cpp
)
target_include_directories(renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(renderer PRIVATE $<TARGET_OBJECTS:render_scene> $<TARGET_OBJECTS:render_camera> $<TARGET_OBJECTS:render_engine> $<TARGET_OBJECTS:render_io_utils> $<TARGET_OBJECTS:render_benchmark> $<TARGET_OBJECTS:renderer_cli_common> $<TARGET_OBJECTS:render_io> $<TARGET_OBJECTS:render_ui> $<TARGET_OBJECTS:implot>)
target_link_libraries(renderer PRIVATE renderer_core Threads::Threads SDL3::SDL3 imgui implot)

add_executable(renderer_cli
  src/main_gui.cpp
)
target_include_directories(renderer_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(renderer_cli PRIVATE $<TARGET_OBJECTS:render_scene> $<TARGET_OBJECTS:render_camera> $<TARGET_OBJECTS:render_engine> $<TARGET_OBJECTS:render_io_utils> $<TARGET_OBJECTS:render_benchmark> $<TARGET_OBJECTS:renderer_cli_common> $<TARGET_OBJECTS:renderer_cli_app> $<TARGET_OBJECTS:render_io> $<TARGET_OBJECTS:implot>)
target_link_libraries(renderer_cli PRIVATE renderer_core Threads::Threads SDL3::SDL3 imgui implot)

enable_testing()

add_executable(renderer_tests
  tests/test_renderer.cpp
  src/viewer_state.cpp
  src/render_job.cpp
)
target_include_directories(renderer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(renderer_tests PRIVATE $<TARGET_OBJECTS:render_scene> $<TARGET_OBJECTS:render_camera> $<TARGET_OBJECTS:render_engine> $<TARGET_OBJECTS:render_io_utils> $<TARGET_OBJECTS:render_benchmark>)
target_link_libraries(renderer_tests PRIVATE renderer_core Threads::Threads)

add_test(NAME renderer_tests COMMAND renderer_tests)
