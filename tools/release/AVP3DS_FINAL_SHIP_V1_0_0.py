#!/usr/bin/env python3
"""
AVP3DS_FINAL_SHIP_V1_0_0_V3.py
==============================

Citadel/JK-style final freeze + clean build + GitHub staging for AvP3DS v1.0.0.

DEFAULT:
    READ-ONLY preflight. No files changed.

--apply:
    Creates:
      SHIP_FREEZE/AVP3DS_V1_0_0_SHIPPED_<timestamp>/
        00_READ_ME_FIRST/
        01_FOR_US_MASTER/
        02_GITHUB_DISTRIBUTION/
        03_FINAL_EVIDENCE/
          Release/
          Build/
          HardwareLogs/
        04_MANIFESTS/

    It:
      * validates the finished hardware-approved source markers
      * snapshots the development source without retail game data
      * creates a clean public source tree
      * deactivates YEET28/HEADROOM2 validation callsites ONLY in the frozen
        public/build snapshot (the proven development tree is untouched)
      * clean-builds from a fresh copied source tree
      * stages AvP_Gold.3dsx + release docs + hashes
      * writes human-readable objective audits and SHA-256 manifests
      * optionally copies the clean public tree into an existing clean Git clone

The script NEVER:
    * modifies the proven development source tree
    * copies retail/commercial AvP game data into the public tree/release
    * creates a remote repository
    * commits
    * pushes
    * tags
    * publishes a GitHub Release

Run from:
    C:/Projects/AVP3DS_Stereo/Source
or:
    /c/Projects/AVP3DS_Stereo/Source

Examples:
    python AVP3DS_FINAL_SHIP_V1_0_0_V3.py
    python AVP3DS_FINAL_SHIP_V1_0_0_V3.py --apply
    python AVP3DS_FINAL_SHIP_V1_0_0_V3.py --apply --github-clone /c/Projects/AvP3DS_GitHub
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

VERSION = "1.0.0"
MILESTONE = "AVP3DS-V1.0.0-FINALSHIP1"
FREEZE_LABEL = "AVP3DS_V1_0_0_SHIPPED"

ROOT = Path.cwd().resolve()

KEY_FILES = [
    Path("Makefile.3ds"),
    Path("src/main.c"),
    Path("src/main_3ds.c"),
    Path("src/avp/game.c"),
    Path("src/avp/psnd.c"),
    Path("src/psnd_3ds.c"),
    Path("src/avp/pmove.c"),
    Path("src/avp/win95/frontend/avp_menus.c"),
    Path("src/avp/win95/frontend/avp_menudata.c"),
]

REQUIRED_MARKERS = [
    "AVP-SHIPFINAL1-VOICEHALF-MENUSAFE",
    "AVP-SHIPFINAL2-MOVESCALE65",
    "AVP-STEREO-S2C2A1-CMDBUF-512K",
    "AVP-STEREO-S2C2-FLAT-SIGHTS",
    "AVP-STEREO-S2C1A2-FLAT-STATES",
    "AVP-STEREO-S2B-DEPTH-WARP",
    "AVP-MARINE-HUD-DOWN12",
    "PRED-HUD1H-UPPER-SUPPRESS",
    "ALIEN-HUD1C3-NATIVE-BACKDROP",
    "AVP-EXIT1A2-FRONTEND-LOOP-FIX",
]

# Never copy these directory names into a public/master source freeze.
BLOCKED_RETAIL_DIRS = {
    "retaildata", "retail_data", "gamedata", "game_data",
    "originaldata", "original_data", "cddata", "cd_data",
    "userretaildata", "user_retail_data",
}

EXCLUDE_DIRS = {
    ".git", ".svn", ".hg", ".vs", ".idea", "__pycache__",
    "ship_freeze", "dist", "baselines", "checkpoints",
}

# Root-only compiler output. Do NOT globally exclude build-3ds because the
# self-contained SDL2 package lives under Libraries/SDL2/build-3ds.
# V3 also treats literal hidden artifact names such as ".map" as generated
# output; pathlib reports those with an empty suffix.
ROOT_BUILD_DIRS = {"build", "build_3ds"}

PUBLIC_EXCLUDE_SUFFIXES = {
    ".3dsx", ".elf", ".cia", ".smdh", ".log", ".map",
    ".o", ".obj", ".dll", ".so", ".dylib", ".exe",
    ".bak", ".orig", ".rej", ".tmp",
}

# Prebuilt SDL2 archives are deliberately retained for the proven self-contained
# 3DS Makefile. Other ad-hoc archives are not copied publicly.
ALLOW_ARCHIVE_PREFIXES = (
    "Libraries/SDL2/",
)

ROOT_DEV_PREFIXES = (
    "AVP_",
    "AVP3DS_",
    "HEADROOM",
    "PERF",
    "AUDIT_",
    "SHIP_",
)

SUSPICIOUS_PUBLIC_EXTS = {
    ".ffl", ".rif", ".smk", ".bik", ".mp3", ".ogg", ".flac",
}

WARN_SIZE = 50 * 1024 * 1024
FAIL_SIZE = 100 * 1024 * 1024


def fail(msg: str) -> None:
    print("ERROR: " + msg, file=sys.stderr)
    raise SystemExit(1)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.rstrip() + "\n", encoding="utf-8", newline="")


def run(cmd: list[str], cwd: Path, timeout: int = 600, check: bool = True) -> subprocess.CompletedProcess:
    try:
        p = subprocess.run(
            [str(x) for x in cmd],
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    except Exception as exc:
        if check:
            fail(f"{' '.join(str(x) for x in cmd)} failed: {exc}")
        return subprocess.CompletedProcess(cmd, 999, stdout=str(exc))

    if check and p.returncode != 0:
        fail(f"{' '.join(str(x) for x in cmd)} failed rc={p.returncode}\n{p.stdout}")
    return p


def source_join(root: Path) -> str:
    chunks = []
    for rel in KEY_FILES:
        p = root / rel
        if p.is_file():
            chunks.append(p.read_text(encoding="utf-8", errors="replace"))
    # HUD markers may live elsewhere under src/avp.
    avp = root / "src/avp"
    if avp.is_dir():
        for p in avp.rglob("*"):
            if p.is_file() and p.suffix.lower() in {".c", ".cpp", ".h", ".hpp"}:
                try:
                    chunks.append(p.read_text(encoding="utf-8", errors="replace"))
                except Exception:
                    pass
    return "\n".join(chunks)


def validate_finished_baseline(root: Path) -> dict:
    missing_files = [rel.as_posix() for rel in KEY_FILES if not (root / rel).is_file()]
    if missing_files:
        fail("missing required source files:\n  " + "\n  ".join(missing_files))

    joined = source_join(root)
    missing_markers = [m for m in REQUIRED_MARKERS if m not in joined]
    if missing_markers:
        fail("finished milestone marker(s) missing:\n  " + "\n  ".join(missing_markers))

    pmove = (root / "src/avp/pmove.c").read_text(encoding="utf-8", errors="replace")
    if pmove.count("forwardSpeed = (forwardSpeed * 13) / 20;") != 1:
        fail("final 65% forward movement scale not uniquely present")
    if pmove.count("strafeSpeed = (strafeSpeed * 13) / 20;") != 1:
        fail("final 65% strafe movement scale not uniquely present")

    joined_key = "\n".join(
        (root / rel).read_text(encoding="utf-8", errors="replace")
        for rel in KEY_FILES if (root / rel).is_file()
    )
    for token in ("YEET32B AUD", "sdmc:/avp_save_trace.log"):
        if token in joined_key:
            fail(f"old ship-rejected debug token still present: {token}")

    hashes = {}
    for rel in KEY_FILES:
        p = root / rel
        hashes[rel.as_posix()] = {
            "sha256": sha256(p),
            "bytes": p.stat().st_size,
        }

    return hashes


def is_root_dev_tool(rel: Path) -> bool:
    if len(rel.parts) != 1:
        return False
    n = rel.name
    if n == Path(__file__).name:
        return True
    return n.endswith((".py", ".txt", ".json")) and n.startswith(ROOT_DEV_PREFIXES)


def skip_reason(rel: Path, mode: str) -> str | None:
    parts_lower = [x.lower() for x in rel.parts]

    if any(p in BLOCKED_RETAIL_DIRS for p in parts_lower):
        return "retail-data directory"

    if any(p in EXCLUDE_DIRS for p in parts_lower):
        return "development/generated directory"

    if rel.parts and rel.parts[0].lower() in ROOT_BUILD_DIRS:
        return "root build directory"

    if rel.parts and rel.parts[0].lower().startswith("cmake-build-"):
        return "root build directory"

    name_lower = rel.name.lower()
    suffix = rel.suffix.lower()

    if mode == "public":
        if is_root_dev_tool(rel):
            return "root engineering patch/audit tool"

        # Path.suffix is empty for dotfiles such as a literal ".map".
        # Treat both ordinary suffixed outputs (foo.map) and exact hidden
        # build-artifact names (.map/.elf/.3dsx/...) as generated debris.
        if (
            suffix in PUBLIC_EXCLUDE_SUFFIXES
            or any(name_lower.endswith(ext) for ext in PUBLIC_EXCLUDE_SUFFIXES)
            or name_lower.endswith("~")
        ):
            return "build/log/backup artifact"

        if suffix == ".a":
            rel_posix = rel.as_posix()
            if not rel_posix.startswith(ALLOW_ARCHIVE_PREFIXES):
                return "non-SDL prebuilt archive"

    elif mode == "master":
        master_build_exts = {
            ".3dsx", ".elf", ".cia", ".smdh", ".log", ".map", ".o", ".obj"
        }
        if (
            suffix in master_build_exts
            or any(name_lower.endswith(ext) for ext in master_build_exts)
        ):
            return "build/log artifact"

    return None


def copy_filtered_tree(src: Path, dst: Path, mode: str) -> tuple[int, int, dict[str, int]]:
    if dst.exists():
        fail(f"destination already exists: {dst}")
    dst.mkdir(parents=True)

    files = 0
    total = 0
    skipped: dict[str, int] = {}

    for dirpath, dirnames, filenames in os.walk(src):
        d = Path(dirpath)
        rel_dir = d.relative_to(src)

        kept_dirs = []
        for name in dirnames:
            rel = rel_dir / name
            reason = skip_reason(rel, mode)
            if reason:
                skipped[reason] = skipped.get(reason, 0) + 1
            else:
                kept_dirs.append(name)
        dirnames[:] = kept_dirs

        for name in filenames:
            rel = rel_dir / name
            reason = skip_reason(rel, mode)
            if reason:
                skipped[reason] = skipped.get(reason, 0) + 1
                continue

            source = src / rel
            target = dst / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            files += 1
            total += source.stat().st_size

    return files, total, skipped


def collapse_headroom_wrappers(text: str, label: str) -> tuple[str, int]:
    """
    Collapse HEADROOM2 wrappers back to their original #else branch.

    Expected shape:
      #ifdef __3DS__
      {
          u64 start = AvP3DS_HeadroomTick();
          REAL CALL;
          AvP3DS_HeadroomAccumulate(...);
      }
      #else
      REAL CALL;
      #endif
    """
    pattern = re.compile(
        r"(?ms)"
        r"^[ \t]*#ifdef __3DS__[ \t]*\n"
        r"[ \t]*\{[ \t]*\n"
        r"[ \t]*unsigned long long avp3ds_hr_start[ \t]*=[ \t]*"
        r"AvP3DS_HeadroomTick\(\);[ \t]*\n"
        r"(?P<instrumented>.*?)"
        r"^[ \t]*AvP3DS_HeadroomAccumulate\([0-9]+,[ \t]*avp3ds_hr_start\);[ \t]*\n"
        r"[ \t]*\}[ \t]*\n"
        r"[ \t]*#else[ \t]*\n"
        r"(?P<original>.*?)"
        r"^[ \t]*#endif[ \t]*(?:\n|$)"
    )

    count = 0
    while True:
        m = pattern.search(text)
        if not m:
            break

        original = m.group("original")
        if "AvP3DS_" in original:
            fail(f"{label}: HEADROOM2 else branch unexpectedly contains AvP3DS instrumentation")

        text = text[:m.start()] + original + text[m.end():]
        count += 1

    return text, count


def collapse_headroom_conditional_wrappers(text: str, label: str) -> tuple[str, int]:
    """
    Collapse the two HEADROOM2 network wrappers whose 3DS branch has an
    `if (...)` line between #ifdef and the timing scope.

    These are audited buckets 8 (NetCollectMessages) and 9 (NetSendMessages).
    V1's simple-wrapper regex correctly collapsed the other 16 buckets but did
    not match these two conditional wrappers.
    """
    pattern = re.compile(
        r"(?ms)"
        r"^[ \t]*#ifdef __3DS__[ \t]*\n"
        r"[ \t]*if[ \t]*\([^\n]*AvP\.Network[^\n]*\)[ \t]*\n"
        r"[ \t]*\{[ \t]*\n"
        r"[ \t]*unsigned long long avp3ds_hr_start[ \t]*=[ \t]*"
        r"AvP3DS_HeadroomTick\(\);[ \t]*\n"
        r"(?P<instrumented>.*?)"
        r"^[ \t]*AvP3DS_HeadroomAccumulate\((?:8|9),[ \t]*avp3ds_hr_start\);[ \t]*\n"
        r"[ \t]*\}[ \t]*\n"
        r"[ \t]*#else[ \t]*\n"
        r"(?P<original>.*?)"
        r"^[ \t]*#endif[ \t]*(?:\n|$)"
    )

    count = 0
    while True:
        m = pattern.search(text)
        if not m:
            break

        original = m.group("original")
        if "AvP3DS_" in original:
            fail(
                f"{label}: conditional HEADROOM2 else branch unexpectedly "
                "contains AvP3DS instrumentation"
            )

        instrumented = m.group("instrumented")
        if "NetCollectMessages" in instrumented:
            if "NetCollectMessages" not in original:
                fail(f"{label}: NetCollectMessages original branch mismatch")
        elif "NetSendMessages" in instrumented:
            if "NetSendMessages" not in original:
                fail(f"{label}: NetSendMessages original branch mismatch")
        else:
            fail(f"{label}: unexpected conditional HEADROOM2 network wrapper")

        text = text[:m.start()] + original + text[m.end():]
        count += 1

    return text, count

def remove_benchmark_calls(text: str) -> tuple[str, int, int]:
    call_count_before = len(re.findall(r"\bAvP3DS_Benchmark[A-Za-z0-9_]*\s*\(\s*\)\s*;", text))

    block = re.compile(
        r"(?ms)"
        r"^[ \t]*#ifdef __3DS__[ \t]*\n"
        r"(?P<body>(?:[ \t]*AvP3DS_Benchmark[A-Za-z0-9_]*\s*\(\s*\)\s*;[ \t]*\n)+)"
        r"[ \t]*#endif[ \t]*(?:\n|$)"
    )
    text, blocks = block.subn("", text)

    # Remove now-unused benchmark extern declarations.
    text = re.sub(
        r"(?m)^[ \t]*extern[^\n;]*\bAvP3DS_Benchmark[A-Za-z0-9_]*\s*\([^;]*\);[ \t]*\n",
        "",
        text,
    )

    call_count_after = len(re.findall(r"\bAvP3DS_Benchmark[A-Za-z0-9_]*\s*\(\s*\)\s*;", text))
    return text, blocks, call_count_before - call_count_after


def remove_headroom_externs(text: str) -> str:
    return re.sub(
        r"(?m)^[ \t]*extern[^\n;]*\bAvP3DS_Headroom[A-Za-z0-9_]*\s*\([^;]*\);[ \t]*\n",
        "",
        text,
    )


def deactivate_validation_diagnostics(public: Path) -> dict:
    main = public / "src/main.c"
    game = public / "src/avp/game.c"

    main_text = main.read_text(encoding="utf-8", errors="strict")
    game_text = game.read_text(encoding="utf-8", errors="strict")

    # Sixteen buckets use the simple wrapper shape. Buckets 8/9 (network
    # collect/send) are conditional wrappers with an `if (AvP.Network...)`
    # between #ifdef and the timing scope; collapse those separately.
    main_text, main_hr = collapse_headroom_wrappers(main_text, "src/main.c")
    game_text, game_hr = collapse_headroom_wrappers(game_text, "src/avp/game.c")
    game_text, game_net_hr = collapse_headroom_conditional_wrappers(
        game_text, "src/avp/game.c"
    )
    total_hr = main_hr + game_hr + game_net_hr

    # Audited final tree: main=7, game simple=9, game conditional network=2.
    if main_hr != 7 or game_hr != 9 or game_net_hr != 2 or total_hr != 18:
        fail(
            "release cleanup HEADROOM2 census mismatch: expected "
            "main=7 game-simple=9 game-network=2 total=18; "
            f"got main={main_hr} game-simple={game_hr} "
            f"game-network={game_net_hr} total={total_hr}"
        )

    main_text, bench_blocks, bench_calls = remove_benchmark_calls(main_text)
    if bench_calls != 5:
        fail(f"release cleanup expected 5 benchmark calls, removed {bench_calls}")
    if bench_blocks != 4:
        fail(f"release cleanup expected 4 benchmark #ifdef blocks, removed {bench_blocks}")

    main_text = remove_headroom_externs(main_text)
    game_text = remove_headroom_externs(game_text)

    marker = (
        "/* AVP3DS-V1.0.0-FINALSHIP1:\n"
        " * Hardware-validation benchmark/HEADROOM callsites are deactivated in the\n"
        " * public/release snapshot. Proven renderer/HUD/stereo/gameplay code remains.\n"
        " */\n"
    )
    if "AVP3DS-V1.0.0-FINALSHIP1" in main_text:
        fail("release marker already present unexpectedly")

    insert = main_text.find("\n")
    main_text = main_text[:insert + 1] + marker + main_text[insert + 1:]

    for label, text in (("src/main.c", main_text), ("src/avp/game.c", game_text)):
        if "AvP3DS_HeadroomTick" in text or "AvP3DS_HeadroomAccumulate" in text:
            fail(f"{label}: HEADROOM2 callsite survived cleanup")
    if re.search(r"\bAvP3DS_Benchmark[A-Za-z0-9_]*\s*\(", main_text):
        fail("src/main.c: benchmark call/declaration survived cleanup")

    main.write_text(main_text, encoding="utf-8")
    game.write_text(game_text, encoding="utf-8")

    return {
        "headroom_wrappers_removed": total_hr,
        "headroom_simple_main_removed": main_hr,
        "headroom_simple_game_removed": game_hr,
        "headroom_network_game_removed": game_net_hr,
        "benchmark_ifdef_blocks_removed": bench_blocks,
        "benchmark_calls_removed": bench_calls,
        "development_source_mutated": False,
    }


def discover_runtime_strings(root: Path) -> list[str]:
    rx = re.compile(r"(?:sdmc:|romfs:)/[^\"'\s)]+", re.I)
    found = set()
    for rel in (Path("src/main.c"), Path("src/main_3ds.c"), Path("src/psnd_3ds.c")):
        p = root / rel
        if not p.is_file():
            continue
        for m in rx.finditer(p.read_text(encoding="utf-8", errors="replace")):
            s = m.group(0)
            if len(s) < 200:
                found.add(s)
    return sorted(found)


def preserve_upstream_readme(public: Path) -> None:
    readme = public / "README.md"
    if readme.is_file():
        upstream = public / "UPSTREAM_README.md"
        if not upstream.exists():
            readme.rename(upstream)


def write_public_docs(public: Path) -> None:
    preserve_upstream_readme(public)
    runtime = discover_runtime_strings(public)

    runtime_lines = "\n".join(f"- `{x}`" for x in runtime) if runtime else (
        "- No single hard-coded `sdmc:/3ds/...` retail-data root was resolved by the "
        "release helper; keep the data layout expected by this AvP Gold source tree."
    )

    readme = f"""# AvP3DS

