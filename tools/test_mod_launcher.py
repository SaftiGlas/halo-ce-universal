"""Tests for the mod launcher: settings, the start map and the fzf lists (tools/mod_launcher.py)."""

import json
from pathlib import Path

import pytest

from tools import mod_fzf, mod_maps
from tools import mod_launcher as launcher


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def mod(mods: Path, name: str) -> Path:
    write(mods / name / "mod.json", json.dumps({"name": name, "version": "1", "description": "test"}))
    return mods / name


@pytest.fixture
def mods(tmp_path):
    """mods/ with a code mod and two mods without code"""
    mods = tmp_path / "mods"
    write(mod(mods, "code") / "source" / "mods" / "code" / "code.c", "int code;\n")
    for name in ("data_b", "data_a"):
        write(mod(mods, name) / "notes.txt", "no code\n")
    return mods


def test_enabled_json_is_taken_over_by_mods_json(mods):
    write(mods / "enabled.json", json.dumps({"enabled": ["code"]}))
    assert launcher.enabled_mods(mods) == ["code"]
    launcher.change(["data_a"], True, mods)
    assert json.loads((mods / "mods.json").read_text())["enabled"] == ["code", "data_a"]


def test_changing_mods_keeps_the_other_settings(mods):
    write(mods / "mods.json", json.dumps({"enabled": [], "import_into": ["wizard"]}))
    launcher.change(["code"], True, mods)
    assert json.loads((mods / "mods.json").read_text())["import_into"] == ["wizard"]


def test_only_mods_that_change_code_are_built(mods):
    assert launcher.changes_code("code", mods)
    assert not launcher.changes_code("data_a", mods)


# ---------- the map the game starts at


def map_file(root: Path, name: str, kind: int) -> None:
    header = bytearray(0x800)
    header[:4] = b"daeh"
    header[0x20:0x20 + len(name)] = name.encode()
    header[0x60:0x64] = kind.to_bytes(4, "little")
    (root / "maps").mkdir(parents=True, exist_ok=True)
    (root / "maps" / f"{name}.map").write_bytes(bytes(header))


@pytest.fixture
def maps(tmp_path):
    root = tmp_path / "data"
    for name, kind in (("b30", 0), ("a10", 0), ("bloodgulch", 1), ("mymap", 1), ("ui", 2)):
        map_file(root, name, kind)
    (root / "maps" / "notes.txt").write_text("not a map")
    (root / "maps" / "broken.map").write_bytes(b"nope")
    return mod_maps.find_maps(root)


def test_maps_are_told_apart_by_their_header(maps):
    assert [(m.name, m.kind) for m in maps] == [
        ("a10", "campaign"), ("b30", "campaign"), ("bloodgulch", "multiplayer"), ("mymap", "multiplayer"),
    ]
    assert mod_maps.find_map("Blood Gulch", maps).name == "bloodgulch"
    assert mod_maps.find_map("levels\\test\\bloodgulch\\bloodgulch", maps).name == "bloodgulch"
    assert mod_maps.find_map("levels/b30/b30", maps).name == "b30"
    assert mod_maps.find_map("nothing", maps) is None


def test_without_data_the_games_own_levels_are_listed(tmp_path):
    names = {m.name for m in mod_maps.find_maps(tmp_path)}
    assert {"a10", "d40", "bloodgulch", "wizard"} <= names and "ui" not in names


def test_a_multiplayer_map_gets_a_variant_and_a_campaign_level_a_difficulty(maps):
    assert launcher.choose_launch("bloodgulch", maps=maps) == {"map": "bloodgulch", "variant": "slayer", "difficulty": ""}
    assert launcher.choose_launch("Blood Gulch", "ctf", maps=maps)["variant"] == "ctf"
    assert launcher.choose_launch("a10", difficulty="hard", maps=maps) == {
        "map": "a10", "variant": "", "difficulty": "hard"}
    assert launcher.choose_launch("a10", maps=maps)["difficulty"] == ""
    for name, variant, difficulty, problem in (
        ("nope", None, None, "no map"),
        ("bloodgulch", "polo", None, "no game variant"),
        ("bloodgulch", None, "hard", "not a difficulty|no difficulty|has a game variant"),
        ("a10", "slayer", None, "campaign level"),
        ("a10", None, "godlike", "no difficulty"),
    ):
        with pytest.raises(launcher.LauncherError, match=problem):
            launcher.choose_launch(name, variant, difficulty, maps=maps)


