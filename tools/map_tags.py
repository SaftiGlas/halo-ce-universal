"""Read the tags of a cache file (.map): list them, and find what a tag needs.

    python -m tools.map_tags MAP list [FILTER]      the tags (index, group, size, name)
    python -m tools.map_tags MAP needs TAG          everything TAG references, recursively
    python -m tools.map_tags MAP fits DONOR TAG     what it takes to bring TAG (and what it needs)
                                                    from the map DONOR into MAP

This does not change any file. It is for working out what bringing tags from
one map into another (mods/FORGE_PLAN.md, "Cross-map tags") takes.

A cache file is (source/cache/cache_files.c): a 0x800 byte header
(tag_data_offset, tag_data_size at 0x10, 0x14), then the tag data, which the
game reads to a fixed address (the tag cache): a tag header, the tag
instances (group, parent groups, index, name, address of the tag's data)
and the tags' data with absolute pointers. A tag reference in a tag's data
is 16 bytes: group tag, pointer to the name, a length (0 in these files) and the
index of the tag it names; the name is what makes one certain, so this
finds references by looking for those four together, and checks the name.
Bitmap pixels, sounds and the models' vertex data are not in the tag data
(sound and texture caches, and the tag header's buffers).
"""

import argparse
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set

HEADER_SIZE = 0x800
TAG_HEADER_SIZE = 0x24
TAG_INSTANCE_SIZE = 0x20
TAG_CACHE_SIZE = 0x01600000  # the game's tag cache (halo_cache_files: 0x01600000)
HEAD, FOOT, TAGS = 0x68656164, 0x666F6F74, 0x74616773  # 'head', 'foot', 'tags'


@dataclass
class Tag:
    index: int
    group: str
    name: str
    address: int  # where the game finds the tag's data
    offset: int = 0  # in the map file
    size: int = 0  # up to the next tag's data; the last tag's is not known


class CacheFile:
    def __init__(self, path: Path):
        self.path = path
        with open(path, "rb") as file:
            header = file.read(HEADER_SIZE)
            if len(header) < HEADER_SIZE or struct.unpack_from("<I", header, 0)[0] != HEAD:
                raise ValueError(f"{path}: not a cache file")
            self.version, self.length = struct.unpack_from("<ii", header, 4)
            self.tag_offset, self.tag_size = struct.unpack_from("<ii", header, 0x10)
            self.name = header[0x20:0x40].split(b"\0")[0].decode("ascii", "replace")
            self.data = self.read_tag_data(file)
        instances, self.scenario_index, _, count = struct.unpack_from("<IiII", self.data, 0)
        if struct.unpack_from("<I", self.data, 0x20)[0] != TAGS:
            raise ValueError(f"{path}: no tag header")
        # the tag instances follow the tag header
        self.base = instances - TAG_HEADER_SIZE
        self.tags: List[Tag] = []
        for index in range(count):
            group, _, _, tag_index, name, address, _, _ = struct.unpack_from(
                "<IIIIIIII", self.data, instances - self.base + index * TAG_INSTANCE_SIZE)
            self.tags.append(Tag(index, fourcc(group), self.string(name), address))
        by_address = sorted((t for t in self.tags if self.contains(t.address)), key=lambda t: t.address)
        for position, tag in enumerate(by_address):
            end = by_address[position + 1].address if position + 1 < len(by_address) else self.base + self.tag_size
            tag.offset = tag.address - self.base
            tag.size = end - tag.address
        self.by_name = {(t.group, t.name): t for t in self.tags}
        self.groups = {t.group for t in self.tags}

    def read_tag_data(self, file) -> bytes:
        """the tag data: read as it is when the file is whole, else the file
        is the header and one zlib stream (the DVD's maps; the game
        decompresses them into its cache, cache_files_decompress_windows.c),
        decompressed only as far as the tag data"""
        file.seek(0, 2)
        if file.tell() >= self.length:
            file.seek(self.tag_offset)
            return file.read(self.tag_size)
        file.seek(HEADER_SIZE)
        inflater = zlib.decompressobj()
        wanted_end = self.tag_offset + self.tag_size - HEADER_SIZE
        produced, chunks = 0, []
        while produced < wanted_end:
            block = file.read(1 << 20)
            if not block:
                break
            chunk = inflater.decompress(block)
            chunks.append(chunk)
            produced += len(chunk)
        data = b"".join(chunks)
        start = self.tag_offset - HEADER_SIZE
        if len(data) < wanted_end:
            raise ValueError(f"{self.path}: the file ends before its tag data")
        return data[start:wanted_end]

    def contains(self, address: int) -> bool:
        return self.base <= address < self.base + self.tag_size

    def string(self, address: int) -> str:
        if not self.contains(address):
            return ""
        start = address - self.base
        return self.data[start:self.data.index(b"\0", start)].decode("ascii", "replace")

    def find(self, name: str, group: Optional[str] = None) -> Tag:
        matches = [t for t in self.tags if t.name.lower() == name.lower() and (group is None or t.group == group)]
        if not matches:
            matches = [t for t in self.tags if name.lower() in t.name.lower()]
        if len(matches) != 1:
            raise ValueError(f"{name!r} names {len(matches)} tags in {self.name}: " +
                             ", ".join(f"{t.group}:{t.name}" for t in matches[:8]))
        return matches[0]

    def references(self, tag: Tag) -> List[Tag]:
        """the tags this tag's data names, in order of appearance"""
        found: List[Tag] = []
        data = self.data[tag.offset:tag.offset + tag.size]
        for position in range(0, len(data) - 15, 4):
            group, name_address, name_length, index = struct.unpack_from("<IIiI", data, position)
            if not (0 <= name_length < 256 and name_address and self.contains(name_address)):
                continue
            group_name = fourcc(group)
            if group_name not in self.groups:
                continue
            target = self.tags[index & 0xFFFF] if (index & 0xFFFF) < len(self.tags) else None
            if target and target.group == group_name and self.string(name_address) == target.name:
                found.append(target)
        return found

    def closure(self, tag: Tag) -> List[Tag]:
        seen: Dict[int, Tag] = {}
        pending = [tag]
        while pending:
            current = pending.pop()
            if current.index in seen:
                continue
            seen[current.index] = current
            pending.extend(self.references(current))
        return sorted(seen.values(), key=lambda t: t.index)


