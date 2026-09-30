"""Source mods for the native Linux build.

A mod is a directory mods/<name>/ with

  mod.json          {"name": ..., "version": ..., "description": ...}
                    (required)
  source/...        files at the same path as the game's source/ replace
  port/linux/game/...
                    them (the game's source/, and the native ports' game
                    units, such as the dev tools' forge.c); files at new
                    paths are added (new .c units are compiled into the game)
  patches/*.patch   unified diffs against the original files (-p1, paths
                    source/... or port/linux/game/...), applied in name order

For an ordered set of enabled mods, `configure.py --mods a b` writes
build/mods/<key>/build.ninja, which builds build/mods/<key>/halo with the
same rules as `ninja linux` (tools/linux_build.py) from build/mods/<key>/tree:
a mirror of the repository's source/ and port/linux/game/ made of symbolic
links to the original files and to mod files, and of the patched copies the
build writes. Units in directories without mod files are compiled from the
repository itself, so with no mods the result is `ninja linux`'s, byte for
byte. The repository is never written, and the root build.ninja (the
byte-matching build and `ninja linux`) knows nothing of mods.

Compiling from a mirrored tree rather than just putting mod directories first
on the include path matters: `#include "x.h"` looks next to the including file
first, so an unmodified unit next to a replaced header would otherwise still
see the original.

Rules: each file may be changed (replaced, added or patched) by one mod
only; two mods changing the same file is an error naming both, so enabled
mods never depend on each other's order. Code several mods need belongs in
the ports (port/linux/include/halo_mod.h: hooks that run a mod's own unit
every frame, and the keyboard), not in a file they all patch. Within one
mod, a file it replaces wins over its own patches of it, which are skipped
with a warning; its patches of one file apply in patch name order; a patch
that does not apply exactly (no fuzz) fails the build naming the mod and
file.

Source mods are native code run with the user's rights: only use trusted
ones.

Usage:
  python -m tools.mod_overlay describe [mod ...]   what each mod changes (JSON)
  python -m tools.mod_overlay list                 the mods in mods/
  python -m tools.mod_overlay patch ...            (a build step; see below)
"""

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Set, Tuple

MODS_DIR = Path("mods")
MODS_BUILD_DIR = Path("build") / "mods"
SOURCE_ROOT = Path("source")
# what mods may change: the game's sources and the ports' game units
OVERLAY_ROOTS = (SOURCE_ROOT, Path("port") / "linux" / "game")
MOD_INFO = "mod.json"
MOD_INFO_FIELDS = ("name", "version", "description")


class ModError(Exception):
    """a mod set that cannot be built"""


# ---------- patches


HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


@dataclass
class FilePatch:
    """the part of a patch file that changes one file"""

    target: str  # source/..., forward slashes
    creates: bool  # the original is /dev/null
    text: str  # the ---/+++ header and its hunks


def _patch_path(header: str) -> Optional[str]:
    """the file a ---/+++ line names, without a/ or b/ (-p1); None for
    /dev/null"""
    name = header[4:].rstrip("\n").split("\t")[0].strip()
    if name.startswith('"') and name.endswith('"'):
        name = name[1:-1]
    if name == "/dev/null":
        return None
    parts = name.split("/", 1)
    if len(parts) != 2:
        raise ValueError(f"{name}: expected a/... or b/... (a -p1 path)")
    return parts[1]


