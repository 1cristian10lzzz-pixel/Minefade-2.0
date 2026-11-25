#!/usr/bin/env python3
"""Compile a validated Bedrock-geometry subset and clips into Minefade .mfmodel v1.

The converter intentionally supports a small, explicit subset of Bedrock geometry:
bones, parent hierarchy, pivots, rotations, cuboids, per-face UV rectangles,
locators, animation rotation/translation/scale channels, and controller states.
It emits a little-endian binary with range checks and CRC32; malformed source
resources fail the build instead of producing a partially usable model.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SHEEP = ROOT / "app/src/main/assets/entities/sheep"
DEFAULT_OUTPUT = SHEEP / "sheep.mfmodel"

HEADER = struct.Struct("<8sHH12I6f")
BONE = struct.Struct("<II6f")
CUBE = struct.Struct("<II12f24f24s")
MATERIAL = struct.Struct("<III4f")
LOCATOR = struct.Struct("<II3f")
CLIP = struct.Struct("<IIfII")
TRACK = struct.Struct("<IIII")
KEY = struct.Struct("<11f")
STATE = struct.Struct("<IIIf")

MAGIC = b"MFMODEL\0"
MAJOR = 1
MINOR = 0
PARENT_NONE = 0xFFFFFFFF
MAX_BONES = 128
MAX_CUBES = 4096
MAX_MATERIALS = 64
MAX_LOCATORS = 128
MAX_CLIPS = 64
MAX_TRACKS = 512
MAX_KEYS = 16384
MAX_STATES = 32
MAX_STRINGS = 65536
MAX_FILE_BYTES = 8 * 1024 * 1024

FACE_ORDER = ("east", "west", "up", "down", "south", "north")
CONDITION = {"idle": 0, "moving": 1, "eating": 2}


def fail(message: str) -> None:
    raise ValueError(message)


def finite_number(value: Any, label: str) -> float:
    result = float(value)
    if not math.isfinite(result):
        fail(f"{label} must be finite")
    return result


def vec3(value: Any, label: str) -> tuple[float, float, float]:
    if not isinstance(value, list) or len(value) != 3:
        fail(f"{label} must be a 3-element array")
    return tuple(finite_number(v, label) for v in value)  # type: ignore[return-value]


def euler_quaternion(degrees: tuple[float, float, float]) -> tuple[float, float, float, float]:
    """XYZ Euler degrees, applied X then Y then Z; runtime uses the same order."""
    x, y, z = (math.radians(v) * 0.5 for v in degrees)
    sx, cx = math.sin(x), math.cos(x)
    sy, cy = math.sin(y), math.cos(y)
    sz, cz = math.sin(z), math.cos(z)
    # q = qz * qy * qx
    qx = sx * cy * cz - cx * sy * sz
    qy = cx * sy * cz + sx * cy * sz
    qz = cx * cy * sz - sx * sy * cz
    qw = cx * cy * cz + sx * sy * sz
    length = math.sqrt(qx*qx + qy*qy + qz*qz + qw*qw)
    if length <= 1e-9 or not math.isfinite(length):
        fail("invalid rotation quaternion")
    return qx/length, qy/length, qz/length, qw/length


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"{path} root must be an object")
    return value


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    import binascii

    if len(rgba) != width * height * 4:
        fail("RGBA texture byte count does not match dimensions")

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)

    scanlines = b"".join(b"\x00" + rgba[y*width*4:(y+1)*width*4] for y in range(height))
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, 9))
        + chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def make_texture(texture_spec: dict[str, Any]) -> tuple[int, int, bytes, dict[str, tuple[int,int,int,int]]]:
    width = int(texture_spec.get("width", 64))
    height = int(texture_spec.get("height", 32))
    if width <= 0 or height <= 0 or width > 1024 or height > 1024:
        fail("texture dimensions outside supported range")
    pixels = bytearray(width * height * 4)
    regions: dict[str, tuple[int,int,int,int]] = {}
    for region in texture_spec.get("regions", []):
        name = str(region.get("name", ""))
        rect = region.get("rect")
        base = region.get("base")
        if not name or not isinstance(rect, list) or len(rect) != 4:
            fail("texture region requires name and rect [x,y,w,h]")
        x, y, w, h = (int(v) for v in rect)
        if w <= 0 or h <= 0 or x < 0 or y < 0 or x+w > width or y+h > height:
            fail(f"texture region {name} outside {width}x{height}")
        if not isinstance(base, list) or len(base) != 3:
            fail(f"texture region {name} has invalid base colour")
        rgb = [max(0, min(255, int(v))) for v in base]
        variation = max(0, min(32, int(region.get("variation", 0))))
        for py in range(y, y+h):
            for px in range(x, x+w):
                # Deterministic small wool/fur pixels; no random seed or runtime work.
                n = ((px * 17 + py * 31 + (px ^ (py * 3))) % (variation*2+1)) - variation if variation else 0
                offset = (py*width + px)*4
                pixels[offset:offset+4] = bytes([max(0,min(255,c+n)) for c in rgb] + [255])
        regions[name] = (x,y,w,h)
    return width, height, bytes(pixels), regions


def face_uvs(cube: dict[str, Any], width: int, height: int) -> tuple[tuple[float,float,float,float], ...]:
    raw = cube.get("uv", [0,0])
    values=[]
    for face in FACE_ORDER:
        rect = None
        if isinstance(raw, dict):
            rect = raw.get(face)
        elif isinstance(raw, list) and len(raw) == 2:
            # Bedrock single-origin UV subset: use a square based on the largest side.
            u,v = int(raw[0]), int(raw[1])
            side = max(1, min(16, int(max(cube.get("size", [1,1,1])))))
            rect=[u,v,side,side]
        if isinstance(rect, dict):
            uv = rect.get("uv", [0,0])
            size = rect.get("uv_size", [1,1])
            if not isinstance(uv,list) or not isinstance(size,list) or len(uv)!=2 or len(size)!=2:
                fail(f"invalid {face} UV object")
            rect=[uv[0],uv[1],size[0],size[1]]
        if not isinstance(rect,list) or len(rect)!=4:
            fail(f"cube UV is missing {face} rectangle")
        x,y,w,h=(finite_number(v, f"{face} UV") for v in rect)
        if w <= 0 or h <= 0 or x < 0 or y < 0 or x+w > width or y+h > height:
            fail(f"{face} UV rectangle exceeds texture")
        values.append(((x+0.5)/width,(y+0.5)/height,(x+w-0.5)/width,(y+h-0.5)/height))
    return tuple(values)


def sample_face_colors(cube: dict[str, Any], rgba: bytes, width: int, height: int) -> bytes:
    rects = []
    raw = cube.get("uv", [0,0])
    for face in FACE_ORDER:
        rect = raw.get(face) if isinstance(raw,dict) else None
        if isinstance(rect,dict):
            uv=rect.get("uv",[0,0]); size=rect.get("uv_size",[1,1])
            rect=[uv[0],uv[1],size[0],size[1]]
        if rect is None and isinstance(raw,list) and len(raw)==2:
            u,v=raw; side=max(1,min(16,int(max(cube.get("size",[1,1,1])))))
            rect=[u,v,side,side]
        if not isinstance(rect,list) or len(rect)!=4:
            fail(f"cube UV missing face '{face}'")
        x,y,w,h=(int(v) for v in rect)
        if x<0 or y<0 or w<=0 or h<=0 or x+w>width or y+h>height:
            fail(f"cube UV for face '{face}' outside texture")
        sx=min(width-1,x+w//2); sy=min(height-1,y+h//2)
        offset=(sy*width+sx)*4
        rects.extend(rgba[offset:offset+4])
    return bytes(rects)


def add_string(table: bytearray, offsets: dict[str,int], value: str) -> int:
    if value in offsets:
        return offsets[value]
    encoded=value.encode("utf-8")
    if not encoded or b"\0" in encoded:
        fail("model strings must be non-empty UTF-8 without NUL")
    offset=len(table)
    table.extend(encoded+b"\0")
    offsets[value]=offset
    if len(table)>MAX_STRINGS:
        fail("model string table exceeds 64 KiB")
    return offset


def sample_channel(channel: Any, time: float, default: tuple[float,...]) -> tuple[float,...]:
    if not isinstance(channel,dict) or not channel:
        return default
    keys=sorted((float(k),vec3(v,"animation key")) for k,v in channel.items())
    if time<=keys[0][0]: return keys[0][1]
    if time>=keys[-1][0]: return keys[-1][1]
    for (t0,v0),(t1,v1) in zip(keys,keys[1:]):
        if t0<=time<=t1:
            amount=(time-t0)/max(t1-t0,1e-6)
            return tuple(a+(b-a)*amount for a,b in zip(v0,v1))
    return default


def build_model(geometry: dict[str,Any], animations: dict[str,Any], controller: dict[str,Any],
                entity: dict[str,Any], rgba: bytes, tex_w: int, tex_h: int) -> bytes:
    geometries=geometry.get("minecraft:geometry")
    if not isinstance(geometries,list) or len(geometries)!=1:
        fail("geometry source must contain exactly one minecraft:geometry entry")
    geo=geometries[0]
    description=geo.get("description",{})
    texture_w=int(description.get("texture_width",tex_w))
    texture_h=int(description.get("texture_height",tex_h))
    if texture_w!=tex_w or texture_h!=tex_h:
        fail("geometry texture dimensions differ from sheep texture")
    bones_raw=geo.get("bones")
    if not isinstance(bones_raw,list) or not bones_raw:
        fail("geometry has no bones")
    if len(bones_raw)>MAX_BONES:
        fail("too many bones")

    bone_index: dict[str,int]={}
    bones=[]
    strings=bytearray()
    string_offsets: dict[str,int]={}
    for index,bone in enumerate(bones_raw):
        name=str(bone.get("name",""))
        if not name or name in bone_index: fail(f"empty or duplicate bone name: {name}")
        parent=bone.get("parent")
        parent_index=PARENT_NONE if parent is None else bone_index.get(str(parent),-1)
        if parent is not None and parent_index<0:
            fail(f"bone {name} parent must occur earlier in topological order")
        pivot=tuple(v/16.0 for v in vec3(bone.get("pivot",[0,0,0]),f"{name}.pivot"))
        rotation=vec3(bone.get("rotation",[0,0,0]),f"{name}.rotation")
        bone_index[name]=index
        bones.append((add_string(strings,string_offsets,name),parent_index,pivot,rotation))

    materials=[("sheep","entities/sheep/sheep.png",0,(1.0,1.0,1.0,1.0))]
    material_records=[(add_string(strings,string_offsets,m[0]),add_string(strings,string_offsets,m[1]),m[2],m[3]) for m in materials]
    cubes=[]
    locators=[]
    bounds_min=[float("inf")]*3
    bounds_max=[float("-inf")]*3
    for bone in bones_raw:
        bname=str(bone["name"])
        bi=bone_index[bname]
        for cube in bone.get("cubes",[]):
            origin=vec3(cube.get("origin"),f"{bname}.cube.origin")
            size=vec3(cube.get("size"),f"{bname}.cube.size")
            pivot=vec3(cube.get("pivot",bone.get("pivot",[0,0,0])),f"{bname}.cube.pivot")
            rotation=vec3(cube.get("rotation",[0,0,0]),f"{bname}.cube.rotation")
            if any(v<=0 for v in size):
                fail(f"{bname} cube size must be positive")
            if min(size)>128:
                fail(f"{bname} cube size exceeds model bounds")
            uv=face_uvs(cube,tex_w,tex_h)
            colors=sample_face_colors(cube,rgba,tex_w,tex_h)
            origin_s=tuple(v/16.0 for v in origin)
            size_s=tuple(v/16.0 for v in size)
            pivot_s=tuple(v/16.0 for v in pivot)
            rotation_s=rotation
            cubes.append((bi,0,origin_s,size_s,pivot_s,rotation_s,uv,colors))
            for axis in range(3):
                bounds_min[axis]=min(bounds_min[axis],origin_s[axis])
                bounds_max[axis]=max(bounds_max[axis],origin_s[axis]+size_s[axis])
            if len(cubes)>MAX_CUBES: fail("too many model cubes")
        raw_locators=bone.get("locators",{})
        if isinstance(raw_locators,dict):
            for locator_name, position in raw_locators.items():
                locators.append((add_string(strings,string_offsets,str(locator_name)),bi,
                                 tuple(v/16.0 for v in vec3(position,f"{bname}.{locator_name}"))))
                if len(locators)>MAX_LOCATORS: fail("too many model locators")

    animation_list=animations.get("animations",{})
    if not isinstance(animation_list,dict) or not animation_list:
        fail("animations file has no clips")
    if len(animation_list)>MAX_CLIPS: fail("too many animation clips")
    clips=[]; tracks=[]; keys=[]
    clip_indices={}
    for clip_name,clip in animation_list.items():
        duration=finite_number(clip.get("animation_length",0),f"{clip_name}.animation_length")
        if duration<=0 or duration>120: fail(f"{clip_name} duration outside 0..120 seconds")
        loop=clip.get("loop",False) in (True,"true","hold_on_last_frame")
        clip_index=len(clips); clip_indices[str(clip_name)]=clip_index
        track_first=len(tracks)
        bone_channels=clip.get("bones",{})
        for bone_name,channels in bone_channels.items():
            if bone_name not in bone_index: fail(f"{clip_name} targets unknown bone {bone_name}")
            channel_times=set()
            for channel_name in ("position","rotation","scale"):
                source=channels.get(channel_name,{})
                if isinstance(source,dict): channel_times.update(float(t) for t in source.keys())
            if not channel_times: continue
            times=sorted(channel_times)
            if len(times)>256 or any(t<0 or t>duration+1e-4 for t in times):
                fail(f"{clip_name}.{bone_name} invalid keyframe times")
            key_first=len(keys)
            for t in times:
                position=sample_channel(channels.get("position",{}),t,(0.0,0.0,0.0))
                rotation=sample_channel(channels.get("rotation",{}),t,(0.0,0.0,0.0))
                scale=sample_channel(channels.get("scale",{}),t,(1.0,1.0,1.0))
                q=euler_quaternion(rotation)
                keys.append((t,*[v/16.0 for v in position],*q,*scale))
                if len(keys)>MAX_KEYS: fail("too many animation keyframes")
            tracks.append((clip_index,bone_index[bone_name],key_first,len(times)))
            if len(tracks)>MAX_TRACKS: fail("too many animation tracks")
        clips.append((add_string(strings,string_offsets,str(clip_name)),1 if loop else 0,
                      duration,track_first,len(tracks)-track_first))

    controller_states=controller.get("states",{})
    if not isinstance(controller_states,dict) or not controller_states:
        fail("controller contains no states")
    states=[]
    for state_name,state in controller_states.items():
        clip_name=str(state.get("clip",""))
        if clip_name not in clip_indices: fail(f"controller state {state_name} references missing clip {clip_name}")
        condition=str(state.get("condition","idle"))
        if condition not in CONDITION: fail(f"controller state {state_name} has unknown condition {condition}")
        blend=finite_number(state.get("blend_duration",0.15),f"{state_name}.blend_duration")
        if blend<0 or blend>2: fail("controller blend duration outside 0..2 seconds")
        states.append((add_string(strings,string_offsets,str(state_name)),
                       clip_indices[clip_name],CONDITION[condition],blend))
        if len(states)>MAX_STATES: fail("too many controller states")

    if not strings or len(strings)>MAX_STRINGS: fail("empty or oversized string table")
    payload=bytearray(strings)
    for name_off,parent,pivot,rotation in bones:
        payload.extend(BONE.pack(name_off,parent,*pivot,*rotation))
    for bi,mi,origin,size,pivot,rotation,uv,colors in cubes:
        flat_uv=[component for rect in uv for component in rect]
        payload.extend(CUBE.pack(bi,mi,*origin,*size,*pivot,*rotation,*flat_uv,colors))
    for name_off,texture_off,flags,color in material_records:
        payload.extend(MATERIAL.pack(name_off,texture_off,flags,*color))
    for name_off,bi,position in locators:
        payload.extend(LOCATOR.pack(name_off,bi,*position))
    for name_off,flags,duration,track_first,track_count in clips:
        payload.extend(CLIP.pack(name_off,flags,duration,track_first,track_count))
    for clip_index,bi,key_first,key_count in tracks:
        payload.extend(TRACK.pack(clip_index,bi,key_first,key_count))
    for key in keys:
        payload.extend(KEY.pack(*key))
    for name_off,clip_index,condition,blend in states:
        payload.extend(STATE.pack(name_off,clip_index,condition,blend))

    header_bytes=HEADER.size
    total_bytes=header_bytes+len(payload)
    if total_bytes>MAX_FILE_BYTES: fail("compiled .mfmodel exceeds 8 MiB")
    crc=zlib.crc32(payload)&0xFFFFFFFF
    if not math.isfinite(bounds_min[0]):
        fail("model contains no geometry")
    bounds=tuple(bounds_min+bounds_max)
    header=HEADER.pack(
        MAGIC,MAJOR,MINOR,header_bytes,total_bytes,crc,
        len(bones),len(cubes),len(material_records),len(locators),
        len(clips),len(tracks),len(keys),len(states),len(strings),*bounds
    )
    return header+payload


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry",type=Path,default=SHEEP/"sheep.geo.json")
    parser.add_argument("--texture",type=Path,default=SHEEP/"sheep.texture.json")
    parser.add_argument("--animations",type=Path,default=SHEEP/"sheep.animations.json")
    parser.add_argument("--controller",type=Path,default=SHEEP/"sheep.controller.json")
    parser.add_argument("--entity",type=Path,default=SHEEP/"sheep.mfentity.json")
    parser.add_argument("--output",type=Path,default=DEFAULT_OUTPUT)
    parser.add_argument("--check",action="store_true",help="compile to memory and validate without writing")
    args=parser.parse_args()
    try:
        entity=load_json(args.entity)
        if entity.get("identifier")!="minefade:sheep" or entity.get("legacy_kind")!=0:
            fail("sheep definition must preserve minefade:sheep / legacy_kind 0")
        tex_spec=load_json(args.texture)
        width,height,rgba,_=make_texture(tex_spec)
        geo=load_json(args.geometry)
        anim=load_json(args.animations)
        ctrl=load_json(args.controller)
        model=build_model(geo,anim,ctrl,entity,rgba,width,height)
        if not args.check:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_bytes(model)
            raw_path=args.output.with_name("sheep.texture.rgba")
            raw_path.write_bytes(rgba)
            write_png(args.output.with_name("sheep.png"),width,height,rgba)
        header=HEADER.unpack_from(model)
        print(
            f"MAES sheep compiled: {len(model)} bytes, "
            f"{header[6]} bones, {header[7]} cubes, {header[10]} clips, "
            f"{header[11]} tracks, {header[12]} keyframes, CRC32={header[5]:08x}"
        )
        return 0
    except (ValueError, OSError, struct.error, KeyError, TypeError) as exc:
        print(f"mfmodelc: error: {exc}",file=sys.stderr)
        return 2


if __name__=="__main__":
    raise SystemExit(main())
