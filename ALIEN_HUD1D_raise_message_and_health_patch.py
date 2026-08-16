#!/usr/bin/env python3
from __future__ import annotations

import shutil
import sys
from pathlib import Path


MARKER = "ALIEN-HUD1D-RAISE-MSG-HEALTH"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    root = Path.cwd().resolve()
    main_path = root / "src" / "main_3ds.c"

    if not main_path.is_file() or not (root / "Makefile.3ds").is_file():
        print("ERROR: run this from C:/Projects/AvP3DS/Source")
        print("Nothing changed.")
        return 1

    original = main_path.read_text(encoding="utf-8")

    if MARKER in original:
        print("ERROR: ALIEN-HUD1D is already installed.")
        print("Nothing changed.")
        return 1

    required_markers = (
        "ALIEN-HUD1A2-LOWER-HEALTH",
        "ALIEN-HUD1B2-LOWER-MESSAGES",
        "ALIEN-HUD1C3-NATIVE-BACKDROP",
        "PRED-HUD1H-UPPER-SUPPRESS",
        "PRED-HUD1E3-SWAP-AND-DEATH-BLACK",
        "AVP-HUD1G1 tracker transform lock",
    )

    for marker in required_markers:
        if marker not in original:
            print(f"ERROR: required baseline marker missing: {marker}")
            print("Nothing changed.")
            return 1

    old_health = "{ 64.0f, 184.0f, 192.0f, 44.0f, 1.00f }"
    new_health = "{ 64.0f, 170.0f, 192.0f, 44.0f, 1.00f }"

    old_message = "{ 40.0f, 16.0f, 240.0f, 32.0f, 0.86f }"
    new_message = "{ 40.0f, 8.0f, 240.0f, 32.0f, 0.86f }"

    # Safety expectations before write:
    # Predator + Alien currently share the 40,16,240,32,0.86 layout.
    if original.count(old_health) != 1:
        print("ERROR: Alien health layout is not in the expected state.")
        print("Nothing changed.")
        return 1

    if original.count(old_message) != 2:
        print("ERROR: shared Predator/Alien message layout is not in the expected state.")
        print("Nothing changed.")
        return 1

    try:
        updated = original

        # Move Alien health up by 14 px.
        updated = replace_once(
            updated,
            old_health,
            new_health,
            "Alien health layout",
        )

        # Move only the Alien message box up by 8 px.
        alien_message_comment = (
            f"    /* ALIEN-HUD1B2-LOWER-MESSAGES: Alien ticker near the Predator position. */\n"
            f"    {old_message}"
        )
        alien_message_replacement = (
            f"    /* ALIEN-HUD1D-RAISE-MSG-HEALTH: Alien ticker raised to fit the amber panel. */\n"
            f"    {new_message}"
        )

        updated = replace_once(
            updated,
            alien_message_comment,
            alien_message_replacement,
            "Alien message layout",
        )

        # Post-write verification.
        if updated.count(new_health) != 1:
            raise RuntimeError("Alien health layout verification failed")

        if updated.count(new_message) != 1:
            raise RuntimeError("Alien message layout verification failed")

        # Predator shared message box should still remain exactly once.
        if updated.count(old_message) != 1:
            raise RuntimeError("Predator message layout changed unexpectedly")

        if "romfs:/Marine_WY_HUD.rgba" not in updated:
            raise RuntimeError("Marine backdrop path missing after patch")

        if "romfs:/Predator_HUD_Backdrop_320x240.rgba" not in updated:
            raise RuntimeError("Predator backdrop path missing after patch")

        if "romfs:/Alien_HUD_Backdrop_320x240.rgba" not in updated:
            raise RuntimeError("Alien backdrop path missing after patch")

        death_clear = (
            "    C3D_RenderTargetClear(\n"
            "        avp3ds_bottom_target,\n"
            "        C3D_CLEAR_ALL,\n"
            "        0x000000FF,\n"
            "        0);\n"
        )
        if updated.count(death_clear) != 1:
            raise RuntimeError("universal death-black clear changed")

        if updated.count(MARKER) != 1:
            raise RuntimeError("marker verification failed")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    backup = main_path.with_name("main_3ds.c.pre-ALIEN-HUD1D.bak")

    if backup.exists():
        print(f"ERROR: backup already exists: {backup}")
        print("Nothing changed.")
        return 1

    shutil.copy2(main_path, backup)

    try:
        main_path.write_text(updated, encoding="utf-8")
    except Exception as exc:
        shutil.copy2(backup, main_path)
        print(f"ERROR while writing: {exc}")
        print("main_3ds.c was restored.")
        return 1

    print("ALIEN-HUD1D installed successfully.")
    print(f"Backup: {backup}")
    print()
    print("Alien message box moved:")
    print("  y: 16 -> 8")
    print("Alien health box moved:")
    print("  y: 184 -> 170")
    print()
    print("Marine, Predator, Alien backdrop, and death-black were preserved.")
    return 0


if __name__ == "__main__":
    sys.exit(main())