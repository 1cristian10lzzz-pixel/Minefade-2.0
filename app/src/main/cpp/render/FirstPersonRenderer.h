#pragma once

#include "../game/Player.h"

#include <GLES3/gl3.h>

#include <array>
#include <cstdint>
#include <vector>

namespace minefade {

// First-person viewmodel is deliberately independent from entity/chunk meshes.
// CPU geometry changes only when item, avatar appearance or handedness changes;
// walking/switch animation is applied with a small view-space matrix each frame.
class FirstPersonRenderer {
public:
    bool Build(const Player& player, std::uint16_t itemId,
               const BlockDefinition* definition, bool leftHanded);
    void Clear();
    bool Upload();
    void DrawHeldAtlas() const;
    void DrawHeldColor() const;
    void DrawArm() const;
    void Shutdown();

    bool HasContent() const noexcept {
        return !armVertices_.empty() || !heldColorVertices_.empty() ||
               !heldAtlasVertices_.empty();
    }
    bool IsDirty() const noexcept { return dirty_; }

private:
    struct GpuPass {
        GLuint vao{0};
        GLuint vbo{0};
        GLsizei count{0};
    };

    static void AppendSolidBox(std::vector<WorldVertex>& output,
                               const Vec3& center, const Vec3& half,
                               const std::array<float, 3>& color);
    static bool StagePass(const std::vector<WorldVertex>& vertices, GpuPass& staged);
    static void ReleasePass(GpuPass& pass);
    static void DrawPass(const GpuPass& pass);

    std::vector<WorldVertex> armVertices_;
    std::vector<WorldVertex> heldColorVertices_;
    std::vector<WorldVertex> heldAtlasVertices_;
    GpuPass armGpu_;
    GpuPass heldColorGpu_;
    GpuPass heldAtlasGpu_;
    std::array<int, 6> cachedAppearance_{{-1,-1,-1,-1,-1,-1}};
    std::uint16_t cachedItemId_{0xffffu};
    bool cachedLeftHanded_{false};
    bool hasCache_{false};
    bool dirty_{true};
};

} // namespace minefade
