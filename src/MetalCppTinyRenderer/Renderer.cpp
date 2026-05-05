#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct RenderOptions
{
    std::filesystem::path meshPath;
    std::filesystem::path texturePath;
    std::filesystem::path outputPath;
    std::string stage;
    uint32_t width = 640;
    uint32_t height = 400;
};

namespace
{
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4
{
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
};

struct Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

struct Uniforms
{
    Mat4 mvp;
    Mat4 model;
    float cameraPosition[4];
    float lightDirection[4];
    float metallic = 0.05f;
    float roughness = 0.35f;
    float ambient = 0.03f;
    float _pad = 0.0f;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> sourcePositionIndices;
    Vec3 minBounds{ 1.0e20f, 1.0e20f, 1.0e20f };
    Vec3 maxBounds{-1.0e20f,-1.0e20f,-1.0e20f };
    bool hasNormals = false;
    bool hasUVs = false;
};

struct TextureData
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct MaterialTextures
{
    TextureData baseColor;
    TextureData roughness;
    TextureData metallic;
    TextureData normal;
    TextureData ao;
};

NS::String* makeString(const char* value)
{
    return NS::String::string(value, NS::UTF8StringEncoding);
}

float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(const Vec3& v)
{
    const float len = std::sqrt(std::max(dot(v, v), 1.0e-12f));
    return {v.x / len, v.y / len, v.z / len};
}

Mat4 identityMatrix()
{
    return {};
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 out{};
    std::fill(std::begin(out.m), std::end(out.m), 0.0f);
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int k = 0; k < 4; ++k)
            {
                out.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
        }
    }
    return out;
}

Mat4 rotationX(float degrees)
{
    const float radians = degrees * 3.1415926535f / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = identityMatrix();
    out.m[5] = c;
    out.m[6] = s;
    out.m[9] = -s;
    out.m[10] = c;
    return out;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * 3.1415926535f / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = identityMatrix();
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

Mat4 perspective(float fovDegrees, float aspect, float nearPlane, float farPlane)
{
    const float tanHalf = std::tan(fovDegrees * 0.5f * 3.1415926535f / 180.0f);
    Mat4 out{};
    std::fill(std::begin(out.m), std::end(out.m), 0.0f);
    out.m[0] = 1.0f / (aspect * tanHalf);
    out.m[5] = 1.0f / tanHalf;
    out.m[10] = farPlane / (nearPlane - farPlane);
    out.m[11] = -1.0f;
    out.m[14] = (nearPlane * farPlane) / (nearPlane - farPlane);
    return out;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 forward = normalize(sub(eye, target));
    const Vec3 right = normalize(cross(up, forward));
    const Vec3 cameraUp = cross(forward, right);

    Mat4 out = identityMatrix();
    out.m[0] = right.x;
    out.m[1] = right.y;
    out.m[2] = right.z;
    out.m[4] = cameraUp.x;
    out.m[5] = cameraUp.y;
    out.m[6] = cameraUp.z;
    out.m[8] = forward.x;
    out.m[9] = forward.y;
    out.m[10] = forward.z;
    out.m[12] = -dot(right, eye);
    out.m[13] = -dot(cameraUp, eye);
    out.m[14] = -dot(forward, eye);
    return out;
}

std::string nextToken(std::istream& in)
{
    std::string token;
    while (in >> token)
    {
        if (!token.empty() && token[0] == '#')
        {
            std::string discard;
            std::getline(in, discard);
            continue;
        }
        return token;
    }
    return {};
}

TextureData loadPPM(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("Could not open texture: " + path.string());
    }
    if (nextToken(in) != "P3")
    {
        throw std::runtime_error("Only ASCII P3 PPM textures are supported.");
    }

    TextureData texture;
    texture.width = static_cast<uint32_t>(std::stoul(nextToken(in)));
    texture.height = static_cast<uint32_t>(std::stoul(nextToken(in)));
    const int maxValue = std::stoi(nextToken(in));
    texture.rgba.resize(texture.width * texture.height * 4);

    for (uint32_t i = 0; i < texture.width * texture.height; ++i)
    {
        const int r = std::stoi(nextToken(in));
        const int g = std::stoi(nextToken(in));
        const int b = std::stoi(nextToken(in));
        texture.rgba[i * 4 + 0] = static_cast<uint8_t>(r * 255 / maxValue);
        texture.rgba[i * 4 + 1] = static_cast<uint8_t>(g * 255 / maxValue);
        texture.rgba[i * 4 + 2] = static_cast<uint8_t>(b * 255 / maxValue);
        texture.rgba[i * 4 + 3] = 255;
    }
    return texture;
}

