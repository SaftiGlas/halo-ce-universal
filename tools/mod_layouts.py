"""Forge layouts in the save root: list them and rename them.

The game saves a layout of a map (port/linux/game/forge_layout.c) to
u/forge/<map>/layout_NN.txt in the save root (NN 01 to 16), with its name
on the first line ("name Layout 3" when first saved); u/forge/<map>/play.txt
holds the number of the layout that plays in every game on the map.

    python -m tools.mod_launcher layouts [MAP]                 list them
    python -m tools.mod_launcher layouts rename MAP N NAME...  rename layout N

The save root is --saves, else HALO_SAVE_ROOT, else the game's default:
~/.local/share/halo-linux ($XDG_DATA_HOME/halo-linux), or %APPDATA%\\halo on
Windows.
"""

import os
import sys
from pathlib import Path
from typing import Dict, List, Optional

SLOT_COUNT = 16
NAME_SIZE = 63


class LayoutError(Exception):
    pass


def default_save_root() -> Path:
    if os.environ.get("HALO_SAVE_ROOT"):
        return Path(os.environ["HALO_SAVE_ROOT"])
    if sys.platform == "win32":
        return Path(os.environ.get("APPDATA", str(Path.home()))) / "halo"
    data_home = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(data_home) / "halo-linux"


def forge_dir(save_root: Path) -> Path:
    return save_root / "u" / "forge"


def layout_path(save_root: Path, map_name: str, slot: int) -> Path:
    return forge_dir(save_root) / map_name / f"layout_{slot:02d}.txt"


def play_slot(save_root: Path, map_name: str) -> Optional[int]:
    path = forge_dir(save_root) / map_name / "play.txt"
    try:
        slot = int(path.read_text().split()[0])
    except (OSError, ValueError, IndexError):
        return None
    return slot if 1 <= slot <= SLOT_COUNT else None


def layouts(save_root: Path, map_name: str) -> List[Dict[str, object]]:
    """the saved layouts of the map: slot, name, and whether it plays"""
    playing = play_slot(save_root, map_name)
    found = []
    for slot in range(1, SLOT_COUNT + 1):
        path = layout_path(save_root, map_name, slot)
        if not path.is_file():
            continue
        first = path.read_text(errors="replace").splitlines()[:1]
        header = path.read_text(errors="replace").split("\n[", 1)[0].splitlines()
        name = first[0][5:] if first and first[0].startswith("name ") and first[0][5:] else f"Layout {slot}"
        found.append({"slot": slot, "name": name, "plays": slot == playing, "listed": "listed 1" in header})
    return found


def maps_with_layouts(save_root: Path) -> List[str]:
    root = forge_dir(save_root)
    if not root.is_dir():
        return []
    return sorted(entry.name for entry in root.iterdir() if entry.is_dir() and layouts(save_root, entry.name))


def rename(save_root: Path, map_name: str, slot: int, name: str) -> str:
    name = " ".join(name.split())
    if not name:
        raise LayoutError("the name is empty")
    if len(name) > NAME_SIZE:
        raise LayoutError(f"the name is longer than {NAME_SIZE} characters")
    path = layout_path(save_root, map_name, slot)
    if not path.is_file():
        raise LayoutError(f"{map_name} has no layout {slot} ({path})")
    lines = path.read_text(errors="replace").splitlines()
    if lines and lines[0].startswith("name "):
        lines[0] = f"name {name}"
    else:
        lines.insert(0, f"name {name}")
    path.write_text("\n".join(lines) + "\n")
    return name


def show(save_root: Path, map_name: Optional[str] = None) -> None:
    names = [map_name] if map_name else maps_with_layouts(save_root)
    if not names:
        print(f"no layouts in {forge_dir(save_root)}")
        return
    for name in names:
        print(f"{name}:")
        found = layouts(save_root, name)
        if not found:
            print("  (none)")
        for layout in found:
            notes = [text for text, on in (("in the map list", layout["listed"]), ("plays on this map", layout["plays"]))
                     if on]
            print(f"  {layout['slot']:2d}. {layout['name']}" + (f"  ({', '.join(notes)})" if notes else ""))


def command(action: Optional[str], values: List[str], save_root: Optional[Path]) -> None:
    root = save_root or default_save_root()
    if action in (None, "list"):
        show(root, values[0] if values else None)
    elif action == "rename":
        if len(values) < 3:
            raise LayoutError("rename MAP N NAME...")
        try:
            slot = int(values[1])
        except ValueError:
            raise LayoutError(f"not a layout number: {values[1]}")
        print(f"{values[0]} layout {slot}: {rename(root, values[0], slot, ' '.join(values[2:]))}")
    else:
        # a map name alone lists that map's layouts
        show(root, action)
