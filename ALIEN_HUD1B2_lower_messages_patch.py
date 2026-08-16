#!/usr/bin/env python3
r"""
Install ALIEN-HUD1B: copy the Alien message ticker to the lower screen.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Effect:
  - Adds a dedicated Alien message capture group.
  - Places Alien messages in the same upper-center region used by Predator:
        x=40, y=16, width=240, height=32
  - Keeps the Alien health placement unchanged.
  - Uses Alien message capture as an additional species signal, so brief
    invulnerability-health blink frames cannot fall through to the Marine
    backdrop path.
  - Captured Alien messages remain lower-screen-only under the completed
    generic upper-HUD suppression policy.

Preserved:
  - completed Marine HUD
  - completed Predator HUD
  - Alien teeth, tongue, claws, weapon, and vision effects upstairs
  - universal black lower screen on death
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "ALIEN-HUD1B2-LOWER-MESSAGES"


def regex_once(
    text: str,
    pattern: str,
    replacement,
    label: str,
    flags: int = 0,
) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 match, found {count}")
    return result


def main() -> int:
    root = Path.cwd().resolve()
    hud_path = root / "src" / "avp" / "hud.c"
    main_path = root / "src" / "main_3ds.c"

    if (
        not hud_path.is_file()
        or not main_path.is_file()
        or not (root / "Makefile.3ds").is_file()
    ):
        print("ERROR: run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    hud_original = hud_path.read_text(encoding="utf-8")
    main_original = main_path.read_text(encoding="utf-8")

    if MARKER in hud_original or MARKER in main_original:
        print("ERROR: ALIEN-HUD1B is already installed.")
        print("Nothing changed.")
        return 1

    required_markers = (
        "ALIEN-HUD1A2-LOWER-HEALTH",
        "PRED-HUD1H-UPPER-SUPPRESS",
        "PRED-HUD1G-MESSAGE-RECENTER",
        "AVP-HUD1G1 tracker transform lock",
        "PRED-HUD1E3-SWAP-AND-DEATH-BLACK",
    )

    for marker in required_markers:
        if marker not in hud_original and marker not in main_original:
            print(f"ERROR: required baseline marker missing: {marker}")
            print("Nothing changed.")
            return 1

    required_layouts = (
        "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
        "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
        "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
        "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
        "{ 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }",
        "{ 116.0f, 72.0f, 88.0f, 32.0f, 1.25f }",
        "{ 64.0f, 184.0f, 192.0f, 44.0f, 1.00f }",
    )

    for entry in required_layouts:
        if main_original.count(entry) != 1:
            print(f"ERROR: completed layout entry missing or duplicated: {entry}")
            print("Nothing changed.")
            return 1

    hud = hud_original
    main_c = main_original

    try:
        # ----------------------------------------------------------
        # Add Alien message group 9 in both translation units.
        # ----------------------------------------------------------
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>[ \t]*)#define[ \t]+"
            r"AVP3DS_HUD_GROUP_ALIEN_STATUS[ \t]+8[ \t]*$",
            lambda m: (
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_ALIEN_STATUS   8\n"
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_ALIEN_MESSAGES 9"
            ),
            "hud.c Alien message group define",
        )

        main_c = regex_once(
            main_c,
            r"(?m)^#define[ \t]+AVP3DS_HUD_GROUP_ALIEN_STATUS[ \t]+8[ \t]*\n"
            r"#define[ \t]+AVP3DS_HUD_GROUP_COUNT[ \t]+9[ \t]*$",
            (
                "#define AVP3DS_HUD_GROUP_ALIEN_STATUS   8\n"
                "#define AVP3DS_HUD_GROUP_ALIEN_MESSAGES 9\n"
                "#define AVP3DS_HUD_GROUP_COUNT          10"
            ),
            "main_3ds.c Alien message group/count",
        )

        # ----------------------------------------------------------
        # Add Alien message layout after the existing Alien health box.
        # ----------------------------------------------------------
        main_c = regex_once(
            main_c,
            r"(?ms)"
            r"(?P<comment>[ \t]*/\* ALIEN-HUD1A: Alien health along the "
            r"lower edge\. \*/[ \t]*\n)"
            r"(?P<entry>[ \t]*\{[ \t]*64\.0f,[ \t]*184\.0f,[ \t]*192\.0f,"
            r"[ \t]*44\.0f,[ \t]*1\.00f[ \t]*\})"
            r"(?P<close>[ \t]*\n\};)",
            lambda m: (
                f"{m.group('comment')}"
                f"{m.group('entry')},\n\n"
                "    /* ALIEN-HUD1B: Alien ticker near the Predator position. */\n"
                "    { 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }"
                f"{m.group('close')}"
            ),
            "Alien message layout box",
        )

        # ----------------------------------------------------------
        # Extend the existing species-specific GADGET_Render capture.
        # We modify only the Predator begin branch and the shared end test.
        # ----------------------------------------------------------
        predator_begin_pattern = re.compile(
            r"""
            (?P<i>^[ \t]*)else[ \t]+if[ \t]*\(
                AvP\.PlayerType[ \t]*==[ \t]*I_Predator
            \)[ \t]*\n
            (?P=i)[ \t]+AvP3DS_BeginHUDCapture\([ \t]*\n
            (?P=i)[ \t]+AVP3DS_HUD_GROUP_PRED_MESSAGES\);[ \t]*$
            """,
            re.MULTILINE | re.VERBOSE,
        )

        def extend_begin(match: re.Match[str]) -> str:
            i = match.group("i")
            return (
                f"{i}else if (AvP.PlayerType == I_Predator)\n"
                f"{i}        AvP3DS_BeginHUDCapture(\n"
                f"{i}                AVP3DS_HUD_GROUP_PRED_MESSAGES);\n"
                f"{i}else if (AvP.PlayerType == I_Alien)\n"
                f"{i}        AvP3DS_BeginHUDCapture(\n"
                f"{i}                AVP3DS_HUD_GROUP_ALIEN_MESSAGES);"
            )

        hud, begin_count = predator_begin_pattern.subn(
            extend_begin,
            hud,
            count=1,
        )

        if begin_count != 1:
            raise RuntimeError(
                "Alien message begin capture extension: "
                f"expected 1 Predator branch, found {begin_count}"
            )

        end_condition_pattern = re.compile(
            r"""
            if[ \t]*\(
                AvP\.PlayerType[ \t]*==[ \t]*I_Marine
                [ \t]*\|\|[ \t]*
                AvP\.PlayerType[ \t]*==[ \t]*I_Predator
            \)
            (?=[ \t]*\n[ \t]*AvP3DS_EndHUDCapture\(\);)
            """,
            re.VERBOSE,
        )

        hud, end_count = end_condition_pattern.subn(
            "if (AvP.PlayerType == I_Marine || "
            "AvP.PlayerType == I_Predator || "
            "AvP.PlayerType == I_Alien)",
            hud,
            count=1,
        )

        if end_count != 1:
            raise RuntimeError(
                "Alien message end capture extension: "
                f"expected 1 condition, found {end_count}"
            )

        # Add a marker next to the Alien begin branch.
        marker_anchor = (
            "            else if (AvP.PlayerType == I_Alien)\n"
            "                    AvP3DS_BeginHUDCapture(\n"
            "                            AVP3DS_HUD_GROUP_ALIEN_MESSAGES);\n"
        )

        marker_replacement = (
            f"            /* {MARKER}: Alien message ticker. */\n"
            + marker_anchor
        )

        if hud.count(marker_anchor) != 1:
            raise RuntimeError(
                "Alien message marker anchor: "
                f"expected 1, found {hud.count(marker_anchor)}"
            )

        hud = hud.replace(marker_anchor, marker_replacement, 1)

        # ----------------------------------------------------------
        # Alien species detection must recognize status OR messages.
        # This also prevents invulnerability blink frames from falling
        # through to the Marine backdrop.
        # ----------------------------------------------------------
        alien_detector_pattern = re.compile(
            r"""
            if[ \t]*\(
                capturedGroup[ \t]*==[ \t]*
                AVP3DS_HUD_GROUP_ALIEN_STATUS
            \)
            """,
            re.VERBOSE,
        )

        main_c, detector_count = alien_detector_pattern.subn(
            "if (capturedGroup == AVP3DS_HUD_GROUP_ALIEN_STATUS ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_ALIEN_MESSAGES)",
            main_c,
            count=1,
        )

        if detector_count != 1:
            raise RuntimeError(
                "Alien detector extension: "
                f"expected 1 status test, found {detector_count}"
            )

        # Add a main_3ds marker beside the new layout comment.
        layout_marker_anchor = (
            "    /* ALIEN-HUD1B: Alien ticker near the Predator position. */\n"
        )
        layout_marker_replacement = (
            f"    /* {MARKER}: Alien ticker near the Predator position. */\n"
        )
        main_c = main_c.replace(
            layout_marker_anchor,
            layout_marker_replacement,
            1,
        )

        # ----------------------------------------------------------
        # Safety checks.
        # ----------------------------------------------------------
        # All previously unique layouts must remain unique, except the
        # Predator message box coordinates which Alien intentionally reuses.
        shared_message_layout = (
            "{ 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }"
        )

        for entry in required_layouts:
            if entry == shared_message_layout:
                continue

            if main_c.count(entry) != 1:
                raise RuntimeError(f"completed layout changed: {entry}")

        if main_c.count(shared_message_layout) != 2:
            raise RuntimeError(
                "Predator/Alien message layout duplication verification failed: "
                f"expected 2, found {main_c.count(shared_message_layout)}"
            )

        for marker in required_markers:
            if marker not in hud and marker not in main_c:
                raise RuntimeError(f"milestone behavior lost: {marker}")

        if main_c.count("romfs:/Marine_WY_HUD.rgba") != 1:
            raise RuntimeError("Marine backdrop path changed")

        if main_c.count("romfs:/Predator_HUD_Backdrop_320x240.rgba") != 1:
            raise RuntimeError("Predator backdrop path changed")

        death_clear = (
            "    C3D_RenderTargetClear(\n"
            "        avp3ds_bottom_target,\n"
            "        C3D_CLEAR_ALL,\n"
            "        0x000000FF,\n"
            "        0);\n"
        )
        if main_c.count(death_clear) != 1:
            raise RuntimeError("universal death-black clear changed")

        if "AVP3DS_HUD_GROUP_COUNT          10" not in main_c:
            raise RuntimeError("HUD group count 10 verification failed")

        if hud.count("AVP3DS_HUD_GROUP_ALIEN_MESSAGES") != 2:
            raise RuntimeError("hud.c Alien message group/caller verification failed")

        if main_c.count("AVP3DS_HUD_GROUP_ALIEN_MESSAGES") < 2:
            raise RuntimeError(
                "main_3ds.c Alien message group/detector verification failed"
            )

        if hud.count(MARKER) != 1 or main_c.count(MARKER) != 1:
            raise RuntimeError("ALIEN-HUD1B marker verification failed")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    hud_backup = hud_path.with_name("hud.c.pre-ALIEN-HUD1B2.bak")
    main_backup = main_path.with_name("main_3ds.c.pre-ALIEN-HUD1B2.bak")

    if hud_backup.exists() or main_backup.exists():
        print("ERROR: ALIEN-HUD1B backup already exists.")
        print("Nothing changed.")
        return 1

    shutil.copy2(hud_path, hud_backup)
    shutil.copy2(main_path, main_backup)

    try:
        hud_path.write_text(hud, encoding="utf-8")
        main_path.write_text(main_c, encoding="utf-8")
    except Exception as exc:
        shutil.copy2(hud_backup, hud_path)
        shutil.copy2(main_backup, main_path)
        print(f"ERROR while writing: {exc}")
        print("Both source files were restored.")
        return 1

    print("ALIEN-HUD1B2 installed successfully.")
    print(f"Backup: {hud_backup}")
    print(f"Backup: {main_backup}")
    print()
    print("Alien lower screen:")
    print("  - health remains at the bottom center")
    print("  - messages use x=40, y=16, w=240, h=32")
    print("  - diagnostic grid remains for backdrop design")
    print()
    print("Marine, Predator, death-black, and upper Alien effects were preserved.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
