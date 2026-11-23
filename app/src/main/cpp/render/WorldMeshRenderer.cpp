#include "WorldMeshRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace minefade {

WorldMeshRenderer::~WorldMeshRenderer() {
    Shutdown();
}

void WorldMeshRenderer::ReleasePass(GLuint& vao, GLuint& vbo,
                                    std::size_t& capacityBytes, GLsizei& drawCount) {
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    vao = 0;
    vbo = 0;
    capacityBytes = 0u;
    drawCount = 0;
}

bool WorldMeshRenderer::UploadPass(GLuint& vao, GLuint& vbo,
                                   std::size_t& capacityBytes, GLsizei& drawCount,
                                   const std::vector<WorldVertex>& source,
                                   std::size_t first, std::size_t count) {
    if (count == 0u) {
        ReleasePass(vao, vbo, capacityBytes, drawCount);
        return true;
    }

    const std::size_t maxBufferBytes =
        static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max());
    if (first > source.size() || count > source.size() - first ||
        count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        count > maxBufferBytes / sizeof(WorldVertex)) {
        ReleasePass(vao, vbo, capacityBytes, drawCount);
        return false;
    }
    const std::size_t requiredBytes = count * sizeof(WorldVertex);
    if (requiredBytes == 0u || requiredBytes > maxBufferBytes) return false;

    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

    if (vao == 0) glGenVertexArrays(1, &vao);
    if (vbo == 0) glGenBuffers(1, &vbo);
    if (vao == 0 || vbo == 0) {
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        ReleasePass(vao, vbo, capacityBytes, drawCount);
        return false;
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (requiredBytes > capacityBytes) {
        // Grow geometrically so repeated edits do not reallocate the same VBO
        // every time its mesh changes size.
        std::size_t newCapacity = capacityBytes == 0u ? sizeof(WorldVertex) * 64u : capacityBytes;
        while (newCapacity < requiredBytes &&
               newCapacity <= maxBufferBytes / 2u) {
            newCapacity *= 2u;
        }
        if (newCapacity < requiredBytes || newCapacity > maxBufferBytes) {
            newCapacity = requiredBytes;
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(newCapacity),
                     nullptr, GL_DYNAMIC_DRAW);
        capacityBytes = newCapacity;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(requiredBytes),
                    source.data() + first);
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

    // RmlUi and the world renderer share one GLES context. Do not leak our VAO
    // or array-buffer binding into the next renderer pass.
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));

    if (glGetError() != GL_NO_ERROR) {
        ReleasePass(vao, vbo, capacityBytes, drawCount);
        return false;
    }
    drawCount = static_cast<GLsizei>(count);
    return true;
}

