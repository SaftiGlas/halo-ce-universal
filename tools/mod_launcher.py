"""Turn mods on and off, choose the map to start at, build and run.

    python -m tools.mod_launcher                  interactive (fzf when installed): toggle, map, run
    python -m tools.mod_launcher list             the mods and whether they are on
    python -m tools.mod_launcher enable MOD ...   turn mods on
    python -m tools.mod_launcher disable MOD ...  turn mods off
    python -m tools.mod_launcher build            build with the enabled mods
    python -m tools.mod_launcher run [--map MAP] [-- ARGS]
                                                  build, then run the game (at a map)
    python -m tools.mod_launcher map [MAP]        the map the game starts at: show, set or none
    python -m tools.mod_launcher maps             the maps of the game data
    python -m tools.mod_launcher import ...       tags to bring into the maps from other maps

Everything is kept in mods/mods.json (an older mods/enabled.json is taken
over the first time):

    {"enabled": [...mods...],
     "launch": {"map": "bloodgulch", "variant": "slayer"},
     "import": ["a30:scen:scenery\\rocks\\boulder_granite_large\\boulder_granite_large"],
     "import_into": ["bloodgulch"]}

Building runs `configure.py --mods` with the enabled mods that change code
(in name order, so a set always builds in the same place; mods without
code need no build) and the arguments the root build.ninja was
configured with, then ninja on the mod build's own ninja file (see
tools/mod_overlay.py); build.ninja and `ninja linux` are left alone.

A "launch" map makes the game open at that map, without the menus: a campaign
level ("a10", with an optional "difficulty") or a multiplayer map ("bloodgulch",
with a game variant, slayer unless one is given), played as a local game.
`run --map` does it for one run; `run --menu` opens the menus once instead. The
game reads HALO_START_MAP, HALO_START_VARIANT and HALO_START_DIFFICULTY (the
settings game.start_map, ... of config.toml; port/linux/game/start_map.c).

"import" lists tags (donor map, tag group, name) that the game brings into
the multiplayer maps (or "import_into": those maps) from the donor maps:
the tag and everything it refers to, so a rock of Halo's first level can
stand in Blood Gulch and be placed from the forge menu. Scenery works so
far; see mods/FORGE_PLAN.md. The game reads HALO_IMPORT and HALO_IMPORT_INTO
(game.import and game.import_into in config.toml;
port/linux/game/tag_import.c). Each map load reads the donor map, which
takes a second or two.

Running sets SDL_VIDEODRIVER=x11 unless it is set already, and whatever environment the enabled mods ask for in
their mod.json ("env", for example HALO_FORGE=1 for mods of the dev tools).
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

from . import mod_maps
from .mod_overlay import MOD_INFO, MODS_DIR, OVERLAY_ROOTS, ModError, available_mods, mod_set_key, read_mod_info

SETTINGS_FILE = "mods.json"
OLD_ENABLED_FILE = "enabled.json"
ROOT_NINJA = Path("build.ninja")


class LauncherError(Exception):
    pass


# ---------- mods.json


def load_settings(mods_dir: Path = MODS_DIR) -> Dict[str, Any]:
    """mods.json, or what enabled.json held, or nothing"""
    for name in (SETTINGS_FILE, OLD_ENABLED_FILE):
        path = mods_dir / name
        if path.is_file():
            try:
                settings = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError) as error:
                raise LauncherError(f"{path}: {error}") from None
            if not isinstance(settings, dict):
                raise LauncherError(f"{path}: expected an object")
            return settings
    return {}


def save_settings(settings: Dict[str, Any], mods_dir: Path = MODS_DIR) -> None:
    path = mods_dir / SETTINGS_FILE
    temporary = path.with_name(f".{path.name}.tmp{os.getpid()}")
    temporary.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


# ---------- enabled mods


def enabled_mods(mods_dir: Path = MODS_DIR) -> List[str]:
    """the enabled mods that still exist, in name order"""
    names = load_settings(mods_dir).get("enabled", [])
    available = set(available_mods(mods_dir))
    return sorted(name for name in set(names) if isinstance(name, str) and name in available)


def set_enabled(names: Sequence[str], mods_dir: Path = MODS_DIR) -> None:
    settings = load_settings(mods_dir)
    settings["enabled"] = sorted(set(names))
    save_settings(settings, mods_dir)


def change(names: Sequence[str], enable: bool, mods_dir: Path = MODS_DIR) -> List[str]:
    available = available_mods(mods_dir)
    unknown = [name for name in names if name not in available]
    if unknown:
        raise LauncherError(
            "no such mod: " + ", ".join(unknown) + f" (mods in {mods_dir}: " + (", ".join(available) or "none") + ")"
        )
    current = set(enabled_mods(mods_dir))
    current = current | set(names) if enable else current - set(names)
    set_enabled(sorted(current), mods_dir)
    return sorted(current)


def mod_info(name: str, mods_dir: Path = MODS_DIR) -> Dict[str, object]:
    """mod.json, or a stand-in naming what is wrong with it"""
    try:
        info: Dict[str, object] = dict(read_mod_info(mods_dir / name))
        raw = json.loads((mods_dir / name / MOD_INFO).read_text(encoding="utf-8"))
        info["env"] = raw.get("env", {})
        return info
    except ValueError as error:
        return {"name": name, "version": "?", "description": f"(broken: {error})", "env": {}}


def changes_code(name: str, mods_dir: Path = MODS_DIR) -> bool:
    """whether the mod changes the game's code (else it needs no build)"""
    mod_dir = mods_dir / name
    return (mod_dir / "patches").is_dir() or any((mod_dir / root).is_dir() for root in OVERLAY_ROOTS)


