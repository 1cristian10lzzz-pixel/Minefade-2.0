#include "RmlUiOpenGLES.h"
#include "../blocks/BlockRegistry.h"
#include "../blocks/BlockAtlasManifest.h"

#include <android/log.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <cstddef>
#include <string>
#include <cstring>
#include <cstdio>

namespace minefade {
namespace {
constexpr const char* kTag = "MinefadeRmlUi";
#define UI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

constexpr char kVertexShader[] = R"(
#version 300 es
precision highp float;
layout(location=0) in vec2 aPosition;
layout(location=1) in vec4 aColour;
layout(location=2) in vec2 aTexCoord;
uniform vec2 uViewport;
uniform vec2 uTranslation;
out vec4 vColour;
out vec2 vTexCoord;
void main() {
    vec2 p = aPosition + uTranslation;
    vec2 clip = vec2((p.x / uViewport.x) * 2.0 - 1.0,
                     1.0 - (p.y / uViewport.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    vColour = aColour;
    vTexCoord = aTexCoord;
}
)";

constexpr char kFragmentShader[] = R"(
#version 300 es
precision mediump float;
in vec4 vColour;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform int uTextured;
out vec4 outColour;
void main() {
    vec4 texel = uTextured == 1 ? texture(uTexture, vTexCoord) : vec4(1.0);
    outColour = vColour * texel;
}
)";
}

RmlUiOpenGLES::~RmlUiOpenGLES() { Shutdown(); }

GLuint RmlUiOpenGLES::CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        const GLenum error = glGetError();
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
        lastError_ = std::string("glCreateShader falló (GL ") + code + ").";
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        lastError_ = std::string("Compilación del shader de RmlUi: ") + log;
        UI_LOGE("%s", lastError_.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool RmlUiOpenGLES::Initialise() {
    Shutdown();
    lastError_.clear();
    while (glGetError() != GL_NO_ERROR) { }
    if (glGetString(GL_VERSION) == nullptr) {
        lastError_ = "No hay un contexto OpenGL ES actual en el hilo de renderizado.";
        UI_LOGE("%s", lastError_.c_str());
        return false;
    }
    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        if (lastError_.empty()) lastError_ = "No se pudieron crear los shaders OpenGL ES de la interfaz.";
        return false;
    }
    program_ = glCreateProgram();
    if (program_ == 0) {
        const GLenum error = glGetError();
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
        lastError_ = std::string("glCreateProgram de RmlUi falló (GL ") + code + ").";
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return false;
    }
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program_, length, nullptr, log.data());
        lastError_ = std::string("Enlace del shader de RmlUi: ") + log;
        UI_LOGE("%s", lastError_.c_str());
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    viewportLocation_ = glGetUniformLocation(program_, "uViewport");
    translationLocation_ = glGetUniformLocation(program_, "uTranslation");
    texturedLocation_ = glGetUniformLocation(program_, "uTextured");
    samplerLocation_ = glGetUniformLocation(program_, "uTexture");
    if (viewportLocation_ < 0 || translationLocation_ < 0 || texturedLocation_ < 0 || samplerLocation_ < 0) {
        lastError_ = "El shader de RmlUi no expuso todos los uniforms requeridos.";
        UI_LOGE("%s", lastError_.c_str());
        Shutdown();
        return false;
    }
    glUseProgram(program_);
    glUniform1i(samplerLocation_, 0);
    glUseProgram(0);
    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
        lastError_ = std::string("Error OpenGL ES al preparar RmlUi (GL ") + code + ").";
        UI_LOGE("%s", lastError_.c_str());
        Shutdown();
        return false;
    }
    return true;
}

void RmlUiOpenGLES::ReleaseGeometryObject(Geometry& geometry) {
    if (geometry.ebo != 0) glDeleteBuffers(1, &geometry.ebo);
    if (geometry.vbo != 0) glDeleteBuffers(1, &geometry.vbo);
    if (geometry.vao != 0) glDeleteVertexArrays(1, &geometry.vao);
    geometry = {};
}

void RmlUiOpenGLES::Shutdown() {
    for (auto& item : geometries_) ReleaseGeometryObject(item.second);
    geometries_.clear();
    for (const auto& item : textures_) {
        const GLuint texture = item.second.object;
        if (texture != 0) glDeleteTextures(1, &texture);
    }
    textures_.clear();
    textureCache_.clear();
    blockAtlasPixels_.clear();
    blockAtlasWidth_=blockAtlasHeight_=0;
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    viewportLocation_ = translationLocation_ = texturedLocation_ = samplerLocation_ = -1;
}

void RmlUiOpenGLES::SetViewport(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
}

