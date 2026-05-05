#include <metal_stdlib>

using namespace metal;

kernel void make_color_test(device uchar4* pixels [[buffer(0)]],
                            constant uint2& size [[buffer(1)]],
                            uint2 id [[thread_position_in_grid]])
{
    if (id.x >= size.x || id.y >= size.y)
    {
        return;
    }

    const uint index = id.y * size.x + id.x;
    const float2 uv = float2(id) / float2(max(size.x - 1, 1u), max(size.y - 1, 1u));
    pixels[index] = uchar4(uchar(uv.x * 255.0),
                           uchar(uv.y * 255.0),
                           uchar((1.0 - uv.x) * 220.0),
                           uchar(255));
}

kernel void grayscale(device uchar4* pixels [[buffer(0)]],
                      constant uint2& size [[buffer(1)]],
                      uint2 id [[thread_position_in_grid]])
{
    if (id.x >= size.x || id.y >= size.y)
    {
        return;
    }

    const uint index = id.y * size.x + id.x;
    const uchar4 src = pixels[index];
    const float gray = 0.299 * float(src.r) + 0.587 * float(src.g) + 0.114 * float(src.b);
    const uchar value = uchar(gray);
    pixels[index] = uchar4(value, value, value, 255);
}

kernel void gaussian_blur(const device uchar4* sourcePixels [[buffer(0)]],
                          device uchar4* destinationPixels [[buffer(1)]],
                          constant uint2& size [[buffer(2)]],
                          uint2 id [[thread_position_in_grid]])
{
    if (id.x >= size.x || id.y >= size.y)
    {
        return;
    }

    const int weights[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1},
    };

    float3 accum = float3(0.0);
    float totalWeight = 0.0;

    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            const uint sampleX = uint(clamp(int(id.x) + offsetX, 0, int(size.x) - 1));
            const uint sampleY = uint(clamp(int(id.y) + offsetY, 0, int(size.y) - 1));
            const uint sampleIndex = sampleY * size.x + sampleX;
            const float weight = float(weights[offsetY + 1][offsetX + 1]);
            const uchar4 sample = sourcePixels[sampleIndex];
            accum += float3(sample.r, sample.g, sample.b) * weight;
            totalWeight += weight;
        }
    }

    const float3 color = accum / totalWeight;
    const uint index = id.y * size.x + id.x;
    destinationPixels[index] = uchar4(uchar(color.r), uchar(color.g), uchar(color.b), uchar(255));
}
