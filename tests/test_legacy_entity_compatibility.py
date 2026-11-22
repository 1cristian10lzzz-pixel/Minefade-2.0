from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ADAPTER=(ROOT/"app/src/main/cpp/entity/LegacyModelAdapter.cpp").read_text(encoding="utf-8")
PLAYER=(ROOT/"app/src/main/cpp/game/Player.cpp").read_text(encoding="utf-8")
ENGINE=(ROOT/"app/src/main/cpp/engine/Engine.cpp").read_text(encoding="utf-8")

def test_legacy_mode_never_uses_new_model_and_auto_requires_validation():
    assert "case LegacyModelMode::Legacy: return false;" in ADAPTER
    assert "case LegacyModelMode::Auto: return modelValidated;" in ADAPTER
    assert "case LegacyModelMode::Maes: return modelValidated;" in ADAPTER

def test_engine_only_excludes_sheep_after_successful_model_validation():
    assert "entityVisuals_.IsSheepReady() ? 0 : -1" in ENGINE
    assert "AppendLegacyMesh" in ENGINE
    assert "entityVisuals_.AppendMesh" in ENGINE
    assert "if (excludedKind >= 0 && entity.kind == excludedKind) continue;" in PLAYER

if __name__=="__main__":
    test_legacy_mode_never_uses_new_model_and_auto_requires_validation()
    test_engine_only_excludes_sheep_after_successful_model_validation()
    print("MAES legacy compatibility checks passed.")
