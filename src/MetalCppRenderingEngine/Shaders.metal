#include <metal_stdlib>

using namespace metal;

struct Vertex
{
    float4 position;
    float4 normal;
    float4 uv;
};

struct Triangle
{
    uint a;
    uint b;
    uint c;
    uint _pad;
};

struct Camera
{
    float4 origin;
    float4 lowerLeft;
    float4 horizontal;
    float4 vertical;
    float4 lightDirection;
    uint width;
    uint height;
    uint textureWidth;
    uint textureHeight;
    uint triangleCount;
    uint3 _pad;
};

struct ImageParams
{
    uint width;
    uint height;
    uint2 _pad;
};

struct ReduceParams
{
    uint pixelCount;
    uint3 _pad;
};

struct PrefixParams
{
    uint count;
    uint3 _pad;
};

struct Particle
{
    float2 position;
    float2 velocity;
};

struct ParticleParams
{
    uint particleCount;
    uint width;
    uint height;
    float dt;
};

struct TileParams
{
    uint width;
    uint height;
    uint tileSize;
    uint particleCount;
    uint tilesX;
    uint tilesY;
    uint2 _pad;
};

struct Hit
{
    bool found;
    float t;
    float3 normal;
    float2 uv;
};

bool intersectTriangle(float3 origin,
                       float3 direction,
                       Vertex v0,
                       Vertex v1,
                       Vertex v2,
                       thread float& t,
                       thread float& u,
                       thread float& v)
{
    const float3 p0 = v0.position.xyz;
    const float3 p1 = v1.position.xyz;
    const float3 p2 = v2.position.xyz;
    const float3 e1 = p1 - p0;
    const float3 e2 = p2 - p0;
    const float3 p = cross(direction, e2);
    const float det = dot(e1, p);
    if (abs(det) < 0.00001)
    {
        return false;
    }

    const float invDet = 1.0 / det;
    const float3 s = origin - p0;
    u = dot(s, p) * invDet;
    if (u < 0.0 || u > 1.0)
    {
        return false;
    }

    const float3 q = cross(s, e1);
    v = dot(direction, q) * invDet;
    if (v < 0.0 || u + v > 1.0)
    {
        return false;
    }

    t = dot(e2, q) * invDet;
    return t > 0.001;
}

kernel void render_mesh(device const Vertex* vertices [[buffer(0)]],
                        device const Triangle* triangles [[buffer(1)]],
                        device const uchar4* texturePixels [[buffer(2)]],
                        constant Camera& camera [[buffer(3)]],
                        device uchar4* output [[buffer(4)]],
                        uint2 id [[thread_position_in_grid]])
{
    if (id.x >= camera.width || id.y >= camera.height)
    {
        return;
    }

    const float2 uvScreen = (float2(id) + 0.5) / float2(camera.width, camera.height);
    const float3 origin = camera.origin.xyz;
    const float3 target = camera.lowerLeft.xyz
                        + uvScreen.x * camera.horizontal.xyz
                        + (1.0 - uvScreen.y) * camera.vertical.xyz;
    const float3 direction = normalize(target - origin);

    Hit hit;
    hit.found = false;
    hit.t = 1.0e20;
    hit.normal = float3(0.0, 0.0, 1.0);
    hit.uv = float2(0.0);

    for (uint i = 0; i < camera.triangleCount; ++i)
    {
        const Triangle tri = triangles[i];
        const Vertex v0 = vertices[tri.a];
        const Vertex v1 = vertices[tri.b];
        const Vertex v2 = vertices[tri.c];

        float t;
        float u;
        float v;
        if (intersectTriangle(origin, direction, v0, v1, v2, t, u, v) && t < hit.t)
        {
            const float w = 1.0 - u - v;
            hit.found = true;
            hit.t = t;
            hit.normal = normalize(w * v0.normal.xyz + u * v1.normal.xyz + v * v2.normal.xyz);
            hit.uv = w * v0.uv.xy + u * v1.uv.xy + v * v2.uv.xy;
        }
    }

    float3 color = float3(0.07, 0.10, 0.12) + 0.18 * float3(uvScreen.y);
    if (hit.found)
    {
        const uint tx = min(uint(fract(hit.uv.x) * camera.textureWidth), camera.textureWidth - 1);
        const uint ty = min(uint(fract(hit.uv.y) * camera.textureHeight), camera.textureHeight - 1);
        const uchar4 texel = texturePixels[ty * camera.textureWidth + tx];
        const float3 albedo = float3(texel.r, texel.g, texel.b) / 255.0;
        const float diffuse = max(dot(hit.normal, normalize(camera.lightDirection.xyz)), 0.0);
        color = albedo * (0.18 + 0.82 * diffuse);
    }

    output[id.y * camera.width + id.x] = uchar4(uchar(saturate(color.r) * 255.0),
                                                uchar(saturate(color.g) * 255.0),
                                                uchar(saturate(color.b) * 255.0),
                                                uchar(255));
}

