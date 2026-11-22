#!/usr/bin/env python3
"""Regression checks for the native RmlUi block-icon atlas loader."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "app/src/main/cpp"

RENDERER = (CPP / "ui/RmlUiOpenGLES.cpp").read_text(encoding="utf-8")
HEADER = (CPP / "ui/RmlUiOpenGLES.h").read_text(encoding="utf-8")
REGISTRY = (CPP / "blocks/BlockRegistry.cpp").read_text(encoding="utf-8")
CATALOG = json.loads(
    (ROOT / "app/src/main/assets/data/minefade_block_catalog.json").read_text(encoding="utf-8")
)


def test_every_catalogued_block_has_a_valid_side_icon_tile():
    blocks = CATALOG["blocks"]
    by_id = {block["id"]: block for block in blocks}
    assert len(blocks) == 414
    for block_id in (0, 305, 306, 413):
        definition = by_id[block_id]
        tile = definition["atlas"]["side"]
        assert 0 <= tile < len(blocks) * 3
        assert block_id >= 306 or block_id < 306
    assert 414 not in by_id


def test_uri_parser_rejects_invalid_ids_without_a_stale_fixed_limit():
    load = RENDERER[RENDERER.index("Rml::TextureHandle RmlUiOpenGLES::LoadTexture"):]
    assert "std::from_chars(begin, end, blockId)" in load
    assert "parsed.ec != std::errc{}" in load
    assert "parsed.ptr != end" in load
    assert "blockId < 0" in load
    assert "GetBlockDefinition(static_cast<BlockId>(blockId))" in load
    assert "definition == nullptr" in load
    assert "blockId>=306" not in load
    assert "blockId >= 306" not in load
    assert "blockId*3+1" not in load
    assert "definition->tileSide" in load


def test_icon_crop_uses_manifest_layout_and_nearest_filtering():
    load = RENDERER[RENDERER.index("Rml::TextureHandle RmlUiOpenGLES::LoadTexture"):]
    assert "kBlockAtlasColumns" in load
    assert "kBlockAtlasCellSize" in load
    assert "kBlockAtlasPadding" in load
    assert "kBlockAtlasTileSize" in load
    assert "kBlockAtlasTileCount" in load
    assert "CreateTexture(" in load
    assert "textureDimensions, GL_NEAREST" in load
    assert "textureCache_" in HEADER
    assert "references" in HEADER
    assert "TextureResource" in HEADER
    assert "GL_TEXTURE_MIN_FILTER" in RENDERER


def test_cache_reference_release_is_balanced():
    assert "++resource->second.references" in RENDERER
    assert "if (found->second.references > 1u)" in RENDERER
    assert "--found->second.references" in RENDERER
    assert "textureCache_.erase(cached)" in RENDERER
    assert "glDeleteTextures(1, &found->second.object)" in RENDERER


if __name__ == "__main__":
    test_every_catalogued_block_has_a_valid_side_icon_tile()
    test_uri_parser_rejects_invalid_ids_without_a_stale_fixed_limit()
    test_icon_crop_uses_manifest_layout_and_nearest_filtering()
    test_cache_reference_release_is_balanced()
    print("RmlUi atlas loader regressions passed.")
