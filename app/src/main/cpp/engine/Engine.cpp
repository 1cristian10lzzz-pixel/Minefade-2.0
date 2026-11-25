#include "Engine.h"
#include "minefade_core.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <set>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace minefade {
namespace {
constexpr const char* kTag = "MinefadeNative";
constexpr float kPi = 3.14159265358979323846f;
// Captures driver compiler/linker diagnostics for the startup error screen.
std::string gWorldShaderDiagnostic;
#define ENGINE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)
#define ENGINE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)
#define ENGINE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, kTag, __VA_ARGS__)

constexpr char kWorldVertexShader[] = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
layout(location=3) in vec2 aTexCoord;
layout(location=4) in float aAtlasTile;
uniform mat4 uMvp;
uniform vec3 uLightDirection;
uniform float uDaylight;
uniform float uTime;
uniform float uQuality;
uniform int uWaterWaves;
uniform int uLavaAnimations;
uniform int uLeafWind;
uniform float uLeafWindStrength;
uniform vec3 uCameraPosition;
out mediump vec3 vColor;
out mediump vec2 vTexCoord;
flat out highp int vAtlasTile;
out mediump float vFogDistanceSquared;
void main() {
    vec3 position = aPosition;
    float fluidCode = aColor.r;
    bool waterSurface = fluidCode < -0.5 && fluidCode > -1.5 && aNormal.y > 0.8;
    bool lavaSurface = fluidCode < -1.5 && aNormal.y > 0.8;
    if (waterSurface && uWaterWaves == 1) {
        float wave = sin(position.x * 0.72 + uTime * 1.25)
                   * cos(position.z * 0.61 - uTime * 0.92);
        position.y += wave * (uQuality > 1.5 ? 0.018 : 0.009);
    }
    if (lavaSurface && uLavaAnimations == 1) {
        float heat = sin(position.x * 0.42 + uTime * 1.7)
                   * cos(position.z * 0.36 - uTime * 1.25);
        position.y += heat * 0.007;
    }
    int leafTile = int(aAtlasTile + 0.5);
    bool leafMaterial = (leafTile >= 72 && leafTile <= 74)
                     || (leafTile >= 606 && leafTile <= 608)
                     || (leafTile >= 927 && leafTile <= 929)
                     || (leafTile >= 951 && leafTile <= 953);
    if (leafMaterial && uLeafWind == 1 && uLeafWindStrength > 0.0) {
        float sway = sin(uTime * 1.35 + position.x * 0.21 + position.z * 0.18);
        float gust = cos(uTime * 0.82 + position.z * 0.13);
        position.x += sway * uLeafWindStrength;
        position.z += gust * uLeafWindStrength * 0.42;
    }
    gl_Position = uMvp * vec4(position, 1.0);
    // Keep squared distance for fog interpolation; avoids a per-vertex square root.
    vec3 fogDelta = position - uCameraPosition;
    vFogDistanceSquared = dot(fogDelta, fogDelta);
    vec3 normal = normalize(aNormal);
    float diffuse = max(dot(normal, normalize(-uLightDirection)), 0.0);
    float qualityFactor = clamp(uQuality * 0.5, 0.0, 1.0);
    float skyExposure = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float ambientBase = mix(0.23, 0.32, qualityFactor);
    float hemisphereFill = mix(0.88, 1.08, skyExposure);
    float directLight = diffuse * (0.12 + 0.58 * uDaylight);
    float illumination = clamp(ambientBase * hemisphereFill + directLight, 0.20, 1.12);
    vec3 material = aColor;
    if (fluidCode < -1.5) material = vec3(1.0, 0.43, 0.16);
    else if (fluidCode < -0.5) material = vec3(0.36, 0.72, 0.91);
    vColor = material * illumination;
    vTexCoord = aTexCoord;
    vAtlasTile = int(aAtlasTile + 0.5);
}
)";

constexpr char kWorldFragmentShader[] = R"(#version 300 es
precision mediump float;
in mediump vec3 vColor;
in mediump vec2 vTexCoord;
flat in highp int vAtlasTile;
in mediump float vFogDistanceSquared;
uniform sampler2D uBlockAtlas;
uniform int uUseAtlas;
uniform sampler2D uEntityTexture;
uniform int uUseEntityTexture;
uniform vec3 uFogColor;
uniform float uFogNear;
uniform float uFogFar;
out vec4 outColor;
vec2 AtlasUv(vec2 localUv, int tile) {
    vec2 atlasSize = vec2(textureSize(uBlockAtlas, 0));
    float column = float(tile - (tile / 32) * 32);
    float row = float(tile / 32);
    vec2 pixel = vec2(column * 18.0 + 1.5 + fract(localUv.x) * 15.0,
                      row * 18.0 + 1.5 + (1.0 - fract(localUv.y)) * 15.0);
    // The RGBA atlas is uploaded row-major without a vertical flip. OpenGL
    // treats the first uploaded row as v=0, so do not invert the atlas row again.
    return vec2(pixel.x / atlasSize.x, pixel.y / atlasSize.y);
}
void main() {
    vec4 texel = (uUseEntityTexture == 1)
        ? texture(uEntityTexture, vTexCoord)
        : ((uUseAtlas == 1) ? texture(uBlockAtlas, AtlasUv(vTexCoord, vAtlasTile)) : vec4(1.0));
    vec3 litColor = texel.rgb * vColor;
    float fogNearSquared = uFogNear * uFogNear;
    float fogFarSquared = uFogFar * uFogFar;
    float fog = smoothstep(fogNearSquared, fogFarSquared, max(vFogDistanceSquared, 0.0));
    outColor = vec4(mix(litColor, uFogColor, fog), texel.a);
}
)";

Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

} // namespace

Engine& GetEngine() {
    static Engine engine;
    return engine;
}

std::string Engine::LastInitError() const { return lastInitError_; }

bool Engine::IsUiReady() const { return ui_.IsReady(); }

void Engine::FallbackAction(int action, bool pressed) {
    if (!ready_) return;
    // 0 jump, 1 crouch, 2 perspective cycle, 3 pause, 4 resume, 5 save/exit.
    // 6..9 are held D-pad directions: forward, backward, left, right.
    if (action == 1) {
        // Match the main RmlUi HUD: crouch is a toggle, not a hold-only control.
        if (!pressed) return;
        crouching_ = !crouching_;
        player_.SetCrouching(crouching_);
        ui_.SetCrouchEnabled(crouching_);
        return;
    }
    if (action >= 6 && action <= 9) {
        fallbackMovementButtons_[static_cast<std::size_t>(action - 6)] = pressed;
        UpdateTouchMovement();
        return;
    }
    if (!pressed) return;
    if (action >= 20 && action <= 28) {
        inventory_.selectedSlot = static_cast<std::uint16_t>(action - 20);
        RefreshInventoryUi();
        return;
    }
    switch (action) {
        case 0:
            if (!paused_ && player_.grounded) {
                player_.Jump();
                if (!player_.grounded) {
                    if (progressionJumps_ < 10000000u) ++progressionJumps_;
                    if (gameMode_ != 1) AddProgressionXp(1u);
                    soundEvents_.fetch_or(16u, std::memory_order_release);
                }
            }
            break;
        case 2: CyclePerspective(); break;
        case 3:
            if (!paused_) {
                fallbackMovementButtons_.fill(false);
                UpdateTouchMovement();
                crouching_ = false;
                player_.SetCrouching(false);
                Pause();
                pauseVisible_ = true;
            } else {
                pauseVisible_ = false;
                inventoryVisible_ = false;
                paused_ = false;
            }
            break;
        case 4:
            pauseVisible_ = false;
            inventoryVisible_ = false;
            Resume();
            break;
        case 5:
            SaveWorld();
            exitRequested_.store(true, std::memory_order_release);
            break;
        case 10: HandleUiAction("attack"); break;
        default: break;
    }
}

void Engine::SetLastInitError(std::string message) { lastInitError_ = std::move(message); }

std::uint32_t Engine::ConsumeSoundEvents() {
    return soundEvents_.exchange(0u, std::memory_order_acq_rel);
}

Engine::Mat4 Engine::Perspective(float fovRadians, float aspect, float nearPlane, float farPlane) {
    const float safeAspect = std::max(aspect, 0.001f);
    const float f = 1.0f / std::tan(fovRadians * 0.5f);
    Mat4 result{};
    result.v[0] = f / safeAspect;
    result.v[5] = f;
    result.v[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    result.v[11] = -1.0f;
    result.v[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return result;
}

Engine::Mat4 Engine::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const auto normalize = [](const Vec3& value) {
        const float length = std::sqrt(std::max(value.x * value.x + value.y * value.y + value.z * value.z, 0.000001f));
        return Vec3{value.x / length, value.y / length, value.z / length};
    };
    const auto cross = [](const Vec3& a, const Vec3& b) {
        return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    const auto dot = [](const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
    const Vec3 forward = normalize({target.x - eye.x, target.y - eye.y, target.z - eye.z});
    const Vec3 side = normalize(cross(forward, up));
    const Vec3 correctedUp = cross(side, forward);
    Mat4 result{};
    result.v[0] = side.x; result.v[1] = correctedUp.x; result.v[2] = -forward.x; result.v[3] = 0.0f;
    result.v[4] = side.y; result.v[5] = correctedUp.y; result.v[6] = -forward.y; result.v[7] = 0.0f;
    result.v[8] = side.z; result.v[9] = correctedUp.z; result.v[10] = -forward.z; result.v[11] = 0.0f;
    result.v[12] = -dot(side, eye); result.v[13] = -dot(correctedUp, eye); result.v[14] = dot(forward, eye); result.v[15] = 1.0f;
    return result;
}

Engine::Mat4 Engine::Multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k)
                value += a.v[static_cast<std::size_t>(k * 4 + row)] * b.v[static_cast<std::size_t>(column * 4 + k)];
            result.v[static_cast<std::size_t>(column * 4 + row)] = value;
        }
    }
    return result;
}

CameraFrustum Engine::ExtractFrustum(const Mat4& mvp) {
    const auto row=[&](int index) {
        return std::array<float,4>{
            mvp.v[static_cast<std::size_t>(index)],
            mvp.v[static_cast<std::size_t>(4+index)],
            mvp.v[static_cast<std::size_t>(8+index)],
            mvp.v[static_cast<std::size_t>(12+index)]
        };
    };
    const auto r0=row(0), r1=row(1), r2=row(2), r3=row(3);
    const auto makePlane=[](const std::array<float,4>& a,
                            const std::array<float,4>& b,float sign) {
        FrustumPlane plane{a[0]+sign*b[0],a[1]+sign*b[1],
                           a[2]+sign*b[2],a[3]+sign*b[3]};
        const float length=std::sqrt(std::max(
            plane.x*plane.x+plane.y*plane.y+plane.z*plane.z,0.000001f));
        plane.x/=length; plane.y/=length; plane.z/=length; plane.d/=length;
        return plane;
    };
    CameraFrustum frustum;
    frustum.planes={{
        makePlane(r3,r0, 1.0f),makePlane(r3,r0,-1.0f),
        makePlane(r3,r1, 1.0f),makePlane(r3,r1,-1.0f),
        makePlane(r3,r2, 1.0f),makePlane(r3,r2,-1.0f)
    }};
    frustum.valid=true;
    return frustum;
}

GLuint Engine::CompileShader(GLenum type, const char* source) {
    const char* stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        const GLenum error = glGetError();
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
        gWorldShaderDiagnostic = std::string("glCreateShader(") + stage +
                ") devolvió 0; GL error " + code;
        ENGINE_LOGE("%s", gWorldShaderDiagnostic.c_str());
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        GLsizei written = 0;
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written, log.data());
        if (written > 0) log.resize(static_cast<std::size_t>(written));
        else log.clear();
        gWorldShaderDiagnostic = std::string("Falló la compilación del shader ") + stage +
                (log.empty() ? ": el controlador no entregó un registro GLSL." : ": " + log);
        ENGINE_LOGE("%s", gWorldShaderDiagnostic.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Engine::CreateWorldProgram() {
    gWorldShaderDiagnostic.clear();
    const auto destroyCurrentProgram = [this]() {
        if (worldProgram_ != 0) glDeleteProgram(worldProgram_);
        worldProgram_ = 0;
    };
    const auto buildProgram = [this, &destroyCurrentProgram](const char* vertexSource,
                                                              const char* fragmentSource,
                                                              std::string& diagnostic) {
        diagnostic.clear();
        const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
        if (vertex == 0) {
            diagnostic = gWorldShaderDiagnostic;
            return false;
        }
        const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
        if (fragment == 0) {
            diagnostic = gWorldShaderDiagnostic;
            glDeleteShader(vertex);
            return false;
        }
        worldProgram_ = glCreateProgram();
        if (worldProgram_ == 0) {
            const GLenum error = glGetError();
            char code[24]{};
            std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
            diagnostic = std::string("glCreateProgram devolvió 0; GL error ") + code;
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            ENGINE_LOGE("%s", diagnostic.c_str());
            return false;
        }
        glAttachShader(worldProgram_, vertex);
        glAttachShader(worldProgram_, fragment);
        glLinkProgram(worldProgram_);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = GL_FALSE;
        glGetProgramiv(worldProgram_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(worldProgram_, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            GLsizei written = 0;
            glGetProgramInfoLog(worldProgram_, static_cast<GLsizei>(log.size()), &written, log.data());
            if (written > 0) log.resize(static_cast<std::size_t>(written));
            else log.clear();
            diagnostic = log.empty() ? "El programa no enlazó y el controlador no entregó un registro."
                                     : "Falló el enlace GLSL: " + log;
            ENGINE_LOGE("%s", diagnostic.c_str());
            destroyCurrentProgram();
            return false;
        }
        return true;
    };
    const auto fetchLocations = [this]() {
        mvpLocation_ = glGetUniformLocation(worldProgram_, "uMvp");
        lightLocation_ = glGetUniformLocation(worldProgram_, "uLightDirection");
        daylightLocation_ = glGetUniformLocation(worldProgram_, "uDaylight");
        atlasLocation_ = glGetUniformLocation(worldProgram_, "uBlockAtlas");
        useAtlasLocation_ = glGetUniformLocation(worldProgram_, "uUseAtlas");
        entityTextureLocation_ = glGetUniformLocation(worldProgram_, "uEntityTexture");
        useEntityTextureLocation_ = glGetUniformLocation(worldProgram_, "uUseEntityTexture");
        timeLocation_ = glGetUniformLocation(worldProgram_, "uTime");
        qualityLocation_ = glGetUniformLocation(worldProgram_, "uQuality");
        waterWavesLocation_ = glGetUniformLocation(worldProgram_, "uWaterWaves");
        lavaAnimationsLocation_ = glGetUniformLocation(worldProgram_, "uLavaAnimations");
        leafWindLocation_ = glGetUniformLocation(worldProgram_, "uLeafWind");
        leafWindStrengthLocation_ = glGetUniformLocation(worldProgram_, "uLeafWindStrength");
        cameraLocation_ = glGetUniformLocation(worldProgram_, "uCameraPosition");
        fogColorLocation_ = glGetUniformLocation(worldProgram_, "uFogColor");
        fogNearLocation_ = glGetUniformLocation(worldProgram_, "uFogNear");
        fogFarLocation_ = glGetUniformLocation(worldProgram_, "uFogFar");
    };

    std::string fullDiagnostic;
    if (buildProgram(kWorldVertexShader, kWorldFragmentShader, fullDiagnostic)) {
        fetchLocations();
        if (mvpLocation_ >= 0 && lightLocation_ >= 0 && daylightLocation_ >= 0 &&
            atlasLocation_ >= 0 && useAtlasLocation_ >= 0 &&
            entityTextureLocation_ >= 0 && useEntityTextureLocation_ >= 0 &&
            timeLocation_ >= 0 && qualityLocation_ >= 0 &&
            waterWavesLocation_ >= 0 && cameraLocation_ >= 0 &&
            fogColorLocation_ >= 0 && fogNearLocation_ >= 0 && fogFarLocation_ >= 0) {
            glUseProgram(worldProgram_);
            glUniform1i(atlasLocation_, 0);
            glUniform1i(useAtlasLocation_, 1);
            glUniform1i(entityTextureLocation_, 1);
            glUniform1i(useEntityTextureLocation_, 0);
            glUseProgram(0);
            ENGINE_LOGI("Full world shader compiled and linked");
            return true;
        }
        fullDiagnostic = "El shader principal enlazó, pero falta al menos un uniform obligatorio.";
        ENGINE_LOGE("%s", fullDiagnostic.c_str());
        destroyCurrentProgram();
    } else {
        ENGINE_LOGW("Primary world shader rejected: %s", fullDiagnostic.c_str());
    }

    // Some mobile driver versions are fragile with larger shader variants. Keep a
    // deliberately small ES 3.0 path so the world remains playable without fog,
    // animated water or per-fragment lighting if the full program is rejected.
    constexpr char fallbackVertex[] = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=2) in vec3 aColor;
layout(location=3) in vec2 aTexCoord;
layout(location=4) in float aAtlasTile;
uniform mat4 uMvp;
out mediump vec3 vColor;
out mediump vec2 vTexCoord;
flat out highp int vAtlasTile;
void main() {
    gl_Position = uMvp * vec4(aPosition, 1.0);
    vColor = aColor;
    vTexCoord = aTexCoord;
    vAtlasTile = int(aAtlasTile + 0.5);
}
)";
    constexpr char fallbackFragment[] = R"(#version 300 es
precision mediump float;
in mediump vec3 vColor;
in mediump vec2 vTexCoord;
flat in highp int vAtlasTile;
uniform sampler2D uBlockAtlas;
uniform int uUseAtlas;
uniform sampler2D uEntityTexture;
uniform int uUseEntityTexture;
out vec4 outColor;
vec2 AtlasUv(vec2 localUv, int tile) {
    vec2 atlasSize = vec2(textureSize(uBlockAtlas, 0));
    float column = float(tile - (tile / 32) * 32);
    float row = float(tile / 32);
    vec2 pixel = vec2(column * 18.0 + 1.5 + fract(localUv.x) * 15.0,
                      row * 18.0 + 1.5 + (1.0 - fract(localUv.y)) * 15.0);
    // The RGBA atlas is uploaded row-major without a vertical flip. OpenGL
    // treats the first uploaded row as v=0, so do not invert the atlas row again.
    return vec2(pixel.x / atlasSize.x, pixel.y / atlasSize.y);
}
void main() {
    vec4 texel = (uUseEntityTexture == 1)
        ? texture(uEntityTexture, vTexCoord)
        : ((uUseAtlas == 1) ? texture(uBlockAtlas, AtlasUv(vTexCoord, vAtlasTile)) : vec4(1.0));
    vec3 material = vColor;
    // Fluid tags are encoded in the red component by the mesh builder. The
    // conservative shader must decode them too; multiplying the negative tag
    // directly by the atlas turns water/lava black on fallback-only drivers.
    if (vColor.r < -1.5) material = vec3(1.0, 0.43, 0.16);
    else if (vColor.r < -0.5) material = vec3(0.36, 0.72, 0.91);
    outColor = vec4(texel.rgb * material, texel.a);
}
)";
    std::string fallbackDiagnostic;
    if (!buildProgram(fallbackVertex, fallbackFragment, fallbackDiagnostic)) {
        destroyCurrentProgram();
        gWorldShaderDiagnostic = "Shader principal: " + fullDiagnostic +
                "\nShader alternativo: " + fallbackDiagnostic;
        if (gWorldShaderDiagnostic.size() > 760u) gWorldShaderDiagnostic.resize(760u);
        return false;
    }
    fetchLocations();
    if (mvpLocation_ < 0 || atlasLocation_ < 0 || useAtlasLocation_ < 0 ||
        entityTextureLocation_ < 0 || useEntityTextureLocation_ < 0) {
        fallbackDiagnostic = "El shader alternativo enlazó, pero faltan uniforms obligatorios.";
        gWorldShaderDiagnostic = "Shader principal: " + fullDiagnostic +
                "\nShader alternativo: " + fallbackDiagnostic;
        ENGINE_LOGE("%s", gWorldShaderDiagnostic.c_str());
        destroyCurrentProgram();
        return false;
    }
    glUseProgram(worldProgram_);
    glUniform1i(atlasLocation_, 0);
    glUniform1i(useAtlasLocation_, 1);
    glUniform1i(entityTextureLocation_, 1);
    glUniform1i(useEntityTextureLocation_, 0);
    glUseProgram(0);
    ENGINE_LOGW("Using conservative ES 3.0 fallback world shader");
    return true;
}

bool Engine::UploadBlockAtlas(const std::uint8_t* pixels, std::size_t bytes, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) return false;
    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    if (maximumTextureSize <= 0 || width > maximumTextureSize || height > maximumTextureSize) {
        ENGINE_LOGE("Atlas dimensions %dx%d exceed GL_MAX_TEXTURE_SIZE=%d",
                    width, height, maximumTextureSize);
        return false;
    }
    const std::size_t safeWidth = static_cast<std::size_t>(width);
    const std::size_t safeHeight = static_cast<std::size_t>(height);
    if (safeWidth > std::numeric_limits<std::size_t>::max() / safeHeight / 4u) {
        ENGINE_LOGE("Atlas byte count would overflow size_t");
        return false;
    }
    const std::size_t expected = safeWidth * safeHeight * 4u;
    if (bytes != expected) {
        ENGINE_LOGE("Atlas byte count mismatch: got %zu expected %zu", bytes, expected);
        return false;
    }

    // This renderer shares one GLES context with entity/UI texture uploads.
    // A bound PBO changes the pixels argument from a CPU address into a byte
    // offset, and a texture upload must not replace another subsystem's unit
    // zero binding. Save and restore all upload state on every exit path.
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTextureBinding = 0;
    GLint previousUnpackAlignment = 4;
    GLint previousUnpackRowLength = 0;
    GLint previousUnpackSkipPixels = 0;
    GLint previousUnpackSkipRows = 0;
    GLint previousPixelUnpackBuffer = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previousUnpackRowLength);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &previousUnpackSkipPixels);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &previousUnpackSkipRows);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousPixelUnpackBuffer);

    const auto restoreUploadState = [&]() {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,
                     static_cast<GLuint>(previousPixelUnpackBuffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, previousUnpackRowLength);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, previousUnpackSkipPixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, previousUnpackSkipRows);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    };

    // glTexImage2D interprets its pointer as a PBO offset while a pixel
    // unpack buffer is bound. Explicitly select client-memory upload mode.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glGenTextures(1, &blockAtlasTexture_);
    if (blockAtlasTexture_ == 0) {
        restoreUploadState();
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, blockAtlasTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    const GLenum error = glGetError();
    restoreUploadState();

    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &blockAtlasTexture_);
        blockAtlasTexture_ = 0;
        ENGINE_LOGE("Block atlas upload failed: GL error 0x%04X",
                    static_cast<unsigned int>(error));
        return false;
    }
    return true;
}

