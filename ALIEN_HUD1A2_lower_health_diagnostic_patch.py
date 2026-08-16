#!/usr/bin/env python3
r"""
Install ALIEN-HUD1A: Alien health on the lower screen.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

This first Alien pass is deliberately narrow:
  - Adds one dedicated Alien health capture group.
  - Moves the live Alien health/armour display to a bottom-center box.
  - Keeps Alien teeth, tongue, claws, weapon, and vision effects upstairs.
  - Uses the diagnostic grid for Alien until the final Alien backdrop is chosen.
  - The captured Alien health is lower-screen-only, matching the completed
    Marine and Predator upper-HUD suppression policy.

Preserved:
  - completed Marine lower HUD and tracker lock
  - completed Predator lower HUD, spear ammo, and message placement
  - universal black lower screen on death
  - current Marine and Predator texture routing
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "ALIEN-HUD1A2-LOWER-HEALTH"


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


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 anchor, found {count}")
    return text.replace(old, new, 1)


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
        print("ERROR: ALIEN-HUD1A is already installed.")
        print("Nothing changed.")
        return 1

    required_markers = (
        "AVP-HUD1G1 tracker transform lock",
        "PRED-HUD1F3-SPEAR-AMMO-PINNED",
        "PRED-HUD1G-MESSAGE-RECENTER",
        "PRED-HUD1H-UPPER-SUPPRESS",
        "PRED-HUD1E3-SWAP-AND-DEATH-BLACK",
    )

    for marker in required_markers:
        if marker not in main_original and marker not in hud_original:
            print(f"ERROR: required completed-HUD marker missing: {marker}")
            print("Nothing changed.")
            return 1

    marine_entries = (
        "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
        "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
        "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
        "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
    )

    predator_entries = (
        "{ 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }",
        "{ 116.0f, 72.0f, 88.0f, 32.0f, 1.25f }",
    )

    for entry in marine_entries + predator_entries:
        if main_original.count(entry) != 1:
            print(f"ERROR: completed layout entry missing or duplicated: {entry}")
            print("Nothing changed.")
            return 1

    hud = hud_original
    main_c = main_original

    try:
        # ----------------------------------------------------------
        # hud.c: group 8 and capture around Alien health only.
        # ----------------------------------------------------------
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>[ \t]*)#define[ \t]+"
            r"AVP3DS_HUD_GROUP_PRED_AMMO[ \t]+7[ \t]*$",
            lambda m: (
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_AMMO     7\n"
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_ALIEN_STATUS  8"
            ),
            "hud.c Alien group define",
        )

        alien_call_pattern = re.compile(
            r"(?ms)"
            r"(?P<i>^[ \t]*)//flash health if invulnerable[ \t]*\n"
            r"(?P=i)if\(\(playerStatusPtr->invulnerabilityTimer/12000 %2\)==0\)"
            r"[ \t]*\n"
            r"(?P=i)\{[ \t]*\n"
            r"(?P<body>[ \t]*)DisplayHealthAndArmour\(\);[ \t]*\n"
            r"(?P=i)\}",
        )

        def replace_alien_call(match: re.Match[str]) -> str:
            i = match.group("i")
            b = match.group("body")
            return (
                f"{i}//flash health if invulnerable\n"
                f"{i}if((playerStatusPtr->invulnerabilityTimer/12000 %2)==0)\n"
                f"{i}{{\n"
                f"{b}/* {MARKER}: Alien health only. */\n"
                f"{b}#ifdef __3DS__\n"
                f"{b}AvP3DS_BeginHUDCapture(\n"
                f"{b}    AVP3DS_HUD_GROUP_ALIEN_STATUS);\n"
                f"{b}#endif\n\n"
                f"{b}DisplayHealthAndArmour();\n\n"
                f"{b}#ifdef __3DS__\n"
                f"{b}AvP3DS_EndHUDCapture();\n"
                f"{b}#endif\n"
                f"{i}}}"
            )

        hud, alien_call_count = alien_call_pattern.subn(
            replace_alien_call,
            hud,
            count=1,
        )
        if alien_call_count != 1:
            raise RuntimeError(
                "Alien health caller capture: "
                f"expected 1 branch, found {alien_call_count}"
            )

        # ----------------------------------------------------------
        # main_3ds.c: group 8/count 9 and lower-center layout.
        # ----------------------------------------------------------
        main_c = regex_once(
            main_c,
            r"(?m)^#define[ \t]+AVP3DS_HUD_GROUP_PRED_AMMO[ \t]+7[ \t]*\n"
            r"#define[ \t]+AVP3DS_HUD_GROUP_COUNT[ \t]+8[ \t]*$",
            (
                "#define AVP3DS_HUD_GROUP_PRED_AMMO     7\n"
                "#define AVP3DS_HUD_GROUP_ALIEN_STATUS  8\n"
                "#define AVP3DS_HUD_GROUP_COUNT         9"
            ),
            "main_3ds.c Alien group/count",
        )

        main_c = regex_once(
            main_c,
            r"(?ms)"
            r"(?P<comment>[ \t]*/\* PRED-HUD1F: spear-gun ammo in center "
            r"red box\. \*/[ \t]*\n)"
            r"(?P<entry>[ \t]*\{[ \t]*116\.0f,[ \t]*72\.0f,[ \t]*88\.0f,"
            r"[ \t]*32\.0f,[ \t]*1\.25f[ \t]*\})"
            r"(?P<close>[ \t]*\n\};)",
            lambda m: (
                f"{m.group('comment')}"
                f"{m.group('entry')},\n\n"
                "    /* ALIEN-HUD1A: Alien health along the lower edge. */\n"
                "    { 64.0f, 184.0f, 192.0f, 44.0f, 1.00f }"
                f"{m.group('close')}"
            ),
            "Alien lower health layout box",
        )

        # Add an Alien frame flag beside the existing Predator flag.
        predator_flag_anchor = (
            "    /* PRED-HUD1A-SPECIES-ISOLATED: true only for Predator capture frames. */\n"
            "    bool predatorHUDCaptured = false;\n"
        )
        flag_replacement = predator_flag_anchor + (
            f"\n    /* {MARKER}: true only for Alien health capture frames. */\n"
            "    bool alienHUDCaptured = false;\n"
        )
        main_c = replace_once(
            main_c,
            predator_flag_anchor,
            flag_replacement,
            "Alien frame flag",
        )

        # Detect Alien before the established Predator capture test.
        detector_anchor = (
            "        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||\n"
        )
        detector_replacement = (
            "        if (capturedGroup == AVP3DS_HUD_GROUP_ALIEN_STATUS)\n"
            "        {\n"
            "            alienHUDCaptured = true;\n"
            "            break;\n"
            "        }\n\n"
            + detector_anchor
        )
        main_c = replace_once(
            main_c,
            detector_anchor,
            detector_replacement,
            "Alien species detector",
        )

        # Alien must use the diagnostic grid, never the Marine backdrop.
        # The exact same Marine fallback condition occurs twice inside
        # AvP3DS_DrawBottomFrame(): once for backdrop geometry and once for
        # texture binding. Both are intentional and both must be gated.
        marine_fallback = (
            "    else if (!predatorHUDCaptured &&\n"
            "             avp3ds_marine_hud_texture_initialized)\n"
        )
        alien_safe_fallback = (
            "    else if (!predatorHUDCaptured &&\n"
            "             !alienHUDCaptured &&\n"
            "             avp3ds_marine_hud_texture_initialized)\n"
        )

        fallback_count = main_c.count(marine_fallback)

        if fallback_count != 2:
            raise RuntimeError(
                "Alien-safe Marine fallback routing: "
                f"expected 2 anchors, found {fallback_count}"
            )

        main_c = main_c.replace(
            marine_fallback,
            alien_safe_fallback,
            2,
        )

        # ----------------------------------------------------------
        # Safety verification.
        # ----------------------------------------------------------
        for entry in marine_entries + predator_entries:
            if main_c.count(entry) != 1:
                raise RuntimeError(f"completed layout changed: {entry}")

        for marker in required_markers:
            if marker not in main_c and marker not in hud:
                raise RuntimeError(f"completed milestone behavior lost: {marker}")

        if main_c.count("romfs:/Marine_WY_HUD.rgba") != 1:
            raise RuntimeError("Marine texture path changed")

        if main_c.count("romfs:/Predator_HUD_Backdrop_320x240.rgba") != 1:
            raise RuntimeError("Predator texture path changed")

        if "const float topY = 12.0f;" not in main_c:
            raise RuntimeError("Predator +12 backdrop offset changed")

        death_clear = (
            "    C3D_RenderTargetClear(\n"
            "        avp3ds_bottom_target,\n"
            "        C3D_CLEAR_ALL,\n"
            "        0x000000FF,\n"
            "        0);\n"
        )
        if main_c.count(death_clear) != 1:
            raise RuntimeError("universal death-black clear changed")

        if "AVP3DS_HUD_GROUP_COUNT         9" not in main_c:
            raise RuntimeError("HUD group count 9 verification failed")

        if hud.count("AVP3DS_HUD_GROUP_ALIEN_STATUS") != 2:
            raise RuntimeError("hud.c Alien group/caller verification failed")

        if main_c.count("AVP3DS_HUD_GROUP_ALIEN_STATUS") < 2:
            raise RuntimeError("main_3ds.c Alien group/detector verification failed")

        if hud.count(MARKER) != 1 or main_c.count(MARKER) != 1:
            raise RuntimeError("ALIEN-HUD1A marker verification failed")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    hud_backup = hud_path.with_name("hud.c.pre-ALIEN-HUD1A2.bak")
    main_backup = main_path.with_name("main_3ds.c.pre-ALIEN-HUD1A2.bak")

    if hud_backup.exists() or main_backup.exists():
        print("ERROR: ALIEN-HUD1A backup already exists.")
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

    print("ALIEN-HUD1A2 installed successfully.")
    print(f"Backup: {hud_backup}")
    print(f"Backup: {main_backup}")
    print()
    print("Expected Alien result:")
    print("  Upper: world, claws, teeth, tongue, and vision remain")
    print("  Upper: duplicated health display is hidden")
    print("  Lower: diagnostic grid with live health at the bottom center")
    print()
    print("Marine and Predator completed HUDs were verified unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
