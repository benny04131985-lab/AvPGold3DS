#!/usr/bin/env python3
r"""
Install PRED-HUD1D2-BACKDROP-DOWN8 against the exact current routing block.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

Effect:
  - Moves only the Predator background quad down 8 pixels.
  - Leaves Predator live HUD captures exactly where they are.
  - Leaves the Marine background helper, texture, layout, and tracker lock untouched.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1D2-BACKDROP-DOWN8"
Y_OFFSET = 8.0


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
        print("ERROR: PRED-HUD1D2 is already installed.")
        print("Nothing changed.")
        return 1

    for required in (
        "AVP-HUD1F",
        "AVP-HUD1G1",
        "PRED-HUD1A-SPECIES-ISOLATED",
        "PRED-HUD1C3-NATIVE-BACKDROP",
    ):
        if required not in original:
            print(f"ERROR: required baseline marker missing: {required}")
            print("Nothing changed.")
            return 1

    marine_entries = (
        "{ 84.0f, 36.0f, 144.0f, 88.0f, 1.35f }",
        "{ 10.0f, 124.0f, 86.0f, 78.0f, 1.00f }",
        "{ 208.0f, 124.0f, 112.0f, 78.0f, 0.82f }",
        "{ 40.0f, 0.0f, 240.0f, 32.0f, 0.86f }",
    )

    for entry in marine_entries:
        if original.count(entry) != 1:
            print(f"ERROR: sealed Marine layout entry missing or duplicated: {entry}")
            print("Nothing changed.")
            return 1

    helper_insert_anchor = """static void AvP3DS_BindBottomGameplayPipeline(void)
"""

    predator_helper = f"""/*
 * {MARKER}.
 *
 * Predator-only copy of the existing textured backdrop quad.
 * Geometry is translated down by {Y_OFFSET:.1f} lower-screen pixels.
 */
static void AvP3DS_AppendPredatorHUDBackdrop(
    AvP3DS_GameVertex *vertices,
    size_t *vertexCount)
{{
    const float maximumU =
        (float)AVP3DS_MARINE_HUD_SOURCE_WIDTH /
        (float)AVP3DS_MARINE_HUD_TEXTURE_WIDTH;

    const float maximumV =
        (float)AVP3DS_MARINE_HUD_SOURCE_HEIGHT /
        (float)AVP3DS_MARINE_HUD_TEXTURE_HEIGHT;

    const float topY = {Y_OFFSET:.1f}f;
    const float bottomY =
        AVP3DS_BOTTOM_HEIGHT + {Y_OFFSET:.1f}f;

    AvP3DS_GameVertex *output =
        vertices + *vertexCount;

    AvP3DS_SetBottomTexturedVertex(
        &output[0],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[1],
        AVP3DS_BOTTOM_WIDTH,
        topY,
        maximumU,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[2],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[3],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[4],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[5],
        0.0f,
        bottomY,
        0.0f,
        0.0f);

    *vertexCount += 6U;
}}

"""

    current_routing = """    if ((predatorHUDCaptured && avp3ds_predator_hud_texture_initialized) ||
        (!predatorHUDCaptured && avp3ds_marine_hud_texture_initialized))
    {
        AvP3DS_AppendMarineHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else
"""

    new_routing = """    if (predatorHUDCaptured &&
        avp3ds_predator_hud_texture_initialized)
    {
        AvP3DS_AppendPredatorHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else if (!predatorHUDCaptured &&
             avp3ds_marine_hud_texture_initialized)
    {
        AvP3DS_AppendMarineHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else
"""

    try:
        text = replace_once(
            original,
            helper_insert_anchor,
            predator_helper + helper_insert_anchor,
            "Predator helper insertion",
        )

        text = replace_once(
            text,
            current_routing,
            new_routing,
            "exact species backdrop routing",
        )

        for entry in marine_entries:
            if text.count(entry) != 1:
                raise RuntimeError(f"Marine layout changed: {entry}")

        if text.count("AvP3DS_AppendMarineHUDBackdrop(") != 2:
            raise RuntimeError(
                "Marine helper definition/call count changed unexpectedly"
            )

        if text.count("AvP3DS_AppendPredatorHUDBackdrop(") != 2:
            raise RuntimeError(
                "Predator helper definition/call count verification failed"
            )

        if text.count("romfs:/Marine_WY_HUD.rgba") != 1:
            raise RuntimeError("Marine texture path changed")

        if text.count("romfs:/Predator_HUD_Backdrop_320x240.rgba") != 1:
            raise RuntimeError("Predator texture path changed")

        if "AVP-HUD1G1 tracker transform lock" not in text:
            raise RuntimeError("Marine tracker lock disappeared")

        if text.count(MARKER) != 1:
            raise RuntimeError(
                f"marker count verification failed: {text.count(MARKER)}"
            )

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    backup = path.with_name("main_3ds.c.pre-PRED-HUD1D2-DOWN8.bak")

    if backup.exists():
        print(f"ERROR: backup already exists: {backup}")
        print("Nothing changed.")
        return 1

    shutil.copy2(path, backup)

    try:
        path.write_text(text, encoding="utf-8")
    except Exception as exc:
        shutil.copy2(backup, path)
        print(f"ERROR while writing: {exc}")
        print("main_3ds.c was restored.")
        return 1

    print("PRED-HUD1D2 installed successfully.")
    print(f"Backup: {backup}")
    print(f"Predator backdrop moved down: {Y_OFFSET:.1f} px")
    print("Predator live HUD and Marine rendering were verified unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
