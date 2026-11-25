#include "FirstPersonRenderer.h"

#include "../world/VoxelWorld.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>

namespace minefade {

void FirstPersonRenderer::AppendSolidBox(
        std::vector<WorldVertex>& output, const Vec3& center,
        const Vec3& half, const std::array<float, 3>& color) {
    AppendCube(output, center, half, color);
}

void FirstPersonRenderer::ReleasePass(GpuPass& pass) {
    if (pass.vbo != 0) glDeleteBuffers(1, &pass.vbo);
    if (pass.vao != 0) glDeleteVertexArrays(1, &pass.vao);
    pass = {};
}

bool FirstPersonRenderer::StagePass(
        const std::vector<WorldVertex>& vertices, GpuPass& staged) {
    staged = {};
    if (vertices.empty()) return true;
    if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        vertices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) /
                              sizeof(WorldVertex)) return false;

    // Do not let an earlier renderer error get attributed to this upload.
    for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) { }

    GLint oldVao = 0;
    GLint oldArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);

    glGenVertexArrays(1, &staged.vao);
    glGenBuffers(1, &staged.vbo);
    bool valid = staged.vao != 0 && staged.vbo != 0;
    if (valid) {
        glBindVertexArray(staged.vao);
        glBindBuffer(GL_ARRAY_BUFFER, staged.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(WorldVertex)),
                     vertices.data(), GL_STATIC_DRAW);
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
        valid = glGetError() == GL_NO_ERROR;
    }

    glBindVertexArray(static_cast<GLuint>(oldVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(oldArrayBuffer));
    if (!valid) {
        ReleasePass(staged);
        return false;
    }
    staged.count = static_cast<GLsizei>(vertices.size());
    return true;
}

void FirstPersonRenderer::DrawPass(const GpuPass& pass) {
    if (pass.vao == 0 || pass.count <= 0) return;
    glBindVertexArray(pass.vao);
    glDrawArrays(GL_TRIANGLES, 0, pass.count);
    glBindVertexArray(0);
}

