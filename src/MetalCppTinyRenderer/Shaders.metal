#include <metal_stdlib>

using namespace metal;

struct Vertex
{
    packed_float3 position;
    packed_float3 normal;
    float2 uv;
};

struct Uniforms
{
    float4x4 mvp;
    float4x4 model;
    float4 cameraPosition;
    float4 lightDirection;
    float metallic;
    float roughness;
    float ambient;
    float _pad;
};

struct VertexOut
{
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
    float2 uv;
};

vertex VertexOut vertex_main(uint vertexID [[vertex_id]],
                             device const Vertex* vertices [[buffer(0)]],
                             constant Uniforms& uniforms [[buffer(1)]])
{
    const Vertex vtx = vertices[vertexID];
    VertexOut out;
    const float3 position = float3(vtx.position);
    const float3 normal = float3(vtx.normal);
    out.position = uniforms.mvp * float4(position, 1.0);
    out.worldPosition = (uniforms.model * float4(position, 1.0)).xyz;
    out.normal = normalize((uniforms.model * float4(normal, 0.0)).xyz);
    out.uv = vtx.uv;
    return out;
}

float distributionGGX(float NdotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(M_PI_F * d * d, 1.0e-5f);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1.0e-5f);
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 tonemapACES(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

float3 srgbToLinear(float3 color)
{
    return pow(clamp(color, 0.0, 1.0), float3(2.2));
}

float3 sampleWorldNormal(VertexOut in,
                         texture2d<float> normalMap,
                         sampler baseSampler)
{
    const float3 tangentNormal = normalMap.sample(baseSampler, in.uv).xyz * 2.0 - 1.0;
    const float3 geometricNormal = normalize(in.normal);
    const float3 dp1 = dfdx(in.worldPosition);
    const float3 dp2 = dfdy(in.worldPosition);
    const float2 duv1 = dfdx(in.uv);
    const float2 duv2 = dfdy(in.uv);
    const float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) < 1.0e-5)
    {
        return geometricNormal;
    }

    const float invDet = 1.0 / det;
    float3 tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) * invDet);
    float3 bitangent = normalize((dp2 * duv1.x - dp1 * duv2.x) * invDet);
    if (!all(isfinite(tangent)) || !all(isfinite(bitangent)))
    {
        return geometricNormal;
    }

    const float3x3 tbn = float3x3(tangent, bitangent, geometricNormal);
    return normalize(tbn * tangentNormal);
}

fragment float4 fragment_albedo(VertexOut in [[stage_in]],
                                texture2d<float> baseColor [[texture(0)]],
                                texture2d<float> roughnessMap [[texture(1)]],
                                texture2d<float> metallicMap [[texture(2)]],
                                texture2d<float> normalMap [[texture(3)]],
                                texture2d<float> aoMap [[texture(4)]],
                                sampler baseSampler [[sampler(0)]],
                                constant Uniforms& uniforms [[buffer(1)]])
{
    const float3 albedo = baseColor.sample(baseSampler, in.uv).rgb;
    return float4(albedo, 1.0);
}

fragment float4 fragment_lambert(VertexOut in [[stage_in]],
                                 texture2d<float> baseColor [[texture(0)]],
                                 texture2d<float> roughnessMap [[texture(1)]],
                                 texture2d<float> metallicMap [[texture(2)]],
                                 texture2d<float> normalMap [[texture(3)]],
                                 texture2d<float> aoMap [[texture(4)]],
                                 sampler baseSampler [[sampler(0)]],
                                 constant Uniforms& uniforms [[buffer(1)]])
{
    const float3 albedo = baseColor.sample(baseSampler, in.uv).rgb;
    const float3 N = sampleWorldNormal(in, normalMap, baseSampler);
    const float3 L = normalize(uniforms.lightDirection.xyz);
    const float diffuse = max(dot(N, L), 0.0);
    const float sky = 0.35 + 0.65 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    const float3 color = albedo * (0.10 + 0.65 * diffuse + 0.25 * sky);
    return float4(color, 1.0);
}

