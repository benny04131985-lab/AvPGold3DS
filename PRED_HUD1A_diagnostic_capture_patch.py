#!/usr/bin/env python3
"""
Install PRED-HUD1A: first Predator lower-screen diagnostic capture.

Run from:
    C:\Projects\AvP3DS\Source
or:
    /c/Projects/AvP3DS/Source

Changes:
  - src/avp/hud.c
      * Adds dedicated Predator wrist and status capture groups.
      * Captures DrawWristDisplay().
      * Captures DisplayPredatorHealthAndEnergy().
  - src/main_3ds.c
      * Expands the HUD layout table from 4 to 6 groups.
      * Keeps the completed Marine upper-HUD suppression behavior.
      * Leaves captured Predator HUD visible on the upper screen for diagnosis.
      * Uses the diagnostic grid instead of the Marine backdrop when Predator
        capture groups are present.
      * Replays Predator status full-screen and the wrist display centrally.

The active Marine HUD layout, tracker lock, and Marine backdrop are preserved.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1A Predator diagnostic capture"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def regex_replace_once(
    text: str,
    pattern: str,
    replacement,
    label: str,
    flags: int = 0,
) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one regex match, found {count}")
    return result


def main() -> int:
    root = Path.cwd().resolve()
    hud_path = root / "src" / "avp" / "hud.c"
    main_path = root / "src" / "main_3ds.c"

    if not hud_path.exists() or not main_path.exists():
        print("ERROR: Run this from C:\\Projects\\AvP3DS\\Source")
        print(f"Missing: {hud_path if not hud_path.exists() else main_path}")
        return 1

    hud = hud_path.read_text(encoding="utf-8", errors="strict")
    main_c = main_path.read_text(encoding="utf-8", errors="strict")

    if MARKER in hud or MARKER in main_c:
        print("ERROR: PRED-HUD1A already appears to be installed; nothing changed.")
        return 1

    for required in ("AVP-HUD1F", "AVP-HUD1G1"):
        if required not in main_c:
            print(f"ERROR: required Marine milestone marker missing: {required}")
            print("Nothing changed.")
            return 1

    original_hud = hud
    original_main = main_c

    try:
        hud = regex_replace_once(
            hud,
            r"(?m)^(?P<indent>\s*)#define AVP3DS_HUD_GROUP_MESSAGES\s+3\s*$",
            lambda m: (
                f"{m.group('indent')}#define AVP3DS_HUD_GROUP_MESSAGES    3\n"
                f"{m.group('indent')}#define AVP3DS_HUD_GROUP_PRED_WRIST  4\n"
                f"{m.group('indent')}#define AVP3DS_HUD_GROUP_PRED_STATUS 5"
            ),
            "hud.c Predator group defines",
        )

        hud = regex_replace_once(
            hud,
            r"(?m)^(?P<indent>\s*)DrawWristDisplay\(\);\s*$",
            lambda m: (
                f"{m.group('indent')}/* {MARKER}: wrist display. */\n"
                f"{m.group('indent')}#ifdef __3DS__\n"
                f"{m.group('indent')}AvP3DS_BeginHUDCapture(\n"
                f"{m.group('indent')}    AVP3DS_HUD_GROUP_PRED_WRIST);\n"
                f"{m.group('indent')}#endif\n\n"
                f"{m.group('indent')}DrawWristDisplay();\n\n"
                f"{m.group('indent')}#ifdef __3DS__\n"
                f"{m.group('indent')}AvP3DS_EndHUDCapture();\n"
                f"{m.group('indent')}#endif"
            ),
            "hud.c DrawWristDisplay call",
        )

        hud = regex_replace_once(
            hud,
            r"(?m)^(?P<indent>\s*)DisplayPredatorHealthAndEnergy\(\);\s*$",
            lambda m: (
                f"{m.group('indent')}/* {MARKER}: health and field charge. */\n"
                f"{m.group('indent')}#ifdef __3DS__\n"
                f"{m.group('indent')}AvP3DS_BeginHUDCapture(\n"
                f"{m.group('indent')}    AVP3DS_HUD_GROUP_PRED_STATUS);\n"
                f"{m.group('indent')}#endif\n\n"
                f"{m.group('indent')}DisplayPredatorHealthAndEnergy();\n\n"
                f"{m.group('indent')}#ifdef __3DS__\n"
                f"{m.group('indent')}AvP3DS_EndHUDCapture();\n"
                f"{m.group('indent')}#endif"
            ),
            "hud.c DisplayPredatorHealthAndEnergy call",
        )

        main_c = regex_replace_once(
            main_c,
            r"(?m)^#define AVP3DS_HUD_GROUP_MESSAGES\s+3\s*\n"
            r"#define AVP3DS_HUD_GROUP_COUNT\s+4\s*$",
            (
                "#define AVP3DS_HUD_GROUP_MESSAGES    3\n"
                "#define AVP3DS_HUD_GROUP_PRED_WRIST  4\n"
                "#define AVP3DS_HUD_GROUP_PRED_STATUS 5\n"
                "#define AVP3DS_HUD_GROUP_COUNT       6"
            ),
            "main_3ds.c Predator group defines/count",
        )

        main_c = regex_replace_once(
            main_c,
            r"(?ms)"
            r"(?P<comment>\s*/\* Mission/objective text:[^\n]*\*/\s*\n)"
            r"(?P<entry>\s*\{\s*40\.0f,\s*0\.0f,\s*240\.0f,\s*32\.0f,\s*0\.86f\s*\})"
            r"(?P<close>\s*\n\};)",
            lambda m: (
                f"{m.group('comment')}"
                f"{m.group('entry')},\n\n"
                "    /* PRED-HUD1A: centered live wrist-display diagnostic. */\n"
                "    { 80.0f, 72.0f, 160.0f, 96.0f, 1.00f },\n\n"
                "    /* PRED-HUD1A: preserve left/right health-energy rails. */\n"
                "    { 0.0f, 0.0f, 320.0f, 240.0f, 1.00f }"
                f"{m.group('close')}"
            ),
            "main_3ds.c layout table extension",
        )

        main_c = replace_once(
            main_c,
            "    if (!avp3ds_tracker_capture_enabled)\n"
            "    {\n"
            "        C3D_DrawArrays(",
            "    /*\n"
            f"     * {MARKER}.\n"
            "     * Marine groups 0-3 remain lower-screen-only. Predator groups\n"
            "     * 4-5 are captured for the lower screen but still submitted\n"
            "     * upstairs until their lower layout is hardware-proven.\n"
            "     */\n"
            "    if (!avp3ds_tracker_capture_enabled ||\n"
            "        avp3ds_hud_capture_group >= AVP3DS_HUD_GROUP_PRED_WRIST)\n"
            "    {\n"
            "        C3D_DrawArrays(",
            "main_3ds.c species-aware upper suppression",
        )

        detector_anchor = (
            "    if (avp3ds_game_vertex_cursor +\n"
            "            AVP3DS_BOTTOM_GRID_MAX_VERTICES >\n"
            "        AVP3DS_GAME_MAX_VERTICES)\n"
        )

        detector_block = (
            "    /*\n"
            f"     * {MARKER}.\n"
            "     * Group presence is a reliable species signal here and avoids\n"
            "     * coupling the platform renderer directly to AvP game structs.\n"
            "     */\n"
            "    bool predatorHUDCaptured = false;\n\n"
            "    for (captureIndex = 0;\n"
            "         captureIndex < avp3ds_tracker_capture_count;\n"
            "         ++captureIndex)\n"
            "    {\n"
            "        const int capturedGroup =\n"
            "            avp3ds_tracker_capture_batches[captureIndex].hudGroup;\n\n"
            "        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_STATUS)\n"
            "        {\n"
            "            predatorHUDCaptured = true;\n"
            "            break;\n"
            "        }\n"
            "    }\n\n"
            + detector_anchor
        )

        main_c = replace_once(
            main_c,
            detector_anchor,
            detector_block,
            "main_3ds.c Predator capture detector",
        )

        backdrop_condition = "if (avp3ds_marine_hud_texture_initialized)"
        condition_count = main_c.count(backdrop_condition)
        if condition_count != 2:
            raise RuntimeError(
                "main_3ds.c Marine backdrop conditions: "
                f"expected 2 anchors, found {condition_count}"
            )

        main_c = main_c.replace(
            backdrop_condition,
            "if (!predatorHUDCaptured && "
            "avp3ds_marine_hud_texture_initialized)",
        )

        if hud == original_hud or main_c == original_main:
            raise RuntimeError("one or both source files were not modified")

        if main_c.count("AVP3DS_HUD_GROUP_COUNT       6") != 1:
            raise RuntimeError("final HUD group count verification failed")

        if hud.count(MARKER) < 2 or main_c.count(MARKER) < 2:
            raise RuntimeError("milestone marker verification failed")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    hud_backup = hud_path.with_name("hud.c.pre-PRED-HUD1A.bak")
    main_backup = main_path.with_name("main_3ds.c.pre-PRED-HUD1A.bak")

    if hud_backup.exists() or main_backup.exists():
        print("ERROR: one or more PRED-HUD1A backup files already exist.")
        print("Nothing changed.")
        return 1

    shutil.copy2(hud_path, hud_backup)
    shutil.copy2(main_path, main_backup)

    try:
        hud_path.write_text(hud, encoding="utf-8")
        main_path.write_text(main_c, encoding="utf-8")
    except Exception:
        shutil.copy2(hud_backup, hud_path)
        shutil.copy2(main_backup, main_path)
        raise

    print("PRED-HUD1A installed successfully.")
    print(f"Backup: {hud_backup}")
    print(f"Backup: {main_backup}")
    print()
    print("Expected hardware result:")
    print("  Marine: unchanged completed lower HUD; duplicate upper HUD remains hidden.")
    print("  Predator upper: original wrist/status/sights/vision remain visible.")
    print("  Predator lower: diagnostic grid with live wrist display and health/energy.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