def test_the_launch_is_kept_next_to_the_other_settings(mods, monkeypatch, maps):
    monkeypatch.setattr(mod_maps, "find_maps", lambda root=None: maps)
    write(mods / "mods.json", json.dumps({"enabled": ["code"], "import_into": ["wizard"]}))
    assert launcher.saved_launch(mods) is None
    assert launcher.main(["--mods-dir", str(mods), "map", "bloodgulch", "--variant", "team_slayer"]) == 0
    settings = json.loads((mods / "mods.json").read_text())
    assert settings["launch"] == {"map": "bloodgulch", "variant": "team_slayer"}
    assert settings["enabled"] == ["code"] and settings["import_into"] == ["wizard"]
    assert launcher.saved_launch(mods) == {"map": "bloodgulch", "variant": "team_slayer", "difficulty": ""}
    assert launcher.main(["--mods-dir", str(mods), "map", "nope"]) == 1
    assert launcher.saved_launch(mods)["map"] == "bloodgulch"
    launcher.change(["data_a"], True, mods)
    assert launcher.saved_launch(mods)["map"] == "bloodgulch"
    assert launcher.main(["--mods-dir", str(mods), "map", "none"]) == 0
    assert "launch" not in json.loads((mods / "mods.json").read_text())


def test_the_game_is_told_where_to_start():
    assert launcher.launch_environment(None) == {}
    assert launcher.launch_environment({"map": "bloodgulch", "variant": "ctf", "difficulty": ""}) == {
        "HALO_START_MAP": "bloodgulch", "HALO_START_VARIANT": "ctf"}
    assert launcher.launch_environment({"map": "a10", "variant": "", "difficulty": "hard"}) == {
        "HALO_START_MAP": "a10", "HALO_START_DIFFICULTY": "hard"}


def test_run_passes_the_map_to_the_game(mods, monkeypatch, tmp_path):
    ran = {}
    monkeypatch.setattr(launcher, "build", lambda mods_dir: Path("/nowhere/halo"))
    monkeypatch.setattr(launcher.subprocess, "run", lambda command, env: ran.update(env=env) or type("R", (), {"returncode": 0})())
    monkeypatch.delenv("HALO_START_MAP", raising=False)
    saved = {"map": "a10", "variant": "", "difficulty": "hard"}
    write(mods / "mods.json", json.dumps({"enabled": [], "launch": {"map": "a10", "difficulty": "hard"}}))
    launcher.run([], mods)                                     # the saved map
    assert ran["env"]["HALO_START_MAP"] == "a10" and ran["env"]["HALO_START_DIFFICULTY"] == "hard"
    launcher.run([], mods, {"map": "bloodgulch", "variant": "ctf", "difficulty": ""})   # one run's
    assert ran["env"]["HALO_START_MAP"] == "bloodgulch" and ran["env"]["HALO_START_VARIANT"] == "ctf"
    launcher.run([], mods, use_saved_launch=False)             # the menu
    assert "HALO_START_MAP" not in ran["env"]
    assert launcher.saved_launch(mods) == saved


# ---------- the fzf screen's lists


def test_the_fzf_list_marks_mods_and_toggles_them(mods, capsys):
    launcher.change(["code"], True, mods)
    fields = [line.split("\t")[0] for line in mod_fzf.mod_lines(mods)]
    assert fields == ["code", "data_a", "data_b"]
    assert "[x]" in mod_fzf.mod_lines(mods)[0] and "[ ]" in mod_fzf.mod_lines(mods)[1]
    assert mod_fzf.main(["--mods-dir", str(mods), "toggle", "data_a"]) == 0
    assert launcher.enabled_mods(mods) == ["code", "data_a"]
    assert mod_fzf.main(["--mods-dir", str(mods), "toggle", "data_a"]) == 0
    assert launcher.enabled_mods(mods) == ["code"]
    assert mod_fzf.main(["--mods-dir", str(mods), "all", "on"]) == 0
    assert launcher.enabled_mods(mods) == ["code", "data_a", "data_b"]
    assert mod_fzf.main(["--mods-dir", str(mods), "all", "off"]) == 0
    assert launcher.enabled_mods(mods) == []
    assert mod_fzf.main(["--mods-dir", str(mods), "toggle", "nope"]) == 1
    assert "no such mod" in capsys.readouterr().out


def test_the_fzf_preview_says_what_a_mod_changes(mods):
    assert "Changes the game's code" in mod_fzf.mod_preview(mods, "code")
    assert "code.c" in mod_fzf.mod_preview(mods, "code")
    assert "Changes no code" in mod_fzf.mod_preview(mods, "data_a")
    assert mod_fzf.mod_preview(mods, "nope") == ""


# ---------- tags brought in from other maps


ROCK = "a30:scen:scenery\\rocks\\boulder_granite_large\\boulder_granite_large"


