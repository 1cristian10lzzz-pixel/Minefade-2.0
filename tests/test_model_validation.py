#!/usr/bin/env python3
from pathlib import Path
import json
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
SHEEP=ROOT/"app/src/main/assets/entities/sheep"
CONVERTER=ROOT/"tools/mfmodelc.py"

def test_model_has_single_root_and_acyclic_topological_parents():
    geometry=json.loads((SHEEP/"sheep.geo.json").read_text(encoding="utf-8"))
    bones=geometry["minecraft:geometry"][0]["bones"]
    names=[bone["name"] for bone in bones]
    assert len(names)==len(set(names))
    assert bones[0]["name"]=="root" and "parent" not in bones[0]
    for index,bone in enumerate(bones[1:],start=1):
        assert bone.get("parent") in names[:index]

def test_converter_rejects_missing_geometry_file():
    result=subprocess.run([sys.executable,str(CONVERTER),"--geometry",str(SHEEP/"missing.geo.json"),"--check"],
                          cwd=ROOT,capture_output=True,text=True)
    assert result.returncode!=0
    assert "cannot read" in result.stderr

if __name__=="__main__":
    test_model_has_single_root_and_acyclic_topological_parents()
    test_converter_rejects_missing_geometry_file()
    print("MAES model validation checks passed.")
