#!/usr/bin/env python3
"""Regression checks for RmlUi's OpenGL ES state isolation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "app/src/main/cpp/ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")


def test_rmlui_state_is_guarded_for_the_entire_ui_frame():
    assert "class ScopedRmlUiGlState" in SOURCE
    render = SOURCE[SOURCE.index("void RmlUiRuntime::UpdateAndRender()"):]
    render = render[:render.index("\n}") + 2]
    assert "ScopedRmlUiGlState stateGuard;" in render
    assert "context_->Update();" in render
    assert "context_->Render();" in render


def test_guard_restores_program_buffers_viewport_and_texture_unit():
    for token in (
        "GL_CURRENT_PROGRAM",
        "GL_VERTEX_ARRAY_BINDING",
        "GL_ARRAY_BUFFER_BINDING",
        "GL_VIEWPORT",
        "GL_ACTIVE_TEXTURE",
        "GL_TEXTURE_BINDING_2D",
        "glBindVertexArray",
        "glBindBuffer(GL_ARRAY_BUFFER",
        "glBindTexture(GL_TEXTURE_2D",
        "glViewport(",
    ):
        assert token in SOURCE


def test_guard_restores_blend_depth_cull_scissor_and_write_masks():
    for token in (
        "GL_BLEND_SRC_RGB",
        "GL_BLEND_DST_RGB",
        "GL_BLEND_SRC_ALPHA",
        "GL_BLEND_DST_ALPHA",
        "GL_BLEND_EQUATION_RGB",
        "GL_BLEND_EQUATION_ALPHA",
        "GL_DEPTH_FUNC",
        "GL_DEPTH_WRITEMASK",
        "GL_COLOR_WRITEMASK",
        "GL_BLEND",
        "GL_DEPTH_TEST",
        "GL_CULL_FACE",
        "GL_SCISSOR_TEST",
        "glBlendFuncSeparate",
        "glBlendEquationSeparate",
        "glDepthFunc",
        "glDepthMask",
        "glColorMask",
    ):
        assert token in SOURCE


def test_runtime_atlas_copy_requires_the_generated_manifest():
    assert '#include "../blocks/BlockAtlasManifest.h"' in SOURCE
    body = SOURCE[SOURCE.index("void RmlUiRuntime::SetBlockAtlas"):]
    body = body[:body.index("\n}") + 2]
    assert "kBlockAtlasWidth" in body
    assert "kBlockAtlasHeight" in body
    assert "kBlockAtlasByteCount" in body


if __name__ == "__main__":
    test_rmlui_state_is_guarded_for_the_entire_ui_frame()
    test_guard_restores_program_buffers_viewport_and_texture_unit()
    test_guard_restores_blend_depth_cull_scissor_and_write_masks()
    test_runtime_atlas_copy_requires_the_generated_manifest()
    print("RmlUi GLES state regressions passed.")