def parse_patch(text: str) -> List[FilePatch]:
    """split a unified diff into its files; hunks are counted so that
    removed lines that look like headers (`--- x`) are not mistaken for them"""
    lines = text.splitlines(keepends=True)
    result: List[FilePatch] = []
    i = 0
    while i < len(lines):
        if not (lines[i].startswith("--- ") and i + 1 < len(lines) and lines[i + 1].startswith("+++ ")):
            i += 1
            continue
        old = _patch_path(lines[i])
        new = _patch_path(lines[i + 1])
        if new is None:
            raise ValueError(f"{old}: patches may not delete files")
        chunk = [lines[i], lines[i + 1]]
        i += 2
        while i < len(lines):
            match = HUNK.match(lines[i])
            if not match:
                break
            old_count = 1 if match.group(2) is None else int(match.group(2))
            new_count = 1 if match.group(4) is None else int(match.group(4))
            chunk.append(lines[i])
            i += 1
            while (old_count > 0 or new_count > 0) and i < len(lines):
                line = lines[i]
                if line.startswith("\\"):  # \ No newline at end of file
                    chunk.append(line)
                    i += 1
                    continue
                kind = line[:1]
                if kind in (" ", "\n", "\r"):
                    old_count -= 1
                    new_count -= 1
                elif kind == "-":
                    old_count -= 1
                elif kind == "+":
                    new_count -= 1
                else:
                    raise ValueError(f"{new}: malformed hunk line {line!r}")
                chunk.append(line)
                i += 1
            if old_count > 0 or new_count > 0:
                raise ValueError(f"{new}: hunk ends early")
            while i < len(lines) and lines[i].startswith("\\"):
                chunk.append(lines[i])
                i += 1
        if old is not None and old != new:
            raise ValueError(f"{old} -> {new}: patches may not rename files")
        result.append(FilePatch(target=new, creates=old is None, text="".join(chunk)))
    if not result:
        raise ValueError("no file changes found (expected a unified diff)")
    return result


def read_patch(path: Path) -> List[FilePatch]:
    return parse_patch(path.read_text(encoding="utf-8", errors="surrogateescape"))


# ---------- resolving a mod set


@dataclass
class ModChanges:
    info: Dict[str, str] = field(default_factory=dict)
    replaced: List[str] = field(default_factory=list)
    added: List[str] = field(default_factory=list)
    patched: List[str] = field(default_factory=list)


@dataclass
class Plan:
    mods: List[str]
    key: str
    root: Path
    # source/... or port/linux/game/... -> the mod file that takes its place
    # (replaced or added)
    files: Dict[str, Tuple[str, Path]] = field(default_factory=dict)
    # the same -> (mod, patch file) in application order
    patches: Dict[str, List[Tuple[str, Path]]] = field(default_factory=dict)
    changes: Dict[str, ModChanges] = field(default_factory=dict)
    warnings: List[str] = field(default_factory=list)

    @property
    def tree(self) -> Path:
        return self.root / "tree"

    @property
    def output(self) -> Path:
        return self.root / "halo"

    @property
    def ninja_file(self) -> Path:
        return self.root / "build.ninja"

    def changed_dirs(self) -> Set[Path]:
        """directories with a mod file or patched copy"""
        return {Path(rel).parent for rel in [*self.files, *self.patches]}

    def added_units(self) -> List[Path]:
        """new game units: added .c files, by a mod file or a patch"""
        added = {
            rel for mod in self.changes.values() for rel in mod.added if rel.endswith(".c")
        }
        return [Path(rel) for rel in sorted(added)]

    def describe(self) -> Dict[str, Any]:
        return {
            "mods": self.mods,
            "key": self.key,
            "build_dir": str(self.root),
            "ninja_file": str(self.ninja_file),
            "binary": str(self.output),
            "changes": {
                name: {
                    **c.info,
                    "replaced": sorted(c.replaced),
                    "added": sorted(c.added),
                    "patched": sorted(c.patched),
                }
                for name, c in self.changes.items()
            },
            "warnings": self.warnings,
        }


def mod_set_key(mods: Sequence[str], mods_dir: Path = MODS_DIR) -> str:
    """the build directory name of an ordered mod set (the order is that of
    the mods' hooks, so a different order is a different build) from a mods
    directory (mods of the same names elsewhere are other mods)"""
    payload = [os.path.abspath(mods_dir), *mods]
    return hashlib.sha256(json.dumps(payload).encode("utf-8")).hexdigest()[:16]