bool WorldMeshRenderer::Sync(const std::vector<ChunkMeshView>& chunkViews,
                             const Vec3& cameraPosition,
                             std::size_t maxChunkUploads,
                             std::size_t maxUploadBytes,
                             double maxUploadMilliseconds) {
    struct DirtyChunk {
        const ChunkMeshView* view{nullptr};
        float distanceSquared{0.0f};
        std::size_t uploadBytes{0u};
    };

    std::set<std::pair<int, int>> active;
    std::vector<DirtyChunk> dirty;
    dirty.reserve(chunkViews.size());
    lastUploadCount_ = 0;
    pendingUploadCount_ = 0;
    lastUploadBytes_ = 0u;
    pendingUploadBytes_ = 0u;
    bool success = true;
    std::size_t totalDirtyBytes = 0u;

    const auto addSaturated = [](std::size_t a, std::size_t b) {
        return b > std::numeric_limits<std::size_t>::max() - a
            ? std::numeric_limits<std::size_t>::max() : a + b;
    };

    for (const ChunkMeshView& view : chunkViews) {
        if (view.opaque == nullptr || view.transparent == nullptr) continue;
        const auto key = std::make_pair(view.chunkX, view.chunkZ);
        active.insert(key);
        const auto existing = chunks_.find(key);
        if (existing != chunks_.end() && existing->second.revision == view.revision) continue;

        const std::size_t opaqueBytes = view.opaque->size() >
                std::numeric_limits<std::size_t>::max() / sizeof(WorldVertex)
            ? std::numeric_limits<std::size_t>::max()
            : view.opaque->size() * sizeof(WorldVertex);
        const std::size_t transparentBytes = view.transparent->size() >
                std::numeric_limits<std::size_t>::max() / sizeof(WorldVertex)
            ? std::numeric_limits<std::size_t>::max()
            : view.transparent->size() * sizeof(WorldVertex);
        const std::size_t requiredBytes = addSaturated(opaqueBytes, transparentBytes);
        totalDirtyBytes = addSaturated(totalDirtyBytes, requiredBytes);

        const float centerX = static_cast<float>(view.chunkX * VoxelWorld::kChunkSize)
            + static_cast<float>(VoxelWorld::kChunkSize) * 0.5f;
        const float centerZ = static_cast<float>(view.chunkZ * VoxelWorld::kChunkSize)
            + static_cast<float>(VoxelWorld::kChunkSize) * 0.5f;
        const float dx = centerX - cameraPosition.x;
        const float dy = (view.minY + view.maxY) * 0.5f - cameraPosition.y;
        const float dz = centerZ - cameraPosition.z;
        dirty.push_back({&view, dx * dx + dy * dy + dz * dz, requiredBytes});
    }

    std::sort(dirty.begin(), dirty.end(), [](const DirtyChunk& a, const DirtyChunk& b) {
        if (a.distanceSquared != b.distanceSquared) return a.distanceSquared < b.distanceSquared;
        if (a.view->chunkX != b.view->chunkX) return a.view->chunkX < b.view->chunkX;
        return a.view->chunkZ < b.view->chunkZ;
    });

    const auto uploadStart = std::chrono::steady_clock::now();
    std::size_t attemptedUploads = 0u;
    for (const DirtyChunk& candidate : dirty) {
        if (attemptedUploads >= maxChunkUploads) break;
        if (attemptedUploads > 0u) {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - uploadStart).count();
            if (elapsedMs >= std::max(0.0, maxUploadMilliseconds)) break;
            const std::size_t remainingBytes = lastUploadBytes_ >= maxUploadBytes
                ? 0u : maxUploadBytes - lastUploadBytes_;
            // Skip a too-large candidate but keep looking for smaller nearby
            // chunks that still fit. The first chunk is always admitted so a
            // large chunk cannot remain permanently stuck in the queue.
            if (candidate.uploadBytes > remainingBytes) continue;
        }

        ++attemptedUploads;
        const ChunkMeshView& view = *candidate.view;
        const auto key = std::make_pair(view.chunkX, view.chunkZ);

        // Build both render passes into fresh GPU buffers. Never invalidate the
        // currently visible chunk if either new pass fails: the previous opaque
        // and transparent meshes remain a coherent pair until both uploads pass.
        GpuChunk staged{};
        const bool opaqueOk = UploadPass(staged.opaqueVao, staged.opaqueVbo,
            staged.opaqueCapacityBytes, staged.opaqueCount, *view.opaque, 0u, view.opaque->size());
        const bool transparentOk = UploadPass(staged.transparentVao, staged.transparentVbo,
            staged.transparentCapacityBytes, staged.transparentCount,
            *view.transparent, 0u, view.transparent->size());
        if (!opaqueOk || !transparentOk) {
            ReleasePass(staged.opaqueVao, staged.opaqueVbo,
                        staged.opaqueCapacityBytes, staged.opaqueCount);
            ReleasePass(staged.transparentVao, staged.transparentVbo,
                        staged.transparentCapacityBytes, staged.transparentCount);
            success = false;
            continue;
        }

        staged.revision = view.revision;
        staged.minY = view.minY;
        staged.maxY = view.maxY;
        const auto existing = chunks_.find(key);
        if (existing == chunks_.end()) {
            chunks_.emplace(key, staged);
        } else {
            GpuChunk previous = existing->second;
            existing->second = staged;
            ReleasePass(previous.opaqueVao, previous.opaqueVbo,
                        previous.opaqueCapacityBytes, previous.opaqueCount);
            ReleasePass(previous.transparentVao, previous.transparentVbo,
                        previous.transparentCapacityBytes, previous.transparentCount);
        }

        lastUploadBytes_ = addSaturated(lastUploadBytes_, candidate.uploadBytes);
        ++lastUploadCount_;
    }

    for (auto it = chunks_.begin(); it != chunks_.end();) {
        if (active.find(it->first) == active.end()) {
            GpuChunk& gpu = it->second;
            ReleasePass(gpu.opaqueVao, gpu.opaqueVbo,
                        gpu.opaqueCapacityBytes, gpu.opaqueCount);
            ReleasePass(gpu.transparentVao, gpu.transparentVbo,
                        gpu.transparentCapacityBytes, gpu.transparentCount);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }

    std::size_t opaqueTotal = 0u;
    std::size_t transparentTotal = 0u;
    residentGpuBytes_ = 0u;
    for (const auto& entry : chunks_) {
        opaqueTotal += static_cast<std::size_t>(entry.second.opaqueCount);
        transparentTotal += static_cast<std::size_t>(entry.second.transparentCount);
        residentGpuBytes_ = addSaturated(residentGpuBytes_, entry.second.opaqueCapacityBytes);
        residentGpuBytes_ = addSaturated(residentGpuBytes_, entry.second.transparentCapacityBytes);
    }
    opaqueVertexCount_ = static_cast<GLsizei>(std::min(
        opaqueTotal, static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())));
    transparentVertexCount_ = static_cast<GLsizei>(std::min(
        transparentTotal, static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())));
    pendingUploadCount_ = dirty.size() - std::min(dirty.size(), lastUploadCount_);
    pendingUploadBytes_ = totalDirtyBytes > lastUploadBytes_
        ? totalDirtyBytes - lastUploadBytes_ : 0u;
    return success;
}
bool WorldMeshRenderer::IsVisible(const ChunkMeshRange& range,
                                  const CameraFrustum& frustum) noexcept {
    const float minX = static_cast<float>(range.chunkX * VoxelWorld::kChunkSize);
    const float minZ = static_cast<float>(range.chunkZ * VoxelWorld::kChunkSize);
    const float minY = std::max(0.0f, range.minY - 1.0f);
    const float maxY = std::min(static_cast<float>(VoxelWorld::kWorldHeight),
                                range.maxY + 1.0f);
    return frustum.IntersectsAabb(minX, minY, minZ,
        minX + static_cast<float>(VoxelWorld::kChunkSize), maxY,
        minZ + static_cast<float>(VoxelWorld::kChunkSize));
}