# ---------- the map the game starts at


def saved_launch(mods_dir: Path = MODS_DIR) -> Optional[Dict[str, str]]:
    """mods.json's "launch": map, variant and difficulty (empty when they do
    not apply), or None for the main menu"""
    launch = load_settings(mods_dir).get("launch")
    if not isinstance(launch, dict) or not isinstance(launch.get("map"), str) or not launch["map"]:
        return None
    return {key: str(launch.get(key) or "") for key in ("map", "variant", "difficulty")}


def choose_launch(
    name: str, variant: Optional[str] = None, difficulty: Optional[str] = None,
    maps: Optional[Sequence[mod_maps.MapInfo]] = None,
) -> Dict[str, str]:
    """checks a map (by name, title or scenario path) with its game variant
    (multiplayer, slayer unless given) or difficulty (campaign)"""
    maps = mod_maps.find_maps() if maps is None else maps
    info = mod_maps.find_map(name, maps)
    if info is None:
        raise LauncherError(f"no map {name!r} (maps: " + ", ".join(m.name for m in maps) + ")")
    if info.kind == mod_maps.MULTIPLAYER:
        if difficulty:
            raise LauncherError(f"{info.name} is a multiplayer map: it has a game variant, not a difficulty")
        variant = variant or mod_maps.VARIANTS[0]
        if variant not in mod_maps.VARIANTS:
            raise LauncherError(f"no game variant {variant!r} (variants: " + ", ".join(mod_maps.VARIANTS) + ")")
        return {"map": info.name, "variant": variant, "difficulty": ""}
    if variant:
        raise LauncherError(f"{info.name} is a campaign level: it has a difficulty, not a game variant")
    if difficulty and difficulty not in mod_maps.DIFFICULTIES:
        raise LauncherError(f"no difficulty {difficulty!r} (difficulties: " + ", ".join(mod_maps.DIFFICULTIES) + ")")
    return {"map": info.name, "variant": "", "difficulty": difficulty or ""}


def set_launch(
    mods_dir: Path, name: str, variant: Optional[str] = None, difficulty: Optional[str] = None,
) -> Dict[str, str]:
    launch = choose_launch(name, variant, difficulty)
    settings = load_settings(mods_dir)
    settings["launch"] = {key: value for key, value in launch.items() if value}
    save_settings(settings, mods_dir)
    return launch


