#!/usr/bin/env python3
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CONVERTER=ROOT/"tools/mfmodelc.py"

def run_converter(output: Path):
    return subprocess.run([sys.executable,str(CONVERTER),"--output",str(output)],
                          cwd=ROOT,capture_output=True,text=True,check=True)

def test_model_and_texture_resources_are_deterministic():
    with tempfile.TemporaryDirectory(prefix="minefade-maes-") as temp:
        root=Path(temp)
        first=root/"first.mfmodel"
        second=root/"second.mfmodel"
        run_converter(first)
        first_texture=first.with_name("sheep.texture.rgba").read_bytes()
        first_png=first.with_name("sheep.png").read_bytes()
        run_converter(second)
        second_texture=second.with_name("sheep.texture.rgba").read_bytes()
        second_png=second.with_name("sheep.png").read_bytes()
        assert first.read_bytes()==second.read_bytes()
        assert first_texture==second_texture
        assert first_png==second_png
        assert len(first_texture)==64*32*4
        assert first_png.startswith(b"\x89PNG\r\n\x1a\n")
        header=struct.Struct("<8sHH12I6f").unpack_from(first.read_bytes())
        assert header[0]==b"MFMODEL\0"
        assert header[4]==len(first.read_bytes())
        assert len(hashlib.sha256(first.read_bytes()).hexdigest())==64

if __name__=="__main__":
    test_model_and_texture_resources_are_deterministic()
    print("MAES entity resource checks passed.")