Nintendo 3DS port of **Aliens versus Predator / AvP Gold**, prepared from the
finished hardware-validated stereo source tree.

Current release: **v{VERSION}**

## v1.0.0 scope

- Single-player Marine, Predator and Alien campaigns.
- Native 3DS/Citro3D upper-screen world renderer.
- True stereoscopic 3D with flat menu/death/sight presentation where required.
- Species-specific lower-screen HUD presentation.
- Save/load/restart and clean return/exit paths.
- Final 3DS menu lockdown removes unsupported desktop multiplayer/network,
  video-mode/detail and PC input-configuration routes.
- Final handheld locomotion tuning keeps the original species speed ratios while
  scaling player forward/back/strafe movement to 65% of the previous port speed.

## Original game data

Commercial Aliens versus Predator game data is **not** part of the intended
public source/release payload. Use your own legally obtained game data with the
layout expected by this port.

## Building

See [BUILDING_3DS.md](BUILDING_3DS.md).

## Installing

See [INSTALLING.md](INSTALLING.md).

## Known limitations

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Upstream / licensing

This tree preserves the upstream license/notice files present in the source
snapshot. See [UPSTREAM_AND_LICENSE.md](UPSTREAM_AND_LICENSE.md).
"""
    write_text(public / "README.md", readme)

    building = f"""# Building AvP3DS v{VERSION}