def clear_launch(mods_dir: Path = MODS_DIR) -> None:
    settings = load_settings(mods_dir)
    if settings.pop("launch", None) is not None:
        save_settings(settings, mods_dir)


def launch_environment(launch: Optional[Dict[str, str]]) -> Dict[str, str]:
    """what tells the game where to start (none: the main menu)"""
    if not launch:
        return {}
    environment = {"HALO_START_MAP": launch["map"]}
    if launch.get("variant"):
        environment["HALO_START_VARIANT"] = launch["variant"]
    if launch.get("difficulty"):
        environment["HALO_START_DIFFICULTY"] = launch["difficulty"]
    return environment


def describe_launch(launch: Optional[Dict[str, str]]) -> str:
    if not launch:
        return "the main menu"
    info = mod_maps.find_map(launch["map"], mod_maps.find_maps())
    detail = launch.get("variant") or launch.get("difficulty")
    return (f"{info.title} ({info.name})" if info else launch["map"]) + (f", {detail}" if detail else "")


def show_maps() -> None:
    root = mod_maps.data_root()
    if root is None:
        print("no game data found (a folder with maps/ here, in assets/, or HALO_DATA_ROOT): the game's own levels")
    for info in mod_maps.find_maps(root):
        print(f"  {info.kind:<11} {info.name:<15} {info.title}")


# ---------- tags brought in from other maps


# what the fzf list offers to bring in: AI characters (actor variants, with their bodies, animations,
# sounds and weapons) and scenery. Any other group can be named on the command line (--group);
# a biped alone has no AI and lies asleep (mods/FORGE_PLAN.md)
IMPORT_GROUPS = {"actv": "AI characters", "scen": "scenery"}


def parse_import(spec: str) -> Tuple[str, str, str]:
    """"a30:scen:scenery\\rocks\\rock" into donor, group, tag name"""
    parts = spec.split(":", 2)
    if len(parts) != 3 or not parts[0] or len(parts[1]) != 4 or not parts[2]:
        raise LauncherError(f"{spec!r} is not donor:group:tag name (for example a30:scen:scenery\\rocks\\boulder)")
    return parts[0], parts[1], parts[2]


def saved_imports(mods_dir: Path = MODS_DIR) -> List[str]:
    """mods.json's "import": tags to bring into the maps, as donor:group:tag name"""
    imports = load_settings(mods_dir).get("import")
    if not isinstance(imports, list):
        return []
    valid = []
    for spec in imports:
        try:
            if isinstance(spec, str):
                parse_import(spec)
                valid.append(spec)
        except LauncherError:
            pass
    return valid


def saved_import_maps(mods_dir: Path = MODS_DIR) -> List[str]:
    """the maps the imports go into: mods.json's "import_into", else every multiplayer map"""
    names = load_settings(mods_dir).get("import_into")
    if isinstance(names, list) and names:
        return [str(name) for name in names]
    return [m.name for m in mod_maps.find_maps() if m.kind == mod_maps.MULTIPLAYER]


def set_imports(mods_dir: Path, specs: Sequence[str]) -> List[str]:
    settings = load_settings(mods_dir)
    kept = list(dict.fromkeys(specs))  # no repeats, in order
    if kept:
        settings["import"] = kept
    else:
        settings.pop("import", None)
    save_settings(settings, mods_dir)
    return kept


def find_importable(donor: str, group: str, name: str, maps: Optional[Sequence[mod_maps.MapInfo]] = None) -> str:
    """the spec of the one tag a name (or part of one) means in the donor map's tags of that group"""
    from .map_tags import CacheFile

    maps = mod_maps.find_maps() if maps is None else maps
    info = mod_maps.find_map(donor, maps)
    root = mod_maps.data_root()
    if info is None or root is None:
        raise LauncherError(f"no map {donor!r} (maps: " + ", ".join(m.name for m in maps) + ")")
    try:
        cache = CacheFile(root / "maps" / f"{info.name}.map")
        tags = [t for t in cache.tags if t.group == group]
        exact = [t for t in tags if t.name.lower() == name.lower()]
        matches = exact or [t for t in tags if name.lower() in t.name.lower()]
    except (ValueError, OSError) as error:
        raise LauncherError(f"{donor}: {error}") from None
    if len(matches) != 1:
        raise LauncherError(
            f"{name!r} names {len(matches)} {group} tags in {info.name}"
            + (": " + ", ".join(t.name for t in matches[:6]) if matches else ""))
    return f"{info.name}:{group}:{matches[0].name}"


