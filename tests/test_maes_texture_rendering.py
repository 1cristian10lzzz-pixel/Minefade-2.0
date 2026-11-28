from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ENGINE=(ROOT/"app/src/main/cpp/engine/Engine.cpp").read_text(encoding="utf-8")
ENGINE_H=(ROOT/"app/src/main/cpp/engine/Engine.h").read_text(encoding="utf-8")
VISUAL=(ROOT/"app/src/main/cpp/entity/EntityVisualSystem.cpp").read_text(encoding="utf-8")
JAVA=(ROOT/"app/src/main/java/com/minefade/nativeengine/MinefadeSurfaceView.java").read_text(encoding="utf-8")
JNI=(ROOT/"app/src/main/cpp/NativeEngine.cpp").read_text(encoding="utf-8")


def test_maes_texture_is_a_real_uv_sampled_rgba_resource():
    assert 'uEntityTexture' in ENGINE
    assert 'uUseEntityTexture' in ENGINE
    assert 'texture(uEntityTexture, vTexCoord)' in ENGINE
    assert 'GL_NEAREST' in ENGINE
    assert 'glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height' in ENGINE
    assert 'constexpr int width = 64' in ENGINE
    assert 'constexpr int height = 32' in ENGINE
    assert 'expectedBytes' in ENGINE
    assert 'entities/sheep/sheep.texture.rgba' in JAVA
    assert '64 * 32 * 4' in JAVA
    assert 'sheepTextureBytes' in JNI


def test_maes_uses_a_separate_gpu_mesh_and_preserves_legacy_path():
    assert 'std::vector<WorldVertex> maesEntityMesh_' in ENGINE_H
    assert 'GLuint maesEntityVao_' in ENGINE_H
    assert 'GLuint maesEntityVbo_' in ENGINE_H
    assert 'GLsizei maesEntityVertexCount_' in ENGINE_H
    assert 'UploadMaesEntityMesh' in ENGINE
    assert 'entities_.AppendLegacyMesh(entityMesh_' in ENGINE
    assert 'entityVisuals_.AppendMesh(entities_.Entities(), maesEntityMesh_' in ENGINE
    assert 'glDrawArrays(GL_TRIANGLES, 0, maesEntityVertexCount_)' in ENGINE


def test_texture_upload_failure_keeps_the_baked_visual_fallback():
    assert 'sheepTexture_ != 0' in ENGINE
    assert 'baked face-colour fallback' in ENGINE
    assert 'textureAvailable' in VISUAL
    assert 'cube.faceColors[offset]' in VISUAL

if __name__=="__main__":
    test_maes_texture_is_a_real_uv_sampled_rgba_resource()
    test_maes_uses_a_separate_gpu_mesh_and_preserves_legacy_path()
    test_texture_upload_failure_keeps_the_baked_visual_fallback()
    print("MAES texture rendering checks passed.")