def read_mod_info(mod_dir: Path) -> Dict[str, str]:
    """a mod's mod.json: its name, version and description; ValueError if it
    is missing or not like that"""
    path = mod_dir / MOD_INFO
    if not path.is_file():
        raise ValueError(f"{path} is missing (it gives the mod's " + ", ".join(MOD_INFO_FIELDS) + ")")
    try:
        info = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{path}: {error}") from None
    if not isinstance(info, dict):
        raise ValueError(f"{path}: expected an object")
    missing = [key for key in MOD_INFO_FIELDS if not isinstance(info.get(key), str) or not info[key].strip()]
    if missing:
        raise ValueError(f"{path}: " + ", ".join(missing) + " must be non-empty strings")
    return {key: info[key] for key in MOD_INFO_FIELDS}


def available_mods(mods_dir: Path = MODS_DIR) -> List[str]:
    if not mods_dir.is_dir():
        return []
    return sorted(p.name for p in mods_dir.iterdir() if p.is_dir() and (p / MOD_INFO).is_file())


def _files_under(root: Path) -> List[Path]:
    return sorted(p for p in root.rglob("*") if p.is_file())


def _rel(path: Path) -> str:
    return path.as_posix()


def _overlay_root(path: Path) -> Optional[Path]:
    """the root of OVERLAY_ROOTS that path is under"""
    for root in OVERLAY_ROOTS:
        if path.parts[: len(root.parts)] == root.parts and len(path.parts) > len(root.parts):
            return root
    return None


def resolve(
    mods: Sequence[str],
    compiled_units: Iterable[str] = (),
    mods_dir: Path = MODS_DIR,
    build_dir: Path = MODS_BUILD_DIR,
    repo_root: Path = Path("."),
) -> Plan:
    """what the ordered mod set changes; ModError if it cannot be built.
    compiled_units are the .c files `ninja linux` compiles, to warn about
    replacements that change nothing."""
    if len(set(mods)) != len(mods):
        raise ModError("a mod is enabled more than once: " + ", ".join(mods))
    key = mod_set_key(mods, mods_dir)
    plan = Plan(mods=list(mods), key=key, root=build_dir / key)
    compiled = set(compiled_units)
    errors: List[str] = []
    roots = " or ".join(f"{_rel(root)}/" for root in OVERLAY_ROOTS)
    # file -> every mod that supplies the whole file
    owners: Dict[str, List[Tuple[str, Path]]] = {}
    # file -> the mods that change it in any way, in order
    changers: Dict[str, List[str]] = {}

    def changed_by(rel: str, name: str) -> None:
        if name not in changers.setdefault(rel, []):
            changers[rel].append(name)

    for name in mods:
        mod_dir = mods_dir / name
        if not mod_dir.is_dir():
            errors.append(f"mod {name}: {mod_dir} does not exist")
            continue
        changes = plan.changes.setdefault(name, ModChanges())
        try:
            changes.info = read_mod_info(mod_dir)
        except ValueError as error:
            errors.append(f"mod {name}: {error}")
        for root in OVERLAY_ROOTS:
            mod_root = mod_dir / root
            if mod_root.is_dir():
                for path in _files_under(mod_root):
                    rel = _rel(root / path.relative_to(mod_root))
                    owners.setdefault(rel, []).append((name, path))
                    changed_by(rel, name)
        patch_dir = mod_dir / "patches"
        if patch_dir.is_dir():
            for patch in sorted(patch_dir.glob("*.patch")):
                try:
                    parts = read_patch(patch)
                except (ValueError, OSError) as error:
                    errors.append(f"mod {name}: {patch}: {error}")
                    continue
                for part in parts:
                    target = Path(part.target)
                    if _overlay_root(target) is None or ".." in target.parts:
                        errors.append(
                            f"mod {name}: {patch}: {part.target} is outside {roots} "
                            "(only the game's sources and the ports' game units can be patched)"
                        )
                        continue
                    exists = (repo_root / target).is_file()
                    if not part.creates and not exists:
                        errors.append(f"mod {name}: {patch}: {part.target} does not exist")
                        continue
                    patches = plan.patches.setdefault(part.target, [])
                    if (name, patch) not in patches:
                        patches.append((name, patch))
                    changed_by(part.target, name)

    for rel, names in sorted(changers.items()):
        if len(names) > 1:
            how = []
            for name in names:
                ways = []
                if any(mod == name for mod, _ in owners.get(rel, [])):
                    ways.append("replaces" if (repo_root / rel).is_file() else "adds")
                if any(mod == name for mod, _ in plan.patches.get(rel, [])):
                    ways.append("patches")
                how.append(f"{name} ({' and '.join(ways)})")
            errors.append(
                f"conflict: {rel} is changed by more than one mod: {', '.join(how)}; "
                "enable only one of them, or move what they share into the ports "
                "(port/linux/include/halo_mod.h)"
            )

    for rel, supplied in sorted(owners.items()):
        if len(changers[rel]) > 1:
            continue
        mod, path = supplied[0]
        plan.files[rel] = (mod, path)
        exists = (repo_root / rel).is_file()
        if exists:
            plan.changes[mod].replaced.append(rel)
            if rel.endswith(".c") and compiled and rel not in compiled:
                plan.warnings.append(
                    f"mod {mod}: {rel} is not compiled by the Linux build "
                    "(it is excluded in port/linux/port.json), so replacing it changes nothing"
                )
        else:
            plan.changes[mod].added.append(rel)

    for rel, patches in sorted(plan.patches.items()):
        if len(changers[rel]) > 1:
            continue
        mod = patches[0][0]
        if rel in plan.files:
            for _, patch in patches:
                plan.warnings.append(
                    f"mod {mod}: {patch} is not applied to {rel}: the mod replaces the whole file"
                )
            continue
        plan.changes[mod].patched.append(rel)
        if not (repo_root / rel).is_file():
            # a patch that creates a file adds it
            plan.changes[mod].added.append(rel)
    plan.patches = {
        rel: p for rel, p in plan.patches.items() if rel not in plan.files and len(changers[rel]) == 1
    }

    if errors:
        raise ModError("\n".join(errors))
    return plan