def fourcc(value: int) -> str:
    return struct.pack(">I", value).decode("latin-1")


def size_text(size: int) -> str:
    return f"{size / 1048576:.2f} MB" if size >= 1048576 else f"{size / 1024:.1f} KB"


def summary(cache: CacheFile, tags: List[Tag]) -> None:
    groups: Dict[str, List[Tag]] = {}
    for tag in tags:
        groups.setdefault(tag.group, []).append(tag)
    print(f"  {len(tags)} tags, {size_text(sum(t.size for t in tags))} of tag data, {len(groups)} groups:")
    for group, members in sorted(groups.items(), key=lambda item: -sum(t.size for t in item[1])):
        print(f"    {group}  {len(members):4} tags  {size_text(sum(t.size for t in members)):>10}")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("map", type=Path)
    parser.add_argument("action", choices=["list", "needs", "fits"])
    parser.add_argument("values", nargs="*")
    args = parser.parse_args(argv)
    try:
        cache = CacheFile(args.map)
        if args.action == "list":
            for tag in cache.tags:
                if not args.values or args.values[0].lower() in tag.name.lower() or args.values[0] == tag.group:
                    print(f"{tag.index:5} {tag.group} {tag.size:9} {tag.name}")
        elif args.action == "needs":
            tag = cache.find(args.values[0])
            print(f"{cache.name}: {tag.group} {tag.name} needs")
            summary(cache, cache.closure(tag))
        else:
            donor = CacheFile(Path(args.values[0]))
            tag = donor.find(args.values[1])
            needed = donor.closure(tag)
            here = {(t.group, t.name) for t in cache.tags}
            missing = [t for t in needed if (t.group, t.name) not in here]
            print(f"bring {tag.group} {tag.name} from {donor.name} into {cache.name}:")
            print(f"  it needs {len(needed)} tags; {len(needed) - len(missing)} the map has already:")
            summary(donor, missing)
            free = TAG_CACHE_SIZE - cache.tag_size
            add = sum(t.size for t in missing)
            print(f"  {cache.name}'s tag data {size_text(cache.tag_size)} of the tag cache's "
                  f"{size_text(TAG_CACHE_SIZE)}: {size_text(free)} free, {size_text(add)} needed"
                  f" -> {'fits' if add <= free else 'does not fit'}")
    except (ValueError, OSError, IndexError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