The final release build uses the project's `Makefile.3ds`.

Requirements:

- devkitPro / devkitARM
- Nintendo 3DS libraries used by this source tree (libctru, Citro3D/Citro2D)
- GNU Make
- the self-contained SDL2 3DS headers/static library under `Libraries/SDL2/`

From the repository root:

```bash
make -f Makefile.3ds -j4
```

Expected primary output:

```text
AvP_Gold.3dsx
```

The final public source snapshot contains the marker:

```text
AVP3DS-V1.0.0-FINALSHIP1
```

That marker identifies the release source copy where hardware-validation
benchmark/HEADROOM callsites have been deactivated. The development source tree
used to collect the final hardware evidence is intentionally left untouched by
the release builder.
"""
    write_text(public / "BUILDING_3DS.md", building)

    installing = f"""# Installing AvP3DS v{VERSION}

1. Use a homebrew-capable Nintendo 3DS. Final hardware validation targeted New
   Nintendo 3DS hardware.
2. Copy `AvP_Gold.3dsx` from the GitHub Release to your preferred Homebrew
   Launcher folder.
3. Provide your own legally obtained Aliens versus Predator / AvP Gold game
   data using the layout expected by this source build.
4. Do not copy development performance logs or audit files to the SD card.

Runtime/resource strings resolved from the final source snapshot:

{runtime_lines}

The GitHub release/source package intentionally does not include commercial
retail game data.
"""
    write_text(public / "INSTALLING.md", installing)

    controls = """# AvP3DS controls

The Nintendo 3DS build uses a fixed native input bridge rather than the desktop
PC control-configuration menus.

Core handheld controls proven by the final source:

- Circle Pad: player movement / strafe
- C-nub: mouse-look style camera control
- START: in-game menu / pause path
- Native 3DS face, shoulder and D-pad buttons are translated through the fixed
  3DS input mapping in `src/main.c` / `src/main_3ds.c`.

The v1.0.0 ship build applies a 65% multiplier to player forward/back/strafe
locomotion after species/run/walk/encumbrance rules. Turning and jump speed are
not scaled by that ship tuning.
"""
    write_text(public / "CONTROLS.md", controls)

    known = """# Known issues / intentional limitations

- v1.0.0 is the finalized single-player release. Desktop multiplayer/network
  and skirmish setup routes are intentionally unavailable on Nintendo 3DS.
- Desktop video mode, renderer/detail and PC input-rebinding surfaces are
  intentionally hidden/rejected; the 3DS renderer and controls are fixed.
- Very dense combat/effects scenes can run below the lighter 25-35+ FPS range.
  The final hardware-approved build remains playable and stable in those scenes.