# ---------- the mirrored tree


def _link(link: Path, target: Path) -> None:
    """point link at target (relative), replacing whatever link was"""
    relative = os.path.relpath(target, link.parent)
    if link.is_symlink() and os.readlink(link) == relative:
        return
    link.parent.mkdir(parents=True, exist_ok=True)
    temporary = link.with_name(f".{link.name}.tmp{os.getpid()}")
    os.symlink(relative, temporary)
    # os.replace swaps the directory entry itself; it never writes through
    # an existing link into the file it points at
    os.replace(temporary, link)


# what a patched file is until the build applies its patches: older than
# anything, so that ninja always rebuilds it
UNPATCHED = b"#error the build has not applied this file's patches yet (tools/mod_overlay.py)\n"


def _unpatched(path: Path) -> None:
    """make path a placeholder for a patched copy, unless it is one already
    (a regular file the build wrote); a link left from a configuration
    without the patch would otherwise look up to date to ninja"""
    if path.is_file() and not path.is_symlink():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp{os.getpid()}")
    temporary.write_bytes(UNPATCHED)
    os.utime(temporary, (0, 0))
    os.replace(temporary, path)


def sync_tree(plan: Plan, repo_root: Path = Path(".")) -> List[str]:
    """make plan.tree mirror the overlay roots: links to the original files
    and to mod files; patched copies are regular files the build writes (a
    placeholder until then). Returns the stale entries left from an earlier
    configuration of this set (files no longer supplied: links to them
    dangle and are ignored)"""
    links: Dict[Path, Path] = {}
    for root in OVERLAY_ROOTS:
        for path in _files_under(repo_root / root):
            links[plan.tree / root / path.relative_to(repo_root / root)] = path
    for rel, (_, path) in plan.files.items():
        links[plan.tree / rel] = path
    patched = {plan.tree / rel for rel in plan.patches}
    for link, target in links.items():
        if link not in patched:
            _link(link, target)
    for path in patched:
        _unpatched(path)
    stale: List[str] = []
    for root in OVERLAY_ROOTS:
        tree_root = plan.tree / root
        if tree_root.is_dir():
            for path in sorted(tree_root.rglob("*")):
                if (path.is_symlink() or path.is_file()) and path not in links and path not in patched:
                    stale.append(_rel(path))
    return stale


