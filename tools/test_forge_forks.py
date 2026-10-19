"""Tests that the Forge copies of two files keep up with the files they copy.

Every build compiles port/linux/src/xinput_sdl_forge.c in place of
port/linux/src/xinput_sdl.c (port/linux/port.json, "excluded_platform_sources"),
and source/camera/director_forge.c in place of source/camera/director.c: each
is the original with the Forge game type's keys or flying camera added. A
change made to an original after its copy was taken reaches no build until
the copy takes it too (the web's touch controls, one gamepad's split screen
and network co-op's spectating were missed so). So every line of an original
must be in its copy, as often as the original has it, but for the few lines
the copy replaces, listed here with why.
"""
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]

# (original, Forge copy, the original's lines the copy replaces)
FORKS = [
    (
        "port/linux/src/xinput_sdl.c",
        "port/linux/src/xinput_sdl_forge.c",
        {
            # the file's name, at the head of its comment
            "XINPUT_SDL.C",
            # a name being typed has the keyboard (forge_typing), as a chat
            # line typed does
            "\t\t/* (a chat line typed takes the keyboard as the console does) */",
            "\t\tconsole_active = console_is_active() || network_chat_typing();",
            # the touch controls are read before the dev tools' filter, in a
            # block that notes what they press (forge_touch_pad_note)
            "\t\ttouch_input_gamepad(&state->Gamepad);",
            "\t\ttouch_input_controls(&state->Gamepad, input.menus);",
            # the touch look turns the free camera, not the player
            # (halo_linux_touch_look, forge_free_camera)
            "\tif (gamepad_index != 0)",
        },
    ),
    (
        "source/camera/director.c",
        "source/camera/director_forge.c",
        {
            "DIRECTOR.C",
        },
    ),
]


def missing_lines(original: str, copy: str, replaced: set) -> list:
    """The original's lines (but blank ones and those replaced) that its copy
    lacks, as often as it lacks them."""
    have = Counter(line.rstrip() for line in copy.splitlines())
    want = Counter(line.rstrip() for line in original.splitlines() if line.strip() and line.rstrip() not in replaced)
    return sorted((want - have).elements())


@pytest.mark.parametrize("original, copy, replaced", FORKS, ids=[fork[1] for fork in FORKS])
def test_forge_copy_has_every_line_of_its_original(original, copy, replaced):
    missing = missing_lines((ROOT / original).read_text(), (ROOT / copy).read_text(), replaced)

    assert not missing, (
        f"{copy} lacks {len(missing)} line(s) of {original}: bring the original's change into the copy "
        f"(a three-way merge from the copy's last common version), or list a line the copy replaces here. "
        f"First: {missing[:5]}"
    )


@pytest.mark.parametrize("original, copy, replaced", FORKS, ids=[fork[1] for fork in FORKS])
def test_a_change_to_an_original_is_caught(original, copy, replaced):
    """Negative control: a line added to the original, and a line both have
    once that the original then has again, must be reported as missing from
    the copy."""
    text = (ROOT / original).read_text()
    copy_text = (ROOT / copy).read_text()
    original_counts = Counter(line.rstrip() for line in text.splitlines())
    copy_counts = Counter(line.rstrip() for line in copy_text.splitlines())
    once = next(line for line in original_counts
                if line.strip() and line not in replaced and original_counts[line] == 1 and copy_counts[line] == 1)
    added = text + "\n/* a change made after the copy was taken */\n"
    doubled = text + "\n" + once + "\n"

    assert missing_lines(added, copy_text, replaced) == ["/* a change made after the copy was taken */"]
    assert missing_lines(doubled, copy_text, replaced) == [once]
