"""The maps the game can start with, for the mod launcher's --map.

The game data folder (the one that holds maps/) has one .map cache file for
each level. Its header says what kind it is (solo, multiplayer or the
menu's own), so the list needs no table of names; the tables below only give
the levels their titles and the campaign its order.

    python -m tools.mod_maps          list the maps found
"""

import os
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence

CAMPAIGN = "campaign"
MULTIPLAYER = "multiplayer"

# in the order of the campaign
CAMPAIGN_TITLES = {
    "a10": "The Pillar of Autumn",
    "a30": "Halo",
    "a50": "Truth and Reconciliation",
    "b30": "The Silent Cartographer",
    "b40": "Assault on the Control Room",
    "c10": "343 Guilty Spark",
    "c20": "The Library",
    "c40": "Two Betrayals",
    "d20": "Keyes",
    "d40": "The Maw",
}

MULTIPLAYER_TITLES = {
    "beavercreek": "Battle Creek",
    "bloodgulch": "Blood Gulch",
    "boardingaction": "Boarding Action",
    "carousel": "Derelict",
    "chillout": "Chill Out",
    "damnation": "Damnation",
    "hangemhigh": "Hang 'Em High",
    "longest": "Longest",
    "prisoner": "Prisoner",
    "putput": "Chiron TL-34",
    "ratrace": "Rat Race",
    "sidewinder": "Sidewinder",
    "wizard": "Wizard",
}

# the built-in game variants (game_engine_get_variant_by_name), the first
# being the usual one
VARIANTS = (
    "slayer", "team_slayer", "ctf", "king", "team_king", "oddball", "team_oddball",
    "race", "team_race", "rally", "elimination", "stalker", "accumulation", "ironctf",
    "forge",
)
DIFFICULTIES = ("easy", "normal", "hard", "impossible")

# the .map header (source/cache/cache_files.h): 'head' at 0, the level's name
# at 0x20, and at 0x60 the kind of level
HEADER_SIZE = 0x64
HEADER_SIGNATURE = b"daeh"
NAME_OFFSET = 0x20
NAME_SIZE = 0x20
KIND_OFFSET = 0x60
KIND_VALUES = {0: CAMPAIGN, 1: MULTIPLAYER}  # 2 is the menu, not a level to play


@dataclass(frozen=True)
class MapInfo:
    name: str  # what --map takes: "bloodgulch"
    title: str
    kind: str  # CAMPAIGN or MULTIPLAYER
    found: bool  # a .map file for it exists


def data_root(environment: Optional[Dict[str, str]] = None, cwd: Optional[Path] = None) -> Optional[Path]:
    """the folder with maps/ as the game finds it: HALO_DATA_ROOT, the
    current folder, then assets/ (paths.data of config.toml, the game's own
    setting, is only known to the game; set HALO_DATA_ROOT for the launcher)"""
    environment = os.environ if environment is None else environment
    cwd = Path.cwd() if cwd is None else cwd
    candidates = []
    if environment.get("HALO_DATA_ROOT"):
        candidates.append(Path(environment["HALO_DATA_ROOT"]))
    candidates += [cwd, cwd / "assets"]
    for candidate in candidates:
        if any(entry.name.lower() == "maps" and entry.is_dir() for entry in _entries(candidate)):
            return candidate
    return None


def _entries(directory: Path) -> List[Path]:
    try:
        return list(directory.iterdir())
    except OSError:
        return []


def read_kind(path: Path) -> Optional[str]:
    """CAMPAIGN or MULTIPLAYER by the .map file's header; None for the menu's
    map, or a file that is not a map"""
    try:
        with open(path, "rb") as file:
            header = file.read(HEADER_SIZE)
    except OSError:
        return None
    if len(header) < HEADER_SIZE or header[:4] != HEADER_SIGNATURE:
        return None
    return KIND_VALUES.get(struct.unpack_from("<I", header, KIND_OFFSET)[0])


def title_of(name: str) -> str:
    return CAMPAIGN_TITLES.get(name) or MULTIPLAYER_TITLES.get(name) or name


def _sort_key(info: MapInfo):
    if info.kind == CAMPAIGN:
        order = list(CAMPAIGN_TITLES)
        return (0, order.index(info.name) if info.name in order else len(order), info.name)
    return (1, 0, info.title.lower())


def find_maps(root: Optional[Path] = None) -> List[MapInfo]:
    """the maps in the data folder's maps/, the campaign in its order, then
    the multiplayer maps by title. Without data, the game's own levels, so
    that a choice can still be made"""
    root = data_root() if root is None else root
    maps: List[MapInfo] = []
    if root is not None:
        folder = next((e for e in _entries(root) if e.name.lower() == "maps" and e.is_dir()), None)
        for path in sorted(_entries(folder)) if folder else []:
            if path.suffix.lower() != ".map" or not path.is_file():
                continue
            kind = read_kind(path)
            if kind:
                name = path.stem.lower()
                maps.append(MapInfo(name, title_of(name), kind, True))
    if not maps:
        maps = [MapInfo(name, title, CAMPAIGN, False) for name, title in CAMPAIGN_TITLES.items()]
        maps += [MapInfo(name, title, MULTIPLAYER, False) for name, title in MULTIPLAYER_TITLES.items()]
    return sorted(maps, key=_sort_key)


def find_map(name: str, maps: Sequence[MapInfo]) -> Optional[MapInfo]:
    """a map by its name ("bloodgulch"), its title ("Blood Gulch") or its
    scenario path (levels\\test\\bloodgulch\\bloodgulch), any case"""
    wanted = name.replace("/", "\\").rstrip("\\").split("\\")[-1].lower()
    wanted_title = name.strip().lower()
    for info in maps:
        if info.name == wanted or info.title.lower() == wanted_title:
            return info
    return None


def main() -> int:
    root = data_root()
    print(f"game data: {root or 'not found (set HALO_DATA_ROOT to the folder that holds maps/)'}")
    for info in find_maps(root):
        print(f"  {info.kind:<11} {info.name:<15} {info.title}" + ("" if info.found else "  (no file)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
