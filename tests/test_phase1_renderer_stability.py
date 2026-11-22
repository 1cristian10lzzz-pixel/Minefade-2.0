#!/usr/bin/env python3
"""Phase 1 regression checks for immutable chunk snapshots, GPU budgets and EGL safety."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "app/src/main/cpp"
WORLD = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
WORLD_H = (CPP / "world/VoxelWorld.h").read_text(encoding="utf-8")
RENDERER = (CPP / "render/WorldMeshRenderer.cpp").read_text(encoding="utf-8")
RENDERER_H = (CPP / "render/WorldMeshRenderer.h").read_text(encoding="utf-8")
ENGINE = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
ENGINE_H = (CPP / "engine/Engine.h").read_text(encoding="utf-8")


def test_meshing_workers_only_read_frozen_chunk_snapshots():
    snapshot = WORLD.index("std::vector<Chunk> snapshots;")
    build = WORLD.index("const auto buildOneChunk", snapshot)
    worker_source = WORLD.index("const Chunk& chunk=snapshots[selectedSnapshotIndices[index]];", build)
    commit = WORLD.index("A future persistent scheduler may finish", worker_source)
    assert snapshot < build < worker_source < commit
    assert "requiredSnapshotKeys.insert({cx, cz});" in WORLD
    assert "requiredSnapshotKeys.insert({cx - 1, cz});" in WORLD
    assert "requiredSnapshotKeys.insert({cx + 1, cz});" in WORLD
    assert "requiredSnapshotKeys.insert({cx, cz - 1});" in WORLD
    assert "requiredSnapshotKeys.insert({cx, cz + 1});" in WORLD
    assert "snapshots.reserve(requiredSnapshotKeys.size());" in WORLD
    assert "source.blocks[Index(PositiveMod(x,kChunkSize), y, PositiveMod(z,kChunkSize))]" in WORLD[snapshot:build]
    assert "const Chunk& chunk=*selected[index]" not in WORLD
    assert "selected[i]->revision != snapshots[selectedSnapshotIndices[i]].revision" in WORLD
    assert "dirtyChunkMeshes_.insert(key);" in WORLD[commit:commit + 500]


def test_block_edits_advance_revision_only_when_the_value_changes():
    start = WORLD.index("bool VoxelWorld::SetBlock(")
    end = WORLD.index("bool VoxelWorld::IsSolid(", start)
    method = WORLD[start:end]
    assert "if (stored == block) return true;" in method
    assert "InvalidateChunkMesh(chunk->x, chunk->z);" in method
    invalidation_start = WORLD.index("void VoxelWorld::InvalidateChunkMesh(")
    invalidation_end = WORLD.index("void VoxelWorld::InvalidateChunkAndNeighbors(", invalidation_start)
    invalidation = WORLD[invalidation_start:invalidation_end]
    assert "chunk->revision = chunk->revision ==" in invalidation
    assert "FindChunk(chunkX, chunkZ)" in invalidation
    assert "std::uint64_t revision{1};" in WORLD_H


def test_world_and_mesh_memory_metrics_are_saturating_and_runtime_only():
    assert "ChunkStorageBytes() const noexcept" in WORLD_H
    assert "MeshCacheMemoryBytes() const noexcept" in WORLD_H
    assert "entry.second.opaque.capacity()" in WORLD
    assert "entry.second.transparent.capacity()" in WORLD
    assert "return chunks_.size() * kChunkVolume * sizeof(BlockId);" in WORLD
    # Runtime chunk revisions must not leak into the persisted MFVOX layout.
    save_start = WORLD.index("bool VoxelWorld::Save(")
    save_end = WORLD.index("bool VoxelWorld::Load(", save_start)
    save = WORLD[save_start:save_end]
    assert "'3'}" in save
    assert "chunk.revision" not in save
    assert "chunk.blocks.data()" in save


def test_gpu_upload_budget_and_dynamic_vbo_reuse_are_wired():
    assert "maxUploadBytes" in RENDERER_H
    assert "maxUploadMilliseconds" in RENDERER_H
    assert "candidate.uploadBytes > remainingBytes" in RENDERER
    assert "elapsedMs >= std::max(0.0, maxUploadMilliseconds)" in RENDERER
    assert "first chunk is always admitted" in RENDERER
    assert "capacityBytes" in RENDERER
    assert "glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(newCapacity)" in RENDERER
    assert "glBufferSubData" in RENDERER
    assert "GL_DYNAMIC_DRAW" in RENDERER
    assert "GL_VERTEX_ARRAY_BINDING" in RENDERER
    assert "GL_ARRAY_BUFFER_BINDING" in RENDERER
    assert "glBindVertexArray(static_cast<GLuint>(previousVertexArray))" in RENDERER


def test_gpu_byte_limits_and_timing_are_passed_from_android_engine():
    assert "uploadBudget, byteBudget, timeBudgetMs" in ENGINE
    assert "graphicsQuality_ <= 0 ? 256u * 1024u" in ENGINE
    assert "graphicsQuality_ >= 2 ? 2u * 1024u * 1024u" in ENGINE
    assert "graphicsQuality_ <= 0 ? 1.5" in ENGINE
    assert "graphicsQuality_ >= 2 ? 4.0" in ENGINE
    assert "lastGpuUploadMs_" in ENGINE_H
    assert "lastMeshBuildMs_" in ENGINE_H


def test_framebuffer_is_rebound_and_bad_surface_sizes_are_ignored():
    resize_start = ENGINE.index("void Engine::Resize(")
    mesh_start = ENGINE.index("void Engine::RebuildWorldMesh(", resize_start)
    resize = ENGINE[resize_start:mesh_start]
    assert "if (width <= 0 || height <= 0)" in resize
    assert "glBindFramebuffer(GL_FRAMEBUFFER, 0);" in resize
    assert "glCheckFramebufferStatus(GL_FRAMEBUFFER)" in resize
    render_start = ENGINE.index("void Engine::Render(")
    touch_start = ENGINE.index("void Engine::Touch(", render_start)
    render = ENGINE[render_start:touch_start]
    assert "glBindFramebuffer(GL_FRAMEBUFFER, 0);" in render
    assert "glDisable(GL_SCISSOR_TEST);" in render
    assert "glViewport(0, 0, width_, height_);" in render
    assert "glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);" in render
    assert "glDepthMask(GL_TRUE);" in render


def test_render_telemetry_records_frame_percentiles_and_cpu_gpu_memory():
    assert "std::array<float, 120> frameTimeSamplesMs_" in ENGINE_H
    assert "frameTimeSampleCount_" in ENGINE
    assert "percentile(0.50)" in ENGINE
    assert "percentile(0.95)" in ENGINE
    assert "percentile(0.99)" in ENGINE
    assert "world_.ChunkStorageBytes()" in ENGINE
    assert "world_.MeshCacheMemoryBytes()" in ENGINE
    assert "worldRenderer_.ResidentGpuBytes()" in ENGINE
    assert "worldRenderer_.PendingUploadBytes()" in ENGINE
    assert "perf frame_ms p50=" in ENGINE


if __name__ == "__main__":
    for check in (
        test_meshing_workers_only_read_frozen_chunk_snapshots,
        test_block_edits_advance_revision_only_when_the_value_changes,
        test_world_and_mesh_memory_metrics_are_saturating_and_runtime_only,
        test_gpu_upload_budget_and_dynamic_vbo_reuse_are_wired,
        test_gpu_byte_limits_and_timing_are_passed_from_android_engine,
        test_framebuffer_is_rebound_and_bad_surface_sizes_are_ignored,
        test_render_telemetry_records_frame_percentiles_and_cpu_gpu_memory,
    ):
        check()
    print("Minefade phase 1 snapshot, budget, framebuffer and telemetry checks passed.")