def import_environment(mods_dir: Path = MODS_DIR) -> Dict[str, str]:
    """what tells the game which tags to bring in, and into which maps"""
    specs = saved_imports(mods_dir)
    if not specs:
        return {}
    return {"HALO_IMPORT": "|".join(specs), "HALO_IMPORT_INTO": ",".join(saved_import_maps(mods_dir))}


def import_command(args: argparse.Namespace) -> None:
    mods_dir = args.mods_dir
    action, values = args.action, args.values
    if action == "add":
        if len(values) < 2:
            raise LauncherError("usage: import add DONOR-MAP TAG [TAG ...] (a name or part of one; --group scen)")
        specs = saved_imports(mods_dir) + [find_importable(values[0], args.group, name) for name in values[1:]]
        set_imports(mods_dir, specs)
    elif action == "remove":
        current = saved_imports(mods_dir)
        gone = [s for s in current if any(v.lower() in s.lower() for v in values)]
        if not values or not gone:
            raise LauncherError("usage: import remove TAG [TAG ...] (part of the tag's name); none matched")
        set_imports(mods_dir, [s for s in current if s not in gone])
    elif action == "clear":
        set_imports(mods_dir, [])
    elif action == "into":
        settings = load_settings(mods_dir)
        if values in ([], ["all"]):
            settings.pop("import_into", None)
        else:
            maps = mod_maps.find_maps()
            names = []
            for value in values:
                info = mod_maps.find_map(value, maps)
                if info is None:
                    raise LauncherError(f"no map {value!r}")
                names.append(info.name)
            settings["import_into"] = names
        save_settings(settings, mods_dir)
    elif action == "list":
        if not values:
            raise LauncherError("usage: import list DONOR-MAP [FILTER]")
        from .map_tags import CacheFile

        info = mod_maps.find_map(values[0], mod_maps.find_maps())
        root = mod_maps.data_root()
        if info is None or root is None:
            raise LauncherError(f"no map {values[0]!r}")
        current = set(saved_imports(mods_dir))
        for tag in CacheFile(root / "maps" / f"{info.name}.map").tags:
            if tag.group == args.group and (len(values) < 2 or values[1].lower() in tag.name.lower()):
                print(f"  [{'x' if f'{info.name}:{tag.group}:{tag.name}' in current else ' '}] {tag.name}")
        return
    show_imports(mods_dir)


def show_imports(mods_dir: Path = MODS_DIR) -> None:
    specs = saved_imports(mods_dir)
    if not specs:
        print("no tags are brought in from other maps (import add DONOR TAG)")
        return
    print(f"{len(specs)} tag(s) brought into " + ", ".join(saved_import_maps(mods_dir)) + ":")
    for spec in specs:
        donor, group, name = parse_import(spec)
        print(f"  {donor:<12} {group}  {name}")


# ---------- build and run


def root_configure_args() -> List[str]:
    """the arguments build.ninja was configured with, without --mods"""
    if not ROOT_NINJA.is_file():
        return []
    match = re.search(r"^configure_args = (.*)$", ROOT_NINJA.read_text(encoding="utf-8"), re.MULTILINE)
    args = shlex.split(match.group(1)) if match else []
    kept: List[str] = []
    skipping = False
    for arg in args:
        if arg in ("--mods", "--mods-dir"):
            skipping = True
            continue
        if skipping and arg.startswith("-"):
            skipping = False
        if not skipping:
            kept.append(arg)
    return kept


