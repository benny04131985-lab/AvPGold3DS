#!/usr/bin/env python3
r"""
Install PRED-HUD1H: suppress the duplicated Predator HUD on the upper screen.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Effect:
  - Captured Marine HUD groups remain lower-screen-only.
  - Captured Predator wrist, status, messages, and spear ammo also become
    lower-screen-only.
  - Upper-screen world, weapon model, sights, vision modes, and scanlines
    remain untouched because they are outside the capture groups.
  - Alien behavior is untouched.

This patch edits only:
    src/main_3ds.c
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1H-UPPER-SUPPRESS"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    root = Path.cwd().resolve()
    path = root / "src" / "main_3ds.c"

    if not path.is_file() or not (root / "Makefile.3ds").is_file():
        print("ERROR: run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    original = path.read_text(encoding="utf-8")

    if MARKER in original:
        print("ERROR: PRED-HUD1H is already installed.")
        print("Nothing changed.")
        return 1

    required_markers = (
        "AVP-HUD1F",
        "AVP-HUD1G1 tracker transform lock",
        "PRED-HUD1A-SPECIES-ISOLATED",
        "PRED-HUD1F3-SPEAR-AMMO-PINNED",
        "PRED-HUD1G-MESSAGE-RECENTER",
        "PRED-HUD1E3-SWAP-AND-DEATH-BLACK",
    )

    for marker in required_markers:
        if marker not in original:
            print(f"ERROR: required baseline marker missing: {marker}")
            print("Nothing changed.")
            return 1

    required_groups = (
        "#define AVP3DS_HUD_GROUP_PRED_WRIST",
        "#define AVP3DS_HUD_GROUP_PRED_STATUS",
        "#define AVP3DS_HUD_GROUP_PRED_MESSAGES",
        "#define AVP3DS_HUD_GROUP_PRED_AMMO",
        "#define AVP3DS_HUD_GROUP_COUNT         8",
    )

    for token in required_groups:
        if token not in original:
            print(f"ERROR: expected Predator capture state missing: {token}")
            print("Nothing changed.")
            return 1

    old_block = """    /*
     * PRED-HUD1A-SPECIES-ISOLATED: species-isolated upper-screen policy.
     * Marine groups 0-3 remain suppressed upstairs.
     * Predator groups 4-5 remain visible during diagnosis.
     */
    if (!avp3ds_tracker_capture_enabled ||
        avp3ds_hud_capture_group >= AVP3DS_HUD_GROUP_PRED_WRIST)
    {
        C3D_DrawArrays(
"""

    new_block = f"""    /*
     * {MARKER}.
     *
     * Every captured lower-screen HUD group is now lower-screen-only.
     * This suppresses the completed Marine and Predator HUD elements upstairs
     * while preserving world geometry, weapon models, sights, vision effects,
     * scanlines, and all non-captured rendering.
     */
    if (!avp3ds_tracker_capture_enabled)
    {{
        C3D_DrawArrays(
"""

    try:
        updated = replace_once(
            original,
            old_block,
            new_block,
            "species-isolated upper-screen policy",
        )

        # Safety checks.
        if updated.count("if (!avp3ds_tracker_capture_enabled)") < 1:
            raise RuntimeError("final capture suppression condition missing")

        if "avp3ds_hud_capture_group >= AVP3DS_HUD_GROUP_PRED_WRIST" in updated:
            raise RuntimeError("diagnostic Predator upper-HUD exception remains")

        for marker in (
            "AVP-HUD1G1 tracker transform lock",
            "PRED-HUD1F3-SPEAR-AMMO-PINNED",
            "PRED-HUD1G-MESSAGE-RECENTER",
            "PRED-HUD1E3-SWAP-AND-DEATH-BLACK",
        ):
            if marker not in updated:
                raise RuntimeError(f"milestone behavior lost: {marker}")

        if updated.count("romfs:/Marine_WY_HUD.rgba") != 1:
            raise RuntimeError("Marine backdrop path changed")

        if updated.count("romfs:/Predator_HUD_Backdrop_320x240.rgba") != 1:
            raise RuntimeError("Predator backdrop path changed")

        if updated.count(MARKER) != 1:
            raise RuntimeError(
                f"marker verification failed: found {updated.count(MARKER)}"
            )

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    backup = path.with_name("main_3ds.c.pre-PRED-HUD1H-UPPER-SUPPRESS.bak")

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

    print("PRED-HUD1H installed successfully.")
    print(f"Backup: {backup}")
    print()
    print("Upper Predator screen now keeps:")
    print("  - world")
    print("  - weapon model")
    print("  - targeting sights")
    print("  - vision effects and scanlines")
    print()
    print("Upper Predator screen now hides:")
    print("  - wrist display")
    print("  - health and field-charge rails")
    print("  - message ticker")
    print("  - spear-gun ammo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
