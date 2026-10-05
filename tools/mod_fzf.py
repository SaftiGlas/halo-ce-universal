"""The mod launcher's screen, made with fzf (https://github.com/junegunn/fzf).

    python -m tools.mod_launcher          opens it when fzf is installed

One list of the mods. Space or Tab turns the mod under the cursor on or off
(the list changes at once and mods/mods.json is saved); type to search.

    Space, Tab   turn the mod on or off      Ctrl-A, Ctrl-X   all on, all off
    Enter        build and run the game      Ctrl-B           build only
    Ctrl-L       choose the map to start     Ctrl-T           tags to bring into the
    Esc          quit                                         maps from other maps

The map chosen with Ctrl-L (a campaign level with its difficulty, or a
multiplayer map with its game variant) is where the game opens, without the
menus; it is kept in mods.json until "Main menu" is chosen there.

Ctrl-T chooses a map to take tags from, and a list of its AI characters
(grunts, hunters, elites...) and scenery (rocks, trees...) where Space turns
each on or off; the game brings the ones that are on, and everything they
need, into the multiplayer maps. The AI tab of the forge menu (the forge_ai
mod) adds the characters, with waypoints for them to patrol.

fzf calls this module back to change the list, so it is also a command:

    python -m tools.mod_fzf list | toggle NAME | all on|off | preview NAME
                            | import-toggle SPEC | import-list FILE
"""

import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

from . import mod_launcher as launcher
from . import mod_maps
from .mod_overlay import OVERLAY_ROOTS, MODS_DIR, ModError, available_mods

SEPARATOR = "\t"
GREEN, DIM, BOLD, YELLOW, RESET = "\033[32m", "\033[2m", "\033[1m", "\033[33m", "\033[0m"

KEYS_HELP = (
    "space/tab: on/off   enter: build+run   ^L: map   ^T: tags from other maps   ^B: build   "
    "^A/^X: all on/off   esc: quit"
)


def available() -> bool:
    """whether the fzf screen can be shown: fzf is installed and this is a terminal"""
    return bool(shutil.which("fzf")) and sys.stdin.isatty() and sys.stdout.isatty()


# ---------- the lists fzf shows (one line for each row: key, tab, what is shown)


def mod_lines(mods_dir: Path) -> List[str]:
    enabled = set(launcher.enabled_mods(mods_dir))
    lines = []
    for name in available_mods(mods_dir):
        info = launcher.mod_info(name, mods_dir)
        on = name in enabled
        mark = f"{GREEN}[x]{RESET}" if on else f"{DIM}[ ]{RESET}"
        kind = "code" if launcher.changes_code(name, mods_dir) else "no code"
        text = f"{mark} {BOLD if on else DIM}{name:<18}{RESET} {DIM}{kind:<8}{RESET} {info['description']}"
        lines.append(f"{name}{SEPARATOR}{text}")
    return lines


def mod_preview(mods_dir: Path, name: str) -> str:
    if name not in available_mods(mods_dir):
        return ""
    info = launcher.mod_info(name, mods_dir)
    on = name in launcher.enabled_mods(mods_dir)
    out = [
        f"{BOLD}{info['name']}{RESET} {info['version']}   {GREEN + 'on' if on else DIM + 'off'}{RESET}",
        "",
        str(info["description"]),
        "",
    ]
    mod_dir = mods_dir / name
    if launcher.changes_code(name, mods_dir):
        out.append("Changes the game's code: the game is built with it (Enter).")
        patches = sorted((mod_dir / "patches").glob("*.patch")) if (mod_dir / "patches").is_dir() else []
        for patch in patches:
            out.append(f"  patch   {patch.name}")
        for root in OVERLAY_ROOTS:
            if (mod_dir / root).is_dir():
                for path in sorted(p for p in (mod_dir / root).rglob("*") if p.is_file()):
                    out.append(f"  source  {path.relative_to(mod_dir).as_posix()}")
    else:
        out.append("Changes no code: no build needed.")
    environment = dict(info.get("env") or {})
    if environment:
        out += ["", "Sets when the game runs:"] + [f"  {key}={value}" for key, value in environment.items()]
    return "\n".join(out)


def map_lines(mods_dir: Path) -> List[str]:
    launch = launcher.saved_launch(mods_dir)
    maps = mod_maps.find_maps()
    lines = [f"-{SEPARATOR}{BOLD}Main menu{RESET} {DIM}(no map: the game opens as usual){RESET}"]
    last_kind = None
    for info in maps:
        if info.kind != last_kind:
            last_kind = info.kind
            lines.append(f"#{SEPARATOR}{DIM}---- {info.kind} ----{RESET}")
        current = f"{YELLOW}*{RESET}" if launch and launch["map"] == info.name else " "
        missing = f" {DIM}(no map file){RESET}" if not info.found else ""
        lines.append(f"{info.name}{SEPARATOR}{current} {info.title:<30} {DIM}{info.name}{RESET}{missing}")
    return lines


