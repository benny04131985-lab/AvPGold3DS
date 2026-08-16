#!/usr/bin/env python3
r"""
Install PRED-HUD1D-BACKDROP-DOWN8.

Purpose:
  Move only the Predator native backdrop down by 8 screen pixels.
  Marine backdrop geometry and all live HUD group transforms remain untouched.

Run from:
    C:/Projects/AvP3DS/Source
or:
    /c/Projects/AvP3DS/Source

This is intentionally one-file and species-specific:
  src/main_3ds.c

Expected result:
  - Predator backdrop lower decorative edge moves down 8 px.
  - Predator health/energy/message placements do not move.
  - Marine lower screen is byte-for-byte behaviorally unchanged.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


MARKER = "PRED-HUD1D-BACKDROP-DOWN8"
PREDATOR_Y_OFFSET = 8.0


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
        print("ERROR: PRED-HUD1D is already installed.")
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

    text = original

    try:
        # Insert a separate Predator backdrop helper immediately after the
        # existing Marine helper. The Marine helper itself is not edited.
        helper_anchor = """    *vertexCount += 6U;
}

static void AvP3DS_BindBottomGameplayPipeline(void)
"""

        predator_helper = f"""    *vertexCount += 6U;
}}

/*
 * {MARKER}.
 *
 * Use the same native texture coordinates as the Marine backdrop, but move
 * only the Predator quad down by {PREDATOR_Y_OFFSET:.1f} lower-screen pixels.
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

    const float topY = {PREDATOR_Y_OFFSET:.1f}f;
    const float bottomY =
        AVP3DS_BOTTOM_HEIGHT + {PREDATOR_Y_OFFSET:.1f}f;

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

static void AvP3DS_BindBottomGameplayPipeline(void)
"""

        text = replace_once(
            text,
            helper_anchor,
            predator_helper,
            "Predator backdrop helper insertion",
        )

        # PRED-HUD1C3 currently chooses whether to build a textured quad, then
        # calls the Marine geometry helper for either species. Split that call
        # into explicit Predator and Marine branches.
        routing_anchor = """    if ((predatorHUDCaptured &&
        avp3ds_predator_hud_texture_initialized) ||
        (!predatorHUDCaptured &&
        avp3ds_marine_hud_texture_initialized))
    {
        AvP3DS_AppendMarineHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else
"""

        routing_replacement = """    if (predatorHUDCaptured &&
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

        text = replace_once(
            text,
            routing_anchor,
            routing_replacement,
            "species-specific backdrop geometry routing",
        )

        # Safety checks.
        for entry in marine_entries:
            if text.count(entry) != 1:
                raise RuntimeError(f"Marine layout changed: {entry}")

        if text.count("AvP3DS_AppendMarineHUDBackdrop(") != 2:
            raise RuntimeError(
                "Marine helper definition/call count changed unexpectedly"
            )

        if text.count("AvP3DS_AppendPredatorHUDBackdrop(") != 2:
            raise RuntimeError(
                "Predator helper definition/call verification failed"
            )

        if text.count("romfs:/Marine_WY_HUD.rgba") != 1:
            raise RuntimeError("Marine asset path changed")

        if text.count("romfs:/Predator_HUD_Backdrop_320x240.rgba") != 1:
            raise RuntimeError("Predator asset path changed")

        if text.count(MARKER) != 1:
            raise RuntimeError(
                f"marker verification failed: found {text.count(MARKER)}"
            )

        if "AVP-HUD1G1 tracker transform lock" not in text:
            raise RuntimeError("Marine tracker lock block disappeared")

    except Exception as exc:
        print(f"ERROR: {exc}")
        print("Nothing changed.")
        return 1

    backup = path.with_name("main_3ds.c.pre-PRED-HUD1D-DOWN8.bak")

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

    print("PRED-HUD1D installed successfully.")
    print(f"Backup: {backup}")
    print(f"Predator backdrop Y offset: +{PREDATOR_Y_OFFSET:.1f} px")
    print()
    print("Marine backdrop geometry and all HUD layout entries were verified unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