TextureData loadImageWithImageIO(const std::filesystem::path& path)
{
    CFStringRef pathString = CFStringCreateWithCString(kCFAllocatorDefault, path.string().c_str(), kCFStringEncodingUTF8);
    if (!pathString)
    {
        throw std::runtime_error("Could not create path string for texture: " + path.string());
    }

    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, pathString, kCFURLPOSIXPathStyle, false);
    CFRelease(pathString);
    if (!url)
    {
        throw std::runtime_error("Could not create file URL for texture: " + path.string());
    }

    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source)
    {
        throw std::runtime_error("Could not open image texture: " + path.string());
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image)
    {
        throw std::runtime_error("Could not decode image texture: " + path.string());
    }

    TextureData texture;
    texture.width = static_cast<uint32_t>(CGImageGetWidth(image));
    texture.height = static_cast<uint32_t>(CGImageGetHeight(image));
    texture.rgba.resize(static_cast<size_t>(texture.width) * texture.height * 4);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        texture.rgba.data(),
        texture.width,
        texture.height,
        8,
        texture.width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context)
    {
        CGImageRelease(image);
        throw std::runtime_error("Could not create bitmap context for texture: " + path.string());
    }

    CGContextDrawImage(context, CGRectMake(0, 0, texture.width, texture.height), image);
    CGContextRelease(context);
    CGImageRelease(image);
    return texture;
}

TextureData loadTexture(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extension == ".ppm")
    {
        return loadPPM(path);
    }
    return loadImageWithImageIO(path);
}