- Loading/front-end presentation can show mild stereo depth/flicker artifacts;
  this was accepted rather than risk the stable gameplay stereo path.
- Final validation and release decisions were made on New Nintendo 3DS hardware.
- Post-v1.0 reproducible crashes or regressions can be addressed as 1.0.x
  hotfixes when accompanied by a useful description/log.
"""
    write_text(public / "KNOWN_ISSUES.md", known)

    notes = f"""# AvP3DS v{VERSION}

First public ship release of the completed Nintendo 3DS port.

## Completed release systems

- Native Citro3D gameplay renderer
- True stereoscopic 3D
- Marine, Predator and Alien lower-screen HUD presentation
- Stable heavy-effects command-buffer configuration
- 3DS-native movement and C-nub look
- Save/load/restart/quit lifecycle
- Species-preserving 65% handheld locomotion tuning
- Single-player-focused menu lockdown
- Final audio-management cadence optimization
- Hardware stress/regression validation

## Distribution policy

The prepared GitHub tree and Release staging deliberately exclude retail game
data, hardware logs, build objects, backups and development patch-cannon files.
"""
    write_text(public / f"RELEASE_NOTES_v{VERSION}.md", notes)

    upstream = """# Upstream and licensing

AvP3DS is built from the Aliens versus Predator / AvP Gold source tree used by
this project plus Nintendo 3DS-specific porting work.

The public preparation process preserves the license, COPYING and NOTICE files
already present in the source snapshot. Those files remain authoritative for
the source components they cover.

Commercial game data, soundtrack/media files and other retail payloads are not
intended to be distributed by this release-preparation tool.

Aliens versus Predator and related names/assets belong to their respective
rights holders. This is an unofficial homebrew port.
"""
    write_text(public / "UPSTREAM_AND_LICENSE.md", upstream)

    gitignore = """# Local 3DS builds
AvP_Gold.3dsx
AvP_Gold.elf
AvP_Gold.smdh
*.3dsx
*.elf
*.cia
*.smdh
*.map
*.o
*.obj

# Diagnostics / logs
*.log
AVP_SHIP1_PERF_*.log
AVP3DS_*_AUDIT_*.txt
AVP3DS_*_AUDIT_*.json

# Backups / patch leftovers
*.bak
*.orig
*.rej
*.tmp
*~

# Generated release work
SHIP_FREEZE/
dist/
build/
build_3ds/
cmake-build-*/

# Local retail data roots
RetailData/
Retail_Data/
GameData/
Game_Data/
OriginalData/
Original_Data/

# Editor / OS
.vs/
.idea/
__pycache__/
.DS_Store
"""
    write_text(public / ".gitignore", gitignore)


def copy_release_tools(public: Path) -> None:
    dst = public / "tools/release"
    dst.mkdir(parents=True, exist_ok=True)

    here = Path(__file__).resolve()
    if here.is_file():
        shutil.copy2(here, dst / "AVP3DS_FINAL_SHIP_V1_0_0.py")

    candidates = [
        ROOT / "AVP3DS_V1_0_0_FINAL_AUDIT.py",
        Path("/mnt/data/AVP3DS_V1_0_0_FINAL_AUDIT.py"),
    ]
    audit = next((p for p in candidates if p.is_file()), None)
    if audit:
        shutil.copy2(audit, dst / "AVP3DS_V1_0_0_FINAL_AUDIT.py")


def scan_public_tree(public: Path) -> dict:
    prohibited = []
    suspicious = []
    huge = []
    warn = []

    for p in public.rglob("*"):
        if not p.is_file():
            continue
        rel = p.relative_to(public)
        rel_posix = rel.as_posix()
        suffix = p.suffix.lower()
        size = p.stat().st_size

        if suffix in PUBLIC_EXCLUDE_SUFFIXES:
            prohibited.append(rel_posix)

        if suffix == ".a" and not rel_posix.startswith(ALLOW_ARCHIVE_PREFIXES):
            prohibited.append(rel_posix)

        if suffix in SUSPICIOUS_PUBLIC_EXTS:
            suspicious.append(rel_posix)

        if any(part.lower() in BLOCKED_RETAIL_DIRS for part in rel.parts):
            prohibited.append(rel_posix)

        if size >= FAIL_SIZE:
            huge.append(rel_posix)
        elif size >= WARN_SIZE:
            warn.append(rel_posix)

    return {
        "prohibited": sorted(set(prohibited)),
        "suspicious": sorted(set(suspicious)),
        "huge": sorted(set(huge)),
        "warn_size": sorted(set(warn)),
    }


def write_public_tree_audit(public: Path, source_hashes: dict, cleanup: dict,
                            copied_files: int, copied_bytes: int,
                            skip_reasons: dict[str, int]) -> None:
    scan = scan_public_tree(public)

    if scan["prohibited"]:
        fail("prohibited build/log/retail files in public tree:\n  " +
             "\n  ".join(scan["prohibited"][:100]))
    if scan["suspicious"]:
        fail("suspicious retail/media files in public tree:\n  " +
             "\n  ".join(scan["suspicious"][:100]))
    if scan["huge"]:
        fail("GitHub >=100 MiB file(s) in public tree:\n  " +
             "\n  ".join(scan["huge"][:100]))

    lines = [
        "AVP3DS v1.0.0 PUBLIC TREE OBJECTIVE AUDIT",
        "=" * 48,
        f"Milestone: {MILESTONE}",
        "",
        "RESULT: PASS",
        "",
        f"Copied public source files before generated docs: {copied_files}",
        f"Copied public source bytes before generated docs: {copied_bytes}",
        "",
        "Release-source cleanup:",
        f"  HEADROOM2 wrappers removed: {cleanup['headroom_wrappers_removed']}",
        f"  benchmark #ifdef blocks removed: {cleanup['benchmark_ifdef_blocks_removed']}",
        f"  benchmark calls removed: {cleanup['benchmark_calls_removed']}",
        "  development source mutated: NO",
        "",
        "Final required marker:",
        "  AVP3DS-V1.0.0-FINALSHIP1: PRESENT",
        "",
        "Public safety:",
        "  prohibited build/log/backups: NONE",
        "  suspicious retail/media extensions: NONE",
        "  >=100 MiB GitHub files: NONE",
        f"  50-99 MiB warnings: {len(scan['warn_size'])}",
        "",
        "Skipped source entries by reason:",
    ]
    for reason, count in sorted(skip_reasons.items()):
        lines.append(f"  {count:6d}  {reason}")

    lines += ["", "Hardware-approved development-source SHA-256 before staging:"]
    for rel, meta in sorted(source_hashes.items()):
        lines.append(f"  {meta['sha256']}  {rel}")

    write_text(public / "PUBLIC_TREE_AUDIT.txt", "\n".join(lines))


def prepare_master(master: Path, source_hashes: dict) -> None:
    source_dst = master / "SourceSnapshot"
    files, total, skipped = copy_filtered_tree(ROOT, source_dst, "master")

    latest_audits = sorted(
        list(ROOT.glob("AVP3DS_V1_0_0_FINAL_AUDIT_*.txt")) +
        list(ROOT.glob("AVP3DS_V1_0_0_FINAL_AUDIT_*.json")),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    audit_dst = master / "FinalAudits"
    audit_dst.mkdir(parents=True, exist_ok=True)
    for p in latest_audits[:4]:
        shutil.copy2(p, audit_dst / p.name)

    write_text(
        master / "README.txt",
        f"""AVP3DS v{VERSION} — FOR-US MASTER FREEZE