def build(mods_dir: Path = MODS_DIR) -> Path:
    """configure and build the enabled mods that change code; the executable"""
    mods = [name for name in enabled_mods(mods_dir) if changes_code(name, mods_dir)]
    command = [sys.executable, "configure.py", *root_configure_args(), "--mods-dir", str(mods_dir), "--mods", *mods]
    print("$ " + " ".join(shlex.quote(arg) for arg in command), flush=True)
    if subprocess.run(command).returncode != 0:
        raise LauncherError("configure.py failed (see above)")
    root = Path("build") / "mods" / mod_set_key(mods, mods_dir)
    command = ["ninja", "-f", str(root / "build.ninja")]
    print("$ " + " ".join(command), flush=True)
    if subprocess.run(command).returncode != 0:
        raise LauncherError("the build failed (see above)")
    return root / "halo"


def run(args: Sequence[str], mods_dir: Path = MODS_DIR, launch: Optional[Dict[str, str]] = None,
        use_saved_launch: bool = True) -> int:
    """builds and runs the game, at the map of launch (else mods.json's, unless
    use_saved_launch is False)"""
    if launch is None and use_saved_launch:
        launch = saved_launch(mods_dir)
    executable = build(mods_dir)
    env = dict(os.environ)
    env.update(launch_environment(launch))
    env.update(import_environment(mods_dir))
    print(f"starting at: {describe_launch(launch)}", flush=True)
    if saved_imports(mods_dir):
        print(f"bringing in {len(saved_imports(mods_dir))} tag(s) from other maps", flush=True)
    env.setdefault("SDL_VIDEODRIVER", "x11")
    for name in enabled_mods(mods_dir):
        for key, value in dict(mod_info(name, mods_dir).get("env") or {}).items():
            env.setdefault(str(key), str(value))
    shown = " ".join(f"{key}={env[key]}" for key in sorted(
        {key for key in env if os.environ.get(key) != env[key]} | {"SDL_VIDEODRIVER"}))
    print(f"$ {shown} {executable} {' '.join(args)}".rstrip(), flush=True)
    return subprocess.run([str(executable), *args], env=env).returncode


# ---------- the interactive launcher


def show(mods_dir: Path = MODS_DIR) -> List[str]:
    names = available_mods(mods_dir)
    enabled = set(enabled_mods(mods_dir))
    if not names:
        print(f"no mods in {mods_dir}/ (a mod is a directory with a {MOD_INFO})")
    print(f"  starts at: {describe_launch(saved_launch(mods_dir))}")
    for number, name in enumerate(names, 1):
        info = mod_info(name, mods_dir)
        mark = "x" if name in enabled else " "
        kind = "" if changes_code(name, mods_dir) else " [no code]"
        print(f"  [{mark}] {number}. {info['name']} {info['version']} ({name}){kind}")
        print(f"         {info['description']}")
    return names


def interactive_map(mods_dir: Path) -> None:
    """choose the map to start at from the terminal"""
    maps = mod_maps.find_maps()
    for number, info in enumerate(maps, 1):
        print(f"  {number:2}. {info.kind:<11} {info.title} ({info.name})")
    choice = input("map number (0: main menu, empty: keep) > ").strip()
    if not choice:
        return
    if choice == "0":
        clear_launch(mods_dir)
    elif choice.isdigit() and 1 <= int(choice) <= len(maps):
        info = maps[int(choice) - 1]
        if info.kind == mod_maps.MULTIPLAYER:
            variant = input(f"game variant ({', '.join(mod_maps.VARIANTS)}; empty: slayer) > ").strip()
            set_launch(mods_dir, info.name, variant=variant or None)
        else:
            difficulty = input(f"difficulty ({', '.join(mod_maps.DIFFICULTIES)}; empty: normal) > ").strip()
            set_launch(mods_dir, info.name, difficulty=difficulty or None)
    else:
        print(f"unknown choice {choice!r}")