fragment float4 fragment_blinn_phong(VertexOut in [[stage_in]],
                                     texture2d<float> baseColor [[texture(0)]],
                                     texture2d<float> roughnessMap [[texture(1)]],
                                     texture2d<float> metallicMap [[texture(2)]],
                                     texture2d<float> normalMap [[texture(3)]],
                                     texture2d<float> aoMap [[texture(4)]],
                                     sampler baseSampler [[sampler(0)]],
                                     constant Uniforms& uniforms [[buffer(1)]])
{
    const float3 albedo = baseColor.sample(baseSampler, in.uv).rgb;
    const float3 N = sampleWorldNormal(in, normalMap, baseSampler);
    const float3 L = normalize(uniforms.lightDirection.xyz);
    const float3 V = normalize(uniforms.cameraPosition.xyz - in.worldPosition);
    const float3 H = normalize(L + V);
    const float diffuse = max(dot(N, L), 0.0);
    const float sky = 0.35 + 0.65 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    const float specular = pow(max(dot(N, H), 0.0), 48.0);
    const float3 color = albedo * (0.08 + 0.55 * diffuse + 0.22 * sky) + specular * 0.28;
    return float4(color, 1.0);
}

fragment float4 fragment_pbr(VertexOut in [[stage_in]],
                             texture2d<float> baseColor [[texture(0)]],
                             texture2d<float> roughnessMap [[texture(1)]],
                             texture2d<float> metallicMap [[texture(2)]],
                             texture2d<float> normalMap [[texture(3)]],
                             texture2d<float> aoMap [[texture(4)]],
                             sampler baseSampler [[sampler(0)]],
                             constant Uniforms& uniforms [[buffer(1)]])
{
    const float3 albedo = srgbToLinear(baseColor.sample(baseSampler, in.uv).rgb);
    const float roughness = clamp(roughnessMap.sample(baseSampler, in.uv).r, 0.04, 1.0);
    const float metallic = clamp(metallicMap.sample(baseSampler, in.uv).r, 0.0, 1.0);
    const float ao = clamp(aoMap.sample(baseSampler, in.uv).r, 0.0, 1.0);
    const float3 N = sampleWorldNormal(in, normalMap, baseSampler);
    const float3 L = normalize(uniforms.lightDirection.xyz);
    const float3 V = normalize(uniforms.cameraPosition.xyz - in.worldPosition);
    const float3 H = normalize(L + V);
    const float3 R = reflect(-V, N);

    const float NdotL = max(dot(N, L), 0.0);
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotH = max(dot(N, H), 0.0);
    const float VdotH = max(dot(V, H), 0.0);

    const float D = distributionGGX(NdotH, roughness);
    const float G = geometrySchlickGGX(NdotV, roughness)
                  * geometrySchlickGGX(NdotL, roughness);
    const float3 F0 = mix(float3(0.04), albedo, metallic);
    const float3 F = fresnelSchlick(VdotH, F0);
    const float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1.0e-5);
    const float3 kD = (1.0 - F) * (1.0 - metallic);
    const float3 diffuse = kD * albedo / M_PI_F;
    const float skyMix = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    const float3 skyColor = mix(float3(0.18, 0.16, 0.14), float3(0.85, 0.92, 1.0), skyMix);
    const float groundMix = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
    const float3 envSpecular = mix(float3(0.16, 0.12, 0.08), skyColor, groundMix) * fresnelSchlick(NdotV, F0);
    const float3 ambientDiffuse = albedo * (0.18 + 0.22 * skyMix) * (1.0 - metallic) * ao;
    float3 color = (diffuse + specular) * NdotL;
    color += ambientDiffuse * uniforms.ambient;
    color += envSpecular * (0.25 + 0.35 * (1.0 - roughness)) * ao;
    color = tonemapACES(color);
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}
