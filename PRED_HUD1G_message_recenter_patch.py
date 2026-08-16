#!/usr/bin/env python3
r"""
Install PRED-HUD1G: restore automatic centering for Predator messages.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Why:
  PRED-HUD1F3 correctly separated spear-gun ammo and pinned the side rails,
  but it also forced the Predator message transform to the layout box's
  top-left corner. That caused the text to appear left-of-center and slightly
  too high.

This patch removes only that Predator-message top-left override. The normal
layout calculation will once again center the live message text horizontally
and vertically inside the existing message box.

Preserved:
  - dedicated spear-gun ammo group and center box
  - locked Predator health/field-charge rails
  - current Predator backdrop and +12 px offset
  - universal black lower screen on death
  - completed Marine HUD and tracker lock
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1G-MESSAGE-RECENTER"


def main() -> int:
    root = Path.cwd().resolve()
    path = root / "src" / "main_3ds.c"

    if not path.is_file() or not (root / "Makefile.3ds").is_file():
        print("ERROR: run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    original = path.read_text(encoding="utf-8")

    if MARKER in original:
        print("ERROR: PRED-HUD1G is already installed.")
        print("Nothing changed.")
        return 1

    required_markers = (
        "AVP-HUD1G1 tracker transform lock",
        "PRED-HUD1F3-SPEAR-AMMO-PINNED",
        "PRED-HUD1E3-SWAP-AND-DEATH-BLACK",
    )

    for marker in required_markers:
        if marker not in original:
            print(f"ERROR: required baseline marker missing: {marker}")
            print("Nothing changed.")
            return 1

    required_entries = (
        "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
        "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
        "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
        "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
        "{ 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }",
        "{ 116.0f, 72.0f, 88.0f, 32.0f, 1.25f }",
    )

    for entry in required_entries:
        if original.count(entry) != 1:
            print(f"ERROR: expected layout entry missing or duplicated: {entry}")
            print("Nothing changed.")
            return 1

    message_override_pattern = re.compile(
        r"""
        \n[ \t]*else[ \t]+if[ \t]*\(
            group[ \t]*==[ \t]*AVP3DS_HUD_GROUP_PRED_MESSAGES
        \)[ \t]*\n
        [ \t]*\{[ \t]*\n
        [ \t]*/\*[ \t]*\n
        [ \t]*\*[ \t]*Keep[ \t]+the[ \t]+message[ \t]+strip's[ \t]+
            top-left[ \t]+anchor[ \t]+fixed[ \t]+while[ \t]+allowing[ \t]*\n
        [ \t]*\*[ \t]*its[ \t]+live[ \t]+content[ \t]+width[ \t]+and[ \t]+
            scale[ \t]+to[ \t]+remain[ \t]+adaptive\.[ \t]*\n
        [ \t]*\*/[ \t]*\n
        [ \t]*transforms\[group\]\.offsetX[ \t]*=[ \t]*box->x;[ \t]*\n
        [ \t]*transforms\[group\]\.offsetY[ \t]*=[ \t]*box->y;[ \t]*\n
        [ \t]*\}
        """,
        re.VERBOSE,
    )

    replacement = (
        "\n\n"
        "        /*\n"
        f"         * {MARKER}:\n"
        "         * Predator messages use the normal fitted transform so their\n"
        "         * live bounds are centered inside the existing message box.\n"
        "         * Spear ammo is now a separate group, so weapon changes cannot\n"
        "         * disturb this message placement.\n"
        "         */"
    )

    updated, count = message_override_pattern.subn(
        replacement,
        original,
        count=1,
    )

    if count != 1:
        print(
            "ERROR: Predator message top-left override: "
            f"expected 1 block, found {count}"
        )
        print("Nothing changed.")
        return 1

    # Safety checks after the proposed edit.
    for entry in required_entries:
        if updated.count(entry) != 1:
            print(f"ERROR: layout entry changed during patch: {entry}")
            print("Nothing changed.")
            return 1

    preserved_checks = (
        "AVP3DS_HUD_GROUP_PRED_AMMO",
        "avp3ds_predator_status_transform_locked",
        "const float topY = 12.0f;",
        "0x000000FF",
        "romfs:/Marine_WY_HUD.rgba",
        "romfs:/Predator_HUD_Backdrop_320x240.rgba",
    )

    for item in preserved_checks:
        if item not in updated:
            print(f"ERROR: preserved feature missing after patch: {item}")
            print("Nothing changed.")
            return 1

    if updated.count(MARKER) != 1:
        print(
            f"ERROR: marker verification failed; found {updated.count(MARKER)}"
        )
        print("Nothing changed.")
        return 1

    backup = path.with_name("main_3ds.c.pre-PRED-HUD1G-MESSAGE-RECENTER.bak")

    if backup.exists():
        print(f"ERROR: backup already exists: {backup}")
        print("Nothing changed.")
        return 1

    shutil.copy2(path, backup)

    try:
        path.write_text(updated, encoding="utf-8")
    except Exception as exc:
        shutil.copy2(backup, path)
        print(f"ERROR while writing: {exc}")
        print("main_3ds.c was restored.")
        return 1

    print("PRED-HUD1G installed successfully.")
    print(f"Backup: {backup}")
    print()
    print("Predator message behavior:")
    print("  - horizontally centered inside the existing top message box")
    print("  - vertically centered inside the existing top message box")
    print("  - unaffected by spear-gun selection")
    print()
    print("Rails, spear ammo, backdrop, death-black, and Marine HUD were preserved.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