void RmlUiOpenGLES::SetBlockAtlas(const std::uint8_t* pixels,std::size_t bytes,
                                  int width,int height) {
    ++blockAtlasVersion_;
    if (pixels == nullptr ||
        width != static_cast<int>(kBlockAtlasWidth) ||
        height != static_cast<int>(kBlockAtlasHeight)) {
        UI_LOGE("RmlUi atlas dimensions do not match the generated manifest: %dx%d",
                width, height);
        blockAtlasPixels_.clear();
        blockAtlasWidth_ = blockAtlasHeight_ = 0;
        return;
    }
    if (bytes < kBlockAtlasByteCount) {
        UI_LOGE("RmlUi block atlas is truncated: %zu < %zu",
                bytes, kBlockAtlasByteCount);
        blockAtlasPixels_.clear();
        blockAtlasWidth_ = blockAtlasHeight_ = 0;
        return;
    }
    blockAtlasPixels_.assign(pixels, pixels + kBlockAtlasByteCount);
    blockAtlasWidth_ = width;
    blockAtlasHeight_ = height;
}

Rml::CompiledGeometryHandle RmlUiOpenGLES::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) return 0;
    Geometry geometry;
    glGenVertexArrays(1, &geometry.vao);
    glGenBuffers(1, &geometry.vbo);
    glGenBuffers(1, &geometry.ebo);
    if (geometry.vao == 0 || geometry.vbo == 0 || geometry.ebo == 0) {
        ReleaseGeometryObject(geometry);
        return 0;
    }

    std::vector<std::uint32_t> packedIndices;
    packedIndices.reserve(indices.size());
    for (const int index : indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= vertices.size()) {
            ReleaseGeometryObject(geometry);
            return 0;
        }
        packedIndices.push_back(static_cast<std::uint32_t>(index));
    }

    glBindVertexArray(geometry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, geometry.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Rml::Vertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(packedIndices.size() * sizeof(std::uint32_t)),
                 packedIndices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
                          reinterpret_cast<const void*>(offsetof(Rml::Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex),
                          reinterpret_cast<const void*>(offsetof(Rml::Vertex, colour)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
                          reinterpret_cast<const void*>(offsetof(Rml::Vertex, tex_coord)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    geometry.indexCount = static_cast<GLsizei>(packedIndices.size());
    const std::uintptr_t id = nextGeometry_++;
    geometries_.emplace(id, geometry);
    return static_cast<Rml::CompiledGeometryHandle>(id);
}

void RmlUiOpenGLES::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                   Rml::TextureHandle textureHandle) {
    const auto geometry = geometries_.find(static_cast<std::uintptr_t>(handle));
    if (geometry == geometries_.end() || program_ == 0) return;

    GLuint texture = 0;
    if (textureHandle != 0) {
        const auto found = textures_.find(static_cast<std::uintptr_t>(textureHandle));
        if (found != textures_.end()) texture = found->second.object;
    }

    glUseProgram(program_);
    glUniform2f(viewportLocation_, static_cast<float>(width_), static_cast<float>(height_));
    glUniform2f(translationLocation_, translation.x, translation.y);
    glUniform1i(texturedLocation_, texture != 0 ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(geometry->second.vao);
    glDrawElements(GL_TRIANGLES, geometry->second.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void RmlUiOpenGLES::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    const auto found = geometries_.find(static_cast<std::uintptr_t>(handle));
    if (found == geometries_.end()) return;
    ReleaseGeometryObject(found->second);
    geometries_.erase(found);
}

Rml::TextureHandle RmlUiOpenGLES::LoadTexture(Rml::Vector2i& textureDimensions,const Rml::String& source) {
    // Only registered block IDs are accepted. Item/entity/UI assets get their
    // own URI namespaces when their dedicated resource loaders are introduced.
    constexpr char prefix[] = "minefade://block/";
    const std::string uri = source;
    if (uri.rfind(prefix, 0) != 0) {
        UI_LOGE("Unsupported RmlUi texture URI: %s", source.c_str());
        return 0;
    }
    if (blockAtlasPixels_.empty() ||
        blockAtlasWidth_ != static_cast<int>(kBlockAtlasWidth) ||
        blockAtlasHeight_ != static_cast<int>(kBlockAtlasHeight)) {
        UI_LOGE("RmlUi block icon requested without a valid atlas manifest-backed texture");
        return 0;
    }

    const char* begin = uri.data() + (sizeof(prefix) - 1u);
    const char* end = uri.data() + uri.size();
    int blockId = -1;
    const auto parsed = std::from_chars(begin, end, blockId);
    if (begin == end || parsed.ec != std::errc{} || parsed.ptr != end || blockId < 0) {
        UI_LOGE("Invalid block ID in RmlUi texture URI: %s", source.c_str());
        return 0;
    }

    const BlockDefinition* definition = GetBlockDefinition(static_cast<BlockId>(blockId));
    if (definition == nullptr) {
        UI_LOGE("RmlUi block icon references unknown BlockId %d", blockId);
        return 0;
    }

    const std::size_t tile = definition->tileSide;
    const std::size_t tileLimit = GetBlockDefinitionCount() * 3u;
    if (tile >= tileLimit || tile >= kBlockAtlasTileCount) {
        UI_LOGE("Block %d maps to invalid atlas tile %zu", blockId, tile);
        return 0;
    }

    const std::string cacheKey = std::to_string(blockAtlasVersion_) + "|" + uri;
    const auto cached = textureCache_.find(cacheKey);
    if (cached != textureCache_.end()) {
        const auto resource = textures_.find(cached->second);
        if (resource != textures_.end()) {
            ++resource->second.references;
            textureDimensions = {
                static_cast<int>(kBlockAtlasTileSize),
                static_cast<int>(kBlockAtlasTileSize)
            };
            return static_cast<Rml::TextureHandle>(cached->second);
        }
        textureCache_.erase(cached);
    }

    const std::size_t column = tile % kBlockAtlasColumns;
    const std::size_t row = tile / kBlockAtlasColumns;
    const std::size_t x0 = column * kBlockAtlasCellSize + kBlockAtlasPadding;
    const std::size_t y0 = row * kBlockAtlasCellSize + kBlockAtlasPadding;
    const std::size_t tileSize = kBlockAtlasTileSize;
    if (x0 + tileSize > static_cast<std::size_t>(blockAtlasWidth_) ||
        y0 + tileSize > static_cast<std::size_t>(blockAtlasHeight_)) {
        UI_LOGE("Block %d atlas tile %zu exceeds manifest dimensions", blockId, tile);
        return 0;
    }

    std::vector<Rml::byte> pixels(tileSize * tileSize * 4u);
    for (std::size_t y = 0; y < tileSize; ++y) {
        const std::size_t sourceOffset =
            ((y0 + y) * static_cast<std::size_t>(blockAtlasWidth_) + x0) * 4u;
        const std::size_t destinationOffset = y * tileSize * 4u;
        std::memcpy(pixels.data() + destinationOffset,
                    blockAtlasPixels_.data() + sourceOffset, tileSize * 4u);
    }

    textureDimensions = {
        static_cast<int>(tileSize),
        static_cast<int>(tileSize)
    };
    const Rml::TextureHandle handle = CreateTexture(
        Rml::Span<const Rml::byte>(pixels.data(), pixels.size()),
        textureDimensions, GL_NEAREST);
    if (handle == 0) return 0;

    const auto resource = textures_.find(static_cast<std::uintptr_t>(handle));
    if (resource != textures_.end()) {
        resource->second.cacheKey = cacheKey;
        textureCache_[cacheKey] = static_cast<std::uintptr_t>(handle);
    }
    return handle;
}

Rml::TextureHandle RmlUiOpenGLES::GenerateTexture(
    Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) {
    return CreateTexture(source, sourceDimensions, GL_LINEAR);
}

Rml::TextureHandle RmlUiOpenGLES::CreateTexture(
    Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions, GLint filter) {
    if (source.empty() || sourceDimensions.x <= 0 || sourceDimensions.y <= 0) return 0;
    const std::size_t width = static_cast<std::size_t>(sourceDimensions.x);
    const std::size_t height = static_cast<std::size_t>(sourceDimensions.y);
    if (width > std::numeric_limits<std::size_t>::max() / height / 4u) return 0;
    const std::size_t expected = width * height * 4u;
    if (source.size() < expected) return 0;

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    GLint previousPixelUnpackBuffer = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousPixelUnpackBuffer);

    // RmlUi supplies a CPU pointer. A stale pixel-unpack buffer would make GLES
    // interpret that pointer as an offset into the buffer instead.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(previousPixelUnpackBuffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sourceDimensions.x, sourceDimensions.y,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, source.data());
    const GLenum uploadError = glGetError();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(previousPixelUnpackBuffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    if (uploadError != GL_NO_ERROR) {
        UI_LOGE("RmlUi texture upload failed with GLES error 0x%04X",
                static_cast<unsigned int>(uploadError));
        glDeleteTextures(1, &texture);
        return 0;
    }

    const std::uintptr_t id = nextTexture_++;
    textures_.emplace(id, TextureResource{texture, 1u, {}});
    return static_cast<Rml::TextureHandle>(id);
}

void RmlUiOpenGLES::ReleaseTexture(Rml::TextureHandle handle) {
    const auto found = textures_.find(static_cast<std::uintptr_t>(handle));
    if (found == textures_.end()) return;
    if (found->second.references > 1u) {
        --found->second.references;
        return;
    }
    if (!found->second.cacheKey.empty()) {
        const auto cached = textureCache_.find(found->second.cacheKey);
        if (cached != textureCache_.end() && cached->second == found->first)
            textureCache_.erase(cached);
    }
    if (found->second.object != 0) glDeleteTextures(1, &found->second.object);
    textures_.erase(found);
}

void RmlUiOpenGLES::EnableScissorRegion(bool enable) {
    scissorEnabled_ = enable;
    if (enable) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
}

void RmlUiOpenGLES::SetScissorRegion(Rml::Rectanglei region) {
    const int left = std::clamp(region.Left(), 0, width_);
    const int top = std::clamp(region.Top(), 0, height_);
    const int right = std::clamp(region.Right(), left, width_);
    const int bottom = std::clamp(region.Bottom(), top, height_);
    const int width = std::max(right - left, 0);
    const int height = std::max(bottom - top, 0);
    glScissor(left, std::max(height_ - bottom, 0), width, height);
}

} // namespace minefade
