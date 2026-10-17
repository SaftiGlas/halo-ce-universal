"""Tests for source mods of the native Linux build (tools/mod_overlay.py)."""

import json
import os
import shutil
from pathlib import Path

import pytest

from tools import mod_overlay
from tools.linux_build import LinuxLayout
from tools.mod_overlay import ModError, apply_patches, mod_set_key, parse_patch, resolve, sync_tree

needs_patch = pytest.mark.skipif(shutil.which("patch") is None, reason="GNU patch is not installed")


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


MOD_NAMES = ("a", "b", "c")


@pytest.fixture
def repo(tmp_path):
    """a source/ and port/linux/game/ tree, and mods/ with empty mods a, b, c"""
    write(tmp_path / "source/game/game.c", '#include "game.h"\nint a = GAME;\n')
    write(tmp_path / "source/game/game.h", "#define GAME 1\n")
    write(tmp_path / "source/game/other.c", '#include "game.h"\n')
    write(tmp_path / "source/main/main.c", "one\ntwo\nthree\n")
    write(tmp_path / "port/linux/game/forge.c", "int forge;\n")
    for name in MOD_NAMES:
        write_info(tmp_path / "mods" / name)
    return tmp_path


def write_info(mod_dir: Path, **info) -> Path:
    fields = {"name": mod_dir.name, "version": "1.0", "description": "a test mod"}
    fields.update(info)
    return write(mod_dir / "mod.json", json.dumps(fields))


def plan_for(repo: Path, mods, compiled=()):
    return resolve(mods, compiled, mods_dir=repo / "mods", build_dir=repo / "build/mods", repo_root=repo)


MAIN_PATCH = "--- a/source/main/main.c\n+++ b/source/main/main.c\n@@ -1,3 +1,3 @@\n one\n-two\n+TWO\n three\n"


# ---------- patches


def test_parse_patch_counts_hunks_so_removed_header_lookalikes_stay_in_the_hunk():
    # "--- x" and "+++ y" are a removed and an added line, not a second file
    text = (
        "diff --git a/source/x.c b/source/x.c\n"
        "--- a/source/x.c\n+++ b/source/x.c\n@@ -1,2 +1,1 @@\n--- x\n+++ y\n-z\n"
    )
    parts = parse_patch(text)
    assert [p.target for p in parts] == ["source/x.c"]
    assert parts[0].text.endswith("--- x\n+++ y\n-z\n")


def test_parse_patch_rejects_short_hunks():
    with pytest.raises(ValueError, match="ends early"):
        parse_patch("--- a/source/x.c\n+++ b/source/x.c\n@@ -1,3 +1,3 @@\n a\n")


def test_parse_patch_splits_files_and_sees_new_ones():
    text = MAIN_PATCH + "--- /dev/null\n+++ b/source/game/new.c\n@@ -0,0 +1 @@\n+int n;\n"
    parts = parse_patch(text)
    assert [(p.target, p.creates) for p in parts] == [("source/main/main.c", False), ("source/game/new.c", True)]


def test_parse_patch_rejects_deletions():
    with pytest.raises(ValueError, match="delete"):
        parse_patch("--- a/source/x.c\n+++ /dev/null\n@@ -1 +0,0 @@\n-x\n")


# ---------- resolving


def test_key_depends_on_order():
    assert mod_set_key(["a", "b"]) != mod_set_key(["b", "a"])


def test_key_depends_on_the_mods_directory(tmp_path):
    assert mod_set_key(["a"], tmp_path / "one") != mod_set_key(["a"], tmp_path / "two")


def test_replaced_added_and_patched_files_are_reported(repo):
    write(repo / "mods/a/source/game/game.h", "#define GAME 2\n")
    write(repo / "mods/a/source/game/extra.c", "int extra;\n")
    write(repo / "mods/b/patches/001.patch", MAIN_PATCH)
    plan = plan_for(repo, ["a", "b"])
    assert plan.changes["a"].replaced == ["source/game/game.h"]
    assert plan.changes["a"].added == ["source/game/extra.c"]
    assert plan.changes["b"].patched == ["source/main/main.c"]
    assert plan.added_units() == [Path("source/game/extra.c")]
    assert not plan.warnings


def test_two_mods_replacing_one_file_conflict(repo):
    write(repo / "mods/a/source/game/game.h", "a\n")
    write(repo / "mods/b/source/game/game.h", "b\n")
    write(repo / "mods/b/source/game/new.h", "b\n")
    write(repo / "mods/c/source/game/new.h", "c\n")
    with pytest.raises(ModError) as error:
        plan_for(repo, ["a", "b", "c"])
    message = str(error.value)
    assert "source/game/game.h is changed by more than one mod: a (replaces), b (replaces)" in message
    assert "source/game/new.h is changed by more than one mod: b (adds), c (adds)" in message


def test_two_mods_patching_one_file_conflict(repo):
    write(repo / "mods/a/patches/001.patch", MAIN_PATCH)
    write(repo / "mods/b/patches/001.patch", MAIN_PATCH)
    with pytest.raises(ModError, match=r"source/main/main.c is changed by more than one mod: a \(patches\), b \(patches\)"):
        plan_for(repo, ["a", "b"])


