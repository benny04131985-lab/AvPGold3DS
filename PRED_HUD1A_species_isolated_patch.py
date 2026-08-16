#!/usr/bin/env python3
r"""
Install PRED-HUD1A as a species-isolated diagnostic capture.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Safety design:
  - All edits are prepared and validated in memory first.
  - No file is written unless every expected anchor is found exactly once.
  - The Marine backdrop logic is changed only inside
    AvP3DS_DrawBottomFrame(), never globally.
  - Marine capture groups 0-3 retain their existing lower-screen-only path.
  - Predator capture groups 4-5 remain visible upstairs during diagnosis.
  - If any write fails, both source files are restored from backups.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1A-SPECIES-ISOLATED"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def regex_once(
    text: str,
    pattern: str,
    replacement,
    label: str,
    flags: int = 0,
) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return result


def split_bottom_function(text: str) -> tuple[str, str, str]:
    start_marker = "static void AvP3DS_DrawBottomFrame(void)"
    end_marker = "\nvoid AvP3DS_GameFrameEnd(void)"

    start = text.find(start_marker)
    if start < 0:
        raise RuntimeError("AvP3DS_DrawBottomFrame start was not found")

    end = text.find(end_marker, start)
    if end < 0:
        raise RuntimeError("AvP3DS_DrawBottomFrame end was not found")

    return text[:start], text[start:end], text[end:]


def main() -> int:
    root = Path.cwd().resolve()
    hud_path = root / "src" / "avp" / "hud.c"
    main_path = root / "src" / "main_3ds.c"

    if not hud_path.is_file() or not main_path.is_file():
        print("ERROR: run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    hud_original = hud_path.read_text(encoding="utf-8")
    main_original = main_path.read_text(encoding="utf-8")

    if MARKER in hud_original or MARKER in main_original:
        print("ERROR: this patch is already installed.")
        print("Nothing changed.")
        return 1

    for required in ("AVP-HUD1F", "AVP-HUD1G1"):
        if required not in main_original:
            print(f"ERROR: sealed Marine marker missing: {required}")
            print("Refusing to patch an unexpected baseline.")
            print("Nothing changed.")
            return 1

    hud = hud_original
    main_c = main_original

    try:
        # --------------------------------------------------------------
        # hud.c: add two Predator-only capture IDs.
        # --------------------------------------------------------------
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>\s*)#define AVP3DS_HUD_GROUP_MESSAGES\s+3\s*$",
            lambda m: (
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_MESSAGES    3\n"
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_WRIST  4\n"
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_STATUS 5"
            ),
            "hud.c Predator group definitions",
        )

        # Predator wrist capture. Upper draw remains enabled by main_3ds logic.
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>\s*)DrawWristDisplay\(\);\s*$",
            lambda m: (
                f"{m.group('i')}/* {MARKER}: Predator wrist only. */\n"
                f"{m.group('i')}#ifdef __3DS__\n"
                f"{m.group('i')}AvP3DS_BeginHUDCapture(\n"
                f"{m.group('i')}    AVP3DS_HUD_GROUP_PRED_WRIST);\n"
                f"{m.group('i')}#endif\n\n"
                f"{m.group('i')}DrawWristDisplay();\n\n"
                f"{m.group('i')}#ifdef __3DS__\n"
                f"{m.group('i')}AvP3DS_EndHUDCapture();\n"
                f"{m.group('i')}#endif"
            ),
            "hud.c DrawWristDisplay call",
        )

        # Predator health/field-charge capture.
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>\s*)DisplayPredatorHealthAndEnergy\(\);\s*$",
            lambda m: (
                f"{m.group('i')}/* {MARKER}: Predator status only. */\n"
                f"{m.group('i')}#ifdef __3DS__\n"
                f"{m.group('i')}AvP3DS_BeginHUDCapture(\n"
                f"{m.group('i')}    AVP3DS_HUD_GROUP_PRED_STATUS);\n"
                f"{m.group('i')}#endif\n\n"
                f"{m.group('i')}DisplayPredatorHealthAndEnergy();\n\n"
                f"{m.group('i')}#ifdef __3DS__\n"
                f"{m.group('i')}AvP3DS_EndHUDCapture();\n"
                f"{m.group('i')}#endif"
            ),
            "hud.c DisplayPredatorHealthAndEnergy call",
        )

        # --------------------------------------------------------------
        # main_3ds.c: extend group IDs/count.
        # --------------------------------------------------------------
        main_c = regex_once(
            main_c,
            r"(?m)^#define AVP3DS_HUD_GROUP_MESSAGES\s+3\s*\n"
            r"#define AVP3DS_HUD_GROUP_COUNT\s+4\s*$",
            (
                "#define AVP3DS_HUD_GROUP_MESSAGES    3\n"
                "#define AVP3DS_HUD_GROUP_PRED_WRIST  4\n"
                "#define AVP3DS_HUD_GROUP_PRED_STATUS 5\n"
                "#define AVP3DS_HUD_GROUP_COUNT       6"
            ),
            "main_3ds.c Predator group definitions/count",
        )

        # Add two Predator-only diagnostic layout boxes after the final
        # Marine message box. Existing Marine entries are not altered.
        main_c = regex_once(
            main_c,
            r"(?ms)"
            r"(?P<comment>\s*/\* Mission/objective text:[^\n]*\*/\s*\n)"
            r"(?P<entry>\s*\{\s*40\.0f,\s*0\.0f,\s*240\.0f,\s*32\.0f,\s*0\.86f\s*\})"
            r"(?P<close>\s*\n\};)",
            lambda m: (
                f"{m.group('comment')}"
                f"{m.group('entry')},\n\n"
                "    /* PRED-HUD1A: Predator wrist diagnostic box. */\n"
                "    { 80.0f, 72.0f, 160.0f, 96.0f, 1.00f },\n\n"
                "    /* PRED-HUD1A: Predator health/energy full-screen rails. */\n"
                "    { 0.0f, 0.0f, 320.0f, 240.0f, 1.00f }"
                f"{m.group('close')}"
            ),
            "main_3ds.c Predator layout boxes",
        )

        # Existing HUD1F behavior suppresses all captured groups. Keep that
        # behavior for Marine IDs 0-3 only; Predator IDs 4-5 stay upstairs.
        main_c = replace_once(
            main_c,
            "    if (!avp3ds_tracker_capture_enabled)\n"
            "    {\n"
            "        C3D_DrawArrays(",
            "    /*\n"
            f"     * {MARKER}: species-isolated upper-screen policy.\n"
            "     * Marine groups 0-3 remain suppressed upstairs.\n"
            "     * Predator groups 4-5 remain visible during diagnosis.\n"
            "     */\n"
            "    if (!avp3ds_tracker_capture_enabled ||\n"
            "        avp3ds_hud_capture_group >= AVP3DS_HUD_GROUP_PRED_WRIST)\n"
            "    {\n"
            "        C3D_DrawArrays(",
            "main_3ds.c upper-screen capture policy",
        )

        # --------------------------------------------------------------
        # Modify Marine-backdrop selection ONLY inside DrawBottomFrame.
        # --------------------------------------------------------------
        before, bottom, after = split_bottom_function(main_c)

        declaration_anchor = (
            "    unsigned int cellX;\n"
            "    unsigned int cellY;\n"
        )
        bottom = replace_once(
            bottom,
            declaration_anchor,
            declaration_anchor
            + "\n"
            + f"    /* {MARKER}: true only for Predator capture frames. */\n"
            + "    bool predatorHUDCaptured = false;\n",
            "DrawBottomFrame Predator flag declaration",
        )

        detector_anchor = (
            "    gridVertexOffset = avp3ds_game_vertex_cursor;\n"
        )
        detector = (
            f"    /* {MARKER}: detect Predator by its dedicated capture IDs. */\n"
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
        bottom = replace_once(
            bottom,
            detector_anchor,
            detector,
            "DrawBottomFrame Predator detector",
        )

        marine_condition = "if (avp3ds_marine_hud_texture_initialized)"
        local_count = bottom.count(marine_condition)
        if local_count != 2:
            raise RuntimeError(
                "DrawBottomFrame Marine backdrop anchors: "
                f"expected 2, found {local_count}"
            )

        bottom = bottom.replace(
            marine_condition,
            "if (!predatorHUDCaptured && "
            "avp3ds_marine_hud_texture_initialized)",
        )

        # Critical safety assertion: no Marine-backdrop condition outside
        # DrawBottomFrame was changed.
        main_c = before + bottom + after

        outside_original = (
            main_original[:main_original.find("static void AvP3DS_DrawBottomFrame(void)")]
            + main_original[
                main_original.find("\nvoid AvP3DS_GameFrameEnd(void)",
                                   main_original.find("static void AvP3DS_DrawBottomFrame(void)"))
            :]
        )
        outside_new = before + after

        # The outside sections legitimately changed earlier (defines/layout/
        # upper draw policy), but the raw Marine backdrop condition count
        # outside DrawBottomFrame must remain identical.
        if outside_original.count(marine_condition) != outside_new.count(marine_condition):
            raise RuntimeError(
                "Marine backdrop logic outside DrawBottomFrame would change"
            )

        # Final validation.
        if hud.count(MARKER) != 2:
            raise RuntimeError("hud.c marker count verification failed")

        if main_c.count(MARKER) < 3:
            raise RuntimeError("main_3ds.c marker verification failed")

        if "AVP3DS_HUD_GROUP_COUNT       6" not in main_c:
            raise RuntimeError("HUD group count verification failed")

        if main_c.count(
            "if (!predatorHUDCaptured && "
            "avp3ds_marine_hud_texture_initialized)"
        ) != 2:
            raise RuntimeError("species-specific backdrop verification failed")

        # The final Marine coordinates must still be present exactly.
        marine_layout_checks = (
            "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
            "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
            "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
            "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
        )
        for entry in marine_layout_checks:
            if main_c.count(entry) != 1:
                raise RuntimeError(
                    f"sealed Marine layout verification failed: {entry}"
                )

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    hud_backup = hud_path.with_name("hud.c.pre-PRED-HUD1A-SPECIES.bak")
    main_backup = main_path.with_name(
        "main_3ds.c.pre-PRED-HUD1A-SPECIES.bak"
    )

    if hud_backup.exists() or main_backup.exists():
        print("ERROR: species-isolated backup already exists.")
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

    print("PRED-HUD1A species-isolated patch installed.")
    print(f"Backup: {hud_backup}")
    print(f"Backup: {main_backup}")
    print()
    print("Marine guarantees:")
    print("  - Existing four layout coordinates were verified unchanged.")
    print("  - Marine backdrop remains selected on non-Predator capture frames.")
    print("  - Marine groups 0-3 remain hidden upstairs.")
    print("  - AVP-HUD1G1 tracker lock remains untouched.")
    print()
    print("Predator diagnostic behavior:")
    print("  - Predator uses diagnostic grid, not the Marine backdrop.")
    print("  - Wrist and health/energy are replayed downstairs.")
    print("  - Predator wrist/status remain visible upstairs for this test.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
