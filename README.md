# Metal-cpp Tutorials

This is an English-language Metal-cpp tutorial web book. The main tutorial content lives in the static HTML books under `books/`; `src/` provides only the final reference implementation for each book, so readers can compare their work while following along.

## Books

- [Metal-cpp in One Weekend](books/MetalCppInOneWeekend.html): Start with compute image processing to understand GPU resources and the command model.
- [Metal-cpp: Offscreen Tiny Renderer](books/MetalCppTheNextWeek.html): Build a pure C++ off-screen renderer, write images, and compare Lambert, Blinn-Phong, and PBR step by step.
- [Metal-cpp: Performance and Compute](books/MetalCppTheRestOfYourLife.html): After Book 2's rendering-quality path, learn about profiling, frame resources, uniform ring buffer, and compute examples such as reduction, prefix sum, blur, particle, and tile binning.

Open [index.html](index.html) locally to enter the three books from the home page.

## Layout

```text
index.html          # web book home page
books/              # static HTML books
style/              # site and book CSS
images/             # diagrams and runtime result images
assets/             # mesh and texture input files used by the tutorial
src/                # One final reference implementation per book
```

`assets/meshes/` contains the main tutorial examples using external model input: UV sphere, Stanford Bunny. See [assets/meshes/README.md](assets/meshes/README.md) for other retained assets and provenance records.

## Dependencies

Requires macOS, Xcode or Command Line Tools, C++17 compiler, and Apple's official `metal-cpp` header file.

Put Apple's official `metal-cpp` header file into `third_party/metal-cpp`, or use `-DMETAL_CPP_ROOT=/path/to/metal-cpp` to specify the location when configuring CMake.

Neither Book 2 nor Book 3 currently relies on a window or Objective-C++. They create off-screen output directly in pure C++; Book 2 focuses on rendering quality, and Book 3 focuses on performance and compute workloads.

## Build with CMake

Configure build directory:

```sh
cmake -S . -B build
```

Build all reference code:

```sh
cmake --build build
```

Only build the final reference code for Book 1:

```sh
cmake --build build --target MetalCppInOneWeekend
```

Only build the final reference code for Book 2:

```sh
cmake --build build --target MetalCppTinyRenderer
```

Only build the final reference code for Book 3:

```sh
cmake --build build --target MetalCppRenderingEngine
```

Run an example. Books 2 and 3 can explicitly pass in mesh and texture files:

```sh
./build/MetalCppInOneWeekend/MetalCppInOneWeekend
./build/MetalCppTinyRenderer/MetalCppTinyRenderer assets/meshes/uv_sphere.obj assets/textures/warm_metal.ppm lambert build/MetalCppTinyRenderer/lambert.ppm
./build/MetalCppRenderingEngine/MetalCppRenderingEngine
```

Book 3 generates these files under `build/MetalCppRenderingEngine/` after running:

- `engine-reference.ppm`
- `engine-blur.ppm`
- `engine-particles.ppm`
- `engine-tile-heatmap.ppm`
- `engine-metrics.txt`

If `metal-cpp` is not in the default location, it can be specified during configuration:

```sh
cmake -S . -B build -DMETAL_CPP_ROOT=/path/to/metal-cpp
```

## Diagram Policy

Wherever illustrations are needed in the text, place them in `images/diagrams/`. The current repository contains SVG diagrams that can be read directly; when you need a version with a more illustrative quality, use GPT Image to generate PNG according to the prompts listed in [images/diagrams/README.md](images/diagrams/README.md), and then replace the reference in the web book.
