#!/usr/bin/env python3
r"""
Install PRED-HUD1B: species-isolated Predator rail inset + message ticker.

This script is designed for the CURRENT partial state:
  - PRED-HUD1A species-isolated capture is already installed.
  - hud.c currently captures Marine OR Predator messages into group 3.
  - No avp3ds_predator_hud_frame flag is required.
  - Marine HUD1F/HUD1G1 must already be present.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Changes:
  src/avp/hud.c
    - Adds dedicated Predator message group 6.
    - Marine messages continue using group 3.
    - Predator messages use group 6.
    - Predator messages remain visible upstairs during diagnosis.

  src/main_3ds.c
    - Adds Predator message group 6 and increases group count to 7.
    - Adds a Predator-only message layout box in the second 16 px row.
    - Moves only Predator status-rail vertices inward by 32 px:
        left rail  -> +32 px
        right rail -> -32 px
    - Includes Predator message capture in Predator-frame detection.
    - Does NOT modify Marine coordinates, Marine backdrop routing,
      Marine top-HUD suppression, or the AVP-HUD1G1 tracker lock.

Safety:
  - All edits are validated in memory first.
  - Nothing is written unless every anchor matches exactly.
  - Backups are created before writing.
  - On write failure, both files are restored.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1B-SPECIES-ISOLATED"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 anchor, found {count}")
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
        raise RuntimeError(f"{label}: expected 1 match, found {count}")
    return result


def main() -> int:
    root = Path.cwd().resolve()
    hud_path = root / "src" / "avp" / "hud.c"
    main_path = root / "src" / "main_3ds.c"

    if not hud_path.is_file() or not main_path.is_file():
        print("ERROR: Run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    hud_original = hud_path.read_text(encoding="utf-8")
    main_original = main_path.read_text(encoding="utf-8")

    if MARKER in hud_original or MARKER in main_original:
        print("ERROR: PRED-HUD1B already appears to be installed.")
        print("Nothing changed.")
        return 1

    required_main_markers = (
        "AVP-HUD1F",
        "AVP-HUD1G1",
        "PRED-HUD1A-SPECIES-ISOLATED",
    )
    for marker in required_main_markers:
        if marker not in main_original:
            print(f"ERROR: required baseline marker missing: {marker}")
            print("Refusing to patch an unexpected source state.")
            print("Nothing changed.")
            return 1

    if "PRED-HUD1A-SPECIES-ISOLATED" not in hud_original:
        print("ERROR: PRED-HUD1A hud.c marker is missing.")
        print("Nothing changed.")
        return 1

    hud = hud_original
    main_c = main_original

    try:
        # --------------------------------------------------------------
        # hud.c: add dedicated Predator message capture group 6.
        # --------------------------------------------------------------
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>\s*)#define AVP3DS_HUD_GROUP_PRED_STATUS\s+5\s*$",
            lambda m: (
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_STATUS   5\n"
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_MESSAGES 6"
            ),
            "hud.c Predator message define",
        )

        # Replace the user's current combined Marine/Predator message capture
        # with dedicated species-specific capture group selection.
        message_pattern = (
            r"(?ms)"
            r"(?P<indent>[ \t]*)#ifdef __3DS__\s*\n"
            r"(?P=indent)if \(AvP\.PlayerType == I_Marine \|\| "
            r"AvP\.PlayerType == I_Predator\)\s*\n"
            r"(?P=indent)[ \t]+AvP3DS_BeginHUDCapture\(\s*\n"
            r"(?P=indent)[ \t]+AVP3DS_HUD_GROUP_MESSAGES\);\s*\n"
            r"(?P=indent)#endif\s*\n\s*"
            r"(?P=indent)GADGET_Render\(\);\s*\n\s*"
            r"(?P=indent)#ifdef __3DS__\s*\n"
            r"(?P=indent)if \(AvP\.PlayerType == I_Marine \|\| "
            r"AvP\.PlayerType == I_Predator\)\s*\n"
            r"(?P=indent)[ \t]+AvP3DS_EndHUDCapture\(\);\s*\n"
            r"(?P=indent)#endif"
        )

        def message_replacement(match: re.Match[str]) -> str:
            i = match.group("indent")
            return (
                f"{i}#ifdef __3DS__\n"
                f"{i}/* {MARKER}: dedicated species message groups. */\n"
                f"{i}if (AvP.PlayerType == I_Marine)\n"
                f"{i}        AvP3DS_BeginHUDCapture(\n"
                f"{i}                AVP3DS_HUD_GROUP_MESSAGES);\n"
                f"{i}else if (AvP.PlayerType == I_Predator)\n"
                f"{i}        AvP3DS_BeginHUDCapture(\n"
                f"{i}                AVP3DS_HUD_GROUP_PRED_MESSAGES);\n"
                f"{i}#endif\n\n"
                f"{i}GADGET_Render();\n\n"
                f"{i}#ifdef __3DS__\n"
                f"{i}if (AvP.PlayerType == I_Marine || "
                f"AvP.PlayerType == I_Predator)\n"
                f"{i}        AvP3DS_EndHUDCapture();\n"
                f"{i}#endif"
            )

        hud = regex_once(
            hud,
            message_pattern,
            message_replacement,
            "hud.c species-specific message capture",
        )

        # --------------------------------------------------------------
        # main_3ds.c: group 6, count 7.
        # --------------------------------------------------------------
        main_c = regex_once(
            main_c,
            r"(?m)^#define AVP3DS_HUD_GROUP_PRED_STATUS\s+5\s*\n"
            r"#define AVP3DS_HUD_GROUP_COUNT\s+6\s*$",
            (
                "#define AVP3DS_HUD_GROUP_PRED_STATUS   5\n"
                "#define AVP3DS_HUD_GROUP_PRED_MESSAGES 6\n"
                "#define AVP3DS_HUD_GROUP_COUNT         7"
            ),
            "main_3ds.c Predator message define/count",
        )

        # Add seventh layout entry after the existing Predator status entry.
        # y=16 places the capture in the second 16-pixel checker row.
        main_c = regex_once(
            main_c,
            r"(?ms)"
            r"(?P<comment>\s*/\* PRED-HUD1A: Predator health/energy "
            r"full-screen rails\. \*/\s*\n)"
            r"(?P<entry>\s*\{\s*0\.0f,\s*0\.0f,\s*320\.0f,\s*240\.0f,"
            r"\s*1\.00f\s*\})"
            r"(?P<close>\s*\n\};)",
            lambda m: (
                f"{m.group('comment')}"
                f"{m.group('entry')},\n\n"
                "    /* PRED-HUD1B: Predator message ticker, second row. */\n"
                "    { 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }"
                f"{m.group('close')}"
            ),
            "main_3ds.c Predator message layout box",
        )

        # Include the dedicated Predator message group in species detection.
        main_c = replace_once(
            main_c,
            "        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_STATUS)\n",
            "        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_STATUS ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_MESSAGES)\n",
            "main_3ds.c Predator capture detection",
        )

        # Insert Predator-only x translation immediately after the normal
        # transformed vertex is produced in the replay loop. This avoids
        # changing the shared transform structure or any Marine logic.
        transform_call = (
            "            AvP3DS_TransformHUDVertex(\n"
            "                &layoutVertices[vertexIndex],\n"
            "                &captureVertices[vertexIndex],\n"
            "                transform);\n"
        )

        inset_block = transform_call + (
            "\n"
            "            /*\n"
            f"             * {MARKER}: move only Predator status rails inward.\n"
            "             * Keep their original size and vertical placement.\n"
            "             */\n"
            "            if (group == AVP3DS_HUD_GROUP_PRED_STATUS)\n"
            "            {\n"
            "                const float sourcePixelX =\n"
            "                    (captureVertices[vertexIndex].position[0] + 1.0f) *\n"
            "                    (AVP3DS_BOTTOM_WIDTH * 0.5f);\n"
            "\n"
            "                float destinationPixelX =\n"
            "                    (layoutVertices[vertexIndex].position[0] + 1.0f) *\n"
            "                    (AVP3DS_BOTTOM_WIDTH * 0.5f);\n"
            "\n"
            "                if (sourcePixelX < AVP3DS_BOTTOM_WIDTH * 0.5f)\n"
            "                    destinationPixelX += 32.0f;\n"
            "                else\n"
            "                    destinationPixelX -= 32.0f;\n"
            "\n"
            "                layoutVertices[vertexIndex].position[0] =\n"
            "                    destinationPixelX /\n"
            "                    (AVP3DS_BOTTOM_WIDTH * 0.5f) - 1.0f;\n"
            "            }\n"
        )

        main_c = replace_once(
            main_c,
            transform_call,
            inset_block,
            "main_3ds.c Predator rail inset",
        )

        # --------------------------------------------------------------
        # Safety validation: sealed Marine state must remain present.
        # --------------------------------------------------------------
        marine_layout_entries = (
            "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
            "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
            "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
            "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
        )
        for entry in marine_layout_entries:
            if main_c.count(entry) != 1:
                raise RuntimeError(
                    f"sealed Marine layout verification failed: {entry}"
                )

        for marker in ("AVP-HUD1F", "AVP-HUD1G1"):
            if marker not in main_c:
                raise RuntimeError(
                    f"sealed Marine marker disappeared: {marker}"
                )

        if main_c.count(
            "if (!predatorHUDCaptured && "
            "avp3ds_marine_hud_texture_initialized)"
        ) != 2:
            raise RuntimeError(
                "Marine/Predator backdrop routing verification failed"
            )

        if "AVP3DS_HUD_GROUP_COUNT         7" not in main_c:
            raise RuntimeError("HUD group count verification failed")

        if hud.count(MARKER) != 1 or main_c.count(MARKER) != 1:
            raise RuntimeError("patch marker verification failed")

        if hud == hud_original or main_c == main_original:
            raise RuntimeError("one or both source files were not modified")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    hud_backup = hud_path.with_name("hud.c.pre-PRED-HUD1B.bak")
    main_backup = main_path.with_name("main_3ds.c.pre-PRED-HUD1B.bak")

    if hud_backup.exists() or main_backup.exists():
        print("ERROR: PRED-HUD1B backup already exists.")
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
        print(f"ERROR while writing files: {exc}")
        print("Both files were restored.")
        return 1

    print("PRED-HUD1B installed successfully.")
    print(f"Backup: {hud_backup}")
    print(f"Backup: {main_backup}")
    print()
    print("Predator changes:")
    print("  - Red health rail moved right 32 px.")
    print("  - Cyan field-charge rail moved left 32 px.")
    print("  - Predator message ticker uses dedicated group 6.")
    print("  - Predator ticker target starts at y=16.")
    print()
    print("Marine verification:")
    print("  - All four final Marine layout entries unchanged.")
    print("  - AVP-HUD1F and AVP-HUD1G1 still present.")
    print("  - Marine backdrop routing unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
