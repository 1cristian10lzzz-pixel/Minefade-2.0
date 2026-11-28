#!/usr/bin/env python3
"""Validate the versioned .mfmodel binary layout and CRC."""
from __future__ import annotations
import struct
import zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/"app/src/main/assets/entities/sheep/sheep.mfmodel"
HEADER=struct.Struct("<8sHH12I6f")

def test_header_counts_ranges_and_crc():
    data=MODEL.read_bytes()
    assert len(data)>=HEADER.size
    values=HEADER.unpack_from(data)
    magic,major,minor=values[:3]
    fields=values[3:15]
    assert magic==b"MFMODEL\0"
    assert (major,minor)==(1,0)
    header_bytes,total_bytes,crc,*counts=fields
    assert header_bytes==HEADER.size
    assert total_bytes==len(data)
    assert zlib.crc32(data[header_bytes:])&0xffffffff==crc
    bone_count,cube_count,material_count,locator_count,clip_count,track_count,key_count,state_count,string_bytes=counts
    assert 0<bone_count<=128
    assert 0<cube_count<=4096
    assert material_count==1
    assert locator_count<=128
    assert 3<=clip_count<=64
    assert track_count>0 and track_count<=512
    assert key_count>0 and key_count<=16384
    assert 3<=state_count<=32
    assert 0<string_bytes<=65536

def test_corrupted_crc_is_detectable():
    data=bytearray(MODEL.read_bytes())
    data[-1]^=0x40
    values=HEADER.unpack_from(data)
    assert zlib.crc32(data[values[3]:])&0xffffffff!=values[5]

if __name__=="__main__":
    test_header_counts_ranges_and_crc()
    test_corrupted_crc_is_detectable()
    print("MAES model format checks passed.")