bool Engine::UploadSheepTexture(const std::uint8_t* pixels, std::size_t bytes) {
    constexpr int width = 64;
    constexpr int height = 32;
    constexpr std::size_t expectedBytes = static_cast<std::size_t>(width * height * 4);
    if (pixels == nullptr || bytes != expectedBytes) {
        ENGINE_LOGW("MAES sheep texture has invalid size (%zu); using baked face-colour fallback", bytes);
        return false;
    }

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousBinding = 0;
    GLint previousUnpackAlignment = 4;
    GLint previousUnpackBuffer = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousBinding);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousUnpackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glGenTextures(1, &sheepTexture_);
    if (sheepTexture_ == 0) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(previousUnpackBuffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousBinding));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, sheepTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    const GLenum error = glGetError();

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(previousUnpackBuffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousBinding));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));

    if (error != GL_NO_ERROR) {
        ENGINE_LOGW("MAES sheep texture upload failed: GL error 0x%04X; using baked face-colour fallback",
                    static_cast<unsigned int>(error));
        glDeleteTextures(1, &sheepTexture_);
        sheepTexture_ = 0;
        return false;
    }
    return true;
}

bool Engine::Initialise(float displayDensity, const std::uint8_t* atlasPixels,
                        std::size_t atlasBytes, int atlasWidth, int atlasHeight,
                        const std::uint8_t* sheepModelBytes, std::size_t sheepModelSize,
                        const std::uint8_t* sheepTextureRgba, std::size_t sheepTextureBytes) {
    // Preserve the previous world first (Shutdown saves it when appropriate), then
    // reset every gameplay subsystem before loading or creating the next session.
    // Without this reset, a failed/missing save could silently reuse stale chunks,
    // entities, drops, inventory and player state from the previous GL session.
    lastInitError_.clear();
    const auto glString = [](GLenum name) {
        const GLubyte* value = glGetString(name);
        return value != nullptr ? reinterpret_cast<const char*>(value) : "no disponible";
    };
    GLint maxTextureSize = 0;
    GLint maxRenderbufferSize = 0;
    GLint extensionCount = 0;
    GLint framebufferBinding = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferBinding);
    GLint maxViewport[2]{0, 0};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
    char extensionSummary[2048]{};
    std::size_t extensionLength = 0u;
    for (GLint index = 0; index < extensionCount; ++index) {
        const GLubyte* extension = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index));
        if (extension == nullptr) continue;
        const int written = std::snprintf(extensionSummary + extensionLength,
            sizeof(extensionSummary) - extensionLength, "%s%s",
            extensionLength == 0u ? "" : " ", reinterpret_cast<const char*>(extension));
        if (written < 0) break;
        if (static_cast<std::size_t>(written) >= sizeof(extensionSummary) - extensionLength) {
            extensionLength = sizeof(extensionSummary) - 1u;
            break;
        }
        extensionLength += static_cast<std::size_t>(written);
    }
    const GLenum startupFramebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    ENGINE_LOGI("GLES startup vendor=%s renderer=%s version=%s GLSL=%s maxTexture=%d maxRenderbuffer=%d maxViewport=%dx%d extensions=%d framebuffer=%d status=0x%04X",
        glString(GL_VENDOR), glString(GL_RENDERER), glString(GL_VERSION),
        glString(GL_SHADING_LANGUAGE_VERSION), maxTextureSize, maxRenderbufferSize,
        maxViewport[0], maxViewport[1], extensionCount, framebufferBinding,
        static_cast<unsigned int>(startupFramebufferStatus));
    if (extensionLength > 0u) ENGINE_LOGI("GLES extensions: %s", extensionSummary);
    if (startupFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        ENGINE_LOGW("Default startup framebuffer is incomplete: 0x%04X",
                    static_cast<unsigned int>(startupFramebufferStatus));
    }
    frameTimeSamplesMs_.fill(0.0f);
    frameTimeSampleCursor_ = frameTimeSampleCount_ = 0u;
    lastChunkGenerationPumpMs_ = lastMeshBuildMs_ = lastGpuUploadMs_ = 0.0f;
    // MainActivity has already paused and saved the previous world on the GL
    // thread before changing storagePath_. Do not save the previous in-memory
    // world again here: storagePath_ now belongs to the NEW world.
    Shutdown(false);
    playerIdentity_ = pendingPlayerIdentity_;
    if (!storagePath_.empty() && !protection_.Load(storagePath_ + ".claims"))
        ENGINE_LOGW("Claim file missing or damaged; protection remains fail-closed");
    world_ = VoxelWorld(0x4D464445u);
    player_ = Player();
    player_.SetAppearance(avatarAppearance_);
    inventory_ = Inventory();
    lastQuickbarSelection_ = 0xffffu;
    selectedItemToastUntil_ = 0.0;
    firstPersonItemId_ = 0xffffu;
    firstPersonSwitchStarted_ = 0.0;
    firstPersonActionStarted_ = 0.0;
    firstPersonActionKind_ = 0;
    progressionXp_ = 0u;
    progressionBlocksMined_ = 0u;
    progressionBlocksPlaced_ = 0u;
    progressionItemsCollected_ = 0u;
    progressionJumps_ = 0u;
    progressionWorldEntered_ = false;
    droppedItems_ = DroppedItemSystem();
    entities_ = EntitySystem();
    environment_ = EnvironmentSystem();
    remotePlayers_.clear();
    pendingBlockEdits_.clear();
    touches_.clear();
    inventoryVisible_ = false;
    pauseVisible_ = false;
    thirdPerson_ = false;
    crouching_ = false;
    player_.SetCrouching(false);
    miningPointerId_ = -1;
    miningElapsed_ = 0.0f;
    miningRequired_ = 0.0f;
    pickupToastText_.clear();
    entityMeshAccumulator_ = 0.0f;
    itemMeshAccumulator_ = 0.0f;
    minimapAccumulator_ = 0.0f;
    playerHealth_ = 20.0f;
    playerHunger_ = 20.0f;
    hungerExhaustion_ = 0.0f;
    hungerDrainTimer_ = 0.0f;
    starvationTimer_ = 0.0f;
    regenerationTimer_ = 0.0f;
    hostileDamageCooldown_ = 0.0f;
    hostileSpawnTimer_ = 0.0f;
    hostileSpawnSequence_ = 0u;
    exitRequested_.store(false, std::memory_order_release);
    soundEvents_.store(0u, std::memory_order_release);

    multiplayerMode_ = pendingMultiplayerMode_;
    multiplayerSlot_ = pendingMultiplayerSlot_;
    gameMode_ = pendingGameMode_;
    allowCheats_ = pendingAllowCheats_;
    starterChestEnabled_ = pendingStarterChestEnabled_;
    requestedWorldSeed_ = pendingWorldSeed_;
    flatWorld_ = pendingFlatWorld_;
    difficulty_ = pendingDifficulty_;
    generateStructures_ = pendingGenerateStructures_;
    graphicsQuality_ = pendingGraphicsQuality_;
    renderDistanceChunks_ = pendingRenderDistanceChunks_;
    waterWaves_ = pendingWaterWaves_;
    fieldOfView_ = pendingFieldOfView_;
    contactShadows_ = pendingContactShadows_;
    player_.SetContactShadows(contactShadows_);
    entities_.SetContactShadows(contactShadows_);
    animationQuality_ = pendingAnimationQuality_;
    animationsEnabled_ = pendingAnimationsEnabled_;
    entityVisuals_.Clear();
    if (sheepModelBytes != nullptr && sheepModelSize > 0u) {
        std::string modelError;
        if (!entityVisuals_.LoadSheepModel(sheepModelBytes, sheepModelSize, modelError)) {
            ENGINE_LOGW("MAES sheep model rejected; retaining legacy kind 0 renderer: %s",
                        modelError.c_str());
        } else {
            ENGINE_LOGI("MAES sheep model loaded: %s", entityVisuals_.SheepDiagnostic().c_str());
        }
    } else {
        ENGINE_LOGW("MAES sheep model asset missing; retaining legacy kind 0 renderer");
    }
    player_.SetAnimationOptions(static_cast<std::uint8_t>(animationQuality_), animationsEnabled_);
    entities_.SetAnimationOptions(static_cast<std::uint8_t>(animationQuality_), animationsEnabled_);
    lookSensitivityX_ = pendingLookSensitivityX_;
    lookSensitivityY_ = pendingLookSensitivityY_;
    invertLookY_ = pendingInvertLookY_;
    leftHandedControls_ = pendingLeftHandedControls_;
    joystickDeadzone_ = pendingJoystickDeadzone_;
    autoJump_ = pendingAutoJump_;
    autoJumpCooldown_ = 0.0f;
    showHudStats_ = pendingShowHudStats_;
    ENGINE_LOGI("Starting Minefade Native Engine C++20 / OpenGL ES 3");
    const auto* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const auto* glslVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    ENGINE_LOGI("OpenGL version=%s GLSL=%s vendor=%s renderer=%s maxTexture=%d",
                glVersion != nullptr ? glVersion : "unknown",
                glslVersion != nullptr ? glslVersion : "unknown",
                glVendor != nullptr ? glVendor : "unknown",
                glRenderer != nullptr ? glRenderer : "unknown", maxTextureSize);

    const auto failInitialisation = [this](const std::string& reason) {
        GLenum glError = glGetError();
        lastInitError_ = reason;
        if (glError != GL_NO_ERROR) {
            char code[24]{};
            std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(glError));
            lastInitError_ += std::string(" | OpenGL error ") + code;
        }
        ENGINE_LOGE("World initialisation failed: %s; releasing partial GL and UI state",
                    lastInitError_.c_str());
        Shutdown();
        return false;
    };

    if (minefade_core_abi_version() != MINEFADE_CORE_ABI_VERSION) {
        return failInitialisation("La versión ABI del núcleo Rust no coincide con la esperada por el motor C++.");
    }

    // Clear any stale driver error before attributing a failure to a startup stage.
    while (glGetError() != GL_NO_ERROR) { }
    if (!CreateWorldProgram()) {
        std::string reason = "No se pudo compilar o enlazar el shader principal del mundo.";
        if (!gWorldShaderDiagnostic.empty()) {
            reason += "\nDetalle GLSL: " + gWorldShaderDiagnostic;
        }
        return failInitialisation(reason);
    }
    if (!UploadBlockAtlas(atlasPixels, atlasBytes, atlasWidth, atlasHeight)) {
        return failInitialisation("No se pudo cargar la textura atlas de bloques. Comprueba sus dimensiones, memoria gráfica y GL_MAX_TEXTURE_SIZE.");
    }
    if (sheepTextureRgba != nullptr && sheepTextureBytes > 0u)
        UploadSheepTexture(sheepTextureRgba, sheepTextureBytes);
    glGenVertexArrays(1, &entityVao_);
    glGenBuffers(1, &entityVbo_);
    glGenVertexArrays(1, &maesEntityVao_);
    glGenBuffers(1, &maesEntityVbo_);
    glGenVertexArrays(1, &itemVao_);
    glGenBuffers(1, &itemVbo_);
    if (entityVao_ == 0 || entityVbo_ == 0 ||
        maesEntityVao_ == 0 || maesEntityVbo_ == 0 ||
        itemVao_ == 0 || itemVbo_ == 0)
        return failInitialisation("El controlador no pudo reservar los VAO/VBO de entidades u objetos. Posible falta de memoria gráfica.");
    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
        return failInitialisation(std::string("Error al reservar buffers OpenGL: ") + code);
    }

    starterChestOpen_ = false;
    starterChestContents_ = {{1064u, 5064u, 64016u, 6004u, 3064u, 0u, 0u, 0u, 0u}};
    bool loadedExistingWorld = false;
    if (multiplayerMode_) {
        // The host opens the selected local save; joining peers generate the same
        // deterministic terrain from the host's advertised seed and settings.
        const bool hasWorldSavePath = storagePath_.size() >= 8u &&
            storagePath_.compare(storagePath_.size() - 8u, 8u, ".mfworld") == 0;
        if (multiplayerSlot_ == 0 && hasWorldSavePath) {
            loadedExistingWorld = world_.Load(storagePath_, player_.position, inventory_.counts);
            if (loadedExistingWorld) world_.SetGenerationOptions(flatWorld_, generateStructures_);
        }
        if (!loadedExistingWorld) {
            world_ = VoxelWorld(requestedWorldSeed_, flatWorld_, generateStructures_);
        }
        const bool isExistingHostWorld = loadedExistingWorld && multiplayerSlot_ == 0;
        const int spawnX = 8 + (multiplayerSlot_ % 4) * 2;
        const int spawnZ = 8 + ((multiplayerSlot_ / 4) % 4) * 2;
        if (!isExistingHostWorld) {
            player_.position = {static_cast<float>(spawnX),
                                static_cast<float>(world_.GetSurfaceHeight(spawnX, spawnZ)) + 1.05f,
                                static_cast<float>(spawnZ)};
        }
        // Loaded worlds may have their player thousands of blocks from origin.
        // Generate the first resident ring around the actual saved position.
        const int generationX = isExistingHostWorld
            ? static_cast<int>(std::floor(player_.position.x)) : spawnX;
        const int generationZ = isExistingHostWorld
            ? static_cast<int>(std::floor(player_.position.z)) : spawnZ;
        world_.GenerateAround(generationX, generationZ, 1);
        world_.RequestGenerateAround(generationX, generationZ, renderDistanceChunks_);
    } else if (!storagePath_.empty()) {
        loadedExistingWorld = world_.Load(storagePath_, player_.position, inventory_.counts);
        if (loadedExistingWorld) world_.SetGenerationOptions(flatWorld_, generateStructures_);
        if (!loadedExistingWorld) {
            // Preserve an unreadable existing save before making a fresh world.
            std::error_code backupError;
            const bool saveExists = std::filesystem::exists(storagePath_, backupError) && !backupError;
            if (saveExists) {
                const std::string backupPath = storagePath_ + ".recovery-backup";
                std::filesystem::copy_file(storagePath_, backupPath,
                    std::filesystem::copy_options::none, backupError);
                if (!backupError) ENGINE_LOGE("Unreadable world preserved at %s", backupPath.c_str());
                else ENGINE_LOGE("Could not preserve unreadable world: %s", backupError.message().c_str());
            }
            world_ = VoxelWorld(requestedWorldSeed_, flatWorld_, generateStructures_);
            world_.GenerateAround(8, 8, 0);
            player_.position = {8.0f, static_cast<float>(world_.GetSurfaceHeight(8, 8)) + 1.05f, 8.0f};
        }
    } else {
        world_ = VoxelWorld(requestedWorldSeed_);
        world_.GenerateAround(8, 8, 0);
        player_.position = {8.0f, static_cast<float>(world_.GetSurfaceHeight(8, 8)) + 1.05f, 8.0f};
    }
    if (loadedExistingWorld && !storagePath_.empty()) {
        // Progression uses a sidecar to keep old MFVOX world formats byte-compatible.
        std::ifstream progressionFile(storagePath_ + ".progression", std::ios::binary);
        if (progressionFile) {
            std::uint32_t magic = 0u, version = 0u, savedXp = 0u;
            std::uint32_t mined = 0u, placed = 0u, collected = 0u, jumps = 0u;
            std::uint32_t entered = 0u;
            progressionFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            progressionFile.read(reinterpret_cast<char*>(&version), sizeof(version));
            progressionFile.read(reinterpret_cast<char*>(&savedXp), sizeof(savedXp));
            progressionFile.read(reinterpret_cast<char*>(&mined), sizeof(mined));
            progressionFile.read(reinterpret_cast<char*>(&placed), sizeof(placed));
            progressionFile.read(reinterpret_cast<char*>(&collected), sizeof(collected));
            progressionFile.read(reinterpret_cast<char*>(&jumps), sizeof(jumps));
            progressionFile.read(reinterpret_cast<char*>(&entered), sizeof(entered));
            if (progressionFile && progressionFile.peek() == std::char_traits<char>::eof() &&
                magic == 0x4D465052u && version == 1u && savedXp <= 100000000u &&
                mined <= 10000000u && placed <= 10000000u && collected <= 10000000u &&
                jumps <= 10000000u && entered <= 1u) {
                progressionXp_ = savedXp;
                progressionBlocksMined_ = mined;
                progressionBlocksPlaced_ = placed;
                progressionItemsCollected_ = collected;
                progressionJumps_ = jumps;
                progressionWorldEntered_ = entered != 0u;
            } else {
                ENGINE_LOGW("Ignored invalid progression sidecar; starting progression safely");
            }
        }
        // Survival stats are stored in a validated sidecar so MFVOX001/002/003
        // remain byte-for-byte compatible with existing worlds.
        std::ifstream survivalFile(storagePath_ + ".survival", std::ios::binary);
        if (survivalFile) {
            std::uint32_t magic = 0u;
            std::uint32_t version = 0u;
            float savedHealth = 20.0f;
            float savedHunger = 20.0f;
            survivalFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            survivalFile.read(reinterpret_cast<char*>(&version), sizeof(version));
            survivalFile.read(reinterpret_cast<char*>(&savedHealth), sizeof(savedHealth));
            survivalFile.read(reinterpret_cast<char*>(&savedHunger), sizeof(savedHunger));
            if (survivalFile && survivalFile.peek() == std::char_traits<char>::eof() &&
                magic == 0x4D465356u && version == 1u &&
                std::isfinite(savedHealth) && std::isfinite(savedHunger) &&
                savedHealth >= 0.0f && savedHealth <= 20.0f &&
                savedHunger >= 0.0f && savedHunger <= 20.0f) {
                playerHealth_ = savedHealth;
                playerHunger_ = savedHunger;
            } else {
                ENGINE_LOGW("Ignored invalid survival sidecar; retaining safe default health/hunger");
            }
        }
        std::ifstream durabilityFile(storagePath_ + ".durability", std::ios::binary);
        if (durabilityFile) {
            std::uint32_t magic = 0u;
            std::uint32_t version = 0u;
            std::array<std::uint16_t, Inventory::kSlotCount> savedDurability{};
            durabilityFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            durabilityFile.read(reinterpret_cast<char*>(&version), sizeof(version));
            durabilityFile.read(reinterpret_cast<char*>(savedDurability.data()),
                static_cast<std::streamsize>(sizeof(savedDurability)));
            bool valid = durabilityFile && durabilityFile.peek() == std::char_traits<char>::eof() &&
                magic == 0x4D464459u && version == 1u;
            if (valid) {
                for (std::size_t slot = 0; slot < savedDurability.size(); ++slot) {
                    if (savedDurability[slot] > 2000u) { valid = false; break; }
                    const std::uint16_t itemId = static_cast<std::uint16_t>(inventory_.counts[slot] / 1000u);
                    if (itemId == 0u) savedDurability[slot] = 0u;
                }
            }
            if (valid) inventory_.toolDurability = savedDurability;
            else ENGINE_LOGW("Ignored invalid tool durability sidecar; legacy tools start at full durability");
        }
        std::ifstream gearFile(storagePath_ + ".armor", std::ios::binary);
        if (gearFile) {
            std::array<std::uint16_t,4> savedGear{};
            gearFile.read(reinterpret_cast<char*>(savedGear.data()),
                          static_cast<std::streamsize>(sizeof(savedGear)));
            if (gearFile && gearFile.peek() == std::char_traits<char>::eof()) {
                for (std::size_t slot = 0; slot < savedGear.size(); ++slot) {
                    const BlockDefinition* item = GetBlockDefinition(static_cast<BlockId>(savedGear[slot]));
                    if (item == nullptr) continue;
                    const std::string_view key(item->key);
                    const bool valid = slot == 0 ? key.find("_helmet") != std::string_view::npos :
                        slot == 1 ? key.find("_chestplate") != std::string_view::npos :
                        slot == 2 ? key.find("_leggings") != std::string_view::npos :
                                    key.find("_boots") != std::string_view::npos;
                    if (valid) inventory_.equippedArmor[slot] = savedGear[slot];
                }
            }
        }
    }
    if (starterChestEnabled_ && !loadedExistingWorld && !multiplayerMode_) {
        const int chestX = 8;
        const int chestZ = 6;
        const int chestY = world_.GetSurfaceHeight(chestX, chestZ) + 1;
        world_.SetBlock(chestX, chestY, chestZ, BlockId::Chest);
    } else if (starterChestEnabled_ && loadedExistingWorld && !storagePath_.empty()) {
        std::ifstream chestFile(storagePath_ + ".chest", std::ios::binary);
        if (chestFile) {
            std::array<std::uint32_t,9> saved{};
            chestFile.read(reinterpret_cast<char*>(saved.data()),
                           static_cast<std::streamsize>(sizeof(saved)));
            if (chestFile && chestFile.peek() == std::char_traits<char>::eof())
                starterChestContents_ = saved;
        }
    }
    if (!player_.ResolveSafeSpawn(world_)) {
        ENGINE_LOGE("Saved spawn is obstructed and no nearby safe position was found");
        world_.GenerateAround(8, 8, 0);
        player_.position = {8.0f, static_cast<float>(world_.GetSurfaceHeight(8, 8)) + 1.001f, 8.0f};
        if (!player_.ResolveSafeSpawn(world_)) {
            ENGINE_LOGE("Fresh spawn recovery failed; generated terrain has no safe player location");
            return failInitialisation("No se pudo encontrar una posición segura para aparecer incluso después de regenerar el terreno inicial.");
        }
    }
    if (!multiplayerMode_) {
        // Stream the nearby ring after the first frame. Synchronously generating
        // nine 16x16x96 chunks and building their meshes here can stall EGL startup
        // on lower-end Android GPUs and trigger the world's loading timeout.
        world_.RequestGenerateAround(static_cast<int>(std::floor(player_.position.x)),
                                     static_cast<int>(std::floor(player_.position.z)),
                                     renderDistanceChunks_);
    }
    footstepElapsed_ = 0.0f;
    lastChunkX_ = static_cast<int>(std::floor(player_.position.x / 16.0f));
    lastChunkZ_ = static_cast<int>(std::floor(player_.position.z / 16.0f));
    LoadEnabledMods();
    player_.SetSpeedMultiplier(modPlayerSpeed_);
    player_.SetJumpMultiplier(modPlayerJump_);
    player_.crouchSpeedMultiplier = modCrouchSpeed_;
    entities_.SetSpeedMultiplier(modEntitySpeed_);
    entities_.SetModOptions(modPassiveEntitySpeed_, modHostileEntitySpeed_,
                            modFleeRadius_, modAlertRadius_, modSpawnRadius_, modEntityCap_,
                            modSpawnInterval_, modSpawnDistance_, modPassiveEntityCap_);
    droppedItems_.SetModOptions(modPickupRadius_, modDropLifetime_);
    entities_.SpawnInitial(world_);
    const Vec3 modSpawn = player_.position;
    for (int i = 0; i < modSheepSpawns_; ++i) entities_.SpawnEntity(modSpawn, 0, world_);
    for (int i = 0; i < modCowSpawns_; ++i) entities_.SpawnEntity(modSpawn, 1, world_);
    for (int i = 0; i < modBirdSpawns_; ++i) entities_.SpawnEntity(modSpawn, 2, world_);
    for (int i = 0; i < modPigSpawns_; ++i) entities_.SpawnEntity(modSpawn, 3, world_);
    for (int i = 0; i < modRabbitSpawns_; ++i) entities_.SpawnEntity(modSpawn, 6, world_);
    for (int i = 0; i < modZombieSpawns_; ++i) entities_.SpawnEntity(modSpawn, 4, world_);
    for (int i = 0; i < modSkeletonSpawns_; ++i) entities_.SpawnEntity(modSpawn, 5, world_);
    RebuildWorldMesh();
    UploadWorldMesh();
    UploadTransparentWorldMesh();
    RebuildEntityMesh();
    UploadEntityMesh();
    UploadItemMesh();
    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%04X", static_cast<unsigned int>(error));
        return failInitialisation(std::string("Falló la construcción o transferencia de las mallas del mundo: ") + code);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glViewport(0, 0, width_, height_);

    // Keep the generated atlas available to RmlUi so quickbar/inventory icons
    // are cropped from the exact same block textures used by the 3D renderer.
    ui_.SetBlockAtlas(atlasPixels,atlasBytes,atlasWidth,atlasHeight);
    ui_.SetStatusVisible(showHudStats_);
    ui_.SetLeftHanded(leftHandedControls_);
    ui_.SetControlMode(controlMode_);
    if (!ui_.Initialise(width_, height_, displayDensity,
                        [this](const std::string& action) { HandleUiAction(action); })) {
        lastInitError_ = "El HUD RmlUi no está disponible; se activarán los controles Android de respaldo.";
        if (!ui_.LastError().empty()) lastInitError_ += "\nDiagnóstico RmlUi: " + ui_.LastError();
        ENGINE_LOGE("RmlUi unavailable; continuing with Android fallback controls: %s",
                    lastInitError_.c_str());
        // The world renderer and simulation are independent from RmlUi. A HUD
        // parser/driver error must not discard a valid world or make it unplayable.
    }
    lastFrame_ = std::chrono::steady_clock::now();
    lastStats_ = lastFrame_;
    ready_ = true;
    paused_ = false;
    progressionWorldEntered_ = true;
    thirdPerson_ = false;
    thirdPersonFront_ = false;
    fallbackMovementButtons_.fill(false);
    ui_.SetPerspectiveEnabled(false);
    ui_.SetMinimapShape(minimapShape_);
    ui_.SetChestButtonVisible(starterChestEnabled_ && !multiplayerMode_);
    RefreshInventoryUi();
    ENGINE_LOGI("World ready: %zu chunks, %d GPU vertices, seed=%u",
                world_.ChunkCount(),
                worldRenderer_.OpaqueVertexCount() + worldRenderer_.TransparentVertexCount(),
                world_.Seed());
    // Commit the first generated world immediately. This makes the first launch
    // distinguishable from an empty menu and ensures the next launch can continue it.
    SaveWorld();
    return true;
}

