"""Forge map variants in the data root: list them, rename and describe them.

The game saves a map variant (port/linux/game/forge_layout.c) as
forge_maps/<map>_<name>.mvar beside custom_maps, in the data root: binary
chunks (docs/forge_mvar.md), whose mvhd holds the variant's name, its
description (what the multiplayer map list says about it), its base map and
whether the map list shows it. A map's variants are numbered as its Map tab
lists them: in the order of their file names, from 1. The game's own Map tab
changes the name and description too.

    python -m tools.mod_launcher layouts [MAP]                   list them
    python -m tools.mod_launcher layouts rename MAP N NAME...    rename variant N
    python -m tools.mod_launcher layouts describe MAP N TEXT...  describe it
                                                                 (no TEXT: none)

The data root is --data, else as the launcher finds it (HALO_DATA_ROOT, the
current folder, assets/: tools/mod_maps.py).
"""

import os
import struct
import zlib
from pathlib import Path
from typing import Dict, List, Optional

from . import mod_maps

SLOT_COUNT = 64
NAME_SIZE = 63
DESCRIPTION_SIZE = 127
MAXIMUM_SIZE = 262144
VERSION = 1
CHUNK_HEADER = struct.Struct("<4sIHH")
BLF_SIZE = CHUNK_HEADER.size + 12
# mvhd: name, description, author, scenario, created, modified, listed, 0, 0, next object id
HEADER = struct.Struct("<64s128s32s256sIIBBHI")


class LayoutError(Exception):
    pass


def forge_maps_dir(data_root: Path) -> Path:
    return data_root / "forge_maps"


def _text(field: bytes) -> str:
    """a text field as the game takes it (forge_layout_clean_text): to its
    first zero, plain characters only, no percent signs, no spaces at the
    ends"""
    text = field.split(b"\0", 1)[0].decode("latin-1")
    return "".join(character for character in text if " " <= character < "\x7f" and character != "%").strip(" ")


def _chunks(data: bytes):
    """each chunk after _blf: tag, version, offset of its contents, size"""
    offset = BLF_SIZE
    while True:
        if len(data) - offset < CHUNK_HEADER.size:
            raise LayoutError("the file ends before its _eof")
        tag, size, version, _ = CHUNK_HEADER.unpack_from(data, offset)
        offset += CHUNK_HEADER.size
        if len(data) - offset < size:
            raise LayoutError(f"the chunk {tag!r} runs past the end of the file")
        if tag == b"_eof":
            return
        yield tag, version, offset, size
        offset += size


def check(data: bytes) -> None:
    """raises LayoutError when the data is not a variant this version reads
    (forge_mvar_open)"""
    if len(data) < BLF_SIZE + CHUNK_HEADER.size or len(data) > MAXIMUM_SIZE:
        raise LayoutError("not a map variant (its size)")
    tag, size, version, _ = CHUNK_HEADER.unpack_from(data, 0)
    magic, length, crc = struct.unpack_from("<4sII", data, CHUNK_HEADER.size)
    if tag != b"_blf" or magic != b"mvar" or size < 12:
        raise LayoutError("not a map variant")
    if not 1 <= version <= VERSION:
        raise LayoutError(f"a map variant of version {version}, newer than this tool reads")
    if length != len(data) or crc != zlib.crc32(data[BLF_SIZE:]) & 0xFFFFFFFF:
        raise LayoutError("a damaged map variant (its length or CRC)")


def seal(data: bytearray) -> None:
    """the file's length and CRC, written again after its chunks changed
    (forge_mvar_seal)"""
    struct.pack_into("<II", data, CHUNK_HEADER.size + 4, len(data), zlib.crc32(bytes(data[BLF_SIZE:])) & 0xFFFFFFFF)


def header_offset(data: bytes) -> int:
    check(data)
    for tag, _, offset, size in _chunks(data):
        if tag == b"mvhd":
            if size < HEADER.size:
                raise LayoutError("the variant's mvhd is too short")
            return offset
    raise LayoutError("the variant has no mvhd")


def read_header(data: bytes) -> Dict[str, object]:
    fields = HEADER.unpack_from(data, header_offset(data))
    return {"name": _text(fields[0]) or "Untitled", "description": _text(fields[1]), "author": _text(fields[2]),
            "scenario": _text(fields[3]), "listed": bool(fields[6] & 1)}


def base_map(scenario: str) -> str:
    """the short name of a variant's base map, as forge_map_folder_name has it"""
    name = scenario.rsplit("\\", 1)[-1]
    return "".join("_" if ord(character) < 32 or character in "/\\:" or (index == 0 and character == ".")
                   else character for index, character in enumerate(name))