def interactive(mods_dir: Path = MODS_DIR) -> int:
    from . import mod_fzf

    if mod_fzf.available():
        return mod_fzf.screen(mods_dir)
    if sys.stdin.isatty() and sys.stdout.isatty():
        print("(install fzf for a screen where mods are turned on and off with one key)")
    while True:
        print("\nHalo mods")
        names = show(mods_dir)
        print("\n  number: turn a mod on or off   m: map to start at"
              "   b: build with mods   r: build and run   q: quit")
        try:
            choice = input("> ").strip().lower()
        except EOFError:
            return 0
        try:
            if choice in ("q", "quit", "exit"):
                return 0
            if choice == "b":
                print(f"built {build(mods_dir)}")
            elif choice == "r":
                run([], mods_dir)
            elif choice == "m":
                interactive_map(mods_dir)
            elif choice.isdigit() and 1 <= int(choice) <= len(names):
                name = names[int(choice) - 1]
                change([name], name not in enabled_mods(mods_dir), mods_dir)
            elif choice:
                print(f"unknown choice {choice!r}")
        except (LauncherError, ModError) as error:
            print(f"error: {error}")
        except EOFError:
            return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Turn mods on and off, build and run the game")
    parser.add_argument("--mods-dir", type=Path, default=MODS_DIR)
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("list", help="the mods and whether they are on")
    for name, text in (("enable", "turn mods on"), ("disable", "turn mods off")):
        command = sub.add_parser(name, help=text)
        command.add_argument("mods", nargs="+")
    sub.add_parser("build", help="build with the enabled mods")
    running = sub.add_parser("run", help="build with the enabled mods, then run the game")
    running.add_argument("--map", help="start at this map (see `maps`) without the menus, for this run")
    running.add_argument("--variant", help="with a multiplayer --map: the game variant (default slayer)")
    running.add_argument("--difficulty", help="with a campaign --map: easy, normal, hard or impossible")
    running.add_argument("--menu", action="store_true", help="open the main menu, not the saved map")
    running.add_argument("args", nargs=argparse.REMAINDER, help="arguments for the game (after --)")
    mapping = sub.add_parser("map", help="the map the game starts at (kept in mods.json)")
    mapping.add_argument("name", nargs="?", help="a map (see `maps`), or none for the main menu; without: show")
    mapping.add_argument("--variant", help="with a multiplayer map: the game variant (default slayer)")
    mapping.add_argument("--difficulty", help="with a campaign map: easy, normal, hard or impossible")
    sub.add_parser("maps", help="the maps of the game data")
    importing = sub.add_parser(
        "import",
        help="tags to bring into the maps from other maps",
        description="show | add DONOR-MAP TAG ... | remove TAG ... | clear | list DONOR-MAP [FILTER] | into MAP ...|all")
    importing.add_argument("action", nargs="?", default="show",
                           choices=["show", "add", "remove", "clear", "list", "into"])
    importing.add_argument("values", nargs="*")
    importing.add_argument("--group", default="scen", help="the tag group (default scen, scenery)")
    args = parser.parse_args(argv)
    try:
        if args.command is None:
            return interactive(args.mods_dir)
        if args.command == "list":
            show(args.mods_dir)
        elif args.command in ("enable", "disable"):
            enabled = change(args.mods, args.command == "enable", args.mods_dir)
            print("enabled: " + (", ".join(enabled) or "none"))
        elif args.command == "build":
            print(f"built {build(args.mods_dir)}")
        elif args.command == "run":
            game_args = args.args[1:] if args.args[:1] == ["--"] else args.args
            if args.menu and (args.map or args.variant or args.difficulty):
                raise LauncherError("--menu and --map cannot be used together")
            if args.map:
                launch = choose_launch(args.map, args.variant, args.difficulty)
            elif args.variant or args.difficulty:
                raise LauncherError("--variant and --difficulty go with --map")
            else:
                launch = None
            return run(game_args, args.mods_dir, launch, use_saved_launch=not args.menu)
        elif args.command == "map":
            if args.name is None:
                print(f"starts at: {describe_launch(saved_launch(args.mods_dir))}")
            elif args.name.lower() in ("none", "menu"):
                clear_launch(args.mods_dir)
                print("starts at: the main menu")
            else:
                print(f"starts at: {describe_launch(set_launch(args.mods_dir, args.name, args.variant, args.difficulty))}")
        elif args.command == "maps":
            show_maps()
        elif args.command == "import":
            import_command(args)
    except (LauncherError, ModError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
