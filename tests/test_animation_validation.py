from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[1]
SHEEP=ROOT/"app/src/main/assets/entities/sheep"

def test_required_sheep_clips_have_keyframes():
    data=json.loads((SHEEP/"sheep.animations.json").read_text(encoding="utf-8"))
    clips=data["animations"]
    assert {"idle","walk","eat"}.issubset(clips)
    for name,clip in clips.items():
        duration=float(clip["animation_length"])
        assert duration>0
        assert isinstance(clip.get("bones"),dict) and clip["bones"]
        for bone,channels in clip["bones"].items():
            assert bone
            for channel,keys in channels.items():
                assert channel in {"position","rotation","scale"}
                times=sorted(float(t) for t in keys)
                assert times and times[0]>=0 and times[-1]<=duration
                for value in keys.values():
                    assert len(value)==3
                    assert all(float(v)==float(v) for v in value)

def test_controller_only_references_existing_clips():
    clips=set(json.loads((SHEEP/"sheep.animations.json").read_text(encoding="utf-8"))["animations"])
    controller=json.loads((SHEEP/"sheep.controller.json").read_text(encoding="utf-8"))
    assert controller["initial_state"] in controller["states"]
    for state in controller["states"].values():
        assert state["clip"] in clips
        assert state["condition"] in {"idle","moving","eating"}
        assert 0<=float(state["blend_duration"])<=2

if __name__=="__main__":
    test_required_sheep_clips_have_keyframes()
    test_controller_only_references_existing_clips()
    print("MAES animation validation checks passed.")