Milestone:
  {MILESTONE}

This folder preserves a non-retail source snapshot and the latest final audit
evidence. It is NOT the GitHub public tree.

Source files copied: {files}
Source bytes copied: {total}

Retail/commercial game-data roots are excluded even from this freeze.

The public source is:
  ../02_GITHUB_DISTRIBUTION/

Release/evidence is:
  ../03_FINAL_EVIDENCE/

The proven development source at:
  {ROOT}
was not mutated by final release cleanup.
""",
    )

    write_text(
        master / "SOURCE_BASELINE_SHA256.txt",
        "\n".join(
            f"{meta['sha256']}  {rel}"
            for rel, meta in sorted(source_hashes.items())
        ),
    )

    write_text(
        master / "COPY_SKIP_REASONS.txt",
        "\n".join(f"{count:6d}  {reason}" for reason, count in sorted(skipped.items())),
    )


def hash_tree(root: Path, output: Path) -> tuple[int, int]:
    rows = []
    count = 0
    total = 0
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        size = p.stat().st_size
        rows.append(f"{sha256(p)}  {size:12d}  {rel}")
        count += 1
        total += size
    write_text(output, "\n".join(rows))
    return count, total


def find_build_artifact(build_root: Path, suffix: str) -> Path | None:
    preferred = build_root / f"AvP_Gold{suffix}"
    if preferred.is_file():
        return preferred

    candidates = [
        p for p in build_root.rglob(f"*{suffix}")
        if p.is_file() and "avp" in p.name.lower()
    ]
    if len(candidates) == 1:
        return candidates[0]
    return None


def build_release(public: Path, build_evidence: Path, jobs: int) -> dict:
    work = build_evidence / "BUILD_WORK"
    if work.exists():
        fail(f"build work destination exists: {work}")
    shutil.copytree(public, work)

    # Prove this is a clean copied tree, not an incremental dev build.
    for pattern in ("*.o", "*.obj", "*.elf", "*.3dsx", "*.smdh", "*.map"):
        leftovers = [
            p for p in work.rglob(pattern)
            if not p.as_posix().startswith((work / "Libraries/SDL2").as_posix())
        ]
        if leftovers:
            fail("fresh BUILD_WORK unexpectedly contains build outputs:\n  " +
                 "\n  ".join(str(p.relative_to(work)) for p in leftovers[:50]))

    cmd = ["make", "-f", "Makefile.3ds", f"-j{jobs}"]
    p = run(cmd, work, timeout=1200, check=False)

    log = [
        f"AVP3DS v{VERSION} FINAL CLEAN BUILD",
        "=" * 48,
        f"Milestone: {MILESTONE}",
        f"Working tree: {work}",
        f"Command: {' '.join(cmd)}",
        f"Return code: {p.returncode}",
        "",
        p.stdout,
    ]
    write_text(build_evidence / "FINAL_BUILD.log", "\n".join(log))

    if p.returncode != 0:
        fail(
            "final clean build failed. See:\n"
            f"  {build_evidence / 'FINAL_BUILD.log'}"
        )

    x3 = find_build_artifact(work, ".3dsx")
    elf = find_build_artifact(work, ".elf")
    smdh = find_build_artifact(work, ".smdh")

    if x3 is None:
        fail("final build succeeded but no unique AvP .3dsx was found")
    if elf is None:
        fail("final build succeeded but no unique AvP .elf was found")

    return {
        "work": work,
        "3dsx": x3,
        "elf": elf,
        "smdh": smdh,
        "3dsx_sha256": sha256(x3),
        "elf_sha256": sha256(elf),
    }


def copy_final_evidence(source_hashes: dict, public: Path, evidence: Path,
                        build: dict) -> Path:
    release = evidence / "Release"
    release.mkdir(parents=True, exist_ok=True)

    x3_dst = release / "AvP_Gold.3dsx"
    shutil.copy2(build["3dsx"], x3_dst)

    if build["smdh"] is not None:
        shutil.copy2(build["smdh"], release / "AvP_Gold.smdh")

    for name in (
        "README.md",
        "INSTALLING.md",
        "CONTROLS.md",
        "KNOWN_ISSUES.md",
        f"RELEASE_NOTES_v{VERSION}.md",
    ):
        p = public / name
        if p.is_file():
            shutil.copy2(p, release / name)

    # Copy unique hardware performance logs only. Never select the old fixed name.
    hw = evidence / "HardwareLogs"
    hw.mkdir(parents=True, exist_ok=True)
    logs = sorted(
        ROOT.glob("AVP_SHIP1_PERF_*_*.log"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for p in logs[:3]:
        shutil.copy2(p, hw / p.name)

    # Latest objective audit(s)
    audit_files = sorted(
        list(ROOT.glob("AVP3DS_V1_0_0_FINAL_AUDIT_*.txt")) +
        list(ROOT.glob("AVP3DS_V1_0_0_FINAL_AUDIT_*.json")),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for p in audit_files[:4]:
        shutil.copy2(p, evidence / p.name)

    write_text(
        evidence / "FINAL_SOURCE_BASELINE_SHA256.txt",
        "\n".join(
            f"{meta['sha256']}  {rel}"
            for rel, meta in sorted(source_hashes.items())
        ),
    )

    write_text(
        release / "FINAL_3DSX_SHA256.txt",
        f"{sha256(x3_dst)}  {x3_dst.name}",
    )

    sums = []
    for p in sorted(release.iterdir()):
        if p.is_file() and p.name != "SHA256SUMS.txt":
            sums.append(f"{sha256(p)}  {p.name}")
    write_text(release / "SHA256SUMS.txt", "\n".join(sums))

    write_text(
        release / "GITHUB_RELEASE_README.txt",
        f"""GITHUB RELEASE ASSETS — AvP3DS v{VERSION}