# ---------- ninja


def _game_units(sln: Any) -> List[str]:
    """the source/... and port/linux/game/... units `ninja linux` compiles"""
    from .linux_build import PORT_CONFIG, game_sources

    config = json.loads(PORT_CONFIG.read_text(encoding="utf-8"))
    units = [source.as_posix() for source in game_sources(config)]
    units += [_rel(p) for p in sorted(Path(config["game_sources"]).glob("*.c"))]
    return units


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


def generate_mod_build(sln: Any, mods: Sequence[str], mods_dir: Path = MODS_DIR) -> Plan:
    """write the ninja file of a mod set; raises ModError"""
    from .linux_build import LinuxLayout, generate_linux_build
    from .ninja_syntax import Writer

    plan = resolve(mods, _game_units(sln), mods_dir=mods_dir)
    stale = sync_tree(plan)
    if stale:
        plan.warnings.append(
            f"{len(stale)} file(s) in {plan.tree} are no longer supplied by any mod or by the repository "
            f"and are ignored; removing {plan.root} cleans them (first: {stale[0]})"
        )

    out = io.StringIO()
    n = Writer(out)
    n.comment(f"Source mod build of: {' '.join(plan.mods) or '(no mods)'}")
    n.comment("generated by configure.py --mods (tools/mod_overlay.py) - do not edit")
    n.variable("ninja_required_version", "1.3")
    # keep this build's .ninja_log and .ninja_deps apart from the root build's
    n.variable("builddir", plan.root)
    n.variable(
        "configure_args",
        [f'"{arg}"' if any(ch.isspace() for ch in arg) else arg for arg in sys.argv[1:]],
    )
    n.variable("python", f'"{sys.executable}"')
    n.newline()

    n.rule(
        name="mod_patch",
        command="$python -m tools.mod_overlay patch --output $out --target $target $original $patches",
        description="MOD PATCH $target",
    )
    patched: List[Path] = []
    for rel, patches in sorted(plan.patches.items()):
        output = plan.tree / rel
        patched.append(output)
        original = Path(rel)
        n.build(
            outputs=output,
            rule="mod_patch",
            inputs=[patch for _, patch in patches],
            implicit=[Path("tools/mod_overlay.py"), *([original] if original.is_file() else [])],
            variables={
                "target": rel,
                "original": f"--original {_quote(original)}" if original.is_file() else "",
                "patches": " ".join(f"--patch {_quote(mod)}={_quote(patch)}" for mod, patch in patches),
            },
        )
    n.newline()

    generate_linux_build(
        n,
        sln,
        LinuxLayout(
            build_dir=plan.root,
            tree=plan.tree,
            mirrored=OVERLAY_ROOTS,
            changed_dirs=plan.changed_dirs(),
            extra_game_sources=plan.added_units(),
            order_only=patched,
            pgo_training=False,
        ),
    )
    n.newline()

    # rerun configure.py when mods (or the sources they mirror) gain or lose
    # files, or a patch changes the files it touches
    watched: List[Path] = [
        Path("configure.py"),
        Path("tools/mod_overlay.py"),
        Path("tools/linux_build.py"),
        Path("port/linux/port.json"),
    ]
    for root in OVERLAY_ROOTS:
        watched += [root, *sorted(p for p in root.rglob("*") if p.is_dir())]
    for name in plan.mods:
        mod_dir = mods_dir / name
        watched += [mod_dir, *sorted(p for p in mod_dir.rglob("*") if p.is_dir())]
        watched += [mod_dir / MOD_INFO]
        watched += sorted((mod_dir / "patches").glob("*.patch"))
    n.rule(
        name="configure",
        command="$python configure.py $configure_args",
        generator=True,
        description="RUN configure.py $configure_args",
    )
    n.build(outputs=plan.ninja_file, rule="configure", implicit=watched)
    n.newline()
    n.default(plan.output)

    plan.root.mkdir(parents=True, exist_ok=True)
    # always written: ninja's regeneration needs it newer than its inputs
    plan.ninja_file.write_text(out.getvalue(), encoding="utf-8")
    (plan.root / "mods.json").write_text(json.dumps(plan.describe(), indent=2) + "\n", encoding="utf-8")
    return plan