kernel void blur_image(device const uchar4* source [[buffer(0)]],
                       device uchar4* destination [[buffer(1)]],
                       constant ImageParams& params [[buffer(2)]],
                       uint2 id [[thread_position_in_grid]])
{
    if (id.x >= params.width || id.y >= params.height)
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
            const uint sampleX = uint(clamp(int(id.x) + offsetX, 0, int(params.width) - 1));
            const uint sampleY = uint(clamp(int(id.y) + offsetY, 0, int(params.height) - 1));
            const uchar4 sample = source[sampleY * params.width + sampleX];
            const float weight = float(weights[offsetY + 1][offsetX + 1]);
            accum += float3(sample.r, sample.g, sample.b) * weight;
            totalWeight += weight;
        }
    }

    const float3 color = accum / max(totalWeight, 1.0);
    destination[id.y * params.width + id.x] = uchar4(uchar(color.r),
                                                     uchar(color.g),
                                                     uchar(color.b),
                                                     uchar(255));
}

kernel void reduce_luminance(device const uchar4* input [[buffer(0)]],
                             device float* partialSums [[buffer(1)]],
                             constant ReduceParams& params [[buffer(2)]],
                             uint gid [[thread_position_in_grid]],
                             uint tid [[thread_index_in_threadgroup]],
                             uint groupId [[threadgroup_position_in_grid]])
{
    threadgroup float scratch[256];

    float value = 0.0;
    if (gid < params.pixelCount)
    {
        const uchar4 pixel = input[gid];
        const float3 rgb = float3(pixel.r, pixel.g, pixel.b) / 255.0;
        value = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    }
    scratch[tid] = value;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (tid < stride)
        {
            scratch[tid] += scratch[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (tid == 0)
    {
        partialSums[groupId] = scratch[0];
    }
}

kernel void prefix_sum_16(device const uint* input [[buffer(0)]],
                          device uint* output [[buffer(1)]],
                          constant PrefixParams& params [[buffer(2)]],
                          uint tid [[thread_index_in_threadgroup]])
{
    threadgroup uint scratch[16];
    if (tid >= params.count)
    {
        return;
    }

    scratch[tid] = input[tid];
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint offset = 1; offset < params.count; offset <<= 1)
    {
        uint value = scratch[tid];
        if (tid >= offset)
        {
            value += scratch[tid - offset];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        scratch[tid] = value;
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    output[tid] = (tid == 0) ? 0 : scratch[tid - 1];
}

kernel void particle_step(device const Particle* currentParticles [[buffer(0)]],
                          device Particle* nextParticles [[buffer(1)]],
                          constant ParticleParams& params [[buffer(2)]],
                          uint id [[thread_position_in_grid]])
{
    if (id >= params.particleCount)
    {
        return;
    }

    Particle particle = currentParticles[id];
    particle.position += particle.velocity * params.dt;

    if (particle.position.x < 0.05 || particle.position.x > 0.95)
    {
        particle.velocity.x *= -1.0;
        particle.position.x = clamp(particle.position.x, 0.05f, 0.95f);
    }
    if (particle.position.y < 0.08 || particle.position.y > 0.92)
    {
        particle.velocity.y *= -1.0;
        particle.position.y = clamp(particle.position.y, 0.08f, 0.92f);
    }

    nextParticles[id] = particle;
}

kernel void rasterize_particles(device const Particle* particles [[buffer(0)]],
                                constant ParticleParams& params [[buffer(1)]],
                                device uchar4* output [[buffer(2)]],
                                uint2 id [[thread_position_in_grid]])
{
    if (id.x >= params.width || id.y >= params.height)
    {
        return;
    }

    const float2 pixel = float2(id) + 0.5;
    float3 color = float3(0.03, 0.05, 0.08);
    float glow = 0.0;

    for (uint i = 0; i < params.particleCount; ++i)
    {
        const float2 particlePixel = particles[i].position * float2(params.width, params.height);
        const float2 delta = (pixel - particlePixel) / float2(params.width, params.height);
        const float dist2 = dot(delta, delta);
        glow += exp(-dist2 * 1600.0);
    }

    color += float3(0.12, 0.25, 0.60) * glow;
    color += float3(0.90, 0.70, 0.28) * pow(min(glow, 1.0), 1.5);
    color = saturate(color);

    output[id.y * params.width + id.x] = uchar4(uchar(color.r * 255.0),
                                                uchar(color.g * 255.0),
                                                uchar(color.b * 255.0),
                                                uchar(255));
}

kernel void tile_bin_particles(device const Particle* particles [[buffer(0)]],
                               constant TileParams& params [[buffer(1)]],
                               device atomic_uint* tileCounts [[buffer(2)]],
                               uint id [[thread_position_in_grid]])
{
    if (id >= params.particleCount)
    {
        return;
    }

    const float2 position = particles[id].position;
    const uint pixelX = min(uint(position.x * params.width), params.width - 1);
    const uint pixelY = min(uint(position.y * params.height), params.height - 1);
    const uint tileX = min(pixelX / params.tileSize, params.tilesX - 1);
    const uint tileY = min(pixelY / params.tileSize, params.tilesY - 1);
    const uint tileIndex = tileY * params.tilesX + tileX;
    atomic_fetch_add_explicit(&tileCounts[tileIndex], 1u, memory_order_relaxed);
}