TextureData makeSolidTexture(uint8_t r, uint8_t g, uint8_t b)
{
    TextureData texture;
    texture.width = 1;
    texture.height = 1;
    texture.rgba = {r, g, b, 255};
    return texture;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::filesystem::path siblingTexturePath(const std::filesystem::path& texturePath, const std::string& suffix)
{
    const std::string stem = texturePath.stem().string();
    std::string prefix = stem;
    const std::array<std::string, 4> knownSuffixes = {
        "_basecolor",
        "_diffuse",
        "_diff",
        "_albedo",
    };
    for (const std::string& known : knownSuffixes)
    {
        if (endsWith(prefix, known))
        {
            prefix.resize(prefix.size() - known.size());
            break;
        }
    }
    return texturePath.parent_path() / (prefix + suffix + texturePath.extension().string());
}

MaterialTextures loadMaterialTextures(const RenderOptions& options, const Uniforms& uniforms)
{
    MaterialTextures textures;
    textures.baseColor = loadTexture(options.texturePath);
    textures.roughness = makeSolidTexture(
        static_cast<uint8_t>(std::clamp(uniforms.roughness, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(uniforms.roughness, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(uniforms.roughness, 0.0f, 1.0f) * 255.0f));
    textures.metallic = makeSolidTexture(
        static_cast<uint8_t>(std::clamp(uniforms.metallic, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(uniforms.metallic, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(uniforms.metallic, 0.0f, 1.0f) * 255.0f));
    textures.normal = makeSolidTexture(128, 128, 255);
    textures.ao = makeSolidTexture(255, 255, 255);

    const std::array<std::pair<std::string, TextureData*>, 4> optionalMaps = {{
        {"_roughness", &textures.roughness},
        {"_metallic", &textures.metallic},
        {"_normal", &textures.normal},
        {"_ao", &textures.ao},
    }};

    for (const auto& [suffix, target] : optionalMaps)
    {
        const std::filesystem::path candidate = siblingTexturePath(options.texturePath, suffix);
        if (std::filesystem::exists(candidate))
        {
            *target = loadTexture(candidate);
        }
    }

    return textures;
}

Vertex parseObjVertex(const std::string& faceToken,
                      const std::vector<Vec3>& positions,
                      const std::vector<Vec2>& uvs,
                      const std::vector<Vec3>& normals,
                      Mesh& mesh,
                      uint32_t& sourcePositionIndex)
{
    std::stringstream token(faceToken);
    std::string part;
    std::vector<int> indices;
    while (std::getline(token, part, '/'))
    {
        indices.push_back(part.empty() ? 0 : std::stoi(part));
    }

    sourcePositionIndex = static_cast<uint32_t>(indices.at(0) - 1);
    const Vec3 position = positions.at(static_cast<size_t>(sourcePositionIndex));
    const Vec2 uv = indices.size() > 1 && indices[1] > 0
        ? uvs.at(static_cast<size_t>(indices[1] - 1))
        : Vec2{0.0f, 0.0f};
    const Vec3 normal = indices.size() > 2 && indices[2] > 0
        ? normals.at(static_cast<size_t>(indices[2] - 1))
        : Vec3{0.0f, 0.0f, 1.0f};
    mesh.hasUVs = mesh.hasUVs || (indices.size() > 1 && indices[1] > 0);
    mesh.hasNormals = mesh.hasNormals || (indices.size() > 2 && indices[2] > 0);

    mesh.minBounds.x = std::min(mesh.minBounds.x, position.x);
    mesh.minBounds.y = std::min(mesh.minBounds.y, position.y);
    mesh.minBounds.z = std::min(mesh.minBounds.z, position.z);
    mesh.maxBounds.x = std::max(mesh.maxBounds.x, position.x);
    mesh.maxBounds.y = std::max(mesh.maxBounds.y, position.y);
    mesh.maxBounds.z = std::max(mesh.maxBounds.z, position.z);

    return {
        {position.x, position.y, position.z},
        {normal.x, normal.y, normal.z},
        {uv.x, uv.y},
    };
}

void generateSmoothNormals(Mesh& mesh)
{
    uint32_t maxSourceIndex = 0;
    for (uint32_t index : mesh.sourcePositionIndices)
    {
        maxSourceIndex = std::max(maxSourceIndex, index);
    }
    std::vector<Vec3> accumulated(static_cast<size_t>(maxSourceIndex + 1), Vec3{0.0f, 0.0f, 0.0f});
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const uint32_t i0 = mesh.indices[i + 0];
        const uint32_t i1 = mesh.indices[i + 1];
        const uint32_t i2 = mesh.indices[i + 2];
        const Vec3 p0{mesh.vertices[i0].position[0], mesh.vertices[i0].position[1], mesh.vertices[i0].position[2]};
        const Vec3 p1{mesh.vertices[i1].position[0], mesh.vertices[i1].position[1], mesh.vertices[i1].position[2]};
        const Vec3 p2{mesh.vertices[i2].position[0], mesh.vertices[i2].position[1], mesh.vertices[i2].position[2]};
        const Vec3 faceNormal = cross(sub(p1, p0), sub(p2, p0));
        const uint32_t pIdx0 = mesh.sourcePositionIndices[i0];
        const uint32_t pIdx1 = mesh.sourcePositionIndices[i1];
        const uint32_t pIdx2 = mesh.sourcePositionIndices[i2];
        accumulated[pIdx0] = {accumulated[pIdx0].x + faceNormal.x, accumulated[pIdx0].y + faceNormal.y, accumulated[pIdx0].z + faceNormal.z};
        accumulated[pIdx1] = {accumulated[pIdx1].x + faceNormal.x, accumulated[pIdx1].y + faceNormal.y, accumulated[pIdx1].z + faceNormal.z};
        accumulated[pIdx2] = {accumulated[pIdx2].x + faceNormal.x, accumulated[pIdx2].y + faceNormal.y, accumulated[pIdx2].z + faceNormal.z};
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const Vec3 n = normalize(accumulated[mesh.sourcePositionIndices[i]]);
        mesh.vertices[i].normal[0] = n.x;
        mesh.vertices[i].normal[1] = n.y;
        mesh.vertices[i].normal[2] = n.z;
    }
    mesh.hasNormals = true;
}

void generateBoundsUVs(Mesh& mesh)
{
    const Vec3 size = {
        std::max(mesh.maxBounds.x - mesh.minBounds.x, 1.0e-5f),
        std::max(mesh.maxBounds.y - mesh.minBounds.y, 1.0e-5f),
        std::max(mesh.maxBounds.z - mesh.minBounds.z, 1.0e-5f),
    };
    for (Vertex& vertex : mesh.vertices)
    {
        const float x = (vertex.position[0] - mesh.minBounds.x) / size.x;
        const float y = (vertex.position[1] - mesh.minBounds.y) / size.y;
        vertex.uv[0] = x;
        vertex.uv[1] = 1.0f - y;
    }
    mesh.hasUVs = true;
}

Mesh loadObj(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("Could not open mesh: " + path.string());
    }

    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    std::vector<Vec3> normals;
    Mesh mesh;
    std::string line;

    while (std::getline(in, line))
    {
        std::stringstream stream(line);
        std::string kind;
        stream >> kind;
        if (kind == "v")
        {
            Vec3 p{};
            stream >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (kind == "vt")
        {
            Vec2 uv{};
            stream >> uv.x >> uv.y;
            uvs.push_back(uv);
        }
        else if (kind == "vn")
        {
            Vec3 n{};
            stream >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (kind == "f")
        {
            std::vector<uint32_t> face;
            std::string item;
            while (stream >> item)
            {
                uint32_t sourcePositionIndex = 0;
                face.push_back(static_cast<uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(parseObjVertex(item, positions, uvs, normals, mesh, sourcePositionIndex));
                mesh.sourcePositionIndices.push_back(sourcePositionIndex);
            }
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                mesh.indices.push_back(face[0]);
                mesh.indices.push_back(face[i]);
                mesh.indices.push_back(face[i + 1]);
            }
        }
    }

    if (!mesh.hasNormals)
    {
        generateSmoothNormals(mesh);
    }
    if (!mesh.hasUVs)
    {
        generateBoundsUVs(mesh);
    }
    return mesh;
}

Vec3 fitCameraPosition(const Mesh& mesh)
{
    const Vec3 size = {
        mesh.maxBounds.x - mesh.minBounds.x,
        mesh.maxBounds.y - mesh.minBounds.y,
        mesh.maxBounds.z - mesh.minBounds.z,
    };
    const float radius = std::max({size.x, size.y, size.z}) * 0.6f;
    const Vec3 center = {
        (mesh.minBounds.x + mesh.maxBounds.x) * 0.5f,
        (mesh.minBounds.y + mesh.maxBounds.y) * 0.5f,
        (mesh.minBounds.z + mesh.maxBounds.z) * 0.5f,
    };
    return {center.x + radius * 0.45f, center.y + radius * 0.20f, center.z + radius * 2.8f};
}

Vec3 meshCenter(const Mesh& mesh)
{
    return {
        (mesh.minBounds.x + mesh.maxBounds.x) * 0.5f,
        (mesh.minBounds.y + mesh.maxBounds.y) * 0.5f,
        (mesh.minBounds.z + mesh.maxBounds.z) * 0.5f,
    };
}

Mat4 chooseModelMatrix(const std::filesystem::path& meshPath)
{
    const std::string stem = meshPath.stem().string();
    if (stem.find("uv_sphere") != std::string::npos)
    {
        return multiply(rotationY(28.0f), rotationX(-14.0f));
    }
    if (stem.find("bunny") != std::string::npos)
    {
        return multiply(rotationY(32.0f), rotationX(-18.0f));
    }
    return identityMatrix();
}

Uniforms makeUniforms(const Mesh& mesh, const RenderOptions& options)
{
    Uniforms uniforms{};
    const Mat4 model = chooseModelMatrix(options.meshPath);
    const Vec3 eye = fitCameraPosition(mesh);
    const Vec3 target = meshCenter(mesh);
    const Mat4 view = lookAt(eye, target, {0.0f, 1.0f, 0.0f});
    const Mat4 proj = perspective(45.0f, static_cast<float>(options.width) / static_cast<float>(options.height), 0.1f, 100.0f);
    uniforms.model = model;
    uniforms.mvp = multiply(multiply(proj, view), model);
    uniforms.cameraPosition[0] = eye.x;
    uniforms.cameraPosition[1] = eye.y;
    uniforms.cameraPosition[2] = eye.z;
    uniforms.cameraPosition[3] = 1.0f;
    uniforms.lightDirection[0] = -0.35f;
    uniforms.lightDirection[1] = 0.65f;
    uniforms.lightDirection[2] = 0.68f;
    uniforms.lightDirection[3] = 0.0f;
    if (options.stage == "pbr")
    {
        uniforms.metallic = 0.32f;
        uniforms.roughness = 0.42f;
        uniforms.ambient = 0.12f;
    }
    else
    {
        uniforms.metallic = 0.05f;
        uniforms.roughness = 0.45f;
        uniforms.ambient = 0.03f;
    }
    return uniforms;
}

const char* fragmentFunctionForStage(const std::string& stage)
{
    if (stage == "albedo")
    {
        return "fragment_albedo";
    }
    if (stage == "lambert")
    {
        return "fragment_lambert";
    }
    if (stage == "blinn")
    {
        return "fragment_blinn_phong";
    }
    if (stage == "pbr")
    {
        return "fragment_pbr";
    }
    return nullptr;
}

MTL::RenderPipelineState* createPipelineState(MTL::Device* device,
                                              MTL::Library* library,
                                              const char* fragmentName)
{
    MTL::Function* vertex = library->newFunction(makeString("vertex_main"));
    MTL::Function* fragment = library->newFunction(makeString(fragmentName));
    if (!vertex || !fragment)
    {
        std::cerr << "Could not load shader functions for stage: " << fragmentName << "\n";
        if (vertex) vertex->release();
        if (fragment) fragment->release();
        return nullptr;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertex);
    descriptor->setFragmentFunction(fragment);
    descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    NS::Error* error = nullptr;
    MTL::RenderPipelineState* pipeline = device->newRenderPipelineState(descriptor, &error);
    if (!pipeline)
    {
        std::cerr << "Could not create pipeline state: " << fragmentName << "\n";
        if (error)
        {
            std::cerr << error->localizedDescription()->utf8String() << "\n";
        }
    }

    descriptor->release();
    vertex->release();
    fragment->release();
    return pipeline;
}

MTL::DepthStencilState* createDepthState(MTL::Device* device)
{
    MTL::DepthStencilDescriptor* descriptor = MTL::DepthStencilDescriptor::alloc()->init();
    descriptor->setDepthCompareFunction(MTL::CompareFunctionLess);
    descriptor->setDepthWriteEnabled(true);
    MTL::DepthStencilState* state = device->newDepthStencilState(descriptor);
    descriptor->release();
    return state;
}

MTL::Texture* createColorTexture(MTL::Device* device, uint32_t width, uint32_t height)
{
    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setStorageMode(MTL::StorageModeManaged);
    descriptor->setUsage(MTL::TextureUsageRenderTarget);
    MTL::Texture* texture = device->newTexture(descriptor);
    descriptor->release();
    return texture;
}

MTL::Texture* createDepthTexture(MTL::Device* device, uint32_t width, uint32_t height)
{
    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    descriptor->setUsage(MTL::TextureUsageRenderTarget);
    MTL::Texture* texture = device->newTexture(descriptor);
    descriptor->release();
    return texture;
}

MTL::Texture* createTexture(MTL::Device* device, const TextureData& textureData)
{
    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    descriptor->setWidth(textureData.width);
    descriptor->setHeight(textureData.height);
    descriptor->setStorageMode(MTL::StorageModeManaged);
    descriptor->setUsage(MTL::TextureUsageShaderRead);

    MTL::Texture* texture = device->newTexture(descriptor);
    texture->replaceRegion(MTL::Region::Make2D(0, 0, textureData.width, textureData.height),
                           0,
                           textureData.rgba.data(),
                           textureData.width * 4);
    descriptor->release();
    return texture;
}

MTL::SamplerState* createSampler(MTL::Device* device)
{
    MTL::SamplerDescriptor* descriptor = MTL::SamplerDescriptor::alloc()->init();
    descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setSAddressMode(MTL::SamplerAddressModeRepeat);
    descriptor->setTAddressMode(MTL::SamplerAddressModeRepeat);
    MTL::SamplerState* sampler = device->newSamplerState(descriptor);
    descriptor->release();
    return sampler;
}

void writePPM(const std::filesystem::path& path, const std::vector<uint8_t>& bgra, uint32_t width, uint32_t height)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t i = 0; i < width * height; ++i)
    {
        const uint8_t* pixel = bgra.data() + i * 4;
        out.put(static_cast<char>(pixel[2]));
        out.put(static_cast<char>(pixel[1]));
        out.put(static_cast<char>(pixel[0]));
    }
}

void writeImageWithImageIO(const std::filesystem::path& path,
                           const std::vector<uint8_t>& bgra,
                           uint32_t width,
                           uint32_t height,
                           CFStringRef imageType)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (uint32_t i = 0; i < width * height; ++i)
    {
        const uint8_t* src = bgra.data() + i * 4;
        uint8_t* dst = rgba.data() + i * 4;
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        rgba.data(),
        width,
        height,
        8,
        width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context)
    {
        throw std::runtime_error("Could not create bitmap context for output image: " + path.string());
    }

    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    if (!image)
    {
        throw std::runtime_error("Could not create CGImage for output image: " + path.string());
    }

    CFStringRef pathString = CFStringCreateWithCString(kCFAllocatorDefault, path.string().c_str(), kCFStringEncodingUTF8);
    if (!pathString)
    {
        CGImageRelease(image);
        throw std::runtime_error("Could not create path string for output image: " + path.string());
    }

    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, pathString, kCFURLPOSIXPathStyle, false);
    CFRelease(pathString);
    if (!url)
    {
        CGImageRelease(image);
        throw std::runtime_error("Could not create file URL for output image: " + path.string());
    }

    CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, imageType, 1, nullptr);
    CFRelease(url);
    if (!destination)
    {
        CGImageRelease(image);
        throw std::runtime_error("Could not create output image destination: " + path.string());
    }

    CGImageDestinationAddImage(destination, image, nullptr);
    const bool ok = CGImageDestinationFinalize(destination);
    CFRelease(destination);
    CGImageRelease(image);
    if (!ok)
    {
        throw std::runtime_error("Could not finalize output image: " + path.string());
    }
}