# ---------- the patch build step


def apply_patches(target: str, original: Optional[Path], patches: Sequence[Tuple[str, Path]], output: Path) -> None:
    """write output: original (or nothing) with each patch's part for target
    applied in order; ModError naming the mod and file if one does not apply"""
    with tempfile.TemporaryDirectory(prefix="mod_patch.") as scratch:
        current = Path(scratch) / "current"
        current.write_bytes(original.read_bytes() if original else b"")
        for index, (mod, patch) in enumerate(patches):
            parts = [part for part in read_patch(patch) if part.target == target]
            if not parts:
                raise ModError(f"mod {mod}: {patch} does not change {target}")
            for part in parts:
                result = Path(scratch) / f"step{index}"
                process = subprocess.run(
                    [
                        "patch", "--batch", "--forward", "--fuzz=0", "--no-backup-if-mismatch",
                        "--reject-file=-", "--quiet", "-p1", "-o", str(result), str(current),
                    ],
                    input=part.text.encode("utf-8", errors="surrogateescape"),
                    capture_output=True,
                )
                if process.returncode != 0:
                    detail = (process.stdout + process.stderr).decode("utf-8", errors="replace").strip()
                    raise ModError(
                        f"mod {mod}: {patch} does not apply to {target}"
                        + (f":\n{detail}" if detail else "")
                    )
                result.replace(current)
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_name(f".{output.name}.tmp{os.getpid()}")
        temporary.write_bytes(current.read_bytes())
        os.replace(temporary, output)


def _patch_main(argv: Sequence[str]) -> None:
    parser = argparse.ArgumentParser(prog="mod_overlay patch")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", required=True, help="source/... the patches change")
    parser.add_argument("--original", type=Path, help="the file patched (none: the patches create it)")
    parser.add_argument("--patch", action="append", default=[], metavar="MOD=FILE")
    args = parser.parse_args(argv)
    patches = []
    for item in args.patch:
        mod, _, path = item.partition("=")
        patches.append((mod, Path(path)))
    try:
        apply_patches(args.target, args.original, patches, args.output)
    except (ModError, ValueError, OSError) as error:
        sys.exit(f"error: {error}")


def main(argv: Optional[Sequence[str]] = None) -> None:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv[:1] == ["patch"]:
        _patch_main(argv[1:])
        return
    parser = argparse.ArgumentParser(description="Source mods of the native Linux build")
    sub = parser.add_subparsers(dest="command", required=True)
    describe = sub.add_parser("describe", help="print what each enabled mod replaces, adds and patches (JSON)")
    describe.add_argument("mods", nargs="*", help="enabled mods in order (default: every mod in mods/)")
    listing = sub.add_parser("list", help="print the mods in mods/")
    for command in (describe, listing):
        command.add_argument("--mods-dir", type=Path, default=MODS_DIR)
    args = parser.parse_args(argv)
    if args.command == "list":
        print("\n".join(available_mods(args.mods_dir)))
        return
    try:
        plan = resolve(args.mods or available_mods(args.mods_dir), mods_dir=args.mods_dir)
    except ModError as error:
        sys.exit(f"error: {error}")
    print(json.dumps(plan.describe(), indent=2))


if __name__ == "__main__":
    main()