def test_an_import_is_donor_group_and_name():
    assert launcher.parse_import(ROCK) == ("a30", "scen", "scenery\\rocks\\boulder_granite_large\\boulder_granite_large")
    for bad in ("a30", "a30:scen", "a30:scenery:x", ":scen:x", "a30:scen:"):
        with pytest.raises(launcher.LauncherError):
            launcher.parse_import(bad)


def test_imports_are_kept_checked_and_given_to_the_game(mods, monkeypatch, maps):
    monkeypatch.setattr(mod_maps, "find_maps", lambda root=None: maps)
    assert launcher.saved_imports(mods) == [] and launcher.import_environment(mods) == {}
    write(mods / "mods.json", json.dumps({"enabled": ["code"], "import": [ROCK, ROCK, "nonsense", 5]}))
    assert launcher.saved_imports(mods) == [ROCK, ROCK]
    assert launcher.set_imports(mods, [ROCK, ROCK, "a30:scen:x"]) == [ROCK, "a30:scen:x"]
    environment = launcher.import_environment(mods)
    assert environment["HALO_IMPORT"] == ROCK + "|a30:scen:x"
    # every multiplayer map, until mods.json names the maps
    assert environment["HALO_IMPORT_INTO"] == "bloodgulch,mymap"
    settings = json.loads((mods / "mods.json").read_text())
    settings["import_into"] = ["mymap"]
    write(mods / "mods.json", json.dumps(settings))
    assert launcher.import_environment(mods)["HALO_IMPORT_INTO"] == "mymap"
    assert json.loads((mods / "mods.json").read_text())["enabled"] == ["code"]
    launcher.set_imports(mods, [])
    assert "import" not in json.loads((mods / "mods.json").read_text())


def test_the_import_command_adds_removes_and_names_the_maps(mods, monkeypatch, maps):
    monkeypatch.setattr(mod_maps, "find_maps", lambda root=None: maps)
    monkeypatch.setattr(launcher, "find_importable", lambda donor, group, name, maps=None: f"{donor}:{group}:{name}")
    base = ["--mods-dir", str(mods), "import"]
    assert launcher.main(base + ["add", "a30", "rock", "tree"]) == 0
    assert launcher.saved_imports(mods) == ["a30:scen:rock", "a30:scen:tree"]
    assert launcher.main(base + ["remove", "TREE"]) == 0
    assert launcher.saved_imports(mods) == ["a30:scen:rock"]
    assert launcher.main(base + ["remove", "nothing"]) == 1
    assert launcher.main(base + ["into", "Blood Gulch"]) == 0
    assert launcher.saved_import_maps(mods) == ["bloodgulch"]
    assert launcher.main(base + ["into", "nowhere"]) == 1
    assert launcher.main(base + ["into", "all"]) == 0
    assert launcher.saved_import_maps(mods) == ["bloodgulch", "mymap"]
    assert launcher.main(base + ["clear"]) == 0
    assert launcher.saved_imports(mods) == []


def test_run_gives_the_game_the_imports(mods, monkeypatch, maps):
    ran = {}
    monkeypatch.setattr(mod_maps, "find_maps", lambda root=None: maps)
    monkeypatch.setattr(launcher, "build", lambda mods_dir: Path("/nowhere/halo"))
    monkeypatch.setattr(launcher.subprocess, "run", lambda command, env: ran.update(env=env) or type("R", (), {"returncode": 0})())
    monkeypatch.delenv("HALO_IMPORT", raising=False)
    launcher.run([], mods)
    assert "HALO_IMPORT" not in ran["env"]
    launcher.set_imports(mods, [ROCK])
    launcher.run([], mods)
    assert ran["env"]["HALO_IMPORT"] == ROCK and ran["env"]["HALO_IMPORT_INTO"] == "bloodgulch,mymap"


def test_the_fzf_rows_of_a_donors_tags_are_marked(mods, tmp_path):
    listing = tmp_path / "tags.tsv"
    listing.write_text(f"{ROCK}\tscenery\\rocks\\boulder_granite_large\\boulder_granite_large\n"
                       "a30:scen:scenery\\trees\\tree\\tree\tscenery\\trees\\tree\\tree\n", encoding="utf-8")
    launcher.set_imports(mods, [ROCK])
    rows = mod_fzf.import_lines(mods, listing)
    assert [row.split("\t")[0] for row in rows] == [ROCK, "a30:scen:scenery\\trees\\tree\\tree"]
    assert "[x]" in rows[0] and "[ ]" in rows[1]
    assert mod_fzf.main(["--mods-dir", str(mods), "import-toggle", ROCK]) == 0
    assert launcher.saved_imports(mods) == []
    assert mod_fzf.main(["--mods-dir", str(mods), "import-toggle", ROCK]) == 0
    assert launcher.saved_imports(mods) == [ROCK]