Tag:
  v{VERSION}

Release title:
  AvP3DS v{VERSION}

Primary executable:
  AvP_Gold.3dsx

3DSX SHA-256:
  {sha256(x3_dst)}

Suggested upload:
  AvP_Gold.3dsx
  AvP_Gold.smdh (if present)
  RELEASE_NOTES_v{VERSION}.md
  INSTALLING.md
  FINAL_3DSX_SHA256.txt
  SHA256SUMS.txt
  AvP3DS-v{VERSION}-3DS.zip

IMPORTANT:
  Upload release binaries/assets to the GitHub Release, not Git history.
  Do not upload retail Aliens versus Predator game data or soundtrack/media.
""",
    )

    zip_path = release / f"AvP3DS-v{VERSION}-3DS.zip"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for p in sorted(release.iterdir()):
            if not p.is_file() or p == zip_path:
                continue
            zf.write(p, arcname=f"AvP3DS-v{VERSION}-3DS/{p.name}")

    write_text(
        release / "FINAL_RELEASE_ZIP_SHA256.txt",
        f"{sha256(zip_path)}  {zip_path.name}",
    )
    return zip_path


def write_manifest_bundle(freeze: Path, source_hashes: dict,
                          public_files: int, public_bytes: int,
                          build: dict, zip_path: Path) -> None:
    manifests = freeze / "04_MANIFESTS"
    manifests.mkdir(parents=True, exist_ok=True)

    counts = {}
    for dirname in (
        "00_READ_ME_FIRST",
        "01_FOR_US_MASTER",
        "02_GITHUB_DISTRIBUTION",
        "03_FINAL_EVIDENCE",
    ):
        root = freeze / dirname
        count, total = hash_tree(root, manifests / f"{dirname}_SHA256.txt")
        counts[dirname] = {"files": count, "bytes": total}

    meta = {
        "milestone": MILESTONE,
        "version": VERSION,
        "created": dt.datetime.now().astimezone().isoformat(),
        "source_root": str(ROOT),
        "development_source_mutated": False,
        "public_copy_files_reported": public_files,
        "public_copy_bytes_reported": public_bytes,
        "final_3dsx_sha256": build["3dsx_sha256"],
        "final_elf_sha256": build["elf_sha256"],
        "release_zip_sha256": sha256(zip_path),
        "source_baseline": source_hashes,
        "trees": counts,
        "git_actions_performed": {
            "commit": False,
            "push": False,
            "tag": False,
            "publish_release": False,
        },
    }
    write_text(manifests / "FREEZE_MANIFEST.json", json.dumps(meta, indent=2, sort_keys=True))


def validate_github_clone(path: Path) -> None:
    if not path.is_dir() or not (path / ".git").exists():
        fail(f"--github-clone is not an existing Git clone: {path}")
    if not shutil.which("git"):
        fail("git is not available in PATH")

    p = run(["git", "status", "--porcelain"], path, timeout=30, check=True)
    if p.stdout.strip():
        fail(
            "--github-clone working tree is not clean. Refusing to replace files:\n"
            + p.stdout
        )


def copy_into_github_clone(public: Path, clone: Path) -> None:
    validate_github_clone(clone)

    for child in clone.iterdir():
        if child.name == ".git":
            continue
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()

    for child in public.iterdir():
        dst = clone / child.name
        if child.is_dir():
            shutil.copytree(child, dst)
        else:
            shutil.copy2(child, dst)

    # Verify .git survived and print status; do not commit.
    if not (clone / ".git").is_dir():
        fail(".git directory was lost while staging GitHub clone")
    status = run(["git", "status", "--short"], clone, timeout=30, check=True)
    write_text(
        clone / "GITHUB_STAGING_STATUS.txt",
        "AvP3DS v1.0.0 public tree staged locally.\n\n"
        "NO commit/push/tag/release was performed.\n\n"
        "git status --short:\n" + status.stdout,
    )


def preflight(args) -> dict:
    if "avp3ds_stereo" not in ROOT.as_posix().lower():
        fail("run from C:/Projects/AVP3DS_Stereo/Source")

    if not shutil.which("make"):
        fail("make is not available in PATH")

    source_hashes = validate_finished_baseline(ROOT)

    makefile = (ROOT / "Makefile.3ds").read_text(encoding="utf-8", errors="replace")
    if "Libraries/SDL2" not in makefile.replace("\\", "/"):
        print("WARNING: Makefile.3ds does not visibly reference Libraries/SDL2.")
        print("         Final copied clean build will be the authority.")

    if args.github_clone:
        validate_github_clone(args.github_clone)

    return source_hashes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true",
                    help="create final freeze, clean-build and stage GitHub/release files")
    ap.add_argument("--github-clone", type=Path,
                    help="optional existing CLEAN local Git clone to populate; .git is preserved")
    ap.add_argument("--jobs", type=int, default=4,
                    help="parallel make jobs (default: 4)")
    args = ap.parse_args()

    if args.jobs < 1 or args.jobs > 32:
        fail("--jobs must be 1..32")

    if args.github_clone is not None:
        args.github_clone = args.github_clone.resolve()

    source_hashes = preflight(args)

    print("=" * 84)
    print(f"AVP3DS FINAL SHIP BUILDER v{VERSION} V2")
    print("=" * 84)
    print(f"Milestone: {MILESTONE}")
    print(f"Source:    {ROOT}")
    print()
    print("Finished markers: PASS")
    print("65% final movement scale: PASS")
    print("Old YEET32B/save-trace debug: ABSENT")
    print("Development source mutation by this tool: NONE")
    print("Commercial retail-data copying: NONE")
    print()
    print("Output shape:")
    print(f"  SHIP_FREEZE/{FREEZE_LABEL}_<timestamp>/")
    print("    00_READ_ME_FIRST/")
    print("    01_FOR_US_MASTER/")
    print("    02_GITHUB_DISTRIBUTION/   <-- public repository tree")
    print("    03_FINAL_EVIDENCE/Release <-- GitHub Release assets")
    print("    04_MANIFESTS/")
    print()

    if not args.apply:
        print("DRY RUN PASSED.")
        print("No files changed.")
        print()
        print("Run:")
        cmd = "python AVP3DS_FINAL_SHIP_V1_0_0_V3.py --apply"
        if args.github_clone is not None:
            cmd += f' --github-clone "{args.github_clone}"'
        print("  " + cmd)
        return 0

    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    freeze_parent = ROOT / "SHIP_FREEZE"
    freeze = freeze_parent / f"{FREEZE_LABEL}_{timestamp}"

    if freeze.exists():
        fail(f"freeze destination already exists: {freeze}")

    readme_dir = freeze / "00_READ_ME_FIRST"
    master = freeze / "01_FOR_US_MASTER"
    public = freeze / "02_GITHUB_DISTRIBUTION"
    evidence = freeze / "03_FINAL_EVIDENCE"
    build_evidence = evidence / "Build"

    freeze.mkdir(parents=True)

    try:
        prepare_master(master, source_hashes)

        public_files, public_bytes, skip_reasons = copy_filtered_tree(
            ROOT, public, "public"
        )

        cleanup = deactivate_validation_diagnostics(public)
        write_public_docs(public)
        copy_release_tools(public)
        write_public_tree_audit(
            public,
            source_hashes,
            cleanup,
            public_files,
            public_bytes,
            skip_reasons,
        )

        # Re-validate the finished milestone after cleanup. All gameplay markers
        # must survive; only validation callsites are removed.
        public_joined = source_join(public)
        for marker in REQUIRED_MARKERS:
            if marker not in public_joined:
                fail(f"public cleanup lost finished milestone marker: {marker}")
        if "AVP3DS-V1.0.0-FINALSHIP1" not in (
            public / "src/main.c"
        ).read_text(encoding="utf-8", errors="replace"):
            fail("public release marker missing after cleanup")

        build = build_release(public, build_evidence, args.jobs)
        zip_path = copy_final_evidence(source_hashes, public, evidence, build)

        write_text(
            readme_dir / "READ_ME_FIRST.txt",
            f"""AVP3DS v{VERSION} — SHIPPED / FINISHED / GITHUB FREEZE

