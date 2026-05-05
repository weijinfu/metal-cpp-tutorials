#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct Float2
{
    float x;
    float y;
};

struct Float4
{
    float x;
    float y;
    float z;
    float w;
};

struct Vertex
{
    Float4 position;
    Float4 normal;
    Float4 uv;
};

struct Triangle
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t _pad;
};

struct Camera
{
    Float4 origin;
    Float4 lowerLeft;
    Float4 horizontal;
    Float4 vertical;
    Float4 lightDirection;
    uint32_t width;
    uint32_t height;
    uint32_t textureWidth;
    uint32_t textureHeight;
    uint32_t triangleCount;
    uint32_t _pad[3];
};

struct ImageParams
{
    uint32_t width;
    uint32_t height;
    uint32_t _pad[2];
};

struct ReduceParams
{
    uint32_t pixelCount;
    uint32_t _pad[3];
};

struct PrefixParams
{
    uint32_t count;
    uint32_t _pad[3];
};

struct Particle
{
    Float2 position;
    Float2 velocity;
};

struct ParticleParams
{
    uint32_t particleCount;
    uint32_t width;
    uint32_t height;
    float dt;
};

struct TileParams
{
    uint32_t width;
    uint32_t height;
    uint32_t tileSize;
    uint32_t particleCount;
    uint32_t tilesX;
    uint32_t tilesY;
    uint32_t _pad[2];
};

struct RingSlice
{
    uint32_t offset = 0;
    void* cpuPointer = nullptr;
};

struct FrameResources
{
    MTL::Buffer* uniformRing = nullptr;
    MTL::Buffer* renderOutput = nullptr;
    MTL::Buffer* blurOutput = nullptr;
    MTL::Buffer* particlePreview = nullptr;
    MTL::Buffer* partialSums = nullptr;
    MTL::Buffer* prefixInput = nullptr;
    MTL::Buffer* prefixOutput = nullptr;
    MTL::Buffer* tileCounts = nullptr;
    MTL::Buffer* particleA = nullptr;
    MTL::Buffer* particleB = nullptr;
    uint32_t ringHead = 0;
};

struct Pipelines
{
    MTL::ComputePipelineState* render = nullptr;
    MTL::ComputePipelineState* blur = nullptr;
    MTL::ComputePipelineState* reduction = nullptr;
    MTL::ComputePipelineState* prefix = nullptr;
    MTL::ComputePipelineState* particleStep = nullptr;
    MTL::ComputePipelineState* particleRaster = nullptr;
    MTL::ComputePipelineState* tile = nullptr;
};

struct Metrics
{
    double cpuEncodeMs = 0.0;
    double cpuWaitMs = 0.0;
    double cpuFrameMs = 0.0;
    double gpuMs = 0.0;
    double kernelMs = 0.0;
    double averageLuminance = 0.0;
    std::array<uint32_t, 16> prefixInput{};
    std::array<uint32_t, 16> prefixOutput{};
    std::vector<uint32_t> tileCounts;
};

constexpr uint32_t kWidth = 512;
constexpr uint32_t kHeight = 320;
constexpr uint32_t kTextureWidth = 4;
constexpr uint32_t kTextureHeight = 4;
constexpr uint32_t kThreadgroup1D = 256;
constexpr uint32_t kImageThreadsX = 16;
constexpr uint32_t kImageThreadsY = 8;
constexpr uint32_t kParticleThreads = 64;
constexpr uint32_t kPrefixCount = 16;
constexpr uint32_t kParticleCount = 96;
constexpr uint32_t kParticleSteps = 24;
constexpr uint32_t kTileSize = 32;
constexpr uint32_t kFrameCount = 2;
constexpr uint32_t kRingBufferBytes = 4096;

NS::String* makeString(const char* value)
{
    return NS::String::string(value, NS::UTF8StringEncoding);
}

MTL::ComputePipelineState* makePipeline(MTL::Device* device, MTL::Library* library, const char* name)
{
    NS::Error* error = nullptr;
    MTL::Function* function = library->newFunction(makeString(name));
    if (!function)
    {
        std::cerr << "Could not load compute function: " << name << "\n";
        return nullptr;
    }

    MTL::ComputePipelineState* pipeline = device->newComputePipelineState(function, &error);
    if (!pipeline)
    {
        std::cerr << "Could not create compute pipeline: " << name << "\n";
        if (error)
        {
            std::cerr << error->localizedDescription()->utf8String() << "\n";
        }
    }
    function->release();
    return pipeline;
}