def test_layouts_are_listed_with_the_one_that_plays(tmp_path, mods, capsys):
    from tools import mod_layouts

    forge = tmp_path / "saves" / "u" / "forge" / "bloodgulch"
    write(forge / "layout_01.txt", "name Layout 1\n[forge]\n")
    write(forge / "layout_03.txt", "name Big base\ndescription Two forts and a bridge\nlisted 1\n[forge]\n")
    write(forge / "layout_02.txt", "[forge]\ndescription not the header's\n")
    write(forge / "play.txt", "3\n")
    found = mod_layouts.layouts(tmp_path / "saves", "bloodgulch")
    assert found == [
        {"slot": 1, "name": "Layout 1", "description": "", "plays": False, "listed": False},
        {"slot": 2, "name": "Layout 2", "description": "", "plays": False, "listed": False},
        {"slot": 3, "name": "Big base", "description": "Two forts and a bridge", "plays": True, "listed": True},
    ]
    assert launcher.main(["--mods-dir", str(mods), "layouts", "--saves", str(tmp_path / "saves")]) == 0
    output = capsys.readouterr().out
    assert "3. Big base  (in the map list, plays on this map)" in output
    assert "      Two forts and a bridge" in output


def test_a_layout_is_renamed_on_its_first_line(tmp_path, mods):
    from tools import mod_layouts

    saves = tmp_path / "saves"
    path = write(saves / "u" / "forge" / "bloodgulch" / "layout_02.txt", "name Layout 2\nversion 1\n[forge]\n")
    assert launcher.main(["--mods-dir", str(mods), "layouts", "rename", "bloodgulch", "2", "Red", "base",
                          "--saves", str(saves)]) == 0
    assert path.read_text().splitlines() == ["name Red base", "version 1", "[forge]"]
    with pytest.raises(mod_layouts.LayoutError):
        mod_layouts.rename(saves, "bloodgulch", 5, "missing")
    with pytest.raises(mod_layouts.LayoutError):
        mod_layouts.rename(saves, "bloodgulch", 2, "x" * 64)
    with pytest.raises(mod_layouts.LayoutError):
        mod_layouts.rename(saves, "bloodgulch", 2, "100% base")


def test_a_layout_is_described_in_its_header(tmp_path, mods, capsys):
    from tools import mod_layouts

    saves = tmp_path / "saves"
    path = write(saves / "u" / "forge" / "bloodgulch" / "layout_02.txt",
                 "name Red base\nversion 1\nlisted 1\n[forge]\nspawn scen 0 0 0\n[sky]\nlook night\n")
    assert launcher.main(["--mods-dir", str(mods), "layouts", "describe", "bloodgulch", "2", "Forts", "and", "a",
                          "bridge", "--saves", str(saves)]) == 0
    assert "bloodgulch layout 2: Forts and a bridge" in capsys.readouterr().out
    assert path.read_text().splitlines() == [
        "name Red base", "description Forts and a bridge", "version 1", "listed 1", "[forge]", "spawn scen 0 0 0",
        "[sky]", "look night"]
    # again replaces it; none takes it away
    assert mod_layouts.describe(saves, "bloodgulch", 2, "  A  canyon ") == "A canyon"
    assert path.read_text().splitlines()[:3] == ["name Red base", "description A canyon", "version 1"]
    assert launcher.main(["--mods-dir", str(mods), "layouts", "describe", "bloodgulch", "2", "--saves",
                          str(saves)]) == 0
    assert "(no description)" in capsys.readouterr().out
    assert path.read_text().splitlines() == [
        "name Red base", "version 1", "listed 1", "[forge]", "spawn scen 0 0 0", "[sky]", "look night"]
    with pytest.raises(mod_layouts.LayoutError):
        mod_layouts.describe(saves, "bloodgulch", 5, "missing")
    with pytest.raises(mod_layouts.LayoutError):
        mod_layouts.describe(saves, "bloodgulch", 2, "x" * 128)


def test_a_layout_goes_with_a_multiplayer_launch():
    launch = launcher.with_layout({"map": "bloodgulch", "variant": "slayer", "difficulty": ""}, 3)
    assert launcher.launch_environment(launch)["HALO_START_LAYOUT"] == "3"
    with pytest.raises(launcher.LauncherError):
        launcher.with_layout({"map": "a10", "variant": "", "difficulty": "hard"}, 3)
    with pytest.raises(launcher.LauncherError):
        launcher.with_layout({"map": "bloodgulch", "variant": "slayer", "difficulty": ""}, 17)