Milestone:
  {MILESTONE}

FINAL GAMEPLAY SOURCE:
  Hardware-approved development tree at:
    {ROOT}

PUBLIC GITHUB TREE:
  ../02_GITHUB_DISTRIBUTION/

GITHUB RELEASE ASSETS:
  ../03_FINAL_EVIDENCE/Release/

FINAL 3DSX SHA-256:
  {build['3dsx_sha256']}

FINAL RELEASE ZIP SHA-256:
  {sha256(zip_path)}

OBJECTIVE FINAL STATUS:
  * Marine / Predator / Alien single-player: ship scope
  * final 65% handheld player locomotion scale: present
  * stereo/HUD/512-KiB command-buffer milestones: preserved
  * unsupported desktop/network menu routes: hidden/rejected
  * YEET32B/save fixed-trace debug: removed
  * YEET28/HEADROOM2 validation callsites: deactivated in public/release snapshot
  * final build: clean copied-tree build, PASS
  * commercial retail data in public/release staging: NONE by automated scan
  * development source mutated by finalizer: NO
  * automatic git commit/push/tag/release: NO

The project is staged as v1.0.0. Final GitHub commit/tag/release actions remain
explicit manual actions, exactly like the Citadel / JK ship workflow.
""",
        )

        write_manifest_bundle(
            freeze, source_hashes, public_files, public_bytes, build, zip_path
        )

        if args.github_clone is not None:
            copy_into_github_clone(public, args.github_clone)

    except Exception:
        # Keep the freeze on failure: it is useful forensic evidence and never
        # overwrites the development source. Re-raise for a visible traceback
        # only if it was not our normal SystemExit path.
        raise

    print()
    print("=" * 84)
    print("AVP3DS v1.0.0 FINAL FREEZE: PASS")
    print("=" * 84)
    print(f"Freeze:          {freeze}")
    print(f"Public Git tree: {public}")
    print(f"Release assets:  {evidence / 'Release'}")
    print(f"Final 3DSX:      {evidence / 'Release' / 'AvP_Gold.3dsx'}")
    print(f"3DSX SHA-256:    {build['3dsx_sha256']}")
    print(f"Release ZIP:     {zip_path}")
    print(f"ZIP SHA-256:     {sha256(zip_path)}")
    if args.github_clone is not None:
        print(f"Git clone staged:{args.github_clone}")
    print()
    print("NO commit / push / tag / GitHub Release was performed.")
    print("This pupper is staged for the explicit final Git submission. :D")
    return 0


if __name__ == "__main__":
    sys.exit(main())