def launch_text(mods_dir: Path) -> str:
    launch = launcher.saved_launch(mods_dir)
    imports = launcher.saved_imports(mods_dir)
    tail = f"   |   Tags from other maps: {len(imports)}" if imports else ""
    if not launch:
        return "Starts at: the main menu" + tail
    info = mod_maps.find_map(launch["map"], mod_maps.find_maps())
    text = f"{info.title} ({launch['map']})" if info else launch["map"]
    detail = launch["variant"] or launch["difficulty"]
    return f"Starts at: {text}" + (f", {detail}" if detail else "") + tail


# ---------- fzf


def python_command(mods_dir: Path, *arguments: str) -> str:
    """a shell command that runs this module, for fzf's bindings"""
    parts = [sys.executable, "-m", "tools.mod_fzf", "--mods-dir", str(mods_dir.resolve()), *arguments]
    # fzf quotes a {1} itself
    return " ".join(part if part == "{1}" else shlex.quote(part) for part in parts)


def fzf(lines: Sequence[str], options: Sequence[str], extra_environment: Optional[dict] = None) -> Tuple[int, List[str]]:
    """runs fzf on the lines; its exit status and the lines it printed"""
    environment = dict(os.environ)
    environment.update(extra_environment or {})
    process = subprocess.run(
        ["fzf", "--ansi", "--layout=reverse", "--border=rounded", "--height=100%", "--info=inline-right",
         f"--delimiter={SEPARATOR}", "--with-nth=2..", "--no-sort", "--cycle", *options],
        input="\n".join(lines) + "\n", stdout=subprocess.PIPE, text=True, env=environment,
    )
    return process.returncode, process.stdout.split("\n")[:-1] if process.stdout else []


def pick_mods(mods_dir: Path) -> Tuple[Optional[str], Optional[str]]:
    """the mod list; the key that ended it ("enter", "ctrl-b", ...) and the mod under the cursor,
    or (None, None) for Esc"""
    list_command = python_command(mods_dir, "list")
    toggle = f"execute-silent({python_command(mods_dir, 'toggle', '{1}')})+reload({list_command})"
    everything = lambda state: f"execute-silent({python_command(mods_dir, 'all', state)})+reload({list_command})"
    status, output = fzf(
        mod_lines(mods_dir),
        [
            "--prompt=mods > ",
            "--header", f"{launch_text(mods_dir)}\n{KEYS_HELP}",
            "--header-first",
            "--track", "--id-nth=1",
            "--expect=enter,ctrl-b,ctrl-l,ctrl-t",
            "--bind", f"space:{toggle}",
            "--bind", f"tab:{toggle}",
            "--bind", f"ctrl-a:{everything('on')}",
            "--bind", f"ctrl-x:{everything('off')}",
            "--preview", python_command(mods_dir, "preview", "{1}"),
            "--preview-window", "right,45%,wrap,<110(down,40%,wrap)",
        ],
    )
    if status not in (0, 1) or not output:
        return None, None
    key = output[0] or "enter"
    name = output[1].split(SEPARATOR)[0] if len(output) > 1 else None
    return key, name


def pick_one(lines: Sequence[str], prompt: str, header: str = "", preview: Optional[str] = None) -> Optional[str]:
    options = [f"--prompt={prompt} > "]
    if header:
        options += ["--header", header, "--header-first"]
    if preview:
        options += ["--preview", preview, "--preview-window", "right,40%,wrap,<100(down,30%,wrap)"]
    status, output = fzf(lines, options)
    if status != 0 or not output:
        return None
    return output[0].split(SEPARATOR)[0]


def choose_map(mods_dir: Path) -> None:
    """Ctrl-L: the map, then its game variant or difficulty; saved in mods.json"""
    while True:
        choice = pick_one(
            map_lines(mods_dir), "map",
            "Enter: start the game at this map        Esc: leave it as it is",
        )
        if choice is None:
            return
        if choice == "#":
            continue
        break
    if choice == "-":
        launcher.clear_launch(mods_dir)
        return
    info = mod_maps.find_map(choice, mod_maps.find_maps())
    if info is None:
        return
    current = launcher.saved_launch(mods_dir) or {}
    if info.kind == mod_maps.MULTIPLAYER:
        first = current.get("variant") or mod_maps.VARIANTS[0]
        order = [first] + [v for v in mod_maps.VARIANTS if v != first]
        variant = pick_one([f"{v}{SEPARATOR}{v}" for v in order], "game variant", f"{info.title}: how is it played?")
        if variant is None:
            return
        launcher.set_launch(mods_dir, info.name, variant=variant)
    else:
        first = current.get("difficulty") or "normal"
        order = [first] + [d for d in mod_maps.DIFFICULTIES if d != first]
        difficulty = pick_one([f"{d}{SEPARATOR}{d}" for d in order], "difficulty", f"{info.title}: how hard?")
        if difficulty is None:
            return
        launcher.set_launch(mods_dir, info.name, difficulty=difficulty)


