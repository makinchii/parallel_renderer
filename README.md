# Parallel Renderer

CPU ray tracer with a GUI-first workflow, benchmark CSV analysis, and CLI automation.

## What It Does

- `renderer` launches the main application with GUI, viewer, benchmark, results, and charts pages.
- `renderer_cli` provides headless render, benchmark, and test modes.
- Benchmark CSV files are the source of truth for analysis.

## Dependencies

All UI dependencies are vendored in `external/`:

- SDL3: `external/SDL`
- Dear ImGui: `external/imgui`
- ImPlot: `external/implot`

Build requirements:

- CMake 3.16+
- a C++20 compiler
- a standard build toolchain for your platform

No separate system install of SDL3, Dear ImGui, or ImPlot is required for the default build.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

GUI application:

```bash
./build/renderer
```

Headless render:

```bash
./build/renderer_cli --mode render --scene simple --width 800 --height 450 --spp 16 --depth 8 --threads 8 --schedule dynamic --save output.png
```

Benchmark CSV generation:

```bash
./build/renderer_cli --mode benchmark --scene heavy --width 800 --height 450 --spp 8 --depth 6 --threads 1,2,4,8 --schedule serial,dynamic --runs 3 --csv results.csv
```

Self-test:

```bash
./build/renderer_cli --mode test
```

## Workflow

The GUI is organized around the benchmark-first workflow:

- `Benchmark` runs benchmark sweeps and writes CSV rows.
- `Results` loads a CSV and shows tabular summaries.
- `Charts` visualizes speedup, runtime, and efficiency from the same CSV data.

`results/` is reserved for saved benchmark output and analysis artifacts.

## Repository Layout

- `src/render_core.h`: shared render config, framebuffer, stats, and utility types.
- `src/scene.h`, `src/scene_registry.cpp`: geometry and built-in scene construction.
- `src/camera.*`: camera construction.
- `src/render_engine.*`: tile renderer, sampling, and scheduling.
- `src/render_job.*`, `src/viewer_state.*`: render-job lifecycle, progress, and shared viewer state.
- `src/viewer.*`, `src/viewer_terminal.cpp`, `src/viewer_sdl.cpp`: viewer entrypoints and presentation paths.
- `src/gui_app.cpp`: GUI shell, benchmark pages, results, and charts.
- `src/benchmark_runner.*`, `src/benchmark_analysis.*`: benchmark execution and CSV analysis.
- `src/png_writer.*`, `src/csv_writer.*`: output writers.
- `tests/`: correctness checks.

## Notes

- The terminal viewer remains available as a fallback.
- The GUI build uses the vendored SDL3, Dear ImGui, and ImPlot sources under `external/`.
- Legacy smoke artifacts should not be treated as current outputs; use `results/` for benchmark data instead.
