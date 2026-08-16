#!/usr/bin/env python3
r"""
Install PRED-HUD1B2: Predator rails inward + dedicated message ticker.

This version is written for the exact current source state after:
  - PRED-HUD1A-SPECIES-ISOLATED succeeded.
  - The user manually changed the existing message capture condition so
    Marine OR Predator currently use AVP3DS_HUD_GROUP_MESSAGES.
  - Earlier PRED-HUD1B script attempts aborted before writing.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Marine safety:
  - Verifies all four final Marine layout entries before writing.
  - Verifies AVP-HUD1F and AVP-HUD1G1 remain present.
  - Does not edit the Marine tracker-lock block.
  - Does not edit Marine backdrop conditions.
  - Does not edit Marine upper-HUD suppression behavior.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1B2-SPECIES-ISOLATED"


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
        print("ERROR: run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    hud_original = hud_path.read_text(encoding="utf-8")
    main_original = main_path.read_text(encoding="utf-8")

    if MARKER in hud_original or MARKER in main_original:
        print("ERROR: PRED-HUD1B2 is already installed.")
        print("Nothing changed.")
        return 1

    for required in (
        "AVP-HUD1F",
        "AVP-HUD1G1",
        "PRED-HUD1A-SPECIES-ISOLATED",
    ):
        if required not in main_original:
            print(f"ERROR: required main_3ds.c marker missing: {required}")
            print("Nothing changed.")
            return 1

    if "PRED-HUD1A-SPECIES-ISOLATED" not in hud_original:
        print("ERROR: PRED-HUD1A hud.c marker missing.")
        print("Nothing changed.")
        return 1

    # Exact sealed Marine coordinate verification before any editing.
    marine_entries = (
        "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
        "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
        "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
        "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
    )
    for entry in marine_entries:
        if main_original.count(entry) != 1:
            print(f"ERROR: sealed Marine coordinate missing or duplicated: {entry}")
            print("Nothing changed.")
            return 1

    hud = hud_original
    main_c = main_original

    try:
        # ----------------------------------------------------------
        # hud.c: add dedicated Predator message group 6.
        # ----------------------------------------------------------
        hud = regex_once(
            hud,
            r"(?m)^(?P<i>[ \t]*)#define[ \t]+"
            r"AVP3DS_HUD_GROUP_PRED_STATUS[ \t]+5[ \t]*$",
            lambda m: (
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_STATUS   5\n"
                f"{m.group('i')}#define AVP3DS_HUD_GROUP_PRED_MESSAGES 6"
            ),
            "hud.c Predator message define",
        )

        # Tolerant replacement for the CURRENT manually edited block.
        # It does not assume #ifdef has the same indentation as the code.
        current_message_block = re.compile(
            r"""
            (?P<lead>[ \t]*)\#ifdef[ \t]+__3DS__[ \t]*\n
            [ \t]*if[ \t]*\(
                [ \t]*AvP\.PlayerType[ \t]*==[ \t]*I_Marine
                [ \t]*\|\|[ \t]*
                AvP\.PlayerType[ \t]*==[ \t]*I_Predator
            [ \t]*\)[ \t]*\n
            [ \t]*AvP3DS_BeginHUDCapture[ \t]*\([ \t]*\n
            [ \t]*AVP3DS_HUD_GROUP_MESSAGES[ \t]*\)[ \t]*;[ \t]*\n
            [ \t]*\#endif[ \t]*\n
            (?P<gap1>[ \t]*\n)*
            [ \t]*GADGET_Render[ \t]*\([ \t]*\)[ \t]*;[ \t]*\n
            (?P<gap2>[ \t]*\n)*
            [ \t]*\#ifdef[ \t]+__3DS__[ \t]*\n
            [ \t]*if[ \t]*\(
                [ \t]*AvP\.PlayerType[ \t]*==[ \t]*I_Marine
                [ \t]*\|\|[ \t]*
                AvP\.PlayerType[ \t]*==[ \t]*I_Predator
            [ \t]*\)[ \t]*\n
            [ \t]*AvP3DS_EndHUDCapture[ \t]*\([ \t]*\)[ \t]*;[ \t]*\n
            [ \t]*\#endif
            """,
            re.VERBOSE,
        )

        def message_replacement(match: re.Match[str]) -> str:
            return (
                "#ifdef __3DS__\n"
                f"            /* {MARKER}: dedicated species message groups. */\n"
                "            if (AvP.PlayerType == I_Marine)\n"
                "                    AvP3DS_BeginHUDCapture(\n"
                "                            AVP3DS_HUD_GROUP_MESSAGES);\n"
                "            else if (AvP.PlayerType == I_Predator)\n"
                "                    AvP3DS_BeginHUDCapture(\n"
                "                            AVP3DS_HUD_GROUP_PRED_MESSAGES);\n"
                "#endif\n\n"
                "            GADGET_Render();\n\n"
                "#ifdef __3DS__\n"
                "            if (AvP.PlayerType == I_Marine || "
                "AvP.PlayerType == I_Predator)\n"
                "                    AvP3DS_EndHUDCapture();\n"
                "#endif"
            )

        hud, count = current_message_block.subn(
            message_replacement,
            hud,
            count=1,
        )
        if count != 1:
            raise RuntimeError(
                "hud.c current combined message block: "
                f"expected 1 match, found {count}"
            )

        # ----------------------------------------------------------
        # main_3ds.c: group 6 and count 7.
        # ----------------------------------------------------------
        main_c = regex_once(
            main_c,
            r"(?m)^#define[ \t]+AVP3DS_HUD_GROUP_PRED_STATUS[ \t]+5[ \t]*\n"
            r"#define[ \t]+AVP3DS_HUD_GROUP_COUNT[ \t]+6[ \t]*$",
            (
                "#define AVP3DS_HUD_GROUP_PRED_STATUS   5\n"
                "#define AVP3DS_HUD_GROUP_PRED_MESSAGES 6\n"
                "#define AVP3DS_HUD_GROUP_COUNT         7"
            ),
            "main_3ds.c group 6/count 7",
        )

        # Add Predator message box after Predator status box.
        main_c = regex_once(
            main_c,
            r"(?ms)"
            r"(?P<comment>[ \t]*/\* PRED-HUD1A: Predator health/energy "
            r"full-screen rails\. \*/[ \t]*\n)"
            r"(?P<entry>[ \t]*\{[ \t]*0\.0f,[ \t]*0\.0f,[ \t]*320\.0f,"
            r"[ \t]*240\.0f,[ \t]*1\.00f[ \t]*\})"
            r"(?P<close>[ \t]*\n\};)",
            lambda m: (
                f"{m.group('comment')}"
                f"{m.group('entry')},\n\n"
                "    /* PRED-HUD1B2: Predator ticker in second checker row. */\n"
                "    { 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }"
                f"{m.group('close')}"
            ),
            "main_3ds.c Predator message layout",
        )

        # Species detector: group 6 also identifies Predator.
        main_c = replace_once(
            main_c,
            "        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_STATUS)\n",
            "        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_STATUS ||\n"
            "            capturedGroup == AVP3DS_HUD_GROUP_PRED_MESSAGES)\n",
            "main_3ds.c Predator detector extension",
        )

        # Move only group 5 vertices inward after the shared transform.
        transform_call = (
            "            AvP3DS_TransformHUDVertex(\n"
            "                &layoutVertices[vertexIndex],\n"
            "                &captureVertices[vertexIndex],\n"
            "                transform);\n"
        )

        inset_code = transform_call + (
            "\n"
            "            /*\n"
            f"             * {MARKER}: Predator status rails only.\n"
            "             * Left rail moves right 32 px; right rail moves left 32 px.\n"
            "             */\n"
            "            if (group == AVP3DS_HUD_GROUP_PRED_STATUS)\n"
            "            {\n"
            "                const float sourcePixelX =\n"
            "                    (captureVertices[vertexIndex].position[0] + 1.0f) *\n"
            "                    (AVP3DS_BOTTOM_WIDTH * 0.5f);\n"
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
            inset_code,
            "main_3ds.c Predator rail inset",
        )

        # ----------------------------------------------------------
        # Final safety checks.
        # ----------------------------------------------------------
        for entry in marine_entries:
            if main_c.count(entry) != 1:
                raise RuntimeError(
                    f"Marine coordinate changed during patch: {entry}"
                )

        for marker in ("AVP-HUD1F", "AVP-HUD1G1"):
            if marker not in main_c:
                raise RuntimeError(f"Marine marker lost: {marker}")

        # Backdrop routing must remain the exact already-tested PRED-HUD1A form.
        route = (
            "if (!predatorHUDCaptured && "
            "avp3ds_marine_hud_texture_initialized)"
        )
        if main_c.count(route) != 2:
            raise RuntimeError(
                "Marine/Predator backdrop routing changed unexpectedly"
            )

        if "AVP3DS_HUD_GROUP_COUNT         7" not in main_c:
            raise RuntimeError("group count 7 verification failed")

        if hud.count(MARKER) != 1:
            raise RuntimeError("hud.c marker count verification failed")

        if main_c.count(MARKER) != 1:
            raise RuntimeError("main_3ds.c marker count verification failed")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    hud_backup = hud_path.with_name("hud.c.pre-PRED-HUD1B2.bak")
    main_backup = main_path.with_name("main_3ds.c.pre-PRED-HUD1B2.bak")

    if hud_backup.exists() or main_backup.exists():
        print("ERROR: PRED-HUD1B2 backup already exists.")
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
        print("Both source files restored.")
        return 1

    print("PRED-HUD1B2 installed successfully.")
    print(f"Backup: {hud_backup}")
    print(f"Backup: {main_backup}")
    print()
    print("Predator:")
    print("  - health rail +32 px toward center")
    print("  - field-charge rail -32 px toward center")
    print("  - dedicated message group 6")
    print("  - message target y=16")
    print()
    print("Marine:")
    print("  - final coordinates verified unchanged")
    print("  - backdrop routing verified unchanged")
    print("  - HUD1F/HUD1G1 verified present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
