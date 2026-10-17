"""Tests for the cache file reader (tools/map_tags.py) on a small map made here."""

import struct
import zlib
from pathlib import Path

from tools import map_tags

BASE = 0x803A6000
TAG_IDS = {"scen": 0x7363656E, "mode": 0x6D6F6465}


def make_map(path: Path, compressed: bool) -> None:
    """two tags: a 'scen' that references a 'mode' (group, name pointer, 0, index)"""
    names = [b"rocks\\rock\0", b"rocks\\rock_model\0"]
    header_size, instance_size = 0x24, 0x20
    table = header_size
    names_at = table + 2 * instance_size
    name_addresses, offset = [], names_at
    for name in names:
        name_addresses.append(BASE + offset)
        offset += len(name)
    offset = (offset + 3) & ~3
    scen_at, mode_at = offset, offset + 32
    body = bytearray(mode_at + 16)
    struct.pack_into("<IiIIIIIII", body, 0, BASE + table, 0, 0, 2, 0, 0, 0, 0, map_tags.TAGS)
    body[0x20:0x24] = struct.pack("<I", map_tags.TAGS)
    for index, (group, address) in enumerate((("scen", scen_at), ("mode", mode_at))):
        struct.pack_into("<IIIIIIII", body, table + index * instance_size,
                         TAG_IDS[group], 0, 0, 0xE0000000 | index, name_addresses[index], BASE + address, 0, 0)
    body[names_at:names_at + sum(map(len, names))] = b"".join(names)
    # the scen's data: some words, then a reference to tag 1 (the mode)
    struct.pack_into("<IIiI", body, scen_at + 8, TAG_IDS["mode"], name_addresses[1], 0, 0xE0000001)
    tag_offset = 0x800
    header = bytearray(0x800)
    header[0:4] = struct.pack("<I", map_tags.HEAD)
    length = tag_offset + len(body)
    struct.pack_into("<iiiii", header, 4, 5, length, 0, tag_offset, len(body))
    header[0x20:0x25] = b"rocks"
    data = bytes(header) + (zlib.compress(bytes(body)) if compressed else bytes(body))
    path.write_bytes(data)


def test_a_whole_and_a_compressed_map_read_alike(tmp_path):
    for compressed in (False, True):
        path = tmp_path / f"rocks_{compressed}.map"
        make_map(path, compressed)
        cache = map_tags.CacheFile(path)
        assert [(t.group, t.name) for t in cache.tags] == [("scen", "rocks\\rock"), ("mode", "rocks\\rock_model")]
        assert cache.tags[0].size == 32 and cache.tags[0].offset % 4 == 0


def test_a_tag_needs_the_tags_it_names(tmp_path):
    path = tmp_path / "rocks.map"
    make_map(path, True)
    cache = map_tags.CacheFile(path)
    scenery = cache.find("rocks\\rock", "scen")
    assert [t.name for t in cache.references(scenery)] == ["rocks\\rock_model"]
    assert [t.name for t in cache.closure(scenery)] == ["rocks\\rock", "rocks\\rock_model"]
    assert cache.closure(cache.tags[1]) == [cache.tags[1]]


def test_a_reference_with_the_wrong_name_is_not_taken(tmp_path):
    path = tmp_path / "rocks.map"
    make_map(path, False)
    data = bytearray(path.read_bytes())
    # point the reference's name at the first tag's name instead
    scen = map_tags.CacheFile(path).tags[0]
    struct.pack_into("<I", data, 0x800 + scen.offset + 12, BASE + 0x24 + 2 * 0x20)
    path.write_bytes(bytes(data))
    cache = map_tags.CacheFile(path)
    assert cache.references(cache.tags[0]) == []
