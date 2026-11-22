#!/usr/bin/env python3
"""Regression checks for safe block-atlas uploads on shared GLES contexts."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "app/src/main/cpp/engine/Engine.cpp").read_text(encoding="utf-8")


def test_block_atlas_upload_isolates_pixel_unpack_buffer():
    upload = SOURCE[SOURCE.index("bool Engine::UploadBlockAtlas"):SOURCE.index("bool Engine::UploadSheepTexture")]
    assert "GL_PIXEL_UNPACK_BUFFER_BINDING" in upload
    assert "glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0)" in upload
    assert "glBindBuffer(GL_PIXEL_UNPACK_BUFFER," in upload
    assert "static_cast<GLuint>(previousPixelUnpackBuffer)" in upload


def test_block_atlas_upload_restores_texture_unit_and_binding():
    upload = SOURCE[SOURCE.index("bool Engine::UploadBlockAtlas"):SOURCE.index("bool Engine::UploadSheepTexture")]
    assert "GL_ACTIVE_TEXTURE" in upload
    assert "GL_TEXTURE_BINDING_2D" in upload
    assert "glActiveTexture(static_cast<GLenum>(previousActiveTexture))" in upload
    assert "glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding))" in upload


def test_block_atlas_upload_restores_pixel_unpack_alignment():
    upload = SOURCE[SOURCE.index("bool Engine::UploadBlockAtlas"):SOURCE.index("bool Engine::UploadSheepTexture")]
    assert "GL_UNPACK_ALIGNMENT" in upload
    assert "glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment)" in upload
    assert "GL_UNPACK_ROW_LENGTH" in upload
    assert "GL_UNPACK_SKIP_PIXELS" in upload
    assert "GL_UNPACK_SKIP_ROWS" in upload
    assert "glPixelStorei(GL_UNPACK_ROW_LENGTH, 0)" in upload
    assert "glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0)" in upload
    assert "glPixelStorei(GL_UNPACK_SKIP_ROWS, 0)" in upload


if __name__ == "__main__":
    test_block_atlas_upload_isolates_pixel_unpack_buffer()
    test_block_atlas_upload_restores_texture_unit_and_binding()
    test_block_atlas_upload_restores_pixel_unpack_alignment()
    print("Block atlas GLES upload-state regressions passed.")