Pipelines makePipelines(MTL::Device* device, MTL::Library* library)
{
    Pipelines pipelines;
    pipelines.render = makePipeline(device, library, "render_mesh");
    pipelines.blur = makePipeline(device, library, "blur_image");
    pipelines.reduction = makePipeline(device, library, "reduce_luminance");
    pipelines.prefix = makePipeline(device, library, "prefix_sum_16");
    pipelines.particleStep = makePipeline(device, library, "particle_step");
    pipelines.particleRaster = makePipeline(device, library, "rasterize_particles");
    pipelines.tile = makePipeline(device, library, "tile_bin_particles");
    return pipelines;
}

bool pipelinesReady(const Pipelines& pipelines)
{
    return pipelines.render
        && pipelines.blur
        && pipelines.reduction
        && pipelines.prefix
        && pipelines.particleStep
        && pipelines.particleRaster
        && pipelines.tile;
}

void releasePipelines(Pipelines& pipelines)
{
    if (pipelines.tile) pipelines.tile->release();
    if (pipelines.particleRaster) pipelines.particleRaster->release();
    if (pipelines.particleStep) pipelines.particleStep->release();
    if (pipelines.prefix) pipelines.prefix->release();
    if (pipelines.reduction) pipelines.reduction->release();
    if (pipelines.blur) pipelines.blur->release();
    if (pipelines.render) pipelines.render->release();
    pipelines = {};
}

void writePPM(const std::filesystem::path& path, const uint8_t* rgba, uint32_t width, uint32_t height)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t i = 0; i < width * height; ++i)
    {
        out.write(reinterpret_cast<const char*>(rgba + i * 4), 3);
    }
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

uint32_t align256(uint32_t value)
{
    return (value + 255u) & ~255u;
}

RingSlice allocateRing(FrameResources& frame, uint32_t bytes)
{
    const uint32_t alignedBytes = align256(bytes);
    if (frame.ringHead + alignedBytes > kRingBufferBytes)
    {
        throw std::runtime_error("Uniform ring buffer overflow.");
    }

    RingSlice slice;
    slice.offset = frame.ringHead;
    slice.cpuPointer = static_cast<uint8_t*>(frame.uniformRing->contents()) + frame.ringHead;
    frame.ringHead += alignedBytes;
    return slice;
}

void clearBuffer(MTL::Buffer* buffer, size_t bytes)
{
    std::memset(buffer->contents(), 0, bytes);
}

MTL::Size makeImageThreadgroup()
{
    return MTL::Size::Make(kImageThreadsX, kImageThreadsY, 1);
}

MTL::Size makeLinearThreadgroup(uint32_t width)
{
    return MTL::Size::Make(width, 1, 1);
}

std::array<Particle, kParticleCount> makeParticles()
{
    std::array<Particle, kParticleCount> particles{};
    for (uint32_t i = 0; i < kParticleCount; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kParticleCount);
        const float angle = t * 6.283185307f;
        const float radius = 0.18f + 0.22f * std::fmod(t * 7.0f, 1.0f);
        particles[i].position = {
            0.5f + std::cos(angle) * radius,
            0.5f + std::sin(angle) * radius * 0.62f,
        };
        particles[i].velocity = {
            0.18f * std::cos(angle + 1.5707963f),
            0.16f * std::sin(angle + 1.5707963f),
        };
    }
    return particles;
}

std::array<uint32_t, kPrefixCount> makePrefixFlags()
{
    return {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1};
}

