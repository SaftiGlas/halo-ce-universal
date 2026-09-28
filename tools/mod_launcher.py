"""Turn source mods on and off, build the game with them and run it.

    python -m tools.mod_launcher                  interactive: toggle, build, run
    python -m tools.mod_launcher list             the mods and whether they are on
    python -m tools.mod_launcher enable MOD ...   turn mods on
    python -m tools.mod_launcher disable MOD ...  turn mods off
    python -m tools.mod_launcher build            build with the enabled mods
    python -m tools.mod_launcher run [-- ARGS]    build, then run the game

The enabled mods are kept in mods/enabled.json. Building runs
`configure.py --mods` with the enabled mods (in name order, so a set always
builds in the same place) and the arguments the root build.ninja was
configured with, then ninja on the mod build's own ninja file (see
tools/mod_overlay.py); build.ninja and `ninja linux` are left alone.

Running sets SDL_VIDEODRIVER=x11 unless it is set already, and whatever
environment the enabled mods ask for in their mod.json ("env", for example
HALO_FORGE=1 for mods of the dev tools).
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

from .mod_overlay import MOD_INFO, MODS_DIR, ModError, available_mods, mod_set_key, read_mod_info

ENABLED_FILE = "enabled.json"
ROOT_NINJA = Path("build.ninja")


class LauncherError(Exception):
    pass


# ---------- enabled mods


def enabled_mods(mods_dir: Path = MODS_DIR) -> List[str]:
    """the enabled mods that still exist, in name order"""
    path = mods_dir / ENABLED_FILE
    if not path.is_file():
        return []
    try:
        names = json.loads(path.read_text(encoding="utf-8")).get("enabled", [])
    except (OSError, ValueError, AttributeError) as error:
        raise LauncherError(f"{path}: {error}") from None
    available = set(available_mods(mods_dir))
    return sorted(name for name in set(names) if isinstance(name, str) and name in available)


def set_enabled(names: Sequence[str], mods_dir: Path = MODS_DIR) -> None:
    path = mods_dir / ENABLED_FILE
    path.write_text(json.dumps({"enabled": sorted(set(names))}, indent=2) + "\n", encoding="utf-8")


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
    """configure and build the enabled mods; the executable"""
    mods = enabled_mods(mods_dir)
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


def run(args: Sequence[str], mods_dir: Path = MODS_DIR) -> int:
    executable = build(mods_dir)
    env = dict(os.environ)
    env.setdefault("SDL_VIDEODRIVER", "x11")
    for name in enabled_mods(mods_dir):
        for key, value in dict(mod_info(name, mods_dir).get("env") or {}).items():
            env.setdefault(str(key), str(value))
    shown = " ".join(f"{key}={env[key]}" for key in sorted(set(env) - set(os.environ) | {"SDL_VIDEODRIVER"}))
    print(f"$ {shown} {executable} {' '.join(args)}".rstrip(), flush=True)
    return subprocess.run([str(executable), *args], env=env).returncode


# ---------- the interactive launcher


def show(mods_dir: Path = MODS_DIR) -> List[str]:
    names = available_mods(mods_dir)
    enabled = set(enabled_mods(mods_dir))
    if not names:
        print(f"no mods in {mods_dir}/ (a mod is a directory with a {MOD_INFO})")
    for number, name in enumerate(names, 1):
        info = mod_info(name, mods_dir)
        mark = "x" if name in enabled else " "
        print(f"  [{mark}] {number}. {info['name']} {info['version']} ({name})")
        print(f"         {info['description']}")
    return names


def interactive(mods_dir: Path = MODS_DIR) -> int:
    while True:
        print("\nHalo source mods")
        names = show(mods_dir)
        print("\n  number: turn a mod on or off   b: build with mods   r: build and run   q: quit")
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
            elif choice.isdigit() and 1 <= int(choice) <= len(names):
                name = names[int(choice) - 1]
                change([name], name not in enabled_mods(mods_dir), mods_dir)
            elif choice:
                print(f"unknown choice {choice!r}")
        except (LauncherError, ModError) as error:
            print(f"error: {error}")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Turn source mods on and off, build with them and run the game")
    parser.add_argument("--mods-dir", type=Path, default=MODS_DIR)
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("list", help="the mods and whether they are on")
    for name, text in (("enable", "turn mods on"), ("disable", "turn mods off")):
        command = sub.add_parser(name, help=text)
        command.add_argument("mods", nargs="+")
    sub.add_parser("build", help="build with the enabled mods")
    running = sub.add_parser("run", help="build with the enabled mods, then run the game")
    running.add_argument("args", nargs=argparse.REMAINDER, help="arguments for the game (after --)")
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
            return run(game_args, args.mods_dir)
    except (LauncherError, ModError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
