import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DEF=ROOT/"app/src/main/assets/entities/sheep/sheep.mfentity.json"

def test_sheep_uses_stable_identifier_and_legacy_kind():
    definition=json.loads(DEF.read_text(encoding="utf-8"))
    assert definition["identifier"]=="minefade:sheep"
    assert definition["legacy_kind"]==0
    assert definition["model"]=="sheep.mfmodel"
    assert definition["physics"]["width"]>0
    assert definition["physics"]["height"]>0
    assert 0<definition["physics"]["eye_height"]<=definition["physics"]["height"]

def test_historical_entity_ids_are_documented_and_untouched():
    source=(ROOT/"app/src/main/cpp/game/Player.h").read_text(encoding="utf-8")
    # Visual state fields must stay separate from gameplay kind/behavior fields.
    assert "int kind{0};" in source
    assert "int behavior{0};" in source
    assert "visualState" in source
    assert "animationClock" in source

if __name__=="__main__":
    test_sheep_uses_stable_identifier_and_legacy_kind()
    test_historical_entity_ids_are_documented_and_untouched()
    print("MAES entity registry checks passed.")