def test_a_replacement_and_another_mods_patch_conflict(repo):
    write(repo / "mods/a/source/main/main.c", "mine\n")
    write(repo / "mods/b/patches/001.patch", MAIN_PATCH)
    with pytest.raises(ModError, match=r"main.c is changed by more than one mod: b \(patches\), a \(replaces\)"):
        plan_for(repo, ["b", "a"])


def test_within_a_mod_the_whole_file_wins_over_its_patch(repo):
    write(repo / "mods/a/source/main/main.c", "mine\n")
    write(repo / "mods/a/patches/001.patch", MAIN_PATCH)
    plan = plan_for(repo, ["a"])
    assert "source/main/main.c" not in plan.patches
    assert plan.changes["a"].patched == []
    assert any("not applied to source/main/main.c" in w for w in plan.warnings)


def test_mods_changing_different_files_work_together(repo):
    write(repo / "mods/a/source/mods/a/a.c", "int a;\n")
    write(repo / "mods/b/source/mods/b/b.c", "int b;\n")
    write(repo / "mods/c/patches/001.patch", MAIN_PATCH)
    plan = plan_for(repo, ["a", "b", "c"])
    assert plan.added_units() == [Path("source/mods/a/a.c"), Path("source/mods/b/b.c")]
    assert list(plan.patches) == ["source/main/main.c"]


def test_ports_game_units_can_be_replaced_and_patched(repo):
    write(repo / "mods/a/patches/001.patch", "--- a/port/linux/game/forge.c\n+++ b/port/linux/game/forge.c\n@@ -1 +1 @@\n-int forge;\n+int forged;\n")
    write(repo / "mods/b/port/linux/game/extra.c", "int extra;\n")
    plan = plan_for(repo, ["a", "b"])
    assert plan.changes["a"].patched == ["port/linux/game/forge.c"]
    assert plan.added_units() == [Path("port/linux/game/extra.c")]
    assert Path("port/linux/game") in plan.changed_dirs()


def test_mod_json_is_required_and_checked(repo):
    (repo / "mods/a/mod.json").unlink()
    write_info(repo / "mods/b", version="")
    write(repo / "mods/c/mod.json", "[")
    with pytest.raises(ModError) as error:
        plan_for(repo, ["a", "b", "c"])
    message = str(error.value)
    assert "mod a:" in message and "mod.json is missing" in message
    assert "mod b:" in message and "version must be non-empty strings" in message
    assert "mod c:" in message


def test_mod_json_is_described(repo):
    write_info(repo / "mods/a", name="Alpha", version="2.1", description="does a")
    described = plan_for(repo, ["a"]).describe()["changes"]["a"]
    assert (described["name"], described["version"], described["description"]) == ("Alpha", "2.1", "does a")


def test_available_mods_are_those_with_mod_json(repo):
    (repo / "mods/stray").mkdir()
    assert mod_overlay.available_mods(repo / "mods") == ["a", "b", "c"]


def test_patches_outside_source_or_of_missing_files_are_errors(repo):
    write(repo / "mods/a/patches/001.patch", MAIN_PATCH.replace("source/main", "port/linux/src"))
    write(repo / "mods/b/patches/001.patch", MAIN_PATCH.replace("main.c", "nothere.c"))
    with pytest.raises(ModError) as error:
        plan_for(repo, ["a", "b"])
    assert "mod a:" in str(error.value) and "is outside source/ or port/linux/game/" in str(error.value)
    assert "mod b:" in str(error.value) and "source/main/nothere.c does not exist" in str(error.value)


def test_unknown_mod_is_an_error(repo):
    with pytest.raises(ModError, match="mod nope"):
        plan_for(repo, ["nope"])


def test_replacing_a_unit_the_build_does_not_compile_warns(repo):
    write(repo / "mods/a/source/game/other.c", "x\n")
    plan = plan_for(repo, ["a"], compiled=["source/game/game.c"])
    assert any("source/game/other.c is not compiled" in w for w in plan.warnings)


# ---------- the tree


def test_tree_mirrors_source_with_mod_files_in_place(repo):
    header = write(repo / "mods/a/source/game/game.h", "#define GAME 2\n")
    write(repo / "mods/b/patches/001.patch", MAIN_PATCH)
    plan = plan_for(repo, ["a", "b"])
    assert sync_tree(plan, repo) == []
    tree = plan.tree / "source"
    # an unmodified unit sits next to the replaced header, so its
    # #include "game.h" finds the mod's
    assert (tree / "game/other.c").resolve() == (repo / "source/game/other.c").resolve()
    assert (tree / "game/game.h").resolve() == header.resolve()
    # a patched file is the build's own copy: a placeholder ninja sees as out of date
    patched = tree / "main/main.c"
    assert not patched.is_symlink() and patched.stat().st_mtime == 0
    assert patched.read_bytes() == mod_overlay.UNPATCHED
    # links are relative, and resyncing is a no-op
    assert not os.readlink(tree / "game/game.h").startswith("/")
    assert sync_tree(plan, repo) == []


