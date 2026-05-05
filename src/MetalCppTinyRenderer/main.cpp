#include <filesystem>
#include <iostream>
#include <string>

struct RenderOptions
{
    std::filesystem::path meshPath;
    std::filesystem::path texturePath;
    std::filesystem::path outputPath;
    std::string stage;
    uint32_t width = 640;
    uint32_t height = 400;
};

bool renderScene(const RenderOptions& options);

namespace
{
std::filesystem::path defaultOutputPath(const std::string& stage)
{
    return std::filesystem::path("build/MetalCppTinyRenderer") / (stage + ".png");
}
}

int main(int argc, char** argv)
{
    RenderOptions options;
    options.meshPath = argc > 1 ? argv[1] : "assets/meshes/uv_sphere.obj";
    options.texturePath = argc > 2 ? argv[2] : "assets/textures/warm_metal.ppm";
    options.stage = argc > 3 ? argv[3] : "lambert";
    options.outputPath = argc > 4 ? argv[4] : defaultOutputPath(options.stage);

    if (!renderScene(options))
    {
        std::cerr << "Usage: MetalCppTinyRenderer [mesh.obj] [texture.(ppm|png|jpg|jpeg)] [albedo|lambert|blinn|pbr] [output.(ppm|png|jpg|jpeg)]\n";
        return 1;
    }

    std::cout << "Wrote " << options.outputPath << "\n";
    return 0;
}