def variants(data_root: Path) -> List[Dict[str, object]]:
    """every readable variant in forge_maps, with its file and base map"""
    folder = forge_maps_dir(data_root)
    found = []
    if not folder.is_dir():
        return found
    for path in folder.iterdir():
        if not path.is_file() or path.suffix.lower() != ".mvar" or len(path.name) >= 64:
            continue
        try:
            header = read_header(path.read_bytes())
        except (LayoutError, OSError, struct.error):
            continue
        found.append({**header, "file": path.name, "path": path, "map": base_map(header["scenario"])})
    return found


def layouts(data_root: Path, map_name: str) -> List[Dict[str, object]]:
    """the map's variants as its Map tab lists them: slot, file, name,
    description, and whether the map list shows it"""
    found = sorted((variant for variant in variants(data_root) if variant["map"].lower() == map_name.lower()),
                   key=lambda variant: str(variant["file"]).lower())[:SLOT_COUNT]
    return [{"slot": slot, "file": variant["file"], "name": variant["name"], "description": variant["description"],
             "listed": variant["listed"]} for slot, variant in enumerate(found, 1)]


def clean_text(text: str, what: str, size: int) -> str:
    """as the game takes it: one line of plain characters, no percent signs
    (the menus would take one for a format)"""
    text = " ".join(text.split())
    if len(text) > size:
        raise LayoutError(f"the {what} is longer than {size} characters")
    if any(not " " <= character < "\x7f" or character == "%" for character in text):
        raise LayoutError(f"the {what} may only have plain (ASCII) characters, and no %")
    return text


def maps_with_layouts(data_root: Path) -> List[str]:
    return sorted({str(variant["map"]) for variant in variants(data_root)}, key=str.lower)


def _variant_path(data_root: Path, map_name: str, slot: int) -> Path:
    for layout in layouts(data_root, map_name):
        if layout["slot"] == slot:
            return forge_maps_dir(data_root) / str(layout["file"])
    raise LayoutError(f"{map_name} has no variant {slot} (in {forge_maps_dir(data_root)})")


def _set_field(data_root: Path, map_name: str, slot: int, field: int, size: int, text: str) -> None:
    """a text field of the variant's mvhd (by its offset in it), written
    where it is; the file is replaced whole, as the game writes one"""
    path = _variant_path(data_root, map_name, slot)
    data = bytearray(path.read_bytes())
    offset = header_offset(bytes(data)) + field
    data[offset:offset + size] = text.encode("ascii").ljust(size, b"\0")[:size]
    seal(data)
    temporary = path.with_name(path.name + ".new")
    temporary.write_bytes(bytes(data))
    os.replace(temporary, path)


def rename(data_root: Path, map_name: str, slot: int, name: str) -> str:
    name = clean_text(name, "name", NAME_SIZE)
    if not name:
        raise LayoutError("the name is empty")
    _set_field(data_root, map_name, slot, 0, 64, name)
    return name


def describe(data_root: Path, map_name: str, slot: int, description: str) -> str:
    """sets the variant's description, the text under its name in the
    multiplayer map list; empty takes it away (the list then says which map
    the variant is made on)"""
    description = clean_text(description, "description", DESCRIPTION_SIZE)
    _set_field(data_root, map_name, slot, 64, 128, description)
    return description


def show(data_root: Path, map_name: Optional[str] = None) -> None:
    names = [map_name] if map_name else maps_with_layouts(data_root)
    if not names:
        print(f"no map variants in {forge_maps_dir(data_root)}")
        return
    for name in names:
        print(f"{name}:")
        found = layouts(data_root, name)
        if not found:
            print("  (none)")
        for layout in found:
            notes = [str(layout["file"])] + (["in the map list"] if layout["listed"] else [])
            print(f"  {layout['slot']:2d}. {layout['name']}  ({', '.join(notes)})")
            if layout["description"]:
                print(f"      {layout['description']}")


def _slot(value: str) -> int:
    try:
        return int(value)
    except ValueError:
        raise LayoutError(f"not a variant number: {value}")


def command(action: Optional[str], values: List[str], data_root: Optional[Path]) -> None:
    root = data_root or mod_maps.data_root()
    if root is None:
        raise LayoutError("no game data found (a folder with maps/ here, in assets/, or HALO_DATA_ROOT); "
                          "give it with --data")
    if action in (None, "list"):
        show(root, values[0] if values else None)
    elif action == "rename":
        if len(values) < 3:
            raise LayoutError("rename MAP N NAME...")
        slot = _slot(values[1])
        print(f"{values[0]} variant {slot}: {rename(root, values[0], slot, ' '.join(values[2:]))}")
    elif action == "describe":
        if len(values) < 2:
            raise LayoutError("describe MAP N TEXT...")
        slot = _slot(values[1])
        description = describe(root, values[0], slot, " ".join(values[2:]))
        print(f"{values[0]} variant {slot}: {description or '(no description)'}")
    else:
        # a map name alone lists that map's variants
        show(root, action)
