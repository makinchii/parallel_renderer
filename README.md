# Parallel Renderer

CPU ray tracer with a GUI shell for rendering, benchmarking, and inspection.

## Setup

Ubuntu WSL, using `apt`:

```bash
sudo add-apt-repository universe
sudo apt update
sudo apt install build-essential cmake pkg-config libsdl3-dev libimgui-dev
```

If `libsdl3-dev` is unavailable on your Ubuntu release, upgrade to a release that packages SDL3. This project expects both SDL3 and Dear ImGui to come from system packages.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/renderer
./build/renderer --mode render --scene simple --width 800 --height 450 --spp 16 --depth 8 --threads 8 --schedule dynamic --save output.png
./build/renderer --mode benchmark --scene heavy --width 800 --height 450 --spp 8 --depth 6 --threads 1,2,4,8 --schedule serial,dynamic --runs 3 --csv results.csv
./build/renderer --mode test
```

## Architecture

- `math.h`: vector math, RNG, reflection and refraction helpers.
- `scene.h` and `scene_registry.cpp`: sphere scenes and materials.
- `camera.h`: camera construction and ray generation.
- `render_engine.cpp`: tile-based renderer and sampling loop.
- `viewer_state.h` and `render_job.h`: shared progress, tile status, pause, and cancel state.
- `benchmark_runner.h`: benchmark orchestration and CSV row collection.
- `png_writer.h` and `csv_writer.h`: image and benchmark output boundaries.

## Algorithms

- Recursive ray tracing with bounded depth.
- Lambertian diffuse scattering using random unit vectors.
- Metal reflection with configurable fuzz.
- Dielectric refraction with Schlick reflectance.
- Anti-aliasing via stochastic pixel sampling.
- Gamma correction with square-root tone mapping.
- Tile decomposition for responsive progress updates.
- Serial, static, and dynamic CPU scheduling.
