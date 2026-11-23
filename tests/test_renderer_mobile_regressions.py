#!/usr/bin/env python3
"""Regression checks for voxel face visibility and mobile chunk rendering."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "app/src/main/cpp"
WORLD = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
HEADER = (CPP / "world/VoxelWorld.h").read_text(encoding="utf-8")
RENDERER = (CPP / "render/WorldMeshRenderer.cpp").read_text(encoding="utf-8")
ENGINE = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
NATIVE_TESTS = (ROOT / "tests/WorldSmokeTests.cpp").read_text(encoding="utf-8")


def test_partial_opaque_voxels_do_not_cull_whole_cube_faces():
    assert "neighborDef->shape == BlockShape::Cube" in WORLD
    assert "(BlockTransparent | BlockFluid)" in WORLD
    assert "BlockId::StoneSlab" in NATIVE_TESTS
    assert "hasUpperEastFace()" in NATIVE_TESTS


def test_mobile_single_chunk_meshing_uses_immutable_background_snapshot():
    assert "pendingMeshBuild_ = std::async(std::launch::async" in WORLD
    assert "snapshot.BuildMesh(" in WORLD
    assert "current->revision == completed.sourceRevision" in WORLD
    assert "mutable std::future<AsyncMeshBuildResult> pendingMeshBuild_" in HEADER
    assert "asyncMeshWorld.BuildMesh(" in NATIVE_TESTS


def test_gpu_mesh_is_swapped_only_after_both_layers_upload():
    assert "GpuChunk staged{};" in RENDERER
    assert "if (!opaqueOk || !transparentOk)" in RENDERER
    assert "existing->second = staged;" in RENDERER
    failure_branch = RENDERER.split("if (!opaqueOk || !transparentOk)", 1)[1].split("staged.revision", 1)[0]
    assert "gpu.opaqueCount = 0" not in failure_branch
    assert "GL_DYNAMIC_DRAW" in RENDERER
    assert "staged.minY = view.minY;" in RENDERER
    assert "staged.maxY = view.maxY;" in RENDERER


def test_renderer_draws_transparent_only_chunks_and_keeps_near_vegetation():
    assert "worldRenderer_.TransparentVertexCount() > 0" in ENGINE
    assert "result.lod>=2 &&" in WORLD
    assert "def->shape==BlockShape::CrossPlant" in WORLD


if __name__ == "__main__":
    test_partial_opaque_voxels_do_not_cull_whole_cube_faces()
    test_mobile_single_chunk_meshing_uses_immutable_background_snapshot()
    test_gpu_mesh_is_swapped_only_after_both_layers_upload()
    test_renderer_draws_transparent_only_chunks_and_keeps_near_vegetation()
    print("Minefade renderer/mobile chunk regression checks passed.")
