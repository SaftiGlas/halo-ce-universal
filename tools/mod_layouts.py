"""Forge layouts in the save root: list them, rename and describe them.

The game saves a layout of a map (port/linux/game/forge_layout.c) to
u/forge/<map>/layout_NN.txt in the save root (NN 01 to 16), with its name
on the first line ("name Layout 3" when first saved) and, when it has one,
a "description" line in its header (what the multiplayer map list says
about it); u/forge/<map>/play.txt holds the number of the layout that plays
in every game on the map. The game's own Map tab changes both too.

    python -m tools.mod_launcher layouts [MAP]                   list them
    python -m tools.mod_launcher layouts rename MAP N NAME...    rename layout N
    python -m tools.mod_launcher layouts describe MAP N TEXT...  describe it
                                                                 (no TEXT: none)

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
DESCRIPTION_SIZE = 127
DESCRIPTION_PREFIX = "description "


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
        header = header_lines(path.read_text(errors="replace").splitlines())
        name = header[0][5:] if header and header[0].startswith("name ") and header[0][5:] else f"Layout {slot}"
        description = next((line[len(DESCRIPTION_PREFIX):] for line in header if line.startswith(DESCRIPTION_PREFIX)),
                           "")
        found.append({"slot": slot, "name": name, "description": description, "plays": slot == playing,
                      "listed": "listed 1" in header})
    return found


def header_lines(lines: List[str]) -> List[str]:
    """the lines before the layout's first part ([forge])"""
    for index, line in enumerate(lines):
        if line.startswith("["):
            return lines[:index]
    return lines


def clean_text(text: str, what: str, size: int) -> str:
    """as the game takes it: one line of plain characters, no percent signs
    (the menus would take one for a format)"""
    text = " ".join(text.split())
    if len(text) > size:
        raise LayoutError(f"the {what} is longer than {size} characters")
    if any(not " " <= character < "\x7f" or character == "%" for character in text):
        raise LayoutError(f"the {what} may only have plain (ASCII) characters, and no %")
    return text


def maps_with_layouts(save_root: Path) -> List[str]:
    root = forge_dir(save_root)
    if not root.is_dir():
        return []
    return sorted(entry.name for entry in root.iterdir() if entry.is_dir() and layouts(save_root, entry.name))


def rename(save_root: Path, map_name: str, slot: int, name: str) -> str:
    name = clean_text(name, "name", NAME_SIZE)
    if not name:
        raise LayoutError("the name is empty")
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


def describe(save_root: Path, map_name: str, slot: int, description: str) -> str:
    """sets the layout's description, the text under its name in the
    multiplayer map list; empty takes it away (the list then says which map
    the layout is made on)"""
    description = clean_text(description, "description", DESCRIPTION_SIZE)
    path = layout_path(save_root, map_name, slot)
    if not path.is_file():
        raise LayoutError(f"{map_name} has no layout {slot} ({path})")
    lines = path.read_text(errors="replace").splitlines()
    header_count = len(header_lines(lines))
    header = [line for line in lines[:header_count] if not line.startswith(DESCRIPTION_PREFIX)]
    if description:
        # after the name, where the game writes it
        header.insert(1 if header and header[0].startswith("name ") else 0, DESCRIPTION_PREFIX + description)
    path.write_text("\n".join(header + lines[header_count:]) + "\n")
    return description


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
            if layout["description"]:
                print(f"      {layout['description']}")


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
    elif action == "describe":
        if len(values) < 2:
            raise LayoutError("describe MAP N TEXT...")
        try:
            slot = int(values[1])
        except ValueError:
            raise LayoutError(f"not a layout number: {values[1]}")
        description = describe(root, values[0], slot, " ".join(values[2:]))
        print(f"{values[0]} layout {slot}: {description or '(no description)'}")
    else:
        # a map name alone lists that map's layouts
        show(root, action)