bool FirstPersonRenderer::Build(
        const Player& player, std::uint16_t itemId,
        const BlockDefinition* definition, bool leftHanded) {
    if (hasCache_ && cachedItemId_ == itemId &&
        cachedAppearance_ == player.appearance &&
        cachedLeftHanded_ == leftHanded) return false;

    armVertices_.clear();
    heldColorVertices_.clear();
    heldAtlasVertices_.clear();

    const float side = leftHanded ? -1.0f : 1.0f;
    const auto sleeve = player.FirstPersonSleeveColor();
    const auto skin = player.FirstPersonSkinColor();

    // The arm is camera-space geometry. It is never inserted into entityMesh_,
    // entity bounds, chunk culling or the player simulation.
    AppendSolidBox(armVertices_, {side * 0.47f, -0.47f, -0.72f},
                   {0.135f, 0.245f, 0.125f}, sleeve);
    AppendSolidBox(armVertices_, {side * 0.395f, -0.275f, -0.845f},
                   {0.115f, 0.065f, 0.105f}, sleeve);
    AppendSolidBox(armVertices_, {side * 0.30f, -0.195f, -0.91f},
                   {0.095f, 0.088f, 0.075f}, skin);

    // Low-poly palm, thumb and four short finger volumes make a recognizable
    // grip silhouette without adding a costly skinned mesh or Rust ABI fields.
    AppendSolidBox(armVertices_, {side * 0.275f, -0.145f, -0.955f},
                   {0.082f, 0.068f, 0.045f}, skin);
    AppendSolidBox(armVertices_, {side * 0.365f, -0.175f, -0.925f},
                   {0.032f, 0.060f, 0.040f}, skin);
    for (int finger = 0; finger < 4; ++finger) {
        const float fingerX = side * (0.215f + static_cast<float>(finger) * 0.038f);
        AppendSolidBox(armVertices_, {fingerX, -0.105f, -0.985f},
                       {0.016f, 0.034f, 0.026f}, skin);
    }

    if (definition != nullptr && itemId != static_cast<std::uint16_t>(BlockId::Air)) {
        const std::string_view key(definition->key);
        const Vec3 held{side * 0.235f, -0.12f, -0.965f};

        if (definition->shape != BlockShape::None) {
            // Placeable blocks use their real top/side/bottom atlas tiles.
            AppendTexturedBlockCube(heldAtlasVertices_, static_cast<BlockId>(itemId),
                                    held, {0.125f, 0.125f, 0.125f});
        } else {
            std::array<float, 3> material{0.48f, 0.30f, 0.16f};
            if (key.find("_stone_") != std::string_view::npos) material = {0.56f,0.60f,0.64f};
            else if (key.find("_copper_") != std::string_view::npos) material = {0.76f,0.39f,0.22f};
            else if (key.find("_iron_") != std::string_view::npos) material = {0.78f,0.82f,0.86f};
            else if (key.find("_gold_") != std::string_view::npos) material = {0.95f,0.73f,0.22f};
            else if (key.find("_diamond_") != std::string_view::npos) material = {0.18f,0.76f,0.85f};
            else if (key.find("_emerald_") != std::string_view::npos) material = {0.13f,0.72f,0.38f};

            const auto handle = [&]() {
                AppendSolidBox(heldColorVertices_,
                    {held.x, held.y - 0.015f, held.z},
                    {0.022f, 0.205f, 0.026f}, {0.47f,0.29f,0.14f});
            };

            if (key.find("_pickaxe") != std::string_view::npos) {
                handle();
                AppendSolidBox(heldColorVertices_, {held.x, held.y + 0.205f, held.z},
                               {0.155f,0.030f,0.035f}, material);
                AppendSolidBox(heldColorVertices_, {held.x - side * 0.135f, held.y + 0.17f, held.z},
                               {0.028f,0.075f,0.035f}, material);
                AppendSolidBox(heldColorVertices_, {held.x + side * 0.135f, held.y + 0.17f, held.z},
                               {0.028f,0.075f,0.035f}, material);
            } else if (key.find("_axe") != std::string_view::npos) {
                handle();
                AppendSolidBox(heldColorVertices_, {held.x + side * 0.11f, held.y + 0.16f, held.z},
                               {0.095f,0.105f,0.050f}, material);
                AppendSolidBox(heldColorVertices_, {held.x + side * 0.16f, held.y + 0.19f, held.z},
                               {0.045f,0.075f,0.052f}, material);
            } else if (key.find("_shovel") != std::string_view::npos) {
                handle();
                AppendSolidBox(heldColorVertices_, {held.x, held.y + 0.225f, held.z},
                               {0.058f,0.092f,0.042f}, material);
                AppendSolidBox(heldColorVertices_, {held.x, held.y + 0.29f, held.z},
                               {0.038f,0.025f,0.038f}, material);
            } else if (key.find("_sword") != std::string_view::npos) {
                handle();
                AppendSolidBox(heldColorVertices_, {held.x, held.y + 0.205f, held.z},
                               {0.090f,0.025f,0.040f}, material);
                AppendSolidBox(heldColorVertices_, {held.x, held.y + 0.325f, held.z},
                               {0.043f,0.145f,0.027f}, material);
                AppendSolidBox(heldColorVertices_, {held.x, held.y + 0.48f, held.z},
                               {0.024f,0.035f,0.020f}, material);
            } else if (key.find("_hoe") != std::string_view::npos) {
                handle();
                AppendSolidBox(heldColorVertices_, {held.x + side * 0.11f, held.y + 0.205f, held.z},
                               {0.125f,0.026f,0.032f}, material);
                AppendSolidBox(heldColorVertices_, {held.x + side * 0.205f, held.y + 0.17f, held.z},
                               {0.027f,0.055f,0.032f}, material);
            } else if (key == "stick" || key.find("stick") != std::string_view::npos) {
                AppendSolidBox(heldColorVertices_, held, {0.014f,0.245f,0.016f},
                               {0.55f,0.36f,0.20f});
                AppendSolidBox(heldColorVertices_, {held.x,held.y + 0.035f,held.z},
                               {0.012f,0.18f,0.014f}, {0.68f,0.49f,0.29f});
            } else if (key.find("helmet") != std::string_view::npos ||
                       key.find("chestplate") != std::string_view::npos ||
                       key.find("leggings") != std::string_view::npos ||
                       key.find("boots") != std::string_view::npos) {
                AppendSolidBox(heldColorVertices_, held, {0.105f,0.12f,0.052f}, material);
                AppendSolidBox(heldColorVertices_, {held.x,held.y + 0.115f,held.z},
                               {0.065f,0.025f,0.055f}, material);
            } else {
                // Other non-placeable items retain a clear, compact silhouette.
                AppendSolidBox(heldColorVertices_, held, {0.070f,0.075f,0.045f},
                               {0.69f,0.48f,0.24f});
            }
        }
    }

    cachedAppearance_ = player.appearance;
    cachedItemId_ = itemId;
    cachedLeftHanded_ = leftHanded;
    hasCache_ = true;
    dirty_ = true;
    return true;
}

void FirstPersonRenderer::Clear() {
    armVertices_.clear();
    heldColorVertices_.clear();
    heldAtlasVertices_.clear();
    cachedItemId_ = 0xffffu;
    hasCache_ = false;
    dirty_ = true;
}

bool FirstPersonRenderer::Upload() {
    if (!dirty_) return true;

    GpuPass stagedArm{};
    GpuPass stagedColor{};
    GpuPass stagedAtlas{};
    const bool valid = StagePass(armVertices_, stagedArm) &&
                       StagePass(heldColorVertices_, stagedColor) &&
                       StagePass(heldAtlasVertices_, stagedAtlas);
    if (!valid) {
        ReleasePass(stagedArm);
        ReleasePass(stagedColor);
        ReleasePass(stagedAtlas);
        return false;
    }

    // All three CPU layers become active together; a failed layer keeps the
    // previous complete viewmodel visible until a retry succeeds.
    ReleasePass(armGpu_);
    ReleasePass(heldColorGpu_);
    ReleasePass(heldAtlasGpu_);
    armGpu_ = stagedArm;
    heldColorGpu_ = stagedColor;
    heldAtlasGpu_ = stagedAtlas;
    dirty_ = false;
    return true;
}

void FirstPersonRenderer::DrawHeldAtlas() const { DrawPass(heldAtlasGpu_); }
void FirstPersonRenderer::DrawHeldColor() const { DrawPass(heldColorGpu_); }
void FirstPersonRenderer::DrawArm() const { DrawPass(armGpu_); }

void FirstPersonRenderer::Shutdown() {
    ReleasePass(armGpu_);
    ReleasePass(heldColorGpu_);
    ReleasePass(heldAtlasGpu_);
    armVertices_.clear();
    heldColorVertices_.clear();
    heldAtlasVertices_.clear();
    cachedItemId_ = 0xffffu;
    hasCache_ = false;
    dirty_ = true;
}

} // namespace minefade