void Engine::Shutdown(bool saveWorld) {
    if (saveWorld && ready_) SaveWorld();
    ui_.Shutdown();
    worldRenderer_.Shutdown();
    firstPersonRenderer_.Shutdown();
    firstPersonItemId_ = 0xffffu;
    firstPersonSwitchStarted_ = 0.0;
    firstPersonActionStarted_ = 0.0;
    firstPersonActionKind_ = 0;
    if (entityVbo_ != 0) glDeleteBuffers(1, &entityVbo_);
    if (entityVao_ != 0) glDeleteVertexArrays(1, &entityVao_);
    if (maesEntityVbo_ != 0) glDeleteBuffers(1, &maesEntityVbo_);
    if (maesEntityVao_ != 0) glDeleteVertexArrays(1, &maesEntityVao_);
    if (itemVbo_ != 0) glDeleteBuffers(1, &itemVbo_);
    if (itemVao_ != 0) glDeleteVertexArrays(1, &itemVao_);
    if (worldProgram_ != 0) glDeleteProgram(worldProgram_);
    if (blockAtlasTexture_ != 0) glDeleteTextures(1, &blockAtlasTexture_);
    if (sheepTexture_ != 0) glDeleteTextures(1, &sheepTexture_);
    entityVbo_ = entityVao_ = itemVbo_ = itemVao_ = 0;
    maesEntityVao_ = maesEntityVbo_ = 0;
    worldProgram_ = blockAtlasTexture_ = sheepTexture_ = 0;
    mvpLocation_ = lightLocation_ = daylightLocation_ = atlasLocation_ = useAtlasLocation_ = -1;
    entityTextureLocation_ = useEntityTextureLocation_ = -1;
    timeLocation_ = qualityLocation_ = waterWavesLocation_ = -1;
    lavaAnimationsLocation_ = leafWindLocation_ = leafWindStrengthLocation_ = -1;
    cameraLocation_ = fogColorLocation_ = fogNearLocation_ = fogFarLocation_ = -1;
    entityVertexCount_ = maesEntityVertexCount_ = itemVertexCount_ = 0;
    worldChunkViews_.clear();
    meshBuildScratch_.clear();
    ready_ = false;
    touches_.clear();
}

void Engine::Resize(int width, int height) {
    // Surface callbacks may briefly report zero while Android is recreating a
    // window. Keep the last valid extent instead of forcing a 1x1 viewport.
    if (width <= 0 || height <= 0) {
        ENGINE_LOGW("Ignoring invalid EGL surface extent %dx%d; keeping %dx%d",
                    width, height, width_, height_);
        return;
    }
    width_ = width;
    height_ = height;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        ENGINE_LOGE("Window framebuffer incomplete after resize %dx%d: 0x%04X",
                    width_, height_, static_cast<unsigned int>(framebufferStatus));
    }
    ui_.Resize(width_, height_);
}

void Engine::RebuildWorldMesh() {
    // Update only dirty cache entries from immutable block snapshots. The normal
    // play path deliberately skips flattening every cached chunk into one vector.
    const auto buildStart = std::chrono::steady_clock::now();
    world_.BuildMesh(meshBuildScratch_, static_cast<int>(std::floor(player_.position.x)),
                     static_cast<int>(std::floor(player_.position.z)),
                     renderDistanceChunks_, nullptr, nullptr, 1u, false);
    lastMeshBuildMs_ = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - buildStart).count();
}

void Engine::UploadWorldMesh() {
    const auto uploadStart = std::chrono::steady_clock::now();
    const int centerX = static_cast<int>(std::floor(player_.position.x));
    const int centerZ = static_cast<int>(std::floor(player_.position.z));
    world_.GetChunkMeshViews(centerX, centerZ, renderDistanceChunks_, worldChunkViews_);
    const std::size_t uploadBudget = graphicsQuality_ <= 0 ? 1u
        : (graphicsQuality_ >= 2 ? 3u : 2u);
    const std::size_t byteBudget = graphicsQuality_ <= 0 ? 256u * 1024u
        : (graphicsQuality_ >= 2 ? 2u * 1024u * 1024u : 768u * 1024u);
    const double timeBudgetMs = graphicsQuality_ <= 0 ? 1.5
        : (graphicsQuality_ >= 2 ? 4.0 : 2.5);
    if (!worldRenderer_.Sync(worldChunkViews_, player_.CameraPosition(),
                             uploadBudget, byteBudget, timeBudgetMs)) {
        ENGINE_LOGE("One or more chunk GPU buffers could not be synchronized; affected chunks will retry on the next upload pass");
    }
    lastGpuUploadMs_ = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - uploadStart).count();
}

void Engine::UploadTransparentWorldMesh() {
    // Opaque and transparent per-chunk buffers are synchronized together.
}

void Engine::RebuildEntityMesh() {
    entityMesh_.clear();
    maesEntityMesh_.clear();
    itemMesh_.clear();
    const Vec3 eye=ViewPosition();
    const Vec3 target=ViewTarget();
    const Mat4 projection=Perspective(fieldOfView_*(kPi/180.0f),
        static_cast<float>(width_)/static_cast<float>(height_),0.08f,120.0f);
    const Mat4 view=LookAt(eye,target,{0.0f,1.0f,0.0f});
    const CameraFrustum frustum=ExtractFrustum(Multiply(projection,view));
    // MAES only replaces the sheep when its external model has passed every
    // format, hierarchy, material and animation validation. All other entity
    // kinds remain on the historical renderer until individually migrated.
    entities_.AppendLegacyMesh(entityMesh_, &frustum,
                               entityVisuals_.IsSheepReady() ? 0 : -1);
    entityVisuals_.AppendMesh(entities_.Entities(), maesEntityMesh_, &frustum,
                              sheepTexture_ != 0);
    if (thirdPerson_) {
        firstPersonRenderer_.Clear();
        firstPersonItemId_ = 0xffffu;
        player_.AppendMesh(entityMesh_);
    } else {
        const std::uint16_t selected = inventory_.SelectedItemId();
        const BlockDefinition* definition = GetBlockDefinition(static_cast<BlockId>(selected));
        if (selected != firstPersonItemId_) {
            firstPersonItemId_ = selected;
            firstPersonSwitchStarted_ = ElapsedSeconds();
        }
        // The viewmodel owns its own camera-space arm/item meshes. It never
        // enters the entity/drop meshes, their frustum, or gameplay raycasts.
        firstPersonRenderer_.Build(player_, selected, definition, leftHandedControls_);
    }
    // Remote players use the same articulated voxel model as the local avatar.
    for (std::size_t i = 0; i + 5u < remotePlayers_.size(); i += 6u) {
        Player remote;
        remote.position = {remotePlayers_[i], remotePlayers_[i + 1u], remotePlayers_[i + 2u]};
        remote.yaw = remotePlayers_[i + 3u];
        remote.movementX = std::clamp(remotePlayers_[i + 4u], -1.0f, 1.0f);
        remote.movementY = std::clamp(remotePlayers_[i + 5u], -1.0f, 1.0f);
        if (!frustum.IntersectsAabb(
                remote.position.x-0.62f,remote.position.y-0.08f,remote.position.z-0.62f,
                remote.position.x+0.62f,remote.position.y+1.95f,remote.position.z+0.62f)) continue;
        // Network v2 also carries local movement intent, so remote avatars
        // animate their stride instead of remaining in a permanent idle pose.
        remote.walkPhase = static_cast<float>(ElapsedSeconds()) * 8.5f *
            std::clamp(std::abs(remote.movementX) + std::abs(remote.movementY), 0.0f, 1.0f);
        remote.animationTime = static_cast<float>(ElapsedSeconds());
        remote.SetAnimationOptions(static_cast<std::uint8_t>(animationQuality_), animationsEnabled_);
        remote.AppendMesh(entityMesh_);
    }
    // Block items use their own atlas-backed draw pass; entity models remain
    // vertex-coloured and therefore do not sample an arbitrary texture pixel.
    droppedItems_.AppendMesh(itemMesh_, &frustum);
}

void Engine::UploadItemMesh() {
    if(itemVao_==0||itemVbo_==0)return;
    glBindVertexArray(itemVao_);
    glBindBuffer(GL_ARRAY_BUFFER,itemVbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(itemMesh_.size()*sizeof(WorldVertex)),
        itemMesh_.empty()?nullptr:itemMesh_.data(),GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex,x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex,nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex,r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex,u)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex,atlasTile)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    itemVertexCount_=static_cast<GLsizei>(itemMesh_.size());
}

void Engine::UploadEntityMesh() {
    if (entityVao_ == 0 || entityVbo_ == 0) return;
    glBindVertexArray(entityVao_);
    glBindBuffer(GL_ARRAY_BUFFER, entityVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(entityMesh_.size() * sizeof(WorldVertex)),
                 entityMesh_.empty() ? nullptr : entityMesh_.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, u)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, atlasTile)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    entityVertexCount_ = static_cast<GLsizei>(entityMesh_.size());
    UploadMaesEntityMesh();
}

