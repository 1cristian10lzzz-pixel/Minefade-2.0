#pragma once

#include "../world/VoxelWorld.h"

#include <GLES3/gl3.h>

#include <cstdint>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace minefade {

// Owns only terrain GPU resources. VoxelWorld remains the CPU authority for
// chunks/meshes; this class updates a GPU buffer only when that chunk's mesh
// revision changes.
class WorldMeshRenderer {
public:
    WorldMeshRenderer() = default;
    ~WorldMeshRenderer();

    WorldMeshRenderer(const WorldMeshRenderer&) = delete;
    WorldMeshRenderer& operator=(const WorldMeshRenderer&) = delete;

    // Uploads are bounded by chunk count, total bytes and elapsed CPU time.
    // The first dirty chunk is allowed through even when oversized to avoid starvation.
    bool Sync(const std::vector<ChunkMeshView>& chunkViews,
              const Vec3& cameraPosition, std::size_t maxChunkUploads,
              std::size_t maxUploadBytes, double maxUploadMilliseconds);
    void Shutdown();

    GLsizei OpaqueVertexCount() const noexcept { return opaqueVertexCount_; }
    GLsizei TransparentVertexCount() const noexcept { return transparentVertexCount_; }
    std::size_t ResidentChunkCount() const noexcept { return chunks_.size(); }
    std::size_t LastUploadCount() const noexcept { return lastUploadCount_; }
    std::size_t PendingUploadCount() const noexcept { return pendingUploadCount_; }
    std::size_t LastUploadBytes() const noexcept { return lastUploadBytes_; }
    std::size_t PendingUploadBytes() const noexcept { return pendingUploadBytes_; }
    std::size_t ResidentGpuBytes() const noexcept { return residentGpuBytes_; }

    void DrawOpaque(const CameraFrustum& frustum) const;
    void DrawTransparent(const CameraFrustum& frustum, const Vec3& cameraPosition,
                         const std::vector<ChunkMeshView>& chunkViews);

private:
    struct GpuChunk {
        GLuint opaqueVao{0};
        GLuint opaqueVbo{0};
        GLuint transparentVao{0};
        GLuint transparentVbo{0};
        std::size_t opaqueCapacityBytes{0};
        std::size_t transparentCapacityBytes{0};
        GLsizei opaqueCount{0};
        GLsizei transparentCount{0};
        std::uint64_t revision{0};
        float minY{0.0f};
        float maxY{0.0f};
    };

    static bool UploadPass(GLuint& vao, GLuint& vbo, std::size_t& capacityBytes,
                           GLsizei& drawCount, const std::vector<WorldVertex>& source,
                           std::size_t first, std::size_t count);
    static void ReleasePass(GLuint& vao, GLuint& vbo, std::size_t& capacityBytes,
                            GLsizei& drawCount);
    static bool IsVisible(const ChunkMeshRange& range, const CameraFrustum& frustum) noexcept;

    std::map<std::pair<int, int>, GpuChunk> chunks_;
    GLuint sortedTransparentVao_{0};
    GLuint sortedTransparentVbo_{0};
    std::size_t sortedTransparentCapacityBytes_{0u};
    std::vector<WorldVertex> sortedTransparentVertices_;
    GLsizei opaqueVertexCount_{0};
    GLsizei transparentVertexCount_{0};
    std::size_t lastUploadCount_{0};
    std::size_t pendingUploadCount_{0};
    std::size_t lastUploadBytes_{0};
    std::size_t pendingUploadBytes_{0};
    std::size_t residentGpuBytes_{0};
};

} // namespace minefade