def import_lines(mods_dir: Path, listing: Path) -> List[str]:
    """the rows of the tags of a donor map (a file of spec, tab, title), marked when brought in"""
    chosen = set(launcher.saved_imports(mods_dir))
    lines = []
    for row in listing.read_text(encoding="utf-8").splitlines():
        spec, _, title = row.partition(SEPARATOR)
        mark = f"{GREEN}[x]{RESET}" if spec in chosen else f"{DIM}[ ]{RESET}"
        folder, _, name = title.rpartition("\\")
        group = spec.split(":")[1] if spec.count(":") >= 2 else ""
        lines.append(f"{spec}{SEPARATOR}{mark} {DIM}{group}{RESET} {BOLD if spec in chosen else ''}{name}{RESET} "
                     f"{DIM}{folder}{RESET}")
    return lines


def choose_imports(mods_dir: Path) -> None:
    """Ctrl-T: a map to take tags from, then its AI characters and scenery to turn on and off"""
    import tempfile

    from .map_tags import CacheFile

    maps = [m for m in mod_maps.find_maps() if m.found and m.name != "ui"]
    donor = pick_one(
        [f"{m.name}{SEPARATOR}{m.title:<30} {DIM}{m.name}{RESET}" for m in maps],
        "take tags from", "Which map has the tags? (Esc: back)")
    if donor is None:
        return
    root = mod_maps.data_root()
    if root is None:
        raise launcher.LauncherError("no game data found")
    print(f"reading {donor}...", flush=True)
    try:
        cache = CacheFile(root / "maps" / f"{donor}.map")
    except (ValueError, OSError) as error:
        raise launcher.LauncherError(f"{donor}: {error}") from None
    rows = sorted(f"{donor}:{t.group}:{t.name}{SEPARATOR}{t.name}" for t in cache.tags if t.group in launcher.IMPORT_GROUPS)
    if not rows:
        raise launcher.LauncherError(f"{donor} has no AI characters or scenery")
    with tempfile.TemporaryDirectory() as folder:
        listing = Path(folder) / "tags.tsv"
        listing.write_text("\n".join(rows) + "\n", encoding="utf-8")
        list_command = python_command(mods_dir, "import-list", str(listing))
        toggle = f"execute-silent({python_command(mods_dir, 'import-toggle', '{1}')})+reload({list_command})"
        fzf(
            import_lines(mods_dir, listing),
            [
                f"--prompt={donor} > ",
                "--header", f"space/tab: bring this tag (and what it needs) into the maps   esc: done\n"
                            "actv: an AI character, added from the AI tab of the forge menu (1, the forge_ai mod);\n"
                            "scen: scenery, placed from the Scenery tab.",
                "--header-first", "--track", "--id-nth=1", "--multi=0",
                "--bind", f"space:{toggle}", "--bind", f"tab:{toggle}",
            ],
        )


def show_error(error: Exception) -> None:
    print(f"\nerror: {error}")
    try:
        input("press Enter to go back to the list ")
    except EOFError:
        pass


def screen(mods_dir: Path = MODS_DIR) -> int:
    """the fzf launcher; 0 when it is left"""
    if not available_mods(mods_dir):
        print(f"no mods in {mods_dir}/ (a mod is a directory with a mod.json)")
        return 0
    while True:
        key, _ = pick_mods(mods_dir)
        if key is None:
            return 0
        try:
            if key == "ctrl-l":
                choose_map(mods_dir)
            elif key == "ctrl-t":
                choose_imports(mods_dir)
            elif key == "ctrl-b":
                print(f"built {launcher.build(mods_dir)}")
                input("press Enter to go back to the list ")
            else:
                launcher.run([], mods_dir)
        except (launcher.LauncherError, ModError) as error:
            show_error(error)
        except (EOFError, KeyboardInterrupt):
            return 0


# ---------- what fzf calls back


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    mods_dir = MODS_DIR
    if arguments[:1] == ["--mods-dir"]:
        mods_dir, arguments = Path(arguments[1]), arguments[2:]
    command, values = (arguments[0], arguments[1:]) if arguments else ("", [])
    try:
        if command == "list":
            print("\n".join(mod_lines(mods_dir)))
        elif command == "toggle" and len(values) == 1:
            launcher.change([values[0]], values[0] not in launcher.enabled_mods(mods_dir), mods_dir)
        elif command == "all" and values in (["on"], ["off"]):
            launcher.change(available_mods(mods_dir), values[0] == "on", mods_dir)
        elif command == "preview" and len(values) == 1:
            print(mod_preview(mods_dir, values[0]))
        elif command == "import-toggle" and len(values) == 1:
            current = launcher.saved_imports(mods_dir)
            launcher.parse_import(values[0])
            launcher.set_imports(mods_dir, [x for x in current if x != values[0]] if values[0] in current
                                 else current + [values[0]])
        elif command == "import-list" and len(values) == 1:
            print("\n".join(import_lines(mods_dir, Path(values[0]))))
        else:
            print(__doc__)
            return 2
    except (launcher.LauncherError, ModError) as error:
        print(f"error: {error}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