void writeOutputImage(const std::filesystem::path& path,
                      const std::vector<uint8_t>& bgra,
                      uint32_t width,
                      uint32_t height)
{
    std::filesystem::create_directories(path.parent_path());

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extension == ".png")
    {
        writeImageWithImageIO(path, bgra, width, height, CFSTR("public.png"));
        return;
    }
    if (extension == ".jpg" || extension == ".jpeg")
    {
        writeImageWithImageIO(path, bgra, width, height, CFSTR("public.jpeg"));
        return;
    }
    writePPM(path, bgra, width, height);
}
}

bool renderScene(const RenderOptions& options)
{
    const char* fragmentName = fragmentFunctionForStage(options.stage);
    if (!fragmentName)
    {
        std::cerr << "Unknown stage: " << options.stage << "\n";
        return false;
    }

    Mesh mesh;
    MaterialTextures materialTextures;
    try
    {
        mesh = loadObj(options.meshPath);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return false;
    }

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device)
    {
        std::cerr << "Metal is not available on this Mac.\n";
        pool->release();
        return false;
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
        return false;
    }

    Uniforms uniforms = makeUniforms(mesh, options);
    try
    {
        materialTextures = loadMaterialTextures(options, uniforms);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        library->release();
        device->release();
        pool->release();
        return false;
    }

    MTL::RenderPipelineState* pipeline = createPipelineState(device, library, fragmentName);
    MTL::DepthStencilState* depthState = createDepthState(device);
    MTL::Texture* colorTexture = createColorTexture(device, options.width, options.height);
    MTL::Texture* depthTexture = createDepthTexture(device, options.width, options.height);
    MTL::Texture* baseColorTexture = createTexture(device, materialTextures.baseColor);
    MTL::Texture* roughnessTexture = createTexture(device, materialTextures.roughness);
    MTL::Texture* metallicTexture = createTexture(device, materialTextures.metallic);
    MTL::Texture* normalTexture = createTexture(device, materialTextures.normal);
    MTL::Texture* aoTexture = createTexture(device, materialTextures.ao);
    MTL::SamplerState* sampler = createSampler(device);

    if (!pipeline || !depthState || !colorTexture || !depthTexture || !baseColorTexture || !roughnessTexture || !metallicTexture || !normalTexture || !aoTexture || !sampler)
    {
        if (sampler) sampler->release();
        if (aoTexture) aoTexture->release();
        if (normalTexture) normalTexture->release();
        if (metallicTexture) metallicTexture->release();
        if (roughnessTexture) roughnessTexture->release();
        if (baseColorTexture) baseColorTexture->release();
        if (depthTexture) depthTexture->release();
        if (colorTexture) colorTexture->release();
        if (depthState) depthState->release();
        if (pipeline) pipeline->release();
        library->release();
        device->release();
        pool->release();
        return false;
    }

    MTL::CommandQueue* queue = device->newCommandQueue();
    MTL::Buffer* vertexBuffer = device->newBuffer(mesh.vertices.data(), sizeof(Vertex) * mesh.vertices.size(), MTL::ResourceStorageModeShared);
    MTL::Buffer* indexBuffer = device->newBuffer(mesh.indices.data(), sizeof(uint32_t) * mesh.indices.size(), MTL::ResourceStorageModeShared);
    MTL::Buffer* uniformBuffer = device->newBuffer(&uniforms, sizeof(Uniforms), MTL::ResourceStorageModeShared);

    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(colorTexture);
    color->setLoadAction(MTL::LoadActionClear);
    color->setStoreAction(MTL::StoreActionStore);
    color->setClearColor(MTL::ClearColor(0.08, 0.22, 0.25, 1.0));

    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(depthTexture);
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setStoreAction(MTL::StoreActionDontCare);
    depth->setClearDepth(1.0);

    MTL::CommandBuffer* commandBuffer = queue->commandBuffer();
    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass);
    encoder->setRenderPipelineState(pipeline);
    encoder->setDepthStencilState(depthState);
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    encoder->setCullMode(MTL::CullModeNone);
    encoder->setViewport(MTL::Viewport{0.0, 0.0, static_cast<double>(options.width), static_cast<double>(options.height), 0.0, 1.0});
    encoder->setScissorRect(MTL::ScissorRect{0, 0, options.width, options.height});
    encoder->setVertexBuffer(vertexBuffer, 0, 0);
    encoder->setVertexBuffer(uniformBuffer, 0, 1);
    encoder->setFragmentBuffer(uniformBuffer, 0, 1);
    encoder->setFragmentTexture(baseColorTexture, 0);
    encoder->setFragmentTexture(roughnessTexture, 1);
    encoder->setFragmentTexture(metallicTexture, 2);
    encoder->setFragmentTexture(normalTexture, 3);
    encoder->setFragmentTexture(aoTexture, 4);
    encoder->setFragmentSamplerState(sampler, 0);
    encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                   mesh.indices.size(),
                                   MTL::IndexTypeUInt32,
                                   indexBuffer,
                                   0);
    encoder->endEncoding();

    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    blit->synchronizeTexture(colorTexture, 0, 0);
    blit->endEncoding();

    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    std::vector<uint8_t> pixels(options.width * options.height * 4);
    colorTexture->getBytes(pixels.data(),
                           options.width * 4,
                           MTL::Region::Make2D(0, 0, options.width, options.height),
                           0);
    try
    {
        writeOutputImage(options.outputPath, pixels, options.width, options.height);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        uniformBuffer->release();
        indexBuffer->release();
        vertexBuffer->release();
        queue->release();
        sampler->release();
        aoTexture->release();
        normalTexture->release();
        metallicTexture->release();
        roughnessTexture->release();
        baseColorTexture->release();
        depthTexture->release();
        colorTexture->release();
        depthState->release();
        pipeline->release();
        library->release();
        device->release();
        pool->release();
        return false;
    }

    uniformBuffer->release();
    indexBuffer->release();
    vertexBuffer->release();
    queue->release();
    sampler->release();
    aoTexture->release();
    normalTexture->release();
    metallicTexture->release();
    roughnessTexture->release();
    baseColorTexture->release();
    depthTexture->release();
    colorTexture->release();
    depthState->release();
    pipeline->release();
    library->release();
    device->release();
    pool->release();
    return true;
}
