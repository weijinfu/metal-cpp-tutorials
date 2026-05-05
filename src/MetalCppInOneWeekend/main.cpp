#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
struct UInt2
{
    uint32_t x;
    uint32_t y;
};

NS::String* makeString(const char* value)
{
    return NS::String::string(value, NS::UTF8StringEncoding);
}

MTL::ComputePipelineState* makePipeline(MTL::Device* device, MTL::Library* library, const char* functionName)
{
    NS::Error* error = nullptr;
    MTL::Function* function = library->newFunction(makeString(functionName));
    if (!function)
    {
        std::cerr << "Could not load compute function: " << functionName << "\n";
        return nullptr;
    }

    MTL::ComputePipelineState* pipeline = device->newComputePipelineState(function, &error);
    if (!pipeline)
    {
        std::cerr << "Could not create compute pipeline: " << functionName << "\n";
    }
    function->release();
    return pipeline;
}

void dispatch(MTL::CommandQueue* queue,
              MTL::ComputePipelineState* pipeline,
              MTL::Buffer* pixels,
              MTL::Buffer* sizeBuffer,
              uint32_t width,
              uint32_t height)
{
    MTL::CommandBuffer* commandBuffer = queue->commandBuffer();
    MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
    encoder->setComputePipelineState(pipeline);
    encoder->setBuffer(pixels, 0, 0);
    encoder->setBuffer(sizeBuffer, 0, 1);
    encoder->dispatchThreads(MTL::Size::Make(width, height, 1), MTL::Size::Make(16, 16, 1));
    encoder->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();
}

void dispatchBlur(MTL::CommandQueue* queue,
                  MTL::ComputePipelineState* pipeline,
                  MTL::Buffer* sourcePixels,
                  MTL::Buffer* destinationPixels,
                  MTL::Buffer* sizeBuffer,
                  uint32_t width,
                  uint32_t height)
{
    MTL::CommandBuffer* commandBuffer = queue->commandBuffer();
    MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
    encoder->setComputePipelineState(pipeline);
    encoder->setBuffer(sourcePixels, 0, 0);
    encoder->setBuffer(destinationPixels, 0, 1);
    encoder->setBuffer(sizeBuffer, 0, 2);
    encoder->dispatchThreads(MTL::Size::Make(width, height, 1), MTL::Size::Make(16, 16, 1));
    encoder->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();
}

void writePPM(const char* path, const uint8_t* rgba, uint32_t width, uint32_t height)
{
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t i = 0; i < width * height; ++i)
    {
        out.write(reinterpret_cast<const char*>(rgba + i * 4), 3);
    }
}
}

int main()
{
    constexpr uint32_t width = 256;
    constexpr uint32_t height = 160;
    const UInt2 size{width, height};

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
        std::cerr << "Could not load metallib: ";
        if (error)
        {
            std::cerr << error->localizedDescription()->utf8String();
        }
        std::cerr << "\n";
        device->release();
        pool->release();
        return 1;
    }

    MTL::ComputePipelineState* makeColor = makePipeline(device, library, "make_color_test");
    MTL::ComputePipelineState* makeGray = makePipeline(device, library, "grayscale");
    MTL::ComputePipelineState* makeBlur = makePipeline(device, library, "gaussian_blur");
    if (!makeColor || !makeGray || !makeBlur)
    {
        if (makeBlur)
        {
            makeBlur->release();
        }
        if (makeGray)
        {
            makeGray->release();
        }
        if (makeColor)
        {
            makeColor->release();
        }
        library->release();
        device->release();
        pool->release();
        return 1;
    }

    MTL::CommandQueue* queue = device->newCommandQueue();
    MTL::Buffer* pixels = device->newBuffer(width * height * 4, MTL::ResourceStorageModeShared);
    MTL::Buffer* blurred = device->newBuffer(width * height * 4, MTL::ResourceStorageModeShared);
    MTL::Buffer* sizeBuffer = device->newBuffer(&size, sizeof(size), MTL::ResourceStorageModeShared);

    dispatch(queue, makeColor, pixels, sizeBuffer, width, height);
    std::filesystem::create_directories("build");
    writePPM("build/color.ppm", static_cast<const uint8_t*>(pixels->contents()), width, height);

    dispatch(queue, makeGray, pixels, sizeBuffer, width, height);
    writePPM("build/grayscale.ppm", static_cast<const uint8_t*>(pixels->contents()), width, height);
    dispatchBlur(queue, makeBlur, pixels, blurred, sizeBuffer, width, height);
    writePPM("build/blur.ppm", static_cast<const uint8_t*>(blurred->contents()), width, height);
    std::cout << "Wrote build/color.ppm, build/grayscale.ppm, and build/blur.ppm\n";

    sizeBuffer->release();
    blurred->release();
    pixels->release();
    queue->release();
    makeBlur->release();
    makeGray->release();
    makeColor->release();
    library->release();
    device->release();
    pool->release();
    return 0;
}