std::vector<uint8_t> makeTileHeatmap(const std::vector<uint32_t>& counts,
                                     uint32_t tilesX,
                                     uint32_t tilesY,
                                     uint32_t tileSize)
{
    const uint32_t width = tilesX * tileSize;
    const uint32_t height = tilesY * tileSize;
    std::vector<uint8_t> rgba(width * height * 4, 255);
    uint32_t maxCount = 1;
    for (uint32_t count : counts)
    {
        maxCount = std::max(maxCount, count);
    }

    for (uint32_t tileY = 0; tileY < tilesY; ++tileY)
    {
        for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
        {
            const uint32_t count = counts[tileY * tilesX + tileX];
            const float t = static_cast<float>(count) / static_cast<float>(maxCount);
            const uint8_t r = static_cast<uint8_t>(18.0f + 237.0f * t);
            const uint8_t g = static_cast<uint8_t>(22.0f + 190.0f * (1.0f - std::abs(t - 0.45f)));
            const uint8_t b = static_cast<uint8_t>(34.0f + 210.0f * (1.0f - t));
            for (uint32_t y = 0; y < tileSize; ++y)
            {
                for (uint32_t x = 0; x < tileSize; ++x)
                {
                    const uint32_t px = tileX * tileSize + x;
                    const uint32_t py = tileY * tileSize + y;
                    const size_t index = (static_cast<size_t>(py) * width + px) * 4;
                    const bool border = (x == 0 || y == 0 || x + 1 == tileSize || y + 1 == tileSize);
                    rgba[index + 0] = border ? 250 : r;
                    rgba[index + 1] = border ? 250 : g;
                    rgba[index + 2] = border ? 250 : b;
                    rgba[index + 3] = 255;
                }
            }
        }
    }
    return rgba;
}

std::string buildMetricsText(const Metrics& metrics, uint32_t tilesX, uint32_t tilesY)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "cpu_encode_ms: " << metrics.cpuEncodeMs << "\n";
    out << "cpu_wait_ms: " << metrics.cpuWaitMs << "\n";
    out << "cpu_frame_ms: " << metrics.cpuFrameMs << "\n";
    out << "gpu_frame_ms: " << metrics.gpuMs << "\n";
    out << "gpu_kernel_ms: " << metrics.kernelMs << "\n";
    out << "average_luminance: " << metrics.averageLuminance << "\n\n";

    out << "prefix_input:  ";
    for (uint32_t value : metrics.prefixInput)
    {
        out << value << ' ';
    }
    out << "\n";

    out << "prefix_output: ";
    for (uint32_t value : metrics.prefixOutput)
    {
        out << value << ' ';
    }
    out << "\n\n";

    out << "tile_counts:\n";
    for (uint32_t y = 0; y < tilesY; ++y)
    {
        for (uint32_t x = 0; x < tilesX; ++x)
        {
            out << std::setw(3) << metrics.tileCounts[y * tilesX + x] << ' ';
        }
        out << "\n";
    }
    return out.str();
}
}