void Engine::UploadMaesEntityMesh() {
    if (maesEntityVao_ == 0 || maesEntityVbo_ == 0) return;
    glBindVertexArray(maesEntityVao_);
    glBindBuffer(GL_ARRAY_BUFFER, maesEntityVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(maesEntityMesh_.size() * sizeof(WorldVertex)),
                 maesEntityMesh_.empty() ? nullptr : maesEntityMesh_.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, u)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          reinterpret_cast<const void*>(offsetof(WorldVertex, atlasTile)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    maesEntityVertexCount_ = static_cast<GLsizei>(maesEntityMesh_.size());
}

double Engine::ElapsedSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Vec3 Engine::ViewPosition() const {
    if (!thirdPerson_) return player_.CameraPosition();
    const Vec3 flatForward{std::cos(player_.yaw), 0.0f, std::sin(player_.yaw)};
    const float cameraSide = thirdPersonFront_ ? 1.0f : -1.0f;
    return {
        player_.position.x + flatForward.x * cameraSide * 3.8f,
        player_.position.y + 2.25f,
        player_.position.z + flatForward.z * cameraSide * 3.8f
    };
}

Vec3 Engine::ViewTarget() const {
    if (!thirdPerson_) {
        const Vec3 eye = player_.CameraPosition();
        const Vec3 forward = player_.Forward();
        return Add(eye, forward);
    }
    const Vec3 flatForward{std::cos(player_.yaw), 0.0f, std::sin(player_.yaw)};
    return {
        player_.position.x + flatForward.x * 0.35f,
        player_.position.y + 1.18f + std::sin(player_.pitch) * 0.80f,
        player_.position.z + flatForward.z * 0.35f
    };
}

Vec3 Engine::ViewDirection() const {
    const Vec3 eye = ViewPosition();
    const Vec3 target = ViewTarget();
    const Vec3 delta{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const float length = std::sqrt(std::max(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z, 0.000001f));
    return {delta.x / length, delta.y / length, delta.z / length};
}

void Engine::Render() {
    if (!ready_) return;
    const auto now = std::chrono::steady_clock::now();
    const float delta = std::chrono::duration<float>(now - lastFrame_).count();
    lastFrame_ = now;
    frameTimeSamplesMs_[frameTimeSampleCursor_] = std::clamp(delta, 0.0f, 1.0f) * 1000.0f;
    frameTimeSampleCursor_ = (frameTimeSampleCursor_ + 1u) % frameTimeSamplesMs_.size();
    frameTimeSampleCount_ = std::min(frameTimeSampleCount_ + 1u, frameTimeSamplesMs_.size());
    if (!paused_) {
        const float safeDelta = std::clamp(delta, 0.0f, 0.05f);
        environment_.Update(safeDelta * modDaySpeed_);
        const bool wasGrounded = player_.grounded;
        const float incomingVerticalVelocity = player_.verticalVelocity;
        player_.Update(world_, safeDelta);
        autoJumpCooldown_ = std::max(0.0f, autoJumpCooldown_ - safeDelta);

        if (gameMode_ != 1) {
            const auto applyPlayerDamage = [this](float rawDamage, const char* source) {
                if (rawDamage <= 0.0f || !std::isfinite(rawDamage) || playerHealth_ <= 0.0f) return;
                std::size_t armorPieces = 0u;
                for (const std::uint16_t item : inventory_.equippedArmor) {
                    if (item != 0u) ++armorPieces;
                }
                const float armorFactor = std::max(0.55f, 1.0f - 0.08f * static_cast<float>(armorPieces));
                playerHealth_ = std::clamp(playerHealth_ - rawDamage * armorFactor, 0.0f, 20.0f);
                ui_.SetChatMessage(std::string(source) + " · Vida " +
                    std::to_string(static_cast<int>(std::ceil(playerHealth_))) + "/20");
                if (playerHealth_ <= 0.0f) {
                    if (!player_.ResolveSafeSpawn(world_)) {
                        const int spawnX = 8;
                        const int spawnZ = 8;
                        player_.position = {8.0f,
                            static_cast<float>(world_.GetSurfaceHeight(spawnX, spawnZ)) + 1.05f, 8.0f};
                    }
                    playerHealth_ = 10.0f;
                    playerHunger_ = std::max(playerHunger_, 8.0f);
                    hungerExhaustion_ = 0.0f;
                    ui_.SetChatMessage("Has reaparecido. Conservas tu inventario.");
                    SaveWorld();
                }
            };

            // A fall only causes damage when the movement solver reports a new landing.
            if (!wasGrounded && player_.grounded && incomingVerticalVelocity < -10.0f) {
                const float fallDamage = std::ceil((-incomingVerticalVelocity - 10.0f) * 0.45f);
                applyPlayerDamage(fallDamage, "Daño por caída");
            }

            if (difficulty_ != 0) {
                const bool moving = std::abs(player_.movementX) + std::abs(player_.movementY) > 0.12f;
                hungerExhaustion_ += safeDelta * (moving ? 0.020f : 0.0012f);
                while (hungerExhaustion_ >= 1.0f && playerHunger_ > 0.0f) {
                    hungerExhaustion_ -= 1.0f;
                    playerHunger_ = std::max(0.0f, playerHunger_ - 1.0f);
                }
            }

            if (playerHunger_ >= 18.0f && playerHealth_ < 20.0f) {
                regenerationTimer_ += safeDelta;
                if (regenerationTimer_ >= 4.0f) {
                    regenerationTimer_ = std::fmod(regenerationTimer_, 4.0f);
                    playerHealth_ = std::min(20.0f, playerHealth_ + 1.0f);
                    playerHunger_ = std::max(0.0f, playerHunger_ - 1.0f);
                }
            } else {
                regenerationTimer_ = 0.0f;
            }

            if (difficulty_ != 0 && playerHunger_ <= 0.0f) {
                starvationTimer_ += safeDelta;
                if (starvationTimer_ >= 8.0f) {
                    starvationTimer_ = std::fmod(starvationTimer_, 8.0f);
                    applyPlayerDamage(1.0f, "Hambre extrema");
                }
            } else {
                starvationTimer_ = 0.0f;
            }

            hostileDamageCooldown_ = std::max(0.0f, hostileDamageCooldown_ - safeDelta);
            if (difficulty_ != 0 && hostileDamageCooldown_ <= 0.0f) {
                const bool hostileNearby = std::any_of(entities_.Entities().begin(),
                    entities_.Entities().end(), [this](const Entity& entity) {
                        if (entity.kind != 4 && entity.kind != 5) return false;
                        const float dx = entity.position.x - player_.position.x;
                        const float dy = entity.position.y - player_.position.y;
                        const float dz = entity.position.z - player_.position.z;
                        return dx * dx + dy * dy + dz * dz < 1.65f;
                    });
                if (hostileNearby) {
                    const float hostileDamage = difficulty_ == 1 ? 1.0f : (difficulty_ == 3 ? 3.0f : 2.0f);
                    applyPlayerDamage(hostileDamage, "Ataque enemigo");
                    hostileDamageCooldown_ = 1.25f;
                }
            }
        }
        if (autoJump_ && autoJumpCooldown_ <= 0.0f && player_.grounded &&
            player_.movementY > 0.28f) {
            // Bedrock-style auto-jump: jump only when forward movement meets a
            // one-block obstacle and the destination column has enough headroom.
            const Vec3 forward = player_.Forward();
            const Vec3 right{-forward.z, 0.0f, forward.x};
            const float moveX = std::clamp(player_.movementX, -1.0f, 1.0f);
            const float moveY = std::clamp(player_.movementY, 0.0f, 1.0f);
            const float dirX = forward.x * moveY + right.x * moveX;
            const float dirZ = forward.z * moveY + right.z * moveX;
            const float length = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (length > 0.20f) {
                const int obstacleX = static_cast<int>(std::floor(player_.position.x + dirX / length * 0.58f));
                const int obstacleZ = static_cast<int>(std::floor(player_.position.z + dirZ / length * 0.58f));
                const int feetY = static_cast<int>(std::floor(player_.position.y + 0.12f));
                const bool obstacle = world_.IsSolid(obstacleX, feetY, obstacleZ);
                const bool clearAbove = !world_.IsSolid(obstacleX, feetY + 1, obstacleZ) &&
                                        !world_.IsSolid(obstacleX, feetY + 2, obstacleZ);
                if (obstacle && clearAbove) {
                    player_.Jump();
                    if (progressionJumps_ < 10000000u) ++progressionJumps_;
                    autoJumpCooldown_ = 0.42f;
                    soundEvents_.fetch_or(16u, std::memory_order_release);
                }
            }
        }
        // Survival worlds spawn a limited number of hostiles after dusk.
        // Spawn anchors are placed away from the player so enemies approach
        // through their normal pursuit steering instead of appearing at contact range.
        if (gameMode_ != 1 && difficulty_ != 0 && environment_.Daylight() < 0.22f) {
            hostileSpawnTimer_ += safeDelta;
            const float hostileSpawnInterval = difficulty_ == 1 ? 78.0f : (difficulty_ == 3 ? 30.0f : 48.0f);
            if (hostileSpawnTimer_ >= hostileSpawnInterval) {
                const auto& currentEntities = entities_.Entities();
                const std::size_t hostileCount = static_cast<std::size_t>(std::count_if(
                    currentEntities.begin(), currentEntities.end(), [](const Entity& entity) {
                        return entity.kind == 4 || entity.kind == 5;
                    }));
                const std::size_t hostileLimit = difficulty_ == 1 ? 2u : (difficulty_ == 3 ? 6u : 4u);
                if (hostileCount < hostileLimit && currentEntities.size() < 128u) {
                    const float angle = static_cast<float>(hostileSpawnSequence_++) * 2.39996323f;
                    const Vec3 anchor{
                        player_.position.x + std::cos(angle) * 12.0f,
                        player_.position.y,
                        player_.position.z + std::sin(angle) * 12.0f
                    };
                    const int kind = (hostileSpawnSequence_ % 3u == 0u) ? 5 : 4;
                    if (entities_.SpawnEntity(anchor, kind, world_)) {
                        ENGINE_LOGI("Night hostile spawned: kind=%d count=%zu", kind, hostileCount + 1u);
                    }
                }
                hostileSpawnTimer_ = 0.0f;
            }
        } else {
            hostileSpawnTimer_ = 0.0f;
        }
        entities_.Update(world_, player_, safeDelta);
        minimapAccumulator_ += safeDelta;
        if (minimapAccumulator_ >= 0.75f) {
            minimapAccumulator_ = std::fmod(minimapAccumulator_, 0.75f);
            const int centerX = static_cast<int>(std::floor(player_.position.x));
            const int centerZ = static_cast<int>(std::floor(player_.position.z));
            const int centerHeight = world_.GetSurfaceHeight(centerX, centerZ);
            std::string mapMarkup;
            mapMarkup.reserve(7u * 7u * 40u);
            for (int dz = -3; dz <= 3; ++dz) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (dx == 0 && dz == 0) {
                        mapMarkup += "<div class=\"map-tile map-player\">▲</div>";
                        continue;
                    }
                    const int height = world_.GetSurfaceHeight(centerX + dx * 3, centerZ + dz * 3);
                    const char* terrainClass = height <= VoxelWorld::kSeaLevel ? "map-water"
                        : (height >= centerHeight + 4 ? "map-high"
                        : (height <= centerHeight - 4 ? "map-low" : "map-mid"));
                    mapMarkup += "<div class=\"map-tile ";
                    mapMarkup += terrainClass;
                    mapMarkup += "\"></div>";
                }
            }
            ui_.SetMinimap(mapMarkup, "X " + std::to_string(centerX) +
                " · Z " + std::to_string(centerZ));
        }

        const bool walking = player_.grounded &&
            (std::abs(player_.movementX) + std::abs(player_.movementY) > 0.12f);
        if (walking) {
            footstepElapsed_ += safeDelta;
            const BlockId underPlayer = world_.GetBlock(
                static_cast<int>(std::floor(player_.position.x)),
                static_cast<int>(std::floor(player_.position.y + 0.08f)),
                static_cast<int>(std::floor(player_.position.z)));
            const float interval = HasBlockFlag(underPlayer, BlockFluid) ? 0.62f : 0.43f;
            if (footstepElapsed_ >= interval) {
                soundEvents_.fetch_or(32u, std::memory_order_release);
                footstepElapsed_ = 0.0f;
            }
        } else {
            footstepElapsed_ = 0.0f;
        }
        std::uint16_t pickedItemId = 0u;
        const std::size_t pickedCount =
            droppedItems_.Update(world_, player_, inventory_, safeDelta, &pickedItemId);
        if (pickedCount > 0u) {
            progressionItemsCollected_ = std::min(10000000u,
                progressionItemsCollected_ + static_cast<std::uint32_t>(std::min<std::size_t>(pickedCount, 10000000u)));
            if (gameMode_ != 1) AddProgressionXp(static_cast<std::uint32_t>(std::min<std::size_t>(pickedCount, 2500000u) * 2u));
            RefreshInventoryUi();
            const BlockDefinition* pickedDefinition =
                GetBlockDefinition(static_cast<BlockId>(pickedItemId));
            const char* pickedName = pickedDefinition != nullptr ? pickedDefinition->nameEs : "Objeto";
            pickupToastText_ = std::string(pickedName) + " ×" + std::to_string(pickedCount);
            pickupToastUntil_ = now + std::chrono::milliseconds(1400);
            ui_.SetPickupToast(pickupToastText_);
            soundEvents_.fetch_or(8u, std::memory_order_release);
        }
        if (!pickupToastText_.empty() && now >= pickupToastUntil_) {
            pickupToastText_.clear();
            ui_.SetPickupToast({});
        }

        UpdateMining(safeDelta);
        const auto generationStart = std::chrono::steady_clock::now();
        const bool generatedChunkReady = world_.PumpChunkGeneration();
        lastChunkGenerationPumpMs_ = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - generationStart).count();
        if (generatedChunkReady) {
            // Start meshing as soon as a generated chunk becomes available, but
            // never rebuild every dirty chunk in one render frame.
            chunkMeshAccumulator_ = 1.0f / 30.0f;
        }
        // Simulation remains frame-stepped, while animated vertex buffers are
        // uploaded at bounded rates to avoid rebuilding all creature/item geometry
        // on every 60–120 Hz screen refresh.
        entityMeshAccumulator_ += safeDelta;
        if (entityMeshAccumulator_ >= (1.0f / 30.0f)) {
            entityMeshAccumulator_ = std::fmod(entityMeshAccumulator_, 1.0f / 30.0f);
            RebuildEntityMesh();
            UploadEntityMesh();
        }
        itemMeshAccumulator_ += safeDelta;
        if (itemMeshAccumulator_ >= 0.05f) {
            itemMeshAccumulator_ = std::fmod(itemMeshAccumulator_, 0.05f);
            UploadItemMesh();
        }
        const int playerChunkX = static_cast<int>(std::floor(player_.position.x / 16.0f));
        const int playerChunkZ = static_cast<int>(std::floor(player_.position.z / 16.0f));
        if (playerChunkX != lastChunkX_ || playerChunkZ != lastChunkZ_) {
            world_.RequestGenerateAround(static_cast<int>(std::floor(player_.position.x)),
                                         static_cast<int>(std::floor(player_.position.z)),
                                         renderDistanceChunks_);
            lastChunkX_ = playerChunkX;
            lastChunkZ_ = playerChunkZ;
            chunkMeshAccumulator_ = 1.0f / 30.0f;
        }

        // Spread expensive greedy-mesh work over a 30 Hz budget. The world
        // worker still generates chunks independently; nearby pending meshes
        // are prioritized by VoxelWorld before farther LOD rings.
        constexpr float kChunkMeshInterval = 1.0f / 30.0f;
        chunkMeshAccumulator_ = std::min(chunkMeshAccumulator_ + safeDelta, kChunkMeshInterval);
        const int meshCenterX = static_cast<int>(std::floor(player_.position.x));
        const int meshCenterZ = static_cast<int>(std::floor(player_.position.z));
        if (chunkMeshAccumulator_ >= kChunkMeshInterval) {
            if (world_.HasPendingMeshBuild(meshCenterX, meshCenterZ, renderDistanceChunks_)) {
                RebuildWorldMesh();
            }
            // Drain deferred GPU work even when the CPU mesh queue is already clean.
            UploadWorldMesh();
            UploadTransparentWorldMesh();
            chunkMeshAccumulator_ = 0.0f;
        }
    }
    // Always draw into the Android window framebuffer, never into an FBO that
    // may have been left bound by another UI or native rendering path.
    // OpenGL ES state is global. Restore the complete surface before clearing.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glClearDepthf(1.0f);

    const auto sky = environment_.SkyColor();
    glClearColor(sky[0], sky[1], sky[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (selectedItemToastUntil_ > 0.0 && ElapsedSeconds() >= selectedItemToastUntil_) {
        ui_.SetSelectedItem({});
        selectedItemToastUntil_ = 0.0;
    }
    // CPU geometry is cached; GPU creation/publication remains on the GLSurfaceView
    // thread where the current EGL context is guaranteed to exist.
    firstPersonRenderer_.Upload();
    const bool hasFirstPerson = !thirdPerson_ && firstPersonRenderer_.HasContent();
    const bool hasAnyWorldPass = worldRenderer_.OpaqueVertexCount() > 0 ||
        worldRenderer_.TransparentVertexCount() > 0 ||
        entityVertexCount_ > 0 || maesEntityVertexCount_ > 0 ||
        itemVertexCount_ > 0 || hasFirstPerson;
    if (worldProgram_ != 0 && hasAnyWorldPass) {
        const Vec3 eye = ViewPosition();
        const Vec3 target = ViewTarget();
        const Mat4 projection = Perspective(fieldOfView_ * (kPi / 180.0f),
                                            static_cast<float>(width_) / static_cast<float>(height_),
                                            0.08f, 120.0f);
        const Mat4 view = LookAt(eye, target, {0.0f, 1.0f, 0.0f});
        const Mat4 mvp = Multiply(projection, view);
        const CameraFrustum frustum = ExtractFrustum(mvp);
        const auto sun = environment_.SunDirection();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glUseProgram(worldProgram_);
        glUniformMatrix4fv(mvpLocation_, 1, GL_FALSE, mvp.v.data());
        glUniform3f(lightLocation_, sun[0], sun[1], sun[2]);
        glUniform1f(daylightLocation_, environment_.Daylight());
        glUniform1f(timeLocation_, static_cast<float>(ElapsedSeconds()));
        glUniform1f(qualityLocation_, static_cast<float>(graphicsQuality_));
        glUniform1i(waterWavesLocation_, waterWaves_ ? 1 : 0);
        if (lavaAnimationsLocation_ >= 0) glUniform1i(lavaAnimationsLocation_, lavaAnimations_ ? 1 : 0);
        if (leafWindLocation_ >= 0) glUniform1i(leafWindLocation_, leafWind_ ? 1 : 0);
        if (leafWindStrengthLocation_ >= 0) glUniform1f(leafWindStrengthLocation_, leafWindStrength_);
        glUniform3f(cameraLocation_, eye.x, eye.y, eye.z);
        glUniform3f(fogColorLocation_, sky[0], sky[1], sky[2]);
        const float fogStart = 12.0f + static_cast<float>(renderDistanceChunks_) * 14.0f;
        const float fogEnd = std::min(120.0f, fogStart + 34.0f + static_cast<float>(graphicsQuality_) * 10.0f);
        glUniform1f(fogNearLocation_, fogStart);
        glUniform1f(fogFarLocation_, fogEnd);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blockAtlasTexture_);
        // Uploads are incremental per-chunk; frustum culling and transparent
        // ordering live in the dedicated terrain renderer component.
        if (worldRenderer_.OpaqueVertexCount() > 0)
            worldRenderer_.DrawOpaque(frustum);

        if (entityVertexCount_ > 0) {
            glUniform1i(useAtlasLocation_, 0);
            glUniform1i(useEntityTextureLocation_, 0);
            glBindVertexArray(entityVao_);
            glDrawArrays(GL_TRIANGLES, 0, entityVertexCount_);
            glBindVertexArray(0);
        }
        if (maesEntityVertexCount_ > 0) {
            glUniform1i(useAtlasLocation_, 0);
            glUniform1i(useEntityTextureLocation_, sheepTexture_ != 0 ? 1 : 0);
            if (sheepTexture_ != 0) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, sheepTexture_);
            }
            glBindVertexArray(maesEntityVao_);
            glDrawArrays(GL_TRIANGLES, 0, maesEntityVertexCount_);
            glBindVertexArray(0);
            glUniform1i(useEntityTextureLocation_, 0);
            glActiveTexture(GL_TEXTURE0);
        }
        if(itemVertexCount_>0) {
            glUniform1i(useAtlasLocation_,1);
            glBindTexture(GL_TEXTURE_2D,blockAtlasTexture_);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glBindVertexArray(itemVao_);
            glDrawArrays(GL_TRIANGLES,0,itemVertexCount_);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        glUniform1i(useAtlasLocation_,1);
        if (worldRenderer_.TransparentVertexCount() > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            worldRenderer_.DrawTransparent(frustum, eye, worldChunkViews_);
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // First-person arm and held item are drawn after world transparencies
        // with a camera-space projection. Their depth still participates in
        // nearby-wall occlusion; no depth clear or world-state mutation occurs.
        if (hasFirstPerson) {
            const float movementAmount = std::clamp(
                std::abs(player_.movementX) + std::abs(player_.movementY), 0.0f, 1.0f);
            const float walkBob = std::sin(player_.walkPhase * 2.0f) * 0.014f * movementAmount;
            const float idleBob = std::sin(player_.animationTime * 1.4f) * 0.003f;
            const float switchElapsed = static_cast<float>(
                std::max(0.0, ElapsedSeconds() - firstPersonSwitchStarted_));
            const float switchProgress = std::clamp(switchElapsed / 0.22f, 0.0f, 1.0f);
            const float switchDip = (1.0f - switchProgress) *
                std::sin((1.0f - switchProgress) * (kPi * 0.5f)) * 0.15f;
            float actionAngle = 0.0f;
            float actionLift = 0.0f;
            if (firstPersonActionKind_ != 0) {
                const float actionElapsed = static_cast<float>(
                    std::max(0.0, ElapsedSeconds() - firstPersonActionStarted_));
                if (actionElapsed < 0.24f) {
                    const float phase = std::clamp(actionElapsed / 0.24f, 0.0f, 1.0f);
                    const float envelope = std::sin(phase * kPi);
                    if (firstPersonActionKind_ == 1) actionAngle = -0.48f * envelope;
                    else if (firstPersonActionKind_ == 2) actionAngle = 0.34f * envelope;
                    else if (firstPersonActionKind_ == 3) {
                        actionAngle = -0.16f * envelope;
                        actionLift = 0.07f * envelope;
                    }
                } else {
                    firstPersonActionKind_ = 0;
                }
            }
            Mat4 viewModel{};
            viewModel.v[0] = 1.0f;
            viewModel.v[5] = std::cos(actionAngle);
            viewModel.v[6] = std::sin(actionAngle);
            viewModel.v[9] = -std::sin(actionAngle);
            viewModel.v[10] = std::cos(actionAngle);
            viewModel.v[15] = 1.0f;
            viewModel.v[12] = std::sin(player_.walkPhase) * 0.008f * movementAmount;
            viewModel.v[13] = walkBob + idleBob - switchDip + actionLift;
            const Mat4 viewModelProjection = Multiply(projection, viewModel);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glUseProgram(worldProgram_);
            glUniformMatrix4fv(mvpLocation_, 1, GL_FALSE, viewModelProjection.v.data());
            glUniform3f(cameraLocation_, 0.0f, 0.0f, 0.0f);
            glUniform1f(fogNearLocation_, 100.0f);
            glUniform1f(fogFarLocation_, 120.0f);
            glUniform1f(daylightLocation_, std::max(0.55f, environment_.Daylight()));
            glUniform1i(useEntityTextureLocation_, 0);
            glUniform1i(useAtlasLocation_, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, blockAtlasTexture_);
            firstPersonRenderer_.DrawHeldAtlas();
            glUniform1i(useAtlasLocation_, 0);
            firstPersonRenderer_.DrawHeldColor();
            firstPersonRenderer_.DrawArm();
            glUniform1i(useAtlasLocation_, 1);
            glUniform3f(cameraLocation_, eye.x, eye.y, eye.z);
            glUniform1f(fogNearLocation_, fogStart);
            glUniform1f(fogFarLocation_, fogEnd);
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glUseProgram(0);
    }

    ++framesSinceStats_;
    if (now - lastStats_ >= std::chrono::seconds(1)) {
        fps_ = static_cast<float>(framesSinceStats_);
        framesSinceStats_ = 0;
        lastStats_ = now;
        std::array<float, 120> sortedFrameTimes = frameTimeSamplesMs_;
        std::sort(sortedFrameTimes.begin(),
                  sortedFrameTimes.begin() + static_cast<std::ptrdiff_t>(frameTimeSampleCount_));
        const auto percentile = [&](double fraction) {
            if (frameTimeSampleCount_ == 0u) return 0.0f;
            const double rank = std::ceil(fraction * static_cast<double>(frameTimeSampleCount_));
            const std::size_t index = std::min(frameTimeSampleCount_ - 1u,
                static_cast<std::size_t>(std::max(1.0, rank) - 1.0));
            return sortedFrameTimes[index];
        };
        const float frameP50 = percentile(0.50);
        const float frameP95 = percentile(0.95);
        const float frameP99 = percentile(0.99);
        char status[220]{};
        if (gameMode_ == 1) {
            std::snprintf(status, sizeof(status),
                          "Creativo · FPS %.0f · F %.1f/%.1fms · C %zu · GPU %.1fMiB",
                          fps_, frameP50, frameP95, world_.ChunkCount(),
                          static_cast<double>(worldRenderer_.ResidentGpuBytes()) / (1024.0 * 1024.0));
        } else {
            std::uint32_t level = 1u;
            std::uint32_t spent = 0u;
            std::uint32_t needed = 100u;
            while (level < 10000u && progressionXp_ >= spent + needed) {
                spent += needed;
                ++level;
                needed = 100u + (level - 1u) * 50u;
            }
            const std::uint32_t intoLevel = progressionXp_ - spent;
            std::snprintf(status, sizeof(status),
                          "VIDA %.0f/20 · COMIDA %.0f/20 · NV %u · XP %u/%u · FPS %.0f",
                          std::ceil(playerHealth_), std::floor(playerHunger_), level, intoLevel, needed, fps_);
        }
        ui_.SetStatus(status);

        if (lastPerfLog_.time_since_epoch().count() == 0 ||
            now - lastPerfLog_ >= std::chrono::seconds(5)) {
            ENGINE_LOGI("perf frame_ms p50=%.2f p95=%.2f p99=%.2f generation_pump_ms=%.2f mesh_ms=%.2f upload_ms=%.2f chunks=%zu pending_generation=%zu cpu_blocks_MiB=%.2f cpu_mesh_MiB=%.2f gpu_MiB=%.2f uploads=%zu upload_bytes=%zu pending_uploads=%zu pending_bytes=%zu",
                frameP50, frameP95, frameP99, lastChunkGenerationPumpMs_, lastMeshBuildMs_,
                lastGpuUploadMs_, world_.ChunkCount(), world_.PendingChunkCount(),
                static_cast<double>(world_.ChunkStorageBytes()) / (1024.0 * 1024.0),
                static_cast<double>(world_.MeshCacheMemoryBytes()) / (1024.0 * 1024.0),
                static_cast<double>(worldRenderer_.ResidentGpuBytes()) / (1024.0 * 1024.0),
                worldRenderer_.LastUploadCount(), worldRenderer_.LastUploadBytes(),
                worldRenderer_.PendingUploadCount(), worldRenderer_.PendingUploadBytes());
            lastPerfLog_ = now;
        }
    }

    ui_.UpdateAndRender();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}

void Engine::UpdateTouchMovement() {
    float moveX = (fallbackMovementButtons_[3] ? 1.0f : 0.0f) -
                  (fallbackMovementButtons_[2] ? 1.0f : 0.0f);
    float moveY = (fallbackMovementButtons_[0] ? 1.0f : 0.0f) -
                  (fallbackMovementButtons_[1] ? 1.0f : 0.0f);
    for (const auto& entry : touches_) {
        const TouchState& touch = entry.second;
        if (!touch.movementPad) continue;
        moveX += touch.movementX;
        moveY += touch.movementY;
    }
    const float magnitude = std::sqrt(moveX * moveX + moveY * moveY);
    if (magnitude > 1.0f) {
        moveX /= magnitude;
        moveY /= magnitude;
    }
    player_.SetMovement(std::clamp(moveX, -1.0f, 1.0f),
                        std::clamp(moveY, -1.0f, 1.0f));
}

void Engine::Touch(int action, int pointerId, float x, float y) {
    constexpr int kDown = 0;
    constexpr int kUp = 1;
    constexpr int kMove = 2;
    constexpr int kCancel = 3;

    if (action == kDown) {
        if (ui_.CrouchButtonAt(x, y)) {
            // The centre crouch button has its own pointer route. It must still
            // respond while another finger owns the camera or a movement key.
            HandleUiAction("crouch");
            TouchState state;
            state.startX = state.lastX = x;
            state.startY = state.lastY = y;
            state.startedAt = ElapsedSeconds();
            state.uiHandled = true;
            touches_[pointerId] = state;
            return;
        }
        float moveX = 0.0f;
        float moveY = 0.0f;
        if (ui_.MovementVectorAt(x, y, moveX, moveY)) {
            TouchState state;
            state.startX = state.lastX = x;
            state.startY = state.lastY = y;
            state.startedAt = ElapsedSeconds();
            state.movementPad = true;
            state.movementX = moveX;
            state.movementY = moveY;
            state.moved = true;
            touches_[pointerId] = state;
            UpdateTouchMovement();
            return;
        }
    }

    auto found = touches_.find(pointerId);
    if (action != kDown && found == touches_.end()) return;
    if (action != kDown && found != touches_.end() && found->second.movementPad) {
        if (action == kMove) {
            float moveX = 0.0f;
            float moveY = 0.0f;
            const bool inside = ui_.MovementVectorAt(x, y, moveX, moveY);
            found->second.movementX = inside ? moveX : 0.0f;
            found->second.movementY = inside ? moveY : 0.0f;
            found->second.lastX = x;
            found->second.lastY = y;
            UpdateTouchMovement();
        } else if (action == kUp || action == kCancel) {
            touches_.erase(found);
            UpdateTouchMovement();
        }
        return;
    }

    const bool uiUnhandled = ui_.Touch(action, pointerId, x, y);
    if (action == kDown) {
        TouchState state;
        state.startX = state.lastX = x;
        state.startY = state.lastY = y;
        state.startedAt = ElapsedSeconds();
        state.uiHandled = !uiUnhandled;
        state.mining = uiUnhandled && gameMode_ != 2;
        touches_[pointerId] = state;
        return;
    }

    TouchState& state = found->second;
    if (state.uiHandled) {
        if (action == kUp || action == kCancel) touches_.erase(found);
        return;
    }

    if (action == kMove) {
        const float deltaX = x - state.lastX;
        const float deltaY = y - state.lastY;
        if (std::abs(x - state.startX) + std::abs(y - state.startY) > 7.0f) {
            state.moved = true;
            state.mining = false;
        }
        if (state.moved && uiUnhandled) {
            const float verticalDirection = invertLookY_ ? -1.0f : 1.0f;
            player_.Rotate(deltaX * lookSensitivityX_,
                           deltaY * lookSensitivityY_ * verticalDirection);
        }
        state.lastX = x;
        state.lastY = y;
    } else if (action == kUp || action == kCancel) {
        if (action == kUp && !state.moved &&
            ElapsedSeconds() - state.startedAt < 0.22) {
            const std::uint16_t selected = inventory_.SelectedItemId();
            Vec3 placementTarget{};
            Vec3 placed{};
            const bool hasPlacementTarget = gameMode_ != 2 && selected > 0 &&
                world_.GetPlacementTargetBlock(ViewPosition(), ViewDirection(), 6.0f, &placementTarget);
            if (hasPlacementTarget && !protection_.CanBuild(playerIdentity_,
                    static_cast<int>(placementTarget.x), static_cast<int>(placementTarget.z))) {
                ui_.SetChatMessage("Zona protegida. El dueño debe autorizarte para construir aquí.");
            } else if (hasPlacementTarget &&
                world_.PlaceRay(ViewPosition(), ViewDirection(), 6.0f,
                                static_cast<BlockId>(selected), &placed)) {
                pendingBlockEdits_.insert(pendingBlockEdits_.end(), {
                    static_cast<int>(placed.x), static_cast<int>(placed.y),
                    static_cast<int>(placed.z), static_cast<int>(selected)
                });
                firstPersonActionStarted_ = ElapsedSeconds();
                firstPersonActionKind_ = 2; // PLACE: only after a successful world edit.
                if (gameMode_ != 1) inventory_.ConsumeSelected();
                if (progressionBlocksPlaced_ < 10000000u) ++progressionBlocksPlaced_;
                if (gameMode_ != 1) AddProgressionXp(1u);
                RefreshInventoryUi();
                soundEvents_.fetch_or(2u, std::memory_order_release);
                if (HasBlockFlag(static_cast<BlockId>(selected), BlockFluid))
                    soundEvents_.fetch_or(524288u, std::memory_order_release);
                if (const BlockDefinition* placedDefinition =
                        GetBlockDefinition(static_cast<BlockId>(selected))) {
                    if (placedDefinition->preferredTool == BlockTool::Axe)
                        soundEvents_.fetch_or(128u, std::memory_order_release);
                    else if (placedDefinition->preferredTool == BlockTool::Shovel)
                        soundEvents_.fetch_or(256u, std::memory_order_release);
                    else if (placedDefinition->preferredTool == BlockTool::Shears)
                        soundEvents_.fetch_or(512u, std::memory_order_release);
                    else if (placedDefinition->preferredTool == BlockTool::Pickaxe)
                        soundEvents_.fetch_or(64u, std::memory_order_release);
                }
                RebuildWorldMesh();
                UploadWorldMesh();
                UploadTransparentWorldMesh();
            }
        }
        if (miningPointerId_ == pointerId) {
            miningPointerId_ = -1;
            miningElapsed_ = 0.0f;
            miningRequired_ = 0.0f;
        }
        ui_.SetTargetBlock({}, 0.0f);
        touches_.erase(found);
    }
}

void Engine::CyclePerspective() {
    if (!thirdPerson_) {
        thirdPerson_ = true;
        thirdPersonFront_ = false;
    } else if (!thirdPersonFront_) {
        thirdPersonFront_ = true;
    } else {
        thirdPerson_ = false;
        thirdPersonFront_ = false;
    }
    ui_.SetPerspectiveEnabled(thirdPerson_);
    RebuildEntityMesh();
    UploadEntityMesh();
}

void Engine::HandleUiAction(const std::string& action) {
    soundEvents_.fetch_or(4u, std::memory_order_release);
    if (action == "chat") {
        chatRequested_.store(true, std::memory_order_release);
    } else if (action == "perspective") {
        CyclePerspective();
    } else if (action == "inventory") {
        inventoryVisible_ = !inventoryVisible_;
        if (inventoryVisible_) RefreshInventoryUi();
        ui_.SetPanelVisible("inventory-panel", inventoryVisible_);
        if (inventoryVisible_) {
            ui_.SetPanelVisible("pause-panel", false);
            pauseVisible_ = false;
            paused_ = true;
            player_.SetMovement(0.0f, 0.0f);
        } else {
            paused_ = pauseVisible_;
        }
    } else if (action == "inventory-close") {
        inventoryVisible_ = false;
        ui_.SetPanelVisible("inventory-panel", false);
        paused_ = pauseVisible_;
    } else if (action == "crafting-open") {
        inventoryVisible_ = false;
        ui_.SetPanelVisible("inventory-panel", false);
        ui_.SetPanelVisible("pause-panel", false);
        pauseVisible_ = false;
        paused_ = true;
        RefreshCraftingUi();
        ui_.SetPanelVisible("crafting-panel", true);
    } else if (action == "crafting-close") {
        ui_.SetPanelVisible("crafting-panel", false);
        inventoryVisible_ = true;
        paused_ = true;
        RefreshInventoryUi();
        ui_.SetPanelVisible("inventory-panel", true);
    } else if (action == "eat") {
        if (gameMode_ == 1) {
            ui_.SetChatMessage("En creativo no necesitas comer.");
        } else if (playerHunger_ >= 20.0f) {
            ui_.SetChatMessage("No tienes hambre.");
        } else {
            const std::uint16_t selected = inventory_.SelectedItemId();
            const BlockDefinition* food = GetBlockDefinition(static_cast<BlockId>(selected));
            float restored = 0.0f;
            if (food != nullptr) {
                const std::string_view key(food->key);
                if (key == "carrots") restored = 4.0f;
                else if (key == "potatoes") restored = 3.0f;
            }
            if (restored <= 0.0f) {
                ui_.SetChatMessage("Selecciona zanahorias o patatas para comer.");
            } else if (inventory_.ConsumeSelected(1u)) {
                firstPersonActionStarted_ = ElapsedSeconds();
                firstPersonActionKind_ = 3; // EAT: item was actually consumed.
                playerHunger_ = std::min(20.0f, playerHunger_ + restored);
                hungerExhaustion_ = 0.0f;
                RefreshInventoryUi();
                ui_.SetChatMessage(std::string("Has comido ") + food->nameEs + ".");
            } else {
                ui_.SetChatMessage("No quedan unidades de ese alimento.");
            }
        }
    } else if (action == "attack") {
        if (gameMode_ == 1) {
            ui_.SetChatMessage("El combate no está disponible en este modo.");
        } else {
            float damage = 2.0f;
            const BlockDefinition* held = GetBlockDefinition(
                static_cast<BlockId>(inventory_.SelectedItemId()));
            if (held != nullptr) {
                const std::string_view key(held->key);
                if (key.find("_sword") != std::string_view::npos) damage = 6.0f;
                else if (key.find("_axe") != std::string_view::npos) damage = 4.0f;
                else if (key.find("_pickaxe") != std::string_view::npos) damage = 3.0f;
            }
            bool defeated = false;
            int defeatedKind = -1;
            if (entities_.DamageNearest(player_.CameraPosition(), player_.Forward(),
                                        3.25f, damage, &defeated, &defeatedKind)) {
                firstPersonActionStarted_ = ElapsedSeconds();
                firstPersonActionKind_ = 1; // ATTACK: an enemy was actually hit.
                if (inventory_.DamageSelectedItem(1u)) RefreshInventoryUi();
                if (defeated) {
                    if (gameMode_ != 1) AddProgressionXp(20u);
                    ui_.SetChatMessage(defeatedKind == 5
                        ? "Has derrotado a un esqueleto."
                        : "Has derrotado a un zombi.");
                } else {
                    ui_.SetChatMessage("Impacto · daño " + std::to_string(static_cast<int>(damage)));
                }
                RebuildEntityMesh();
                UploadEntityMesh();
            } else {
                ui_.SetChatMessage("No hay ningún enemigo frente a ti.");
            }
        }
    } else if (action == "craft-planks") {
        CraftRecipe("planks");
    } else if (action == "craft-sticks") {
        CraftRecipe("sticks");
    } else if (action == "craft-table") {
        CraftRecipe("table");
    } else if (action == "craft-wooden-pickaxe") {
        CraftRecipe("wooden_pickaxe");
    } else if (action == "craft-wooden-axe") {
        CraftRecipe("wooden_axe");
    } else if (action == "craft-wooden-shovel") {
        CraftRecipe("wooden_shovel");
    } else if (action == "craft-wooden-sword") {
        CraftRecipe("wooden_sword");
    } else if (action.rfind("craft-", 0) == 0) {
        CraftRecipe(action.substr(6));
    } else if (action == "pause") {
        pauseVisible_ = !pauseVisible_;
        paused_ = pauseVisible_;
        if (pauseVisible_) {
            inventoryVisible_ = false;
            ui_.SetPanelVisible("inventory-panel", false);
            player_.SetMovement(0.0f, 0.0f);
        }
        ui_.SetPanelVisible("pause-panel", pauseVisible_);
        paused_ = pauseVisible_ || inventoryVisible_;
    } else if (action == "resume") {
        pauseVisible_ = false;
        paused_ = inventoryVisible_;
        ui_.SetPanelVisible("pause-panel", false);
    } else if (action == "save") {
        SaveWorld();
        ui_.SetStatus("Mundo guardado");
    } else if (action == "save-exit") {
        SaveWorld();
        exitRequested_.store(true, std::memory_order_release);
    } else if (action == "jump") {
        const bool wasGrounded = player_.grounded;
        player_.Jump();
        if (wasGrounded && !player_.grounded) {
            if (progressionJumps_ < 10000000u) ++progressionJumps_;
            if (gameMode_ != 1) AddProgressionXp(1u);
            soundEvents_.fetch_or(16u, std::memory_order_release);
        }
    } else if (action == "crouch") {
        crouching_ = !crouching_;
        player_.SetCrouching(crouching_);
        ui_.SetCrouchEnabled(crouching_);
    } else if (action == "starter-chest-open") {
        OpenStarterChest();
    } else if (action == "starter-chest-close") {
        starterChestOpen_ = false;
        ui_.SetPanelVisible("starter-chest-panel", false);
    } else if (action == "starter-chest-take") {
        TakeStarterChestContents();
    } else if (action.rfind("creative-select-", 0) == 0) {
        if (gameMode_ != 1) return;
        try {
            const int itemId = std::stoi(action.substr(16));
            if (itemId > 0 && itemId < static_cast<int>(GetBlockDefinitionCount()) &&
                HasBlockFlag(static_cast<BlockId>(itemId), BlockPlaceable)) {
                inventory_.counts[inventory_.selectedSlot] =
                    static_cast<std::uint32_t>(itemId) * 1000u + 64u;
                RefreshInventoryUi();
                ui_.SetChatMessage(std::string("Seleccionado: ") +
                    GetBlockDefinition(static_cast<BlockId>(itemId))->nameEs);
            }
        } catch (...) {
            ui_.SetChatMessage("No se pudo seleccionar ese bloque.");
        }
    } else if (action.rfind("gear-slot-", 0) == 0) {
        try {
            const std::size_t slot = static_cast<std::size_t>(std::stoul(action.substr(10)));
            if (inventory_.UnequipArmor(slot)) RefreshInventoryUi();
            else ui_.SetChatMessage("No hay espacio para guardar la pieza de armadura.");
        } catch (...) { ui_.SetChatMessage("No se pudo quitar la armadura."); }
    } else if (action.rfind("inventory-slot-", 0) == 0) {
        try {
            const std::size_t slot = static_cast<std::size_t>(std::stoul(action.substr(15)));
            if (slot < inventory_.counts.size()) {
                if (inventory_.EquipArmorFromSlot(slot)) {
                    RefreshInventoryUi();
                } else if (slot < Inventory::kHotbarSlotCount) {
                    inventory_.selectedSlot = static_cast<std::uint16_t>(slot);
                    RefreshInventoryUi();
                } else {
                    std::swap(inventory_.counts[slot], inventory_.counts[inventory_.selectedSlot]);
                    std::swap(inventory_.toolDurability[slot],
                              inventory_.toolDurability[inventory_.selectedSlot]);
                    RefreshInventoryUi();
                }
            }
        } catch (...) { ui_.SetChatMessage("No se pudo mover ese objeto."); }
    } else if (action.rfind("slot-", 0) == 0 && action.size() == 6) {
        const char digit = action[5];
        if (digit >= '0' && digit <= '8') {
            inventory_.selectedSlot = static_cast<std::uint16_t>(digit - '0');
            RefreshInventoryUi();
        }
    }
}


bool Engine::HasNearbyBlock(const std::string& blockKey) const {
    const BlockDefinition* definition = FindBlockDefinition(blockKey);
    if (definition == nullptr) return false;
    const BlockId stationId = static_cast<BlockId>(definition->id);
    const int centerX = static_cast<int>(std::floor(player_.position.x));
    const int centerY = static_cast<int>(std::floor(player_.position.y));
    const int centerZ = static_cast<int>(std::floor(player_.position.z));
    for (int y = std::max(0, centerY - 1); y <= std::min(VoxelWorld::kWorldHeight - 1, centerY + 2); ++y) {
        for (int z = centerZ - 2; z <= centerZ + 2; ++z) {
            for (int x = centerX - 2; x <= centerX + 2; ++x) {
                if (world_.GetBlock(x, y, z) == stationId) return true;
            }
        }
    }
    return false;
}

bool Engine::HasNearbyCraftingTable() const { return HasNearbyBlock("crafting_table"); }
bool Engine::HasNearbyFurnace() const { return HasNearbyBlock("furnace"); }
bool Engine::HasNearbyStonecutter() const { return HasNearbyBlock("stonecutter"); }

bool Engine::CraftRecipe(const std::string& recipe) {
    struct Ingredient { std::uint16_t id; std::uint16_t amount; };
    std::array<Ingredient, 9> ingredients{};
    std::size_t ingredientCount = 0u;
    std::uint16_t outputId = 0u;
    std::uint16_t outputAmount = 1u;
    bool needsTable = false;
    bool needsFurnace = false;
    bool needsStonecutter = false;
    const auto addIngredient = [&ingredients, &ingredientCount](std::uint16_t id, std::uint16_t amount) {
        if (id == 0u || amount == 0u || ingredientCount >= ingredients.size()) return false;
        ingredients[ingredientCount++] = {id, amount};
        return true;
    };
    const auto findId = [](const std::string& key) -> std::uint16_t {
        const BlockDefinition* d = FindBlockDefinition(key);
        return d == nullptr ? 0u : static_cast<std::uint16_t>(d->id);
    };

    if (recipe == "planks") {
        const std::uint16_t log = findId("log");
        const std::uint16_t log2 = findId("log2");
        if (inventory_.CountItem(log) > 0u) addIngredient(log, 1u);
        else if (inventory_.CountItem(log2) > 0u) addIngredient(log2, 1u);
        else { ui_.SetChatMessage("Necesitas un tronco para fabricar tablones."); return false; }
        outputId = findId("planks"); outputAmount = 4u;
    } else if (recipe == "sticks") {
        addIngredient(findId("planks"), 2u);
        outputId = findId("minefade_stick"); outputAmount = 4u;
    } else if (recipe == "table") {
        addIngredient(findId("planks"), 4u);
        outputId = findId("crafting_table");
    } else if (recipe == "torch") {
        if (!addIngredient(findId("coal_ore"), 1u) || !addIngredient(findId("minefade_stick"), 1u)) return false;
        outputId = findId("torch"); outputAmount = 4u;
    } else if (recipe == "chest") {
        if (!addIngredient(findId("planks"), 8u)) return false;
        outputId = findId("chest"); needsTable = true;
    } else if (recipe == "furnace") {
        if (!addIngredient(findId("cobblestone"), 8u)) return false;
        outputId = findId("furnace"); needsTable = true;
    } else if (recipe == "wooden_door") {
        if (!addIngredient(findId("planks"), 6u)) return false;
        outputId = findId("wooden_door"); outputAmount = 3u; needsTable = true;
    } else if (recipe == "ladder") {
        if (!addIngredient(findId("minefade_stick"), 7u)) return false;
        outputId = findId("ladder"); outputAmount = 3u; needsTable = true;
    } else if (recipe == "fence") {
        if (!addIngredient(findId("planks"), 4u) || !addIngredient(findId("minefade_stick"), 2u)) return false;
        outputId = findId("fence"); outputAmount = 3u; needsTable = true;
    } else if (recipe == "wooden_slab") {
        if (!addIngredient(findId("planks"), 3u)) return false;
        outputId = findId("wooden_slab"); outputAmount = 6u; needsTable = true;
    } else if (recipe == "oak_stairs") {
        if (!addIngredient(findId("planks"), 6u)) return false;
        outputId = findId("oak_stairs"); outputAmount = 4u; needsTable = true;
    } else if (recipe == "stone_stairs") {
        if (!addIngredient(findId("cobblestone"), 6u)) return false;
        outputId = findId("stone_stairs"); outputAmount = 4u; needsTable = true;
    } else if (recipe == "lever") {
        if (!addIngredient(findId("cobblestone"), 1u) || !addIngredient(findId("minefade_stick"), 1u)) return false;
        outputId = findId("lever");
    } else if (recipe == "wooden_pressure_plate") {
        if (!addIngredient(findId("planks"), 2u)) return false;
        outputId = findId("wooden_pressure_plate");
    } else if (recipe == "stone_pressure_plate") {
        if (!addIngredient(findId("cobblestone"), 2u)) return false;
        outputId = findId("stone_pressure_plate");
    } else if (recipe == "wooden_button") {
        if (!addIngredient(findId("planks"), 1u)) return false;
        outputId = findId("wooden_button");
    } else if (recipe == "stone_button") {
        if (!addIngredient(findId("cobblestone"), 1u)) return false;
        outputId = findId("stone_button");
    } else if (recipe == "trapdoor") {
        if (!addIngredient(findId("planks"), 6u)) return false;
        outputId = findId("trapdoor"); outputAmount = 2u; needsTable = true;
    } else if (recipe == "standing_sign") {
        if (!addIngredient(findId("planks"), 6u) || !addIngredient(findId("minefade_stick"), 1u)) return false;
        outputId = findId("standing_sign"); outputAmount = 3u; needsTable = true;
    } else if (recipe == "rail") {
        if (!addIngredient(findId("minefade_iron_ingot"), 6u) || !addIngredient(findId("minefade_stick"), 1u)) return false;
        outputId = findId("rail"); outputAmount = 16u; needsTable = true;
    } else if (recipe == "copper_ingot" || recipe == "iron_ingot" || recipe == "gold_ingot") {
        const std::string metal = recipe.substr(0, recipe.find('_'));
        if (!addIngredient(findId("minefade_raw_" + metal), 1u) ||
            !addIngredient(findId("coal_ore"), 1u)) return false;
        outputId = findId("minefade_" + recipe);
        needsFurnace = true;
    } else if (recipe == "diamond_gem" || recipe == "emerald_gem") {
        const std::string gem = recipe.substr(0, recipe.find('_'));
        addIngredient(findId(gem + "_ore"), 1u);
        outputId = findId("minefade_" + gem);
    } else if (recipe == "glass" || recipe == "stone") {
        if (!addIngredient(findId(recipe == "glass" ? "sand" : "cobblestone"), 1u) ||
            !addIngredient(findId("coal_ore"), 1u)) return false;
        outputId = findId(recipe);
        needsFurnace = true;
    } else if (recipe == "iron_block" || recipe == "gold_block" ||
               recipe == "copper_block" || recipe == "diamond_block" ||
               recipe == "emerald_block" || recipe == "coal_block" ||
               recipe == "redstone_block") {
        std::string material = recipe.substr(0, recipe.find('_'));
        if (material == "iron" || material == "gold" || material == "copper")
            material = "minefade_" + material + "_ingot";
        else if (material == "diamond" || material == "emerald")
            material = "minefade_" + material;
        else if (material == "redstone") material = "redstone_wire";
        else if (material == "coal") material = "coal_ore";
        if (!addIngredient(findId(material), 9u)) return false;
        outputId = findId(recipe); needsTable = true;
    } else if (recipe == "iron_bars") {
        if (!addIngredient(findId("minefade_iron_ingot"), 6u)) return false;
        outputId = findId("iron_bars"); outputAmount = 16u; needsTable = true;
    } else if (recipe == "glass_pane") {
        if (!addIngredient(findId("glass"), 6u)) return false;
        outputId = findId("glass_pane"); outputAmount = 16u; needsTable = true;
    } else if (recipe == "cobblestone_wall") {
        if (!addIngredient(findId("cobblestone"), 6u)) return false;
        outputId = findId("cobblestone_wall"); outputAmount = 6u; needsTable = true;
    } else if (recipe == "nether_brick_fence") {
        if (!addIngredient(findId("nether_brick"), 6u)) return false;
        outputId = findId("nether_brick_fence"); outputAmount = 6u; needsTable = true;
    } else if (recipe == "stonebrick") {
        if (!addIngredient(findId("stone"), 4u)) return false;
        outputId = findId("stonebrick"); outputAmount = 4u; needsTable = true;
    } else if (recipe == "stonecutter") {
        if (!addIngredient(findId("stone"), 3u) ||
            !addIngredient(findId("minefade_iron_ingot"), 1u)) return false;
        outputId = findId("stonecutter"); needsTable = true;
    } else if (recipe == "hopper") {
        if (!addIngredient(findId("minefade_iron_ingot"), 5u) ||
            !addIngredient(findId("chest"), 1u)) return false;
        outputId = findId("hopper"); needsTable = true;
    } else if (recipe == "anvil") {
        if (!addIngredient(findId("iron_block"), 3u) ||
            !addIngredient(findId("minefade_iron_ingot"), 4u)) return false;
        outputId = findId("anvil"); needsTable = true;
    } else if (recipe == "dropper") {
        if (!addIngredient(findId("cobblestone"), 7u) ||
            !addIngredient(findId("redstone_wire"), 1u)) return false;
        outputId = findId("dropper"); needsTable = true;
    } else if (recipe == "piston") {
        if (!addIngredient(findId("planks"), 3u) ||
            !addIngredient(findId("cobblestone"), 4u) ||
            !addIngredient(findId("minefade_iron_ingot"), 1u) ||
            !addIngredient(findId("redstone_wire"), 1u)) return false;
        outputId = findId("piston"); needsTable = true;
    } else if (recipe == "redstone_torch") {
        if (!addIngredient(findId("redstone_wire"), 1u) ||
            !addIngredient(findId("minefade_stick"), 1u)) return false;
        outputId = findId("redstone_torch");
    } else if (recipe == "redstone_lamp") {
        if (!addIngredient(findId("glowstone"), 1u) ||
            !addIngredient(findId("redstone_wire"), 4u)) return false;
        outputId = findId("redstone_lamp"); needsTable = true;
    } else if (recipe == "fence_gate") {
        if (!addIngredient(findId("planks"), 2u) ||
            !addIngredient(findId("minefade_stick"), 4u)) return false;
        outputId = findId("fence_gate"); needsTable = true;
    } else if (recipe == "sandstone") {
        if (!addIngredient(findId("sand"), 4u)) return false;
        outputId = findId("sandstone");
    } else if (recipe == "stone_slab") {
        if (!addIngredient(findId("stone"), 3u)) return false;
        outputId = findId("stone_slab"); outputAmount = 6u; needsStonecutter = true;
    } else if (recipe == "stone_brick_stairs" || recipe == "brick_stairs" ||
               recipe == "nether_brick_stairs" || recipe == "sandstone_stairs" ||
               recipe == "spruce_stairs" || recipe == "birch_stairs" ||
               recipe == "jungle_stairs" || recipe == "acacia_stairs" ||
               recipe == "dark_oak_stairs" || recipe == "cherry_stairs") {
        std::string material = "planks";
        if (recipe == "stone_brick_stairs") material = "stonebrick";
        else if (recipe == "brick_stairs") material = "brick_block";
        else if (recipe == "nether_brick_stairs") material = "nether_brick";
        else if (recipe == "sandstone_stairs") material = "sandstone";
        if (!addIngredient(findId(material), 6u)) return false;
        outputId = findId(recipe); outputAmount = 4u;
        needsStonecutter = material != "planks";
        needsTable = material == "planks";
    } else if (recipe == "spruce_door" || recipe == "birch_door" ||
               recipe == "jungle_door" || recipe == "acacia_door" ||
               recipe == "dark_oak_door") {
        if (!addIngredient(findId("planks"), 6u)) return false;
        outputId = findId(recipe); outputAmount = 3u; needsTable = true;
    } else if (recipe == "spruce_fence_gate" || recipe == "birch_fence_gate" ||
               recipe == "jungle_fence_gate" || recipe == "acacia_fence_gate" ||
               recipe == "dark_oak_fence_gate") {
        if (!addIngredient(findId("planks"), 2u) ||
            !addIngredient(findId("minefade_stick"), 4u)) return false;
        outputId = findId(recipe); needsTable = true;
    } else if (recipe == "golden_rail") {
        if (!addIngredient(findId("minefade_gold_ingot"), 6u) ||
            !addIngredient(findId("minefade_stick"), 1u) ||
            !addIngredient(findId("redstone_wire"), 1u)) return false;
        outputId = findId("golden_rail"); outputAmount = 6u; needsTable = true;
    } else if (recipe == "detector_rail") {
        if (!addIngredient(findId("minefade_iron_ingot"), 6u) ||
            !addIngredient(findId("stone_pressure_plate"), 1u) ||
            !addIngredient(findId("redstone_wire"), 1u)) return false;
        outputId = findId("detector_rail"); outputAmount = 6u; needsTable = true;
    } else if (recipe == "activator_rail") {
        if (!addIngredient(findId("minefade_iron_ingot"), 6u) ||
            !addIngredient(findId("minefade_stick"), 2u) ||
            !addIngredient(findId("redstone_wire"), 1u)) return false;
        outputId = findId("activator_rail"); outputAmount = 6u; needsTable = true;
    } else {
        const std::size_t split = recipe.find('_');
        if (split == std::string::npos) return false;
        const std::string tier = recipe.substr(0, split);
        const std::string kind = recipe.substr(split + 1u);
        const bool isTool = kind == "pickaxe" || kind == "axe" || kind == "shovel" ||
                            kind == "sword" || kind == "hoe";
        const bool isArmor = kind == "helmet" || kind == "chestplate" ||
                             kind == "leggings" || kind == "boots";
        if (!isTool && !isArmor) return false;
        std::string materialKey;
        std::uint16_t materialAmount = 1u;
        if (tier == "wooden") materialKey = "planks";
        else if (tier == "stone") materialKey = "cobblestone";
        else if (tier == "copper" || tier == "iron" || tier == "gold")
            materialKey = "minefade_" + tier + "_ingot";
        else if (tier == "diamond" || tier == "emerald")
            materialKey = "minefade_" + tier;
        else return false;
        if (isTool) {
            const std::uint16_t headCount = kind == "shovel" ? 1u :
                (kind == "sword" ? 2u : (kind == "hoe" ? 2u : 3u));
            const std::uint16_t stickCount = kind == "sword" ? 1u : 2u;
            if (!addIngredient(findId(materialKey), headCount) ||
                !addIngredient(findId("minefade_stick"), stickCount)) return false;
        } else {
            materialAmount = kind == "helmet" ? 5u : kind == "chestplate" ? 8u :
                             kind == "leggings" ? 7u : 4u;
            if (!addIngredient(findId(materialKey), materialAmount)) return false;
        }
        outputId = findId("minefade_" + tier + "_" + kind);
        needsTable = true;
    }

    if (outputId == 0u) {
        ui_.SetChatMessage("La receta no está disponible en este catálogo.");
        return false;
    }
    if (needsTable && !HasNearbyCraftingTable()) {
        ui_.SetChatMessage("Necesitas una mesa de trabajo cerca para fabricar este objeto.");
        return false;
    }
    if (needsFurnace && !HasNearbyFurnace()) {
        ui_.SetChatMessage("Necesitas un horno cerca para fundir este material.");
        return false;
    }
    if (needsStonecutter && !HasNearbyStonecutter()) {
        ui_.SetChatMessage("Necesitas un cortapiedras cerca para fabricar esta pieza.");
        return false;
    }
    // Craft against a private inventory copy. Either the ingredients and output
    // are committed together, or the live inventory stays unchanged.
    Inventory stagedInventory = inventory_;
    for (std::size_t i = 0; i < ingredientCount; ++i) {
        if (!stagedInventory.ConsumeItem(ingredients[i].id, ingredients[i].amount)) {
            ui_.SetChatMessage("Faltan materiales para esta receta.");
            return false;
        }
    }
    if (!stagedInventory.Add(outputId, outputAmount)) {
        ui_.SetChatMessage("No hay espacio suficiente para guardar el resultado.");
        return false;
    }
    inventory_ = stagedInventory;
    if (gameMode_ != 1) AddProgressionXp(5u);
    const BlockDefinition* output = GetBlockDefinition(static_cast<BlockId>(outputId));
    ui_.SetChatMessage(output != nullptr ? std::string("Fabricado: ") + output->nameEs :
                       "Objeto fabricado.");
    RefreshInventoryUi();
    return true;
}

std::string Engine::QuickbarSnapshot() const {
    std::ostringstream output;
    output << inventory_.selectedSlot;
    for (std::size_t slot = 0; slot < Inventory::kHotbarSlotCount; ++slot) {
        const std::uint32_t encoded = inventory_.counts[slot];
        const std::uint16_t itemId = static_cast<std::uint16_t>(encoded / 1000u);
        const std::uint16_t amount = static_cast<std::uint16_t>(encoded % 1000u);
        const BlockDefinition* definition = itemId == 0u
            ? nullptr : GetBlockDefinition(static_cast<BlockId>(itemId));
        output << ';' << itemId << '|' << amount << '|'
               << (definition == nullptr ? "" : definition->nameEs);
    }
    return output.str();
}

void Engine::AddProgressionXp(std::uint32_t amount) {
    if (amount == 0u) return;
    progressionXp_ = std::min(100000000u, progressionXp_ + std::min(amount, 100000000u));
}

std::string Engine::ProgressionSnapshot() const {
    std::ostringstream output;
    output << "v1|" << progressionBlocksMined_ << '|' << progressionBlocksPlaced_ << '|'
           << progressionItemsCollected_ << '|' << progressionJumps_ << '|'
           << (progressionWorldEntered_ ? 1 : 0) << '|' << progressionXp_;
    return output.str();
}

std::string Engine::ExecuteCommand(const std::string& rawCommand) {
    if (!ready_) return "El mundo todavía no está preparado.";
    std::string command = rawCommand;
    const std::size_t first = command.find_first_not_of(" \\t\\r\n");
    if (first == std::string::npos) return "Escribe /ayuda para ver los comandos.";
    command = command.substr(first);
    if (!command.empty() && command.front() == '/') command.erase(command.begin());
    std::istringstream input(command);
    std::string verb;
    input >> verb;
    std::transform(verb.begin(), verb.end(), verb.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (verb == "ayuda" || verb == "help") {
        return "/claim help · /oveja [cantidad] · /vaca [cantidad] · /pollo [cantidad] · /cerdo [cantidad] · "
               "/zombi [cantidad] · /esqueleto [cantidad] · /conejo [cantidad] · "
               "/modo supervivencia|creativo|aventura · /tiempo dia|noche · "
               "/dar <id> [cantidad] · /tp <x> <y> <z> · /semilla · /inventario · /fabricar <receta>";
    }
    if (verb == "claim" || verb == "parcela") {
        std::string action;
        input >> action;
        if (action.empty() || action == "help") {
            return "/claim id · /claim create [radio] · /claim list · /claim info · "
                   "/claim trust <id> · /claim untrust <id> · /claim abandon · "
                   "/claim flag <pvp|explosions|fire|containers|mob_grief> <on|off>";
        }
        if (action == "id") return "Tu ID de protección: " + playerIdentity_;
        if (action == "list") return protection_.ListFor(playerIdentity_);
        if (action == "info") return protection_.DescribeAt(playerIdentity_,
            static_cast<int>(std::floor(player_.position.x)),
            static_cast<int>(std::floor(player_.position.z)));
        std::string feedback;
        if (action == "create") {
            int radius = 16;
            if (!(input >> radius)) radius = 16;
            protection_.CreateClaim(playerIdentity_,
                static_cast<int>(std::floor(player_.position.x)),
                static_cast<int>(std::floor(player_.position.z)), radius, &feedback);
            return feedback;
        }
        if (action == "abandon") {
            protection_.Abandon(playerIdentity_, &feedback);
            return feedback;
        }
        if (action == "trust" || action == "untrust") {
            std::string identity;
            input >> identity;
            if (identity.empty()) return "Indica el ID de jugador. Puedes consultarlo con /claim id.";
            if (action == "trust") protection_.Trust(playerIdentity_, identity,
                ProtectionManager::kAllPermissions, &feedback);
            else protection_.Untrust(playerIdentity_, identity, &feedback);
            return feedback;
        }
        if (action == "flag") {
            std::string flag, value;
            input >> flag >> value;
            if (value != "on" && value != "off") return "Uso: /claim flag <bandera> on|off.";
            protection_.SetFlag(playerIdentity_, flag, value == "on", &feedback);
            return feedback;
        }
        return "Comando de parcela desconocido. Escribe /claim help.";
    }
    if (verb == "inventario" || verb == "inventory") {
        std::ostringstream summary;
        summary << "INVENTARIO · 36 ESPACIOS\n";
        bool any = false;
        for (std::size_t slot = 0; slot < Inventory::kSlotCount; ++slot) {
            const std::uint32_t encoded = inventory_.counts[slot];
            const std::uint16_t id = static_cast<std::uint16_t>(encoded / 1000u);
            const std::uint16_t amount = static_cast<std::uint16_t>(encoded % 1000u);
            if (id == 0u || amount == 0u) continue;
            const BlockDefinition* item = GetBlockDefinition(static_cast<BlockId>(id));
            if (item == nullptr) continue;
            any = true;
            summary << (slot + 1u) << ". " << item->nameEs << " × " << amount;
            if (slot < Inventory::kHotbarSlotCount) summary << " · Barra rápida";
            summary << "\n";
        }
        if (!any) summary << "Vacío.\n";
        summary << "Espacio seleccionado: " << (inventory_.selectedSlot + 1u) << "/9.";
        return summary.str();
    }
    if (verb == "fabricar" || verb == "craft") {
        std::string recipe;
        input >> recipe;
        if (recipe.empty() || recipe == "help" || recipe == "list")
            return "Recetas: planks, sticks, table, torch, chest, furnace, glass, stone, "
                   "wooden_door, trapdoor, ladder, fence, fence_gate, iron_bars, glass_pane, "
                   "stonebrick, cobblestone_wall, nether_brick_fence, iron_block, gold_block, "
                   "copper_block, diamond_block, emerald_block, coal_block, redstone_block, "
                   "hopper, anvil, stonecutter, dropper, piston, redstone_torch, redstone_lamp, "
                   "wooden/stone/copper/iron/gold/diamond/emerald tools and armor.";
        std::transform(recipe.begin(), recipe.end(), recipe.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return CraftRecipe(recipe) ? "Fabricación completada: " + recipe + "." :
            "No se pudo fabricar " + recipe + ". Comprueba materiales y mesa de trabajo.";
    }
    if (!allowCheats_) return "Los trucos están desactivados en este mundo. Actívalos en sus opciones.";

    if (verb == "oveja" || verb == "vaca" || verb == "pollo" || verb == "cerdo" ||
        verb == "zombi" || verb == "esqueleto" || verb == "conejo") {
        int count = 1;
        if (!(input >> count)) count = 1;
        count = std::clamp(count, 1, 16);
        const int kind = verb == "oveja" ? 0 : (verb == "vaca" ? 1 :
            (verb == "pollo" ? 2 : (verb == "cerdo" ? 3 :
            (verb == "zombi" ? 4 : (verb == "esqueleto" ? 5 : 6)))));
        int created = 0;
        for (int i = 0; i < count; ++i)
            if (entities_.SpawnEntity(player_.position, kind, world_)) ++created;
        if (created > 0) {
            static constexpr std::array<std::uint32_t,7> creatureSounds{{
                1024u, 2048u, 4096u, 8192u, 16384u, 32768u, 65536u
            }};
            soundEvents_.fetch_or(creatureSounds[static_cast<std::size_t>(kind)],
                                  std::memory_order_release);
            soundEvents_.fetch_or(262144u, std::memory_order_release);
        }
        RebuildEntityMesh();
        UploadEntityMesh();
        if (created == 1) {
            const std::string article = (verb == "oveja" || verb == "vaca") ? "una " : "un ";
            return "Ha aparecido " + article + verb + ".";
        }
        return "Han aparecido " + std::to_string(created) + " entidades: " + verb + ".";
    }
    if (verb == "modo") {
        std::string mode;
        input >> mode;
        std::transform(mode.begin(), mode.end(), mode.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (mode == "creativo" || mode == "creative") gameMode_ = 1;
        else if (mode == "aventura" || mode == "adventure") gameMode_ = 2;
        else if (mode == "supervivencia" || mode == "survival") gameMode_ = 0;
        else return "Uso: /modo supervivencia, /modo creativo o /modo aventura.";
        RefreshInventoryUi();
        soundEvents_.fetch_or(262144u, std::memory_order_release);
        return "Modo de juego: " + mode + ". Se guardará con el mundo.";
    }
    if (verb == "tiempo" || verb == "time") {
        std::string value;
        input >> value;
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "dia" || value == "día" || value == "day") environment_.SetTimeOfDay(0.25f);
        else if (value == "noche" || value == "night") environment_.SetTimeOfDay(0.75f);
        else return "Uso: /tiempo dia o /tiempo noche.";
        soundEvents_.fetch_or(262144u, std::memory_order_release);
        return "El tiempo del mundo ha cambiado.";
    }
    if (verb == "dar" || verb == "give") {
        int itemId = 0, amount = 1;
        if (!(input >> itemId)) return "Uso: /dar <id_del_bloque> [cantidad].";
        if (!(input >> amount)) amount = 1;
        itemId = std::clamp(itemId, 1, static_cast<int>(GetBlockDefinitionCount()) - 1);
        amount = std::clamp(amount, 1, 999);
        if (!inventory_.Add(static_cast<std::uint16_t>(itemId), static_cast<std::uint16_t>(amount)))
            return "No hay espacio en el inventario o el bloque no admite esa cantidad.";
        RefreshInventoryUi();
        return "Objeto añadido al inventario.";
    }
    if (verb == "tp" || verb == "teletransportar") {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!(input >> x >> y >> z)) return "Uso: /tp <x> <y> <z>.";
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            y < 1.0f || y >= static_cast<float>(VoxelWorld::kWorldHeight - 2))
            return "La posición indicada no es válida.";
        player_.position = {x, y, z};
        player_.verticalVelocity = 0.0f;
        if (!player_.ResolveSafeSpawn(world_)) return "No se encontró una posición segura cerca de esas coordenadas.";
        RebuildWorldMesh();
        UploadWorldMesh();
        UploadTransparentWorldMesh();
        return "Teletransporte completado.";
    }
    if (verb == "semilla" || verb == "seed") {
        return "Semilla del mundo: " + std::to_string(world_.Seed()) + ".";
    }
    return "Comando desconocido. Escribe /ayuda.";
}

void Engine::OpenStarterChest() {
    Vec3 target{};
    BlockId type = BlockId::Air;
    if (!starterChestEnabled_ ||
        !world_.GetRayTargetBlock(ViewPosition(), ViewDirection(), 6.0f, &target, &type) ||
        type != BlockId::Chest) {
        ui_.SetChatMessage("Mira al cofre y pulsa COFRE para abrirlo.");
        return;
    }
    if (!protection_.CanUseContainer(playerIdentity_, static_cast<int>(target.x),
                                     static_cast<int>(target.z))) {
        ui_.SetChatMessage("Este cofre está protegido. Pide permiso a su dueño.");
        return;
    }
    starterChestPosition_ = target;
    std::string markup;
    for (std::size_t i = 0; i < starterChestContents_.size(); ++i) {
        const std::uint32_t encoded = starterChestContents_[i];
        if (encoded == 0u) continue;
        const auto id = static_cast<std::uint16_t>(encoded / 1000u);
        const auto amount = encoded % 1000u;
        const BlockDefinition* definition = GetBlockDefinition(static_cast<BlockId>(id));
        if (definition == nullptr || amount == 0u) continue;
        markup += "<div class='inventory-row'><img class='inventory-icon' src='minefade://block/"
            + std::to_string(id) + "'/><span>" + definition->nameEs + " × "
            + std::to_string(amount) + "</span></div>";
    }
    ui_.SetChestContents(markup.empty() ? "<div class='inventory-row'>El cofre está vacío.</div>" : markup);
    starterChestOpen_ = true;
    ui_.SetPanelVisible("starter-chest-panel", true);
    soundEvents_.fetch_or(131072u, std::memory_order_release);
}

void Engine::TakeStarterChestContents() {
    if (!starterChestOpen_) return;
    std::uint32_t taken = 0u;
    for (std::uint32_t& encoded : starterChestContents_) {
        if (encoded == 0u) continue;
        const auto itemId = static_cast<std::uint16_t>(encoded / 1000u);
        const auto amount = static_cast<std::uint16_t>(encoded % 1000u);
        if (itemId != 0u && amount != 0u && inventory_.Add(itemId, amount)) {
            taken += amount;
            encoded = 0u;
        }
    }
    if (taken == 0u) {
        ui_.SetChatMessage("No hay espacio libre suficiente en tu inventario.");
        return;
    }
    const bool empty = std::all_of(starterChestContents_.begin(), starterChestContents_.end(),
                                   [](std::uint32_t value) { return value == 0u; });
    if (empty) {
        world_.SetBlock(static_cast<int>(starterChestPosition_.x),
                        static_cast<int>(starterChestPosition_.y),
                        static_cast<int>(starterChestPosition_.z), BlockId::Air);
        starterChestOpen_ = false;
        ui_.SetPanelVisible("starter-chest-panel", false);
        RebuildWorldMesh();
        UploadWorldMesh();
        UploadTransparentWorldMesh();
        SaveWorld();
    } else {
        OpenStarterChest();
    }
    RefreshInventoryUi();
    ui_.SetChatMessage("Has recogido " + std::to_string(taken) + " objetos del cofre.");
}

void Engine::UpdateMining(float deltaSeconds) {
    auto active = std::find_if(touches_.begin(), touches_.end(), [](const auto& entry) {
        const TouchState& state = entry.second;
        return state.mining && !state.moved && !state.uiHandled && !state.joystick;
    });

    if (active == touches_.end()) {
        miningPointerId_ = -1;
        miningElapsed_ = 0.0f;
        miningRequired_ = 0.0f;
        Vec3 target{};
        BlockId targetType = BlockId::Air;
        if (world_.GetRayTargetBlock(ViewPosition(), ViewDirection(), 6.0f,
                                     &target, &targetType)) {
            const BlockDefinition* definition = GetBlockDefinition(targetType);
            ui_.SetTargetBlock(definition != nullptr ? definition->nameEs : "", 0.0f);
        } else {
            ui_.SetTargetBlock({}, 0.0f);
        }
        return;
    }

    const int pointerId = active->first;
    Vec3 target{};
    BlockId targetType = BlockId::Air;
    if (!world_.GetRayTargetBlock(player_.CameraPosition(), player_.Forward(), 6.0f,
                                  &target, &targetType)) {
        miningPointerId_ = pointerId;
        miningElapsed_ = 0.0f;
        miningRequired_ = 0.0f;
        miningBlockType_ = BlockId::Air;
        ui_.SetTargetBlock({}, 0.0f);
        return;
    }

    const BlockDefinition* definition = GetBlockDefinition(targetType);
    if (definition == nullptr) {
        ui_.SetTargetBlock({}, 0.0f);
        return;
    }

    const bool changedTarget = miningPointerId_ != pointerId ||
        miningBlockType_ != targetType ||
        static_cast<int>(miningBlock_.x) != static_cast<int>(target.x) ||
        static_cast<int>(miningBlock_.y) != static_cast<int>(target.y) ||
        static_cast<int>(miningBlock_.z) != static_cast<int>(target.z);
    if (changedTarget) {
        miningPointerId_ = pointerId;
        miningBlock_ = target;
        miningBlockType_ = targetType;
        miningElapsed_ = 0.0f;
        miningRequired_ = 0.0f;
    }

    if (definition->hardness < 0.0f) {
        ui_.SetTargetBlock(definition->nameEs, 0.0f);
        return;
    }

    if (gameMode_ == 2) {
        ui_.SetTargetBlock(definition->nameEs, 0.0f);
        return;
    }
    // Wooden tools accelerate the material they are designed to harvest.
    float toolMultiplier = 1.0f;
    const BlockDefinition* heldDefinition =
        GetBlockDefinition(static_cast<BlockId>(inventory_.SelectedItemId()));
    if (heldDefinition != nullptr) {
        const std::string_view heldKey(heldDefinition->key);
        const bool matchingPickaxe = definition->preferredTool == BlockTool::Pickaxe &&
            heldKey.find("_pickaxe") != std::string_view::npos;
        const bool matchingAxe = definition->preferredTool == BlockTool::Axe &&
            heldKey.find("_axe") != std::string_view::npos;
        const bool matchingShovel = definition->preferredTool == BlockTool::Shovel &&
            heldKey.find("_shovel") != std::string_view::npos;
        if (matchingPickaxe || matchingAxe || matchingShovel) {
            if (heldKey.find("_diamond_") != std::string_view::npos) toolMultiplier = 0.30f;
            else if (heldKey.find("_emerald_") != std::string_view::npos) toolMultiplier = 0.34f;
            else if (heldKey.find("_gold_") != std::string_view::npos) toolMultiplier = 0.38f;
            else if (heldKey.find("_iron_") != std::string_view::npos) toolMultiplier = 0.45f;
            else if (heldKey.find("_copper_") != std::string_view::npos) toolMultiplier = 0.52f;
            else if (heldKey.find("_stone_") != std::string_view::npos) toolMultiplier = 0.62f;
            else toolMultiplier = 0.78f;
        } else if (definition->preferredTool != BlockTool::None &&
                   (heldKey.find("_sword") != std::string_view::npos ||
                    heldKey.find("_hoe") != std::string_view::npos ||
                    heldKey == "minefade_stick")) toolMultiplier = 1.15f;
    }
    // Creative mode breaks ordinary blocks almost instantly; survival keeps the hardness curve.
    miningRequired_ = gameMode_ == 1 ? 0.07f
        : std::clamp(definition->hardness * 1.5f * toolMultiplier /
                         std::clamp(modMiningSpeed_, 0.5f, 3.0f), 0.12f, 10.0f);
    miningElapsed_ += std::clamp(deltaSeconds, 0.0f, 0.05f);
    const float progress = std::clamp(miningElapsed_ / miningRequired_, 0.0f, 1.0f);
    ui_.SetTargetBlock(definition->nameEs, progress);
    if (progress < 1.0f) return;

    if (!protection_.CanBuild(playerIdentity_, static_cast<int>(target.x), static_cast<int>(target.z))) {
        ui_.SetChatMessage("Zona protegida. El dueño debe autorizarte para romper bloques.");
        active->second.mining = false;
        miningPointerId_ = -1;
        miningElapsed_ = 0.0f;
        miningRequired_ = 0.0f;
        ui_.SetTargetBlock(definition->nameEs, 0.0f);
        return;
    }
    Vec3 broken{};
    BlockId brokenType = BlockId::Air;
    if (world_.BreakRay(ViewPosition(), ViewDirection(), 6.0f, &broken, &brokenType)) {
        firstPersonActionStarted_ = ElapsedSeconds();
        firstPersonActionKind_ = 1; // MINE: a block was actually broken.
        pendingBlockEdits_.insert(pendingBlockEdits_.end(), {
            static_cast<int>(broken.x), static_cast<int>(broken.y), static_cast<int>(broken.z),
            static_cast<int>(BlockId::Air)
        });
        if (progressionBlocksMined_ < 10000000u) ++progressionBlocksMined_;
        if (gameMode_ != 1) AddProgressionXp(2u);
        const BlockDefinition* brokenDefinition = GetBlockDefinition(brokenType);
        if (brokenDefinition != nullptr && brokenDefinition->dropId > 0u) {
            const auto dropQuantity = static_cast<std::uint16_t>(std::clamp(modDropQuantity_, 1, 4));
            droppedItems_.Spawn({broken.x + 0.5f, broken.y + 0.55f, broken.z + 0.5f},
                                brokenDefinition->dropId, dropQuantity);
        }
        if (inventory_.DamageSelectedItem(1u)) RefreshInventoryUi();
        soundEvents_.fetch_or(1u, std::memory_order_release);
        if (HasBlockFlag(brokenType, BlockFluid))
            soundEvents_.fetch_or(524288u, std::memory_order_release);
        if (brokenDefinition != nullptr) {
            if (brokenDefinition->preferredTool == BlockTool::Axe)
                soundEvents_.fetch_or(128u, std::memory_order_release);
            else if (brokenDefinition->preferredTool == BlockTool::Shovel)
                soundEvents_.fetch_or(256u, std::memory_order_release);
            else if (brokenDefinition->preferredTool == BlockTool::Shears)
                soundEvents_.fetch_or(512u, std::memory_order_release);
            else if (brokenDefinition->preferredTool == BlockTool::Pickaxe)
                soundEvents_.fetch_or(64u, std::memory_order_release);
        }
        RebuildWorldMesh();
        UploadWorldMesh();
        UploadTransparentWorldMesh();
    }

    active->second.mining = false;
    active->second.moved = true;
    miningPointerId_ = -1;
    miningElapsed_ = 0.0f;
    miningRequired_ = 0.0f;
    ui_.SetTargetBlock({}, 0.0f);
}

void Engine::RefreshInventoryUi() {
    std::string markup;
    markup.reserve(gameMode_ == 1 ? 42000u : 2400u);
    for(std::size_t i=0;i<inventory_.counts.size();++i) {
        const std::uint32_t encoded=inventory_.counts[i];
        const std::uint16_t itemId=static_cast<std::uint16_t>(encoded/1000u);
        const std::uint32_t quantity=encoded%1000u;
        const BlockDefinition* definition=GetBlockDefinition(static_cast<BlockId>(itemId));
        const char* name=definition!=nullptr?definition->nameEs:"Vacío";
        std::string icon;
        if(quantity>0u&&itemId>0u) {
            icon="<img class='inventory-icon' src='minefade://block/"+
                std::to_string(itemId)+"'/>";
        } else {
            icon="<span class='inventory-icon empty-icon'>—</span>";
        }
        markup+="<button class='inventory-row' id='inventory-slot-"+std::to_string(i)+"'>"+icon+"<span>";
        markup+=name;
        if(quantity>0u)markup+=" × "+std::to_string(quantity);
        const std::uint16_t itemMaxDurability = inventory_.SlotItemMaxDurability(i);
        const std::uint16_t itemDurability = inventory_.SlotItemDurability(i);
        if (itemMaxDurability > 0u) {
            const int percent = std::clamp(
                static_cast<int>((static_cast<unsigned int>(itemDurability) * 100u) /
                                 static_cast<unsigned int>(itemMaxDurability)), 0, 100);
            markup += "<span class='inventory-durability'><span style='width:" +
                std::to_string(percent) + "%'></span></span>";
        }
        markup+="</span></button>";

        std::string slotMarkup="·";
        if(quantity>0u&&itemId>0u) {
            slotMarkup="<img src='minefade://block/"+std::to_string(itemId)+"'/>";
            if(quantity>1u)slotMarkup+="<span class='slot-count'>"+
                std::to_string(quantity)+"</span>";
            if (itemMaxDurability > 0u) {
                const int percent = std::clamp(
                    static_cast<int>((static_cast<unsigned int>(itemDurability) * 100u) /
                                     static_cast<unsigned int>(itemMaxDurability)), 0, 100);
                slotMarkup += "<span class='slot-durability'><span style='width:" +
                    std::to_string(percent) + "%'></span></span>";
            }
        }
        ui_.SetQuickbarSlot(static_cast<int>(i),slotMarkup,i==inventory_.selectedSlot);
    }
    if (gameMode_ == 1) {
        markup.clear();
        markup += "<div class='creative-heading'>CATÁLOGO CREATIVO · SELECCIONA UN BLOQUE</div>";
        for (std::size_t id = 1; id < GetBlockDefinitionCount(); ++id) {
            const auto blockId = static_cast<BlockId>(id);
            const BlockDefinition* definition = GetBlockDefinition(blockId);
            if (definition == nullptr || !HasBlockFlag(blockId, BlockPlaceable)) continue;
            markup += "<div class='inventory-row creative-row'><img class='inventory-icon' src='minefade://block/"
                + std::to_string(id) + "'/><span class='creative-name'>" + definition->nameEs
                + "</span><button id='creative-select-" + std::to_string(id)
                + "' class='creative-add'>USAR</button></div>";
        }
    }
    ui_.SetInventoryContents(markup);
    std::string materialMarkup;
    materialMarkup.reserve(1200u);
    materialMarkup += "<div class='recipe-category'>RECURSOS GUARDADOS</div>";
    for (std::size_t i = 0; i < inventory_.counts.size(); ++i) {
        const std::uint32_t encoded = inventory_.counts[i];
        const std::uint16_t id = static_cast<std::uint16_t>(encoded / 1000u);
        const std::uint32_t quantity = encoded % 1000u;
        if (id == 0u || quantity == 0u) continue;
        const BlockDefinition* item = GetBlockDefinition(static_cast<BlockId>(id));
        if (item == nullptr) continue;
        materialMarkup += "<div class='material-row'><img class='inventory-icon' src='minefade://block/" +
            std::to_string(id) + "'/><span>" + std::string(item->nameEs) + " × " +
            std::to_string(quantity) + "</span></div>";
    }
    ui_.SetInventoryCatalogContents(materialMarkup);
    std::string equipmentMarkup;
    const std::array<const char*,4> equipmentNames{{"Casco","Pechera","Grebas","Botas"}};
    for (std::size_t slot = 0; slot < inventory_.equippedArmor.size(); ++slot) {
        const std::uint16_t id = inventory_.equippedArmor[slot];
        equipmentMarkup += "<button class='equipment-slot' id='gear-slot-" +
            std::to_string(slot) + "'>";
        if (id > 0u) equipmentMarkup += "<img class='inventory-icon' src='minefade://block/" +
            std::to_string(id) + "'/>";
        else equipmentMarkup += equipmentNames[slot];
        equipmentMarkup += "</button>";
    }
    ui_.SetEquipmentContents(equipmentMarkup);
    const bool quickbarSelectionChanged =
        inventory_.selectedSlot != lastQuickbarSelection_;
    if (quickbarSelectionChanged) {
        lastQuickbarSelection_ = inventory_.selectedSlot;
        selectedItemToastUntil_ = ElapsedSeconds() + 1.25;
    }
    const std::uint16_t selectedId = inventory_.SelectedItemId();
    const BlockDefinition* selectedDefinition =
        GetBlockDefinition(static_cast<BlockId>(selectedId));
    if (selectedDefinition != nullptr && selectedId > 0u &&
        ElapsedSeconds() < selectedItemToastUntil_) {
        std::string selectedMarkup = "<img src='minefade://block/" +
            std::to_string(selectedId) + "'/><span>" + selectedDefinition->nameEs + "</span>";
        const std::uint16_t maximum = inventory_.SelectedItemMaxDurability();
        if (maximum > 0u) {
            const std::uint16_t remaining = inventory_.SelectedItemDurability();
            const int percent = std::clamp(
                static_cast<int>((static_cast<unsigned int>(remaining) * 100u) /
                                 static_cast<unsigned int>(maximum)), 0, 100);
            selectedMarkup += "<span class='selected-durability'>Durabilidad " +
                std::to_string(remaining) + "/" + std::to_string(maximum) + "</span>";
        }
        ui_.SetSelectedItem(selectedMarkup);
    } else {
        ui_.SetSelectedItem({});
    }
}

void Engine::RefreshCraftingUi() {
    std::string markup;
    markup.reserve(9000u);
    int index = 0;
    const auto addRecipe = [&markup, &index](const std::string& recipe, const std::string& label) {
        markup += "<button id='crafting-recipe-" + std::to_string(index++) +
            "' data-action='craft-" + recipe + "'>" + label + "</button>";
    };
    markup += "<div class='recipe-category'>BÁSICOS</div>";
    addRecipe("planks", "Tronco → 4 tablones");
    addRecipe("sticks", "2 tablones → 4 palos");
    addRecipe("table", "4 tablones → mesa");
    markup += "<div class='recipe-category'>SUPERVIVENCIA Y CONSTRUCCIÓN</div>";
    addRecipe("torch", "Mena de carbón + palo → 4 antorchas");
    addRecipe("chest", "8 tablones → cofre");
    addRecipe("furnace", "8 adoquines → horno");
    addRecipe("wooden_door", "6 tablones → 3 puertas");
    addRecipe("trapdoor", "6 tablones → 2 trampillas");
    addRecipe("ladder", "7 palos → 3 escaleras");
    addRecipe("fence", "4 tablones + 2 palos → 3 vallas");
    addRecipe("wooden_slab", "3 tablones → 6 losas");
    addRecipe("oak_stairs", "6 tablones → 4 escaleras de madera");
    addRecipe("stone_stairs", "6 adoquines → 4 escaleras de piedra");
    markup += "<div class='recipe-category'>MECANISMOS</div>";
    addRecipe("lever", "Adoquín + palo → palanca");
    addRecipe("wooden_pressure_plate", "2 tablones → placa de madera");
    addRecipe("stone_pressure_plate", "2 adoquines → placa de piedra");
    addRecipe("wooden_button", "1 tablón → botón de madera");
    addRecipe("stone_button", "1 adoquín → botón de piedra");
    addRecipe("standing_sign", "6 tablones + palo → 3 señales");
    addRecipe("rail", "6 lingotes de hierro + palo → 16 raíles");
    markup += "<div class='recipe-category'>PROCESAR MINERALES</div>";
    addRecipe("copper_ingot", "Mena de cobre + carbón → lingote");
    addRecipe("iron_ingot", "Mena de hierro + carbón → lingote");
    addRecipe("gold_ingot", "Mena de oro + carbón → lingote");
    addRecipe("diamond_gem", "Mena de diamante → diamante");
    addRecipe("emerald_gem", "Mena de esmeralda → esmeralda");
    const std::array<std::pair<const char*, const char*>, 7> tiers{{
        {"wooden","MADERA"},{"stone","PIEDRA"},{"copper","COBRE"},
        {"iron","HIERRO"},{"gold","ORO"},{"diamond","DIAMANTE"},{"emerald","ESMERALDA"}
    }};
    const std::array<std::pair<const char*, const char*>, 9> gear{{
        {"pickaxe","Pico"},{"axe","Hacha"},{"shovel","Pala"},{"sword","Espada"},{"hoe","Azada"},
        {"helmet","Casco"},{"chestplate","Pechera"},{"leggings","Grebas"},{"boots","Botas"}
    }};
    for (const auto& tier : tiers) {
        markup += "<div class='recipe-category'>" + std::string(tier.second) + " · HERRAMIENTAS Y ARMADURA</div>";
        for (const auto& item : gear) {
            std::string label = std::string(item.second) + " de " + tier.second;
            addRecipe(std::string(tier.first) + "_" + item.first, label);
        }
    }
    ui_.SetCraftingContents(markup);
}

bool Engine::SaveWorld() {
    if (multiplayerMode_ && multiplayerSlot_ != 0) return true;
    if (storagePath_.empty()) {
        ENGINE_LOGE("Cannot save: Android private storage path has not been supplied");
        return false;
    }
    // Persistence errors must never be reported as a graphics/world startup failure.
    // A user should still be able to enter and play while the storage problem is visible
    // in Logcat; the original world file remains untouched if the atomic save fails.
    try {
        if (!world_.Save(storagePath_, player_.position, inventory_.counts)) {
            ENGINE_LOGE("Failed to save world to %s", storagePath_.c_str());
            return false;
        } else {
            ENGINE_LOGI("World saved to private app storage");
            if (!protection_.Save())
                ENGINE_LOGW("Protection claims could not be saved; existing claims file was preserved");
            {
                const std::string progressionPath = storagePath_ + ".progression";
                const std::string progressionTemp = progressionPath + ".tmp";
                const std::uint32_t magic = 0x4D465052u;
                const std::uint32_t version = 1u;
                const std::uint32_t entered = progressionWorldEntered_ ? 1u : 0u;
                std::ofstream progressionFile(progressionTemp, std::ios::binary | std::ios::trunc);
                if (progressionFile) {
                    progressionFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
                    progressionFile.write(reinterpret_cast<const char*>(&version), sizeof(version));
                    progressionFile.write(reinterpret_cast<const char*>(&progressionXp_), sizeof(progressionXp_));
                    progressionFile.write(reinterpret_cast<const char*>(&progressionBlocksMined_), sizeof(progressionBlocksMined_));
                    progressionFile.write(reinterpret_cast<const char*>(&progressionBlocksPlaced_), sizeof(progressionBlocksPlaced_));
                    progressionFile.write(reinterpret_cast<const char*>(&progressionItemsCollected_), sizeof(progressionItemsCollected_));
                    progressionFile.write(reinterpret_cast<const char*>(&progressionJumps_), sizeof(progressionJumps_));
                    progressionFile.write(reinterpret_cast<const char*>(&entered), sizeof(entered));
                    progressionFile.flush();
                    progressionFile.close();
                    std::error_code progressionError;
                    std::filesystem::rename(progressionTemp, progressionPath, progressionError);
                    if (progressionError) {
                        std::filesystem::remove(progressionPath, progressionError);
                        progressionError.clear();
                        std::filesystem::rename(progressionTemp, progressionPath, progressionError);
                    }
                    if (progressionError) {
                        ENGINE_LOGE("Could not atomically save progression: %s", progressionError.message().c_str());
                        std::filesystem::remove(progressionTemp);
                    }
                } else {
                    ENGINE_LOGE("Could not create progression sidecar temporary file");
                }
            }
            {
                const std::string gearPath = storagePath_ + ".armor";
                const std::string gearTemp = gearPath + ".tmp";
                std::ofstream gearFile(gearTemp, std::ios::binary | std::ios::trunc);
                if (gearFile) {
                    gearFile.write(reinterpret_cast<const char*>(inventory_.equippedArmor.data()),
                                   static_cast<std::streamsize>(sizeof(inventory_.equippedArmor)));
                    gearFile.flush();
                    gearFile.close();
                    std::error_code gearError;
                    std::filesystem::rename(gearTemp, gearPath, gearError);
                    if (gearError) {
                        std::filesystem::remove(gearPath, gearError);
                        gearError.clear();
                        std::filesystem::rename(gearTemp, gearPath, gearError);
                    }
                    if (gearError) std::filesystem::remove(gearTemp);
                }
            }
            {
                const std::string modePath = storagePath_ + ".mode";
                const std::string modeTemp = modePath + ".tmp";
                std::ofstream modeFile(modeTemp, std::ios::binary | std::ios::trunc);
                if (modeFile) {
                    const std::int32_t value = static_cast<std::int32_t>(gameMode_);
                    modeFile.write(reinterpret_cast<const char*>(&value), sizeof(value));
                    modeFile.flush();
                    modeFile.close();
                    std::error_code error;
                    std::filesystem::rename(modeTemp, modePath, error);
                    if (error) {
                        std::filesystem::remove(modePath, error);
                        error.clear();
                        std::filesystem::rename(modeTemp, modePath, error);
                    }
                    if (error) std::filesystem::remove(modeTemp);
                }
            }
            {
                const std::string survivalPath = storagePath_ + ".survival";
                const std::string survivalTemp = survivalPath + ".tmp";
                const std::uint32_t magic = 0x4D465356u;
                const std::uint32_t version = 1u;
                std::ofstream survivalFile(survivalTemp, std::ios::binary | std::ios::trunc);
                if (survivalFile) {
                    survivalFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
                    survivalFile.write(reinterpret_cast<const char*>(&version), sizeof(version));
                    survivalFile.write(reinterpret_cast<const char*>(&playerHealth_), sizeof(playerHealth_));
                    survivalFile.write(reinterpret_cast<const char*>(&playerHunger_), sizeof(playerHunger_));
                    survivalFile.flush();
                    survivalFile.close();
                    std::error_code error;
                    std::filesystem::rename(survivalTemp, survivalPath, error);
                    if (error) {
                        std::filesystem::remove(survivalPath, error);
                        error.clear();
                        std::filesystem::rename(survivalTemp, survivalPath, error);
                    }
                    if (error) {
                        ENGINE_LOGE("Could not atomically save survival stats: %s", error.message().c_str());
                        std::filesystem::remove(survivalTemp);
                    }
                } else {
                    ENGINE_LOGE("Could not create survival sidecar temporary file");
                }
            }
            {
                const std::string durabilityPath = storagePath_ + ".durability";
                const std::string durabilityTemp = durabilityPath + ".tmp";
                const std::uint32_t magic = 0x4D464459u;
                const std::uint32_t version = 1u;
                std::ofstream durabilityFile(durabilityTemp, std::ios::binary | std::ios::trunc);
                if (durabilityFile) {
                    durabilityFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
                    durabilityFile.write(reinterpret_cast<const char*>(&version), sizeof(version));
                    durabilityFile.write(reinterpret_cast<const char*>(inventory_.toolDurability.data()),
                        static_cast<std::streamsize>(sizeof(inventory_.toolDurability)));
                    durabilityFile.flush();
                    durabilityFile.close();
                    std::error_code error;
                    std::filesystem::rename(durabilityTemp, durabilityPath, error);
                    if (error) {
                        std::filesystem::remove(durabilityPath, error);
                        error.clear();
                        std::filesystem::rename(durabilityTemp, durabilityPath, error);
                    }
                    if (error) {
                        ENGINE_LOGE("Could not atomically save tool durability: %s", error.message().c_str());
                        std::filesystem::remove(durabilityTemp);
                    }
                } else {
                    ENGINE_LOGE("Could not create tool durability temporary file");
                }
            }
            if (starterChestEnabled_) {
                const std::string chestPath = storagePath_ + ".chest";
                const std::string tempPath = chestPath + ".tmp";
                std::ofstream chestFile(tempPath, std::ios::binary | std::ios::trunc);
                if (chestFile) {
                    chestFile.write(reinterpret_cast<const char*>(starterChestContents_.data()),
                                    static_cast<std::streamsize>(sizeof(starterChestContents_)));
                    chestFile.flush();
                    chestFile.close();
                    std::error_code error;
                    std::filesystem::rename(tempPath, chestPath, error);
                    if (error) {
                        std::filesystem::remove(chestPath, error);
                        error.clear();
                        std::filesystem::rename(tempPath, chestPath, error);
                    }
                    if (error) std::filesystem::remove(tempPath);
                }
            }
        }
    } catch (const std::exception& error) {
        ENGINE_LOGE("World save raised an exception: %s", error.what());
        return false;
    } catch (...) {
        ENGINE_LOGE("World save raised an unknown exception");
        return false;
    }
    return true;
}

bool Engine::Pause() {
    if (!ready_) return true;
    const bool saved = SaveWorld();
    paused_ = true;
    player_.SetMovement(0.0f, 0.0f);
    return saved;
}

void Engine::Resume() {
    if (!ready_) return;
    paused_ = pauseVisible_ || inventoryVisible_;
}

void Engine::SetStoragePath(std::string path) {
    if (path.size() >= 8u && path.compare(path.size() - 8u, 8u, ".mfworld") == 0)
        storagePath_ = std::move(path);
    else
        storagePath_ = std::move(path) + "/worlds/default.mfworld";
}

void Engine::SetWorldOptions(int gameMode, bool allowCheats, bool starterChest, std::uint32_t seed, bool flatWorld, int difficulty, bool generateStructures) {
    // Setters are called before the next nativeInit, while the previous world may
    // still be ready/paused. Stage these values so they cannot be discarded or
    // applied to the outgoing world during a world transition.
    pendingGameMode_ = std::clamp(gameMode, 0, 2);
    pendingAllowCheats_ = allowCheats;
    pendingStarterChestEnabled_ = starterChest;
    pendingWorldSeed_ = seed == 0u ? 0x4D464445u : seed;
    pendingFlatWorld_ = flatWorld;
    pendingDifficulty_ = std::clamp(difficulty, 0, 3);
    pendingGenerateStructures_ = generateStructures;
}

void Engine::SetModsDirectory(std::string path) {
    if (!ready_) modsDirectory_ = std::move(path);
}

void Engine::LoadEnabledMods() {
    modSheepSpawns_ = modCowSpawns_ = modBirdSpawns_ = modPigSpawns_ = 0;
    modRabbitSpawns_ = modZombieSpawns_ = modSkeletonSpawns_ = 0;
    modPlayerSpeed_ = 1.0f;
    modDaySpeed_ = 1.0f;
    modEntitySpeed_ = 1.0f;
    modPlayerJump_ = 1.0f;
    modCrouchSpeed_ = 0.46f;
    modPassiveEntitySpeed_ = 1.0f;
    modHostileEntitySpeed_ = 1.0f;
    modFleeRadius_ = 6.0f;
    modAlertRadius_ = 15.0f;
    modSpawnRadius_ = 1.8f;
    modPickupRadius_ = 1.65f;
    modDropLifetime_ = 300.0f;
    modMiningSpeed_ = 1.0f;
    modDropQuantity_ = 1;
    modEntityCap_ = 128;
    modSpawnInterval_ = 0.0f;
    modSpawnDistance_ = 18.0f;
    modPassiveEntityCap_ = 24;
    if (modsDirectory_.empty()) return;

    std::error_code error;
    if (!std::filesystem::is_directory(modsDirectory_, error) || error) return;

    std::vector<std::filesystem::path> packs;
    for (std::filesystem::recursive_directory_iterator it(
             modsDirectory_, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        const auto& entry = *it;
        std::error_code statusError;
        if (entry.is_symlink(statusError)) {
            if (entry.is_directory(statusError)) it.disable_recursion_pending();
            continue;
        }
        if (statusError || !entry.is_regular_file(statusError) || statusError ||
            entry.path().extension() != ".mfmod") continue;

        const std::size_t depth = static_cast<std::size_t>(it.depth());
        if (depth == 0u) {
            packs.push_back(entry.path()); // Legacy flat MFMod v1 file.
        } else if (depth == 2u && entry.path().filename() == "gameplay.mfmod" &&
                   entry.path().parent_path().filename() == "data") {
            const std::filesystem::path packageRoot = entry.path().parent_path().parent_path();
            std::error_code manifestError;
            if (std::filesystem::is_regular_file(packageRoot / "manifest.json", manifestError) &&
                !manifestError) {
                packs.push_back(entry.path()); // Validated MFMod v2 settings payload.
            }
        }
    }
    if (error) {
        ENGINE_LOGW("Mod directory traversal ended early: %s", error.message().c_str());
        error.clear();
    }
    std::sort(packs.begin(), packs.end());
    bool hasExplicitSpawnDistance = false;
    bool hasExplicitPassiveCap = false;

    const auto isAllowedKey = [](const std::string& key) {
        static const std::unordered_set<std::string> allowed{
            "id", "name", "version", "description", "enabled",
            "spawn.sheep", "spawn.cow", "spawn.bird", "spawn.pig",
            "spawn.rabbit", "spawn.zombie", "spawn.skeleton",
            "player.speed", "player.jump", "player.crouch_speed",
            "entity.speed", "entity.passive_speed", "entity.hostile_speed",
            "entity.flee_radius", "entity.alert_radius", "entity.spawn_radius",
            "world.day_speed", "world.entity_cap", "world.spawn_interval",
            "world.spawn_distance", "world.passive_cap",
            "drops.pickup_radius", "drops.lifetime", "drops.quantity",
            "mining.speed"
        };
        return allowed.find(key) != allowed.end();
    };
    const auto parseFiniteNumber = [](const std::string& text, float& output) {
        try {
            std::size_t consumed = 0;
            const float value = std::stof(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value)) return false;
            output = value;
            return true;
        } catch (...) {
            return false;
        }
    };
    const auto validIdentifier = [](const std::string& id) {
        if (id.size() < 2u || id.size() > 64u) return false;
        const auto isAlphaNumeric = [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        };
        if (!isAlphaNumeric(static_cast<unsigned char>(id.front()))) return false;
        for (const unsigned char c : id) {
            if (!isAlphaNumeric(c) && c != '_' && c != '.' && c != '-') return false;
        }
        return true;
    };

    for (const auto& pack : packs) {
        const std::uintmax_t size = std::filesystem::file_size(pack, error);
        if (error || size == 0u || size > 32768u) {
            error.clear();
            continue;
        }

        std::ifstream file(pack, std::ios::binary);
        if (!file) continue;
        std::unordered_map<std::string, std::string> values;
        std::string line;
        std::size_t bytes = 0;
        std::size_t lineCount = 0;
        bool valid = true;

        while (std::getline(file, line)) {
            bytes += line.size() + 1u;
            if (bytes > 32768u || ++lineCount > 128u) {
                valid = false;
                break;
            }
            const std::size_t first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#') continue;
            const std::size_t split = line.find('=', first);
            if (split == std::string::npos || split == first) {
                valid = false;
                break;
            }
            const std::size_t keyEnd = line.find_last_not_of(" \t", split - 1u);
            if (keyEnd == std::string::npos || keyEnd < first) {
                valid = false;
                break;
            }
            const std::string key = line.substr(first, keyEnd - first + 1u);
            const std::size_t valueStart = line.find_first_not_of(" \t", split + 1u);
            std::string value = valueStart == std::string::npos ? std::string{} : line.substr(valueStart);
            const std::size_t valueEnd = value.find_last_not_of(" \t\r");
            if (valueEnd != std::string::npos) value.resize(valueEnd + 1u);
            if (key.empty() || key.size() > 64u || value.size() > 512u ||
                !isAllowedKey(key) || values.find(key) != values.end()) {
                valid = false;
                break;
            }
            values.emplace(key, std::move(value));
        }
        if (!valid || file.bad()) continue;

        const auto id = values.find("id");
        const auto name = values.find("name");
        const auto version = values.find("version");
        const auto enabled = values.find("enabled");
        if (id == values.end() || name == values.end() || version == values.end() ||
            enabled == values.end() || !validIdentifier(id->second) ||
            name->second.empty() || name->second.size() > 96u ||
            (version->second != "1" && version->second != "1.0") ||
            (enabled->second != "true" && enabled->second != "false")) continue;
        const auto description = values.find("description");
        if (description != values.end() && description->second.size() > 512u) continue;
        if (enabled->second != "true") continue;

        const auto readSpawn = [&values](const char* key, int& output) {
            const auto found = values.find(key);
            if (found == values.end()) { output = 0; return true; }
            try {
                std::size_t consumed = 0;
                const int number = std::stoi(found->second, &consumed);
                if (consumed != found->second.size() || number < 0 || number > 24) return false;
                output = number;
                return true;
            } catch (...) {
                return false;
            }
        };
        const auto readRange = [&values, &parseFiniteNumber](const char* key, float fallback,
                                                              float minimum, float maximum,
                                                              float& output) {
            const auto found = values.find(key);
            if (found == values.end()) { output = fallback; return true; }
            float number = 0.0f;
            if (!parseFiniteNumber(found->second, number) ||
                number < minimum || number > maximum) return false;
            output = number;
            return true;
        };
        const auto readIntRange = [&values](const char* key, int fallback, int minimum,
                                             int maximum, int& output) {
            const auto found = values.find(key);
            if (found == values.end()) { output = fallback; return true; }
            try {
                std::size_t consumed = 0;
                const int number = std::stoi(found->second, &consumed);
                if (consumed != found->second.size() || number < minimum || number > maximum)
                    return false;
                output = number;
                return true;
            } catch (...) {
                return false;
            }
        };

        int sheep = 0, cows = 0, birds = 0, pigs = 0;
        int rabbits = 0, zombies = 0, skeletons = 0;
        int dropQuantity = 1, entityCap = 128, passiveEntityCap = 24;
        float playerSpeed = 1.0f, playerJump = 1.0f, daySpeed = 1.0f, entitySpeed = 1.0f;
        float crouchSpeed = 0.46f, passiveSpeed = 1.0f, hostileSpeed = 1.0f;
        float fleeRadius = 6.0f, alertRadius = 15.0f, spawnRadius = 1.8f;
        float pickupRadius = 1.65f, dropLifetime = 300.0f, miningSpeed = 1.0f;
        float spawnInterval = 0.0f, spawnDistance = 18.0f;
        if (!readSpawn("spawn.sheep", sheep) || !readSpawn("spawn.cow", cows) ||
            !readSpawn("spawn.bird", birds) || !readSpawn("spawn.pig", pigs) ||
            !readSpawn("spawn.rabbit", rabbits) || !readSpawn("spawn.zombie", zombies) ||
            !readSpawn("spawn.skeleton", skeletons) ||
            !readRange("player.speed", 1.0f, 0.5f, 2.0f, playerSpeed) ||
            !readRange("player.jump", 1.0f, 0.75f, 1.5f, playerJump) ||
            !readRange("player.crouch_speed", 0.46f, 0.25f, 1.0f, crouchSpeed) ||
            !readRange("entity.speed", 1.0f, 0.5f, 2.0f, entitySpeed) ||
            !readRange("entity.passive_speed", 1.0f, 0.5f, 2.0f, passiveSpeed) ||
            !readRange("entity.hostile_speed", 1.0f, 0.5f, 2.0f, hostileSpeed) ||
            !readRange("entity.flee_radius", 6.0f, 2.0f, 16.0f, fleeRadius) ||
            !readRange("entity.alert_radius", 15.0f, 4.0f, 32.0f, alertRadius) ||
            !readRange("entity.spawn_radius", 1.8f, 1.0f, 8.0f, spawnRadius) ||
            !readRange("world.day_speed", 1.0f, 0.25f, 4.0f, daySpeed) ||
            !readIntRange("world.entity_cap", 128, 8, 128, entityCap) ||
            !readRange("world.spawn_interval", 0.0f, 30.0f, 900.0f, spawnInterval) ||
            !readRange("world.spawn_distance", 18.0f, 8.0f, 48.0f, spawnDistance) ||
            !readIntRange("world.passive_cap", 24, 1, 64, passiveEntityCap) ||
            !readRange("drops.pickup_radius", 1.65f, 0.8f, 4.0f, pickupRadius) ||
            !readRange("drops.lifetime", 300.0f, 30.0f, 900.0f, dropLifetime) ||
            !readIntRange("drops.quantity", 1, 1, 4, dropQuantity) ||
            !readRange("mining.speed", 1.0f, 0.5f, 3.0f, miningSpeed)) continue;

        modSheepSpawns_ = std::clamp(modSheepSpawns_ + sheep, 0, 24);
        modCowSpawns_ = std::clamp(modCowSpawns_ + cows, 0, 24);
        modBirdSpawns_ = std::clamp(modBirdSpawns_ + birds, 0, 24);
        modPigSpawns_ = std::clamp(modPigSpawns_ + pigs, 0, 24);
        modRabbitSpawns_ = std::clamp(modRabbitSpawns_ + rabbits, 0, 16);
        modZombieSpawns_ = std::clamp(modZombieSpawns_ + zombies, 0, 8);
        modSkeletonSpawns_ = std::clamp(modSkeletonSpawns_ + skeletons, 0, 8);
        modPlayerSpeed_ = std::clamp(modPlayerSpeed_ * playerSpeed, 0.5f, 2.0f);
        modPlayerJump_ = std::clamp(modPlayerJump_ * playerJump, 0.75f, 1.5f);
        modCrouchSpeed_ = std::clamp(modCrouchSpeed_ * (crouchSpeed / 0.46f), 0.25f, 1.0f);
        modEntitySpeed_ = std::clamp(modEntitySpeed_ * entitySpeed, 0.5f, 2.0f);
        modPassiveEntitySpeed_ = std::clamp(modPassiveEntitySpeed_ * passiveSpeed, 0.5f, 2.0f);
        modHostileEntitySpeed_ = std::clamp(modHostileEntitySpeed_ * hostileSpeed, 0.5f, 2.0f);
        modFleeRadius_ = std::clamp(modFleeRadius_ * (fleeRadius / 6.0f), 2.0f, 16.0f);
        modAlertRadius_ = std::clamp(modAlertRadius_ * (alertRadius / 15.0f), 4.0f, 32.0f);
        modSpawnRadius_ = std::clamp(modSpawnRadius_ * (spawnRadius / 1.8f), 1.0f, 8.0f);
        modDaySpeed_ = std::clamp(modDaySpeed_ * daySpeed, 0.25f, 4.0f);
        modPickupRadius_ = std::clamp(modPickupRadius_ * (pickupRadius / 1.65f), 0.8f, 4.0f);
        modDropLifetime_ = std::clamp(modDropLifetime_ * (dropLifetime / 300.0f), 30.0f, 900.0f);
        modDropQuantity_ = std::clamp(modDropQuantity_ * dropQuantity, 1, 4);
        modMiningSpeed_ = std::clamp(modMiningSpeed_ * miningSpeed, 0.5f, 3.0f);
        modEntityCap_ = std::min(modEntityCap_, entityCap);
        if (spawnInterval > 0.0f)
            modSpawnInterval_ = modSpawnInterval_ <= 0.0f ? spawnInterval
                : std::min(modSpawnInterval_, spawnInterval);
        if (values.find("world.spawn_distance") != values.end()) {
            modSpawnDistance_ = hasExplicitSpawnDistance
                ? std::min(modSpawnDistance_, spawnDistance) : spawnDistance;
            hasExplicitSpawnDistance = true;
        }
        if (values.find("world.passive_cap") != values.end()) {
            modPassiveEntityCap_ = hasExplicitPassiveCap
                ? std::min(modPassiveEntityCap_, passiveEntityCap) : passiveEntityCap;
            hasExplicitPassiveCap = true;
        }
    }
}

bool Engine::ConsumeExitRequest() {
    return exitRequested_.exchange(false, std::memory_order_acq_rel);
}

void Engine::SetPlayerIdentity(std::string identity) {
    if (!ProtectionManager::ValidIdentity(identity)) identity = "local-device";
    pendingPlayerIdentity_ = identity;
    if (!ready_) playerIdentity_ = identity;
}

bool Engine::CanRemoteModifyBlockForIdentity(const std::string& identity, int x, int z) const {
    return ProtectionManager::ValidIdentity(identity) && protection_.CanBuild(identity, x, z);
}

void Engine::SetMultiplayerMode(bool enabled, int playerSlot) {
    pendingMultiplayerMode_ = enabled;
    pendingMultiplayerSlot_ = std::clamp(playerSlot, 0, 31);
    if (!ready_) {
        multiplayerMode_ = pendingMultiplayerMode_;
        multiplayerSlot_ = pendingMultiplayerSlot_;
    }
    remotePlayers_.clear();
    pendingBlockEdits_.clear();
}

void Engine::SetGraphicsOptions(int quality,int renderDistanceChunks,bool waterWaves,
                                 float fieldOfView,bool contactShadows) {
    pendingGraphicsQuality_=std::clamp(quality,0,2);
    pendingRenderDistanceChunks_=std::clamp(renderDistanceChunks,1,3);
    pendingWaterWaves_=waterWaves;
    pendingFieldOfView_=std::clamp(fieldOfView,60.0f,100.0f);
    pendingContactShadows_=contactShadows;
    graphicsQuality_=pendingGraphicsQuality_;
    renderDistanceChunks_=pendingRenderDistanceChunks_;
    waterWaves_=pendingWaterWaves_;
    fieldOfView_=pendingFieldOfView_;
    contactShadows_=pendingContactShadows_;
    if(ready_) {
        player_.SetContactShadows(contactShadows_);
        entities_.SetContactShadows(contactShadows_);
        world_.RequestGenerateAround(static_cast<int>(std::floor(player_.position.x)),
                                     static_cast<int>(std::floor(player_.position.z)),
                                     renderDistanceChunks_);
        RebuildWorldMesh();
        UploadWorldMesh();
        UploadTransparentWorldMesh();
        RebuildEntityMesh();
        UploadEntityMesh();
    }
}

void Engine::SetShaderOptions(bool lavaAnimations, bool leafWind, float leafWindStrength) {
    lavaAnimations_ = lavaAnimations;
    leafWind_ = leafWind;
    leafWindStrength_ = std::clamp(leafWindStrength, 0.0f, 0.12f);
}

void Engine::SetInputOptions(float sensitivityX, float sensitivityY, float joystickDeadzone, bool invertLookY, bool leftHanded, int controlMode, bool autoJump) {
    pendingLookSensitivityX_ = std::clamp(sensitivityX, 0.35f, 1.50f);
    pendingLookSensitivityY_ = std::clamp(sensitivityY, 0.35f, 1.50f);
    pendingJoystickDeadzone_ = std::clamp(joystickDeadzone, 0.05f, 0.30f);
    pendingInvertLookY_ = invertLookY;
    pendingLeftHandedControls_ = leftHanded;
    lookSensitivityX_ = pendingLookSensitivityX_;
    lookSensitivityY_ = pendingLookSensitivityY_;
    invertLookY_ = pendingInvertLookY_;
    leftHandedControls_ = pendingLeftHandedControls_;
    // Keep the legacy argument for old preferences; the visible scheme is now one D-pad.
    controlMode_ = 0;
    pendingAutoJump_ = autoJump;
    autoJump_ = autoJump;
    if (!autoJump_) autoJumpCooldown_ = 0.0f;
    ui_.SetLeftHanded(leftHandedControls_);
    ui_.SetControlMode(controlMode_);
}

void Engine::SetHudOptions(bool showStats) {
    pendingShowHudStats_ = showStats;
    showHudStats_ = showStats;
    ui_.SetStatusVisible(showHudStats_);
}

void Engine::SetMinimapShape(int shape) {
    minimapShape_ = std::clamp(shape, 0, 1);
    ui_.SetMinimapShape(minimapShape_);
}

void Engine::SetAnimationOptions(int quality, bool enabled) {
    pendingAnimationQuality_ = std::clamp(quality, 0, 2);
    pendingAnimationsEnabled_ = enabled;
    animationQuality_ = pendingAnimationQuality_;
    animationsEnabled_ = pendingAnimationsEnabled_;
    player_.SetAnimationOptions(static_cast<std::uint8_t>(animationQuality_), animationsEnabled_);
    entities_.SetAnimationOptions(static_cast<std::uint8_t>(animationQuality_), animationsEnabled_);
    if (ready_) {
        RebuildEntityMesh();
        UploadEntityMesh();
    }
}

void Engine::SetAvatarAppearance(const int* values,std::size_t count) {
    if(values==nullptr||count<avatarAppearance_.size())return;
    for(std::size_t i=0;i<avatarAppearance_.size();++i)
        avatarAppearance_[i]=std::clamp(values[i],0,5);
    player_.SetAppearance(avatarAppearance_);
    if(ready_) {
        RebuildEntityMesh();
        UploadEntityMesh();
    }
}

void Engine::GetPlayerState(float output[6]) const {
    if (output == nullptr) return;
    output[0] = player_.position.x;
    output[1] = player_.position.y;
    output[2] = player_.position.z;
    output[3] = player_.yaw;
    output[4] = player_.movementX;
    output[5] = player_.movementY;
}

void Engine::SetRemotePlayers(const float* values, std::size_t floatCount) {
    if (values == nullptr || floatCount < 6u) {
        remotePlayers_.clear();
        return;
    }
    const std::size_t count = std::min<std::size_t>(floatCount - (floatCount % 6u), static_cast<std::size_t>(42u));
    remotePlayers_.assign(values, values + count);
}

void Engine::ApplyRemoteBlockEdits(const int* values, std::size_t valueCount) {
    if (values == nullptr || valueCount < 4u) return;
    bool changed = false;
    for (std::size_t i = 0; i + 3u < valueCount; i += 4u) {
        const int x = values[i], y = values[i + 1u], z = values[i + 2u], id = values[i + 3u];
        if (id < 0 || id >= static_cast<int>(GetBlockDefinitionCount())) continue;
        const BlockId previous = world_.GetBlock(x, y, z);
        if (world_.SetBlock(x, y, z, static_cast<BlockId>(id))) {
            changed = true;
            if (multiplayerMode_ && multiplayerSlot_ == 0 && id == static_cast<int>(BlockId::Air)
                    && previous != BlockId::Air) {
                const BlockDefinition* definition = GetBlockDefinition(previous);
                if (definition != nullptr && definition->dropId > 0u) {
                    droppedItems_.Spawn({static_cast<float>(x) + 0.5f,
                                         static_cast<float>(y) + 0.55f,
                                         static_cast<float>(z) + 0.5f},
                                        definition->dropId, 1u);
                }
            }
        }
    }
    if (changed && ready_) {
        RebuildWorldMesh();
        UploadWorldMesh();
        UploadTransparentWorldMesh();
    }
}

std::vector<int> Engine::ConsumeLocalBlockEdits() {
    std::vector<int> result;
    result.swap(pendingBlockEdits_);
    return result;
}

} // namespace minefade