void WorldMeshRenderer::DrawOpaque(const CameraFrustum& frustum) const {
    for (const auto& entry : chunks_) {
        const auto& key = entry.first;
        const GpuChunk& gpu = entry.second;
        if (gpu.opaqueVao == 0 || gpu.opaqueCount <= 0) continue;
        const float minX = static_cast<float>(key.first * VoxelWorld::kChunkSize);
        const float minZ = static_cast<float>(key.second * VoxelWorld::kChunkSize);
        const float minY = std::max(0.0f, gpu.minY - 1.0f);
        const float maxY = std::min(static_cast<float>(VoxelWorld::kWorldHeight),
                                    gpu.maxY + 1.0f);
        if (!frustum.IntersectsAabb(minX, minY, minZ,
                minX + static_cast<float>(VoxelWorld::kChunkSize), maxY,
                minZ + static_cast<float>(VoxelWorld::kChunkSize))) continue;
        glBindVertexArray(gpu.opaqueVao);
        glDrawArrays(GL_TRIANGLES, 0, gpu.opaqueCount);
    }
    glBindVertexArray(0);
}

void WorldMeshRenderer::DrawTransparent(const CameraFrustum& frustum,
                                        const Vec3& cameraPosition) const {
    struct VisibleChunk {
        float distanceSquared{0.0f};
        const GpuChunk* gpu{nullptr};
    };
    std::vector<VisibleChunk> visible;
    visible.reserve(chunks_.size());
    for (const auto& entry : chunks_) {
        const auto& key = entry.first;
        const GpuChunk& gpu = entry.second;
        if (gpu.transparentVao == 0 || gpu.transparentCount <= 0) continue;
        const float minX = static_cast<float>(key.first * VoxelWorld::kChunkSize);
        const float minZ = static_cast<float>(key.second * VoxelWorld::kChunkSize);
        const float minY = std::max(0.0f, gpu.minY - 1.0f);
        const float maxY = std::min(static_cast<float>(VoxelWorld::kWorldHeight),
                                    gpu.maxY + 1.0f);
        if (!frustum.IntersectsAabb(minX, minY, minZ,
                minX + static_cast<float>(VoxelWorld::kChunkSize), maxY,
                minZ + static_cast<float>(VoxelWorld::kChunkSize))) continue;
        const float dx = minX + VoxelWorld::kChunkSize * 0.5f - cameraPosition.x;
        const float dy = (minY + maxY) * 0.5f - cameraPosition.y;
        const float dz = minZ + VoxelWorld::kChunkSize * 0.5f - cameraPosition.z;
        visible.push_back({dx * dx + dy * dy + dz * dz, &gpu});
    }
    std::sort(visible.begin(), visible.end(),
        [](const VisibleChunk& a, const VisibleChunk& b) {
            return a.distanceSquared > b.distanceSquared;
        });
    for (const VisibleChunk& chunk : visible) {
        glBindVertexArray(chunk.gpu->transparentVao);
        glDrawArrays(GL_TRIANGLES, 0, chunk.gpu->transparentCount);
    }
    glBindVertexArray(0);
}

void WorldMeshRenderer::Shutdown() {
    for (auto& entry : chunks_) {
        GpuChunk& gpu = entry.second;
        ReleasePass(gpu.opaqueVao, gpu.opaqueVbo, gpu.opaqueCapacityBytes, gpu.opaqueCount);
        ReleasePass(gpu.transparentVao, gpu.transparentVbo,
                    gpu.transparentCapacityBytes, gpu.transparentCount);
    }
    chunks_.clear();
    opaqueVertexCount_ = 0;
    transparentVertexCount_ = 0;
    lastUploadCount_ = 0;
    pendingUploadCount_ = 0;
    lastUploadBytes_ = 0u;
    pendingUploadBytes_ = 0u;
    residentGpuBytes_ = 0u;
}

} // namespace minefade