int main()
{
    const std::array<Vertex, 4> vertices = {{
        {{-1.25f, -0.75f, -2.4f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{ 1.25f, -0.75f, -2.4f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{ 1.25f,  0.75f, -2.4f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}},
        {{-1.25f,  0.75f, -2.4f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
    }};
    const std::array<Triangle, 2> triangles = {{
        {0, 1, 2, 0},
        {0, 2, 3, 0},
    }};
    const std::array<uint8_t, kTextureWidth * kTextureHeight * 4> texture = {
        238, 196, 105, 255,  53, 104, 130, 255, 238, 196, 105, 255,  53, 104, 130, 255,
         53, 104, 130, 255, 238, 196, 105, 255,  53, 104, 130, 255, 238, 196, 105, 255,
        238, 196, 105, 255,  53, 104, 130, 255, 238, 196, 105, 255,  53, 104, 130, 255,
         53, 104, 130, 255, 238, 196, 105, 255,  53, 104, 130, 255, 238, 196, 105, 255,
    };
    const Camera camera = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {-1.6f, -1.0f, -1.0f, 0.0f},
        { 3.2f,  0.0f,  0.0f, 0.0f},
        { 0.0f,  2.0f,  0.0f, 0.0f},
        {-0.35f, 0.65f, 0.68f, 0.0f},
        kWidth,
        kHeight,
        kTextureWidth,
        kTextureHeight,
        static_cast<uint32_t>(triangles.size()),
        {0, 0, 0},
    };

    const auto particles = makeParticles();
    const auto prefixFlags = makePrefixFlags();
    const uint32_t pixelCount = kWidth * kHeight;
    const uint32_t partialCount = (pixelCount + kThreadgroup1D - 1) / kThreadgroup1D;
    const uint32_t tilesX = (kWidth + kTileSize - 1) / kTileSize;
    const uint32_t tilesY = (kHeight + kTileSize - 1) / kTileSize;
    const uint32_t tileCount = tilesX * tilesY;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device)
    {
        std::cerr << "Metal is not available on this Mac.\n";
        pool->release();
        return 1;
    }

    NS::Error* error = nullptr;
    MTL::Library* library = device->newLibrary(makeString(METALLIB_PATH), &error);
    if (!library)
    {
        std::cerr << "Could not load metallib.\n";
        if (error)
        {
            std::cerr << error->localizedDescription()->utf8String() << "\n";
        }
        device->release();
        pool->release();
        return 1;
    }

    Pipelines pipelines = makePipelines(device, library);
    if (!pipelinesReady(pipelines))
    {
        releasePipelines(pipelines);
        library->release();
        device->release();
        pool->release();
        return 1;
    }

    MTL::Buffer* vertexBuffer = device->newBuffer(vertices.data(), sizeof(Vertex) * vertices.size(), MTL::ResourceStorageModeShared);
    MTL::Buffer* triangleBuffer = device->newBuffer(triangles.data(), sizeof(Triangle) * triangles.size(), MTL::ResourceStorageModeShared);
    MTL::Buffer* textureBuffer = device->newBuffer(texture.data(), texture.size(), MTL::ResourceStorageModeShared);

    std::array<FrameResources, kFrameCount> frames{};
    for (FrameResources& frame : frames)
    {
        frame.uniformRing = device->newBuffer(kRingBufferBytes, MTL::ResourceStorageModeShared);
        frame.renderOutput = device->newBuffer(pixelCount * 4, MTL::ResourceStorageModeShared);
        frame.blurOutput = device->newBuffer(pixelCount * 4, MTL::ResourceStorageModeShared);
        frame.particlePreview = device->newBuffer(pixelCount * 4, MTL::ResourceStorageModeShared);
        frame.partialSums = device->newBuffer(partialCount * sizeof(float), MTL::ResourceStorageModeShared);
        frame.prefixInput = device->newBuffer(kPrefixCount * sizeof(uint32_t), MTL::ResourceStorageModeShared);
        frame.prefixOutput = device->newBuffer(kPrefixCount * sizeof(uint32_t), MTL::ResourceStorageModeShared);
        frame.tileCounts = device->newBuffer(tileCount * sizeof(uint32_t), MTL::ResourceStorageModeShared);
        frame.particleA = device->newBuffer(sizeof(Particle) * kParticleCount, MTL::ResourceStorageModeShared);
        frame.particleB = device->newBuffer(sizeof(Particle) * kParticleCount, MTL::ResourceStorageModeShared);
        frame.ringHead = 0;
    }

    MTL::CommandQueue* queue = device->newCommandQueue();
    Metrics metrics{};

    for (uint32_t frameIndex = 0; frameIndex < kFrameCount; ++frameIndex)
    {
        FrameResources& frame = frames[frameIndex % kFrameCount];
        frame.ringHead = 0;
        std::memcpy(frame.prefixInput->contents(), prefixFlags.data(), prefixFlags.size() * sizeof(uint32_t));
        std::memcpy(frame.particleA->contents(), particles.data(), particles.size() * sizeof(Particle));
        clearBuffer(frame.renderOutput, pixelCount * 4);
        clearBuffer(frame.blurOutput, pixelCount * 4);
        clearBuffer(frame.particlePreview, pixelCount * 4);
        clearBuffer(frame.partialSums, partialCount * sizeof(float));
        clearBuffer(frame.prefixOutput, kPrefixCount * sizeof(uint32_t));
        clearBuffer(frame.tileCounts, tileCount * sizeof(uint32_t));

        const RingSlice cameraSlice = allocateRing(frame, sizeof(Camera));
        std::memcpy(cameraSlice.cpuPointer, &camera, sizeof(Camera));

        const ImageParams imageParams{kWidth, kHeight, 0, 0};
        const RingSlice imageSlice = allocateRing(frame, sizeof(ImageParams));
        std::memcpy(imageSlice.cpuPointer, &imageParams, sizeof(ImageParams));

        const ReduceParams reduceParams{pixelCount, {0, 0, 0}};
        const RingSlice reduceSlice = allocateRing(frame, sizeof(ReduceParams));
        std::memcpy(reduceSlice.cpuPointer, &reduceParams, sizeof(ReduceParams));

        const PrefixParams prefixParams{kPrefixCount, {0, 0, 0}};
        const RingSlice prefixSlice = allocateRing(frame, sizeof(PrefixParams));
        std::memcpy(prefixSlice.cpuPointer, &prefixParams, sizeof(PrefixParams));

        const ParticleParams particleParams{kParticleCount, kWidth, kHeight, 1.0f / 120.0f};
        const RingSlice particleSlice = allocateRing(frame, sizeof(ParticleParams));
        std::memcpy(particleSlice.cpuPointer, &particleParams, sizeof(ParticleParams));

        const TileParams tileParams{kWidth, kHeight, kTileSize, kParticleCount, tilesX, tilesY, {0, 0}};
        const RingSlice tileSlice = allocateRing(frame, sizeof(TileParams));
        std::memcpy(tileSlice.cpuPointer, &tileParams, sizeof(TileParams));

        const auto cpuFrameStart = std::chrono::high_resolution_clock::now();
        MTL::CommandBuffer* commandBuffer = queue->commandBuffer();

        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.render);
            encoder->setBuffer(vertexBuffer, 0, 0);
            encoder->setBuffer(triangleBuffer, 0, 1);
            encoder->setBuffer(textureBuffer, 0, 2);
            encoder->setBuffer(frame.uniformRing, cameraSlice.offset, 3);
            encoder->setBuffer(frame.renderOutput, 0, 4);
            encoder->dispatchThreads(MTL::Size::Make(kWidth, kHeight, 1), makeImageThreadgroup());
            encoder->endEncoding();
        }

        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.reduction);
            encoder->setBuffer(frame.renderOutput, 0, 0);
            encoder->setBuffer(frame.partialSums, 0, 1);
            encoder->setBuffer(frame.uniformRing, reduceSlice.offset, 2);
            encoder->dispatchThreads(MTL::Size::Make(partialCount * kThreadgroup1D, 1, 1),
                                     makeLinearThreadgroup(kThreadgroup1D));
            encoder->endEncoding();
        }

        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.blur);
            encoder->setBuffer(frame.renderOutput, 0, 0);
            encoder->setBuffer(frame.blurOutput, 0, 1);
            encoder->setBuffer(frame.uniformRing, imageSlice.offset, 2);
            encoder->dispatchThreads(MTL::Size::Make(kWidth, kHeight, 1), makeImageThreadgroup());
            encoder->endEncoding();
        }

        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.prefix);
            encoder->setBuffer(frame.prefixInput, 0, 0);
            encoder->setBuffer(frame.prefixOutput, 0, 1);
            encoder->setBuffer(frame.uniformRing, prefixSlice.offset, 2);
            encoder->dispatchThreads(MTL::Size::Make(kPrefixCount, 1, 1), makeLinearThreadgroup(kPrefixCount));
            encoder->endEncoding();
        }

        MTL::Buffer* particleSource = frame.particleA;
        MTL::Buffer* particleTarget = frame.particleB;
        for (uint32_t step = 0; step < kParticleSteps; ++step)
        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.particleStep);
            encoder->setBuffer(particleSource, 0, 0);
            encoder->setBuffer(particleTarget, 0, 1);
            encoder->setBuffer(frame.uniformRing, particleSlice.offset, 2);
            encoder->dispatchThreads(MTL::Size::Make(kParticleCount, 1, 1), makeLinearThreadgroup(kParticleThreads));
            encoder->endEncoding();
            std::swap(particleSource, particleTarget);
        }

        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.particleRaster);
            encoder->setBuffer(particleSource, 0, 0);
            encoder->setBuffer(frame.uniformRing, particleSlice.offset, 1);
            encoder->setBuffer(frame.particlePreview, 0, 2);
            encoder->dispatchThreads(MTL::Size::Make(kWidth, kHeight, 1), makeImageThreadgroup());
            encoder->endEncoding();
        }

        {
            MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
            encoder->setComputePipelineState(pipelines.tile);
            encoder->setBuffer(particleSource, 0, 0);
            encoder->setBuffer(frame.uniformRing, tileSlice.offset, 1);
            encoder->setBuffer(frame.tileCounts, 0, 2);
            encoder->dispatchThreads(MTL::Size::Make(kParticleCount, 1, 1), makeLinearThreadgroup(kParticleThreads));
            encoder->endEncoding();
        }

        const auto cpuEncodeEnd = std::chrono::high_resolution_clock::now();
        commandBuffer->commit();
        const auto cpuWaitStart = std::chrono::high_resolution_clock::now();
        commandBuffer->waitUntilCompleted();
        const auto cpuFrameEnd = std::chrono::high_resolution_clock::now();

        metrics.cpuEncodeMs = std::chrono::duration<double, std::milli>(cpuEncodeEnd - cpuFrameStart).count();
        metrics.cpuWaitMs = std::chrono::duration<double, std::milli>(cpuFrameEnd - cpuWaitStart).count();
        metrics.cpuFrameMs = std::chrono::duration<double, std::milli>(cpuFrameEnd - cpuFrameStart).count();

        const double gpuStart = commandBuffer->GPUStartTime();
        const double gpuEnd = commandBuffer->GPUEndTime();
        const double kernelStart = commandBuffer->kernelStartTime();
        const double kernelEnd = commandBuffer->kernelEndTime();
        metrics.gpuMs = gpuEnd > gpuStart ? (gpuEnd - gpuStart) * 1000.0 : 0.0;
        metrics.kernelMs = kernelEnd > kernelStart ? (kernelEnd - kernelStart) * 1000.0 : 0.0;

        const float* partialSums = static_cast<const float*>(frame.partialSums->contents());
        double luminanceSum = 0.0;
        for (uint32_t i = 0; i < partialCount; ++i)
        {
            luminanceSum += partialSums[i];
        }
        metrics.averageLuminance = luminanceSum / static_cast<double>(pixelCount);

        metrics.prefixInput = prefixFlags;
        std::memcpy(metrics.prefixOutput.data(), frame.prefixOutput->contents(), metrics.prefixOutput.size() * sizeof(uint32_t));

        metrics.tileCounts.resize(tileCount);
        std::memcpy(metrics.tileCounts.data(), frame.tileCounts->contents(), tileCount * sizeof(uint32_t));

        if (frameIndex == kFrameCount - 1)
        {
            const std::filesystem::path outputDir = "build/MetalCppRenderingEngine";
            writePPM(outputDir / "engine-reference.ppm",
                     static_cast<const uint8_t*>(frame.renderOutput->contents()),
                     kWidth,
                     kHeight);
            writePPM(outputDir / "engine-blur.ppm",
                     static_cast<const uint8_t*>(frame.blurOutput->contents()),
                     kWidth,
                     kHeight);
            writePPM(outputDir / "engine-particles.ppm",
                     static_cast<const uint8_t*>(frame.particlePreview->contents()),
                     kWidth,
                     kHeight);
            const std::vector<uint8_t> tileHeatmap = makeTileHeatmap(metrics.tileCounts, tilesX, tilesY, kTileSize);
            writePPM(outputDir / "engine-tile-heatmap.ppm", tileHeatmap.data(), tilesX * kTileSize, tilesY * kTileSize);
            writeText(outputDir / "engine-metrics.txt", buildMetricsText(metrics, tilesX, tilesY));
        }

        commandBuffer->release();
    }

    std::cout << "Wrote build/MetalCppRenderingEngine/engine-reference.ppm\n";
    std::cout << "Wrote build/MetalCppRenderingEngine/engine-blur.ppm\n";
    std::cout << "Wrote build/MetalCppRenderingEngine/engine-particles.ppm\n";
    std::cout << "Wrote build/MetalCppRenderingEngine/engine-tile-heatmap.ppm\n";
    std::cout << "Wrote build/MetalCppRenderingEngine/engine-metrics.txt\n";
    return 0;
}