def test_a_link_left_where_a_patch_now_applies_becomes_a_placeholder(repo):
    sync_tree(plan_for(repo, []), repo)
    write(repo / "mods/b/patches/001.patch", MAIN_PATCH)
    plan = plan_for(repo, ["b"])
    # the same set directory as a set that had no patch here (a stand-in for
    # disabling and re-enabling a patch): never written through
    plan.root = plan_for(repo, []).root
    sync_tree(plan, repo)
    assert (repo / "source/main/main.c").read_text() == "one\ntwo\nthree\n"
    assert (plan.tree / "source/main/main.c").read_bytes() == mod_overlay.UNPATCHED


def test_tree_goes_back_to_the_original_and_reports_stale_files(repo):
    write(repo / "mods/a/source/game/game.h", "#define GAME 2\n")
    added = write(repo / "mods/a/source/game/extra.h", "\n")
    sync_tree(plan_for(repo, ["a"]), repo)
    (repo / "mods/a/source/game/game.h").rename(repo / "mods/a/game.h.off")
    added.rename(repo / "mods/a/extra.h.off")
    plan = plan_for(repo, ["a"])
    stale = sync_tree(plan, repo)
    assert (plan.tree / "source/game/game.h").resolve() == (repo / "source/game/game.h").resolve()
    assert stale == [(plan.tree / "source/game/extra.h").as_posix()]


# ---------- applying patches


@needs_patch
def test_patches_apply_in_order(repo, tmp_path):
    first = write(repo / "mods/a/patches/001.patch", MAIN_PATCH)
    second = write(
        repo / "mods/b/patches/001.patch",
        "--- a/source/main/main.c\n+++ b/source/main/main.c\n@@ -2,2 +2,2 @@\n TWO\n-three\n+THREE\n",
    )
    output = tmp_path / "out/main.c"
    apply_patches("source/main/main.c", repo / "source/main/main.c", [("a", first), ("b", second)], output)
    assert output.read_text() == "one\nTWO\nTHREE\n"


@needs_patch
def test_patch_that_creates_a_file(repo, tmp_path):
    patch = write(repo / "mods/a/patches/001.patch", "--- /dev/null\n+++ b/source/game/new.c\n@@ -0,0 +1 @@\n+int n;\n")
    output = tmp_path / "new.c"
    apply_patches("source/game/new.c", None, [("a", patch)], output)
    assert output.read_text() == "int n;\n"


@needs_patch
def test_failing_patch_names_mod_and_file_and_leaves_no_output(repo, tmp_path):
    patch = write(repo / "mods/a/patches/001.patch", MAIN_PATCH.replace("-two", "-deux"))
    output = tmp_path / "main.c"
    with pytest.raises(ModError) as error:
        apply_patches("source/main/main.c", repo / "source/main/main.c", [("a", patch)], output)
    assert "mod a:" in str(error.value) and "does not apply to source/main/main.c" in str(error.value)
    assert not output.exists()


# ---------- the Linux build's view


def test_default_layout_changes_nothing():
    layout = LinuxLayout()
    assert layout.source("source/game/game.c") == Path("source/game/game.c")
    assert layout.cflags(Path("build/linux")) == []


def test_tree_layout_maps_source_only_and_hides_the_tree():
    layout = LinuxLayout(
        build_dir=Path("build/mods/k"), tree=Path("build/mods/k/tree"), changed_dirs={Path("source/game")}
    )
    assert layout.source("source/saved films") == Path("build/mods/k/tree/source/saved films")
    assert layout.source("port/linux/include") == Path("port/linux/include")
    assert layout.cflags(Path("build/linux")) == [
        "-ffile-prefix-map=build/mods/k/=build/linux/",
        "-ffile-prefix-map=./build/mods/k/=./build/linux/",
        "-ffile-prefix-map=build/mods/k/tree/=",
    ]


def test_only_units_next_to_mod_files_compile_from_the_tree():
    layout = LinuxLayout(tree=Path("build/mods/k/tree"), changed_dirs={Path("source/game")})
    changed, unchanged = Path("source/game/game.c"), Path("source/main/main.c")
    assert layout.unit(changed) == Path("build/mods/k/tree/source/game/game.c")
    assert layout.unit_cflags(changed) == ["-mllvm -static-func-strip-dirname-prefix=4"]
    assert layout.unit(unchanged) == unchanged
    assert layout.unit_cflags(unchanged) == []


def test_tree_mirrors_the_ports_game_units_too(repo):
    plan = plan_for(repo, [])
    sync_tree(plan, repo)
    link = plan.tree / "port/linux/game/forge.c"
    assert link.is_symlink() and link.resolve() == (repo / "port/linux/game/forge.c").resolve()


def test_mod_layout_maps_the_ports_game_units():
    layout = LinuxLayout(
        tree=Path("build/mods/k/tree"),
        mirrored=mod_overlay.OVERLAY_ROOTS,
        changed_dirs={Path("port/linux/game")},
    )
    forge = Path("port/linux/game/forge.c")
    assert layout.unit(forge) == Path("build/mods/k/tree/port/linux/game/forge.c")
    assert layout.source("port/linux/include") == Path("port/linux/include")
