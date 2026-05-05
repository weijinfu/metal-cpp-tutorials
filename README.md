# Metal-cpp Tutorials

这是一个Metal-cpp教程的中文网页书项目。教程主体是 `books/` 里的静态 HTML book，`src/` 只提供每本书最终参考实现，方便读者在跟做过程中对照最终结果。

## Books

- [Metal-cpp in One Weekend](books/MetalCppInOneWeekend.html): 从 compute 图像处理开始，先理解 GPU 资源和命令模型。
- [Metal-cpp: Offscreen Tiny Renderer](books/MetalCppTheNextWeek.html): 用纯 C++ 做离屏静态渲染，输出图像，并逐步比较 Lambert、Blinn-Phong 和 PBR。
- [Metal-cpp: Performance and Compute](books/MetalCppTheRestOfYourLife.html): 在第二册画质路线之后，讲 profiling、frame resources、uniform ring buffer，以及 reduction、prefix sum、blur、particle、tile binning 这些 compute 示例。

本地打开 [index.html](index.html) 可以从首页进入三本书。

## Layout

```text
index.html          # 网页书首页
books/              # 静态 HTML book
style/              # 站点和书籍 CSS
images/             # 图解和运行结果图
assets/             # 教程用 mesh 和 texture 输入文件
src/                # 每本书一个最终参考实现
```

`assets/meshes/` 里包含教程主样例用外部模型输入：UV sphere、Stanford Bunny。其他保留资产和来源记录见 [assets/meshes/README.md](assets/meshes/README.md)。

## Dependencies

需要 macOS、Xcode 或 Command Line Tools、C++17 编译器，以及 Apple 官方 `metal-cpp` 头文件。

将 Apple 官方 `metal-cpp` 头文件放到 `third_party/metal-cpp`，或在配置 CMake 时用 `-DMETAL_CPP_ROOT=/path/to/metal-cpp` 指定位置。

第二册和第三册当前都不依赖窗口或 Objective-C++。它们直接在纯 C++ 中创建离屏输出；第二册聚焦渲染质量，第三册聚焦性能和 compute 工作负载。

## Build with CMake

配置构建目录：

```sh
cmake -S . -B build
```

构建所有参考代码：

```sh
cmake --build build
```

只构建第一册最终参考代码：

```sh
cmake --build build --target MetalCppInOneWeekend
```

只构建第二册最终参考代码：

```sh
cmake --build build --target MetalCppTinyRenderer
```

只构建第三册最终参考代码：

```sh
cmake --build build --target MetalCppRenderingEngine
```

运行某个示例。第二册和第三册可以显式传入 mesh 与 texture 文件：

```sh
./build/MetalCppInOneWeekend/MetalCppInOneWeekend
./build/MetalCppTinyRenderer/MetalCppTinyRenderer assets/meshes/uv_sphere.obj assets/textures/warm_metal.ppm lambert build/MetalCppTinyRenderer/lambert.ppm
./build/MetalCppRenderingEngine/MetalCppRenderingEngine
```

第三册运行后会在 `build/MetalCppRenderingEngine/` 下生成：

- `engine-reference.ppm`
- `engine-blur.ppm`
- `engine-particles.ppm`
- `engine-tile-heatmap.ppm`
- `engine-metrics.txt`

如果 `metal-cpp` 不在默认位置，可以配置时指定：

```sh
cmake -S . -B build -DMETAL_CPP_ROOT=/path/to/metal-cpp
```

## Diagram Policy

正文里需要图解的地方都放在 `images/diagrams/`。当前仓库包含可直接阅读的 SVG 图解；需要更有插画质感的版本时，按 [images/diagrams/README.md](images/diagrams/README.md) 中列出的提示词方向用 GPT Image 生成 PNG，然后替换网页书中的引用。
