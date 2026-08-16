#!/usr/bin/env python3

from __future__ import annotations

from datetime import datetime
from hashlib import sha256
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


BASELINE_NAME = "AvP3DS_YEET33D_FULL_AUDIO_BASELINE"


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def run_capture(command: list[str], cwd: Path) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except FileNotFoundError:
        return f"Command unavailable: {command[0]}\n"

    return (
        f"$ {' '.join(command)}\n"
        f"exit code: {result.returncode}\n\n"
        f"{result.stdout}"
    )


def hash_file(path: Path) -> str:
    digest = sha256()

    with path.open("rb") as file:
        while True:
            block = file.read(1024 * 1024)

            if not block:
                break

            digest.update(block)

    return digest.hexdigest()


source_root = Path.cwd().resolve()




required = [
    source_root / "src" / "main.c",
    source_root / "src" / "main_3ds.c",
    source_root / "src" / "psnd_3ds.c",
    source_root / "Makefile.3ds",
    source_root / "AvP_Gold.elf",
]

missing = [str(path) for path in required if not path.exists()]

if missing:
    fail(
        "run this from /c/Projects/AvP3DS/Source; missing:\n  "
        + "\n  ".join(missing)
    )

project_root = source_root.parent
baselines_root = project_root / "Baselines"
baseline_root = baselines_root / BASELINE_NAME
zip_path = baselines_root / f"{BASELINE_NAME}.zip"

if baseline_root.exists():
    fail(
        f"baseline already exists:\n{baseline_root}\n"
        "Rename or remove it before running this script again."
    )

if zip_path.exists():
    fail(
        f"baseline ZIP already exists:\n{zip_path}\n"
        "Rename or remove it before running this script again."
    )

print("Creating AvP3DS YEET33D baseline...")
print(f"Source:      {source_root}")
print(f"Destination: {baseline_root}")

baselines_root.mkdir(parents=True, exist_ok=True)
baseline_root.mkdir()

snapshot_root = baseline_root / "Source"
build_root = baseline_root / "Build"
git_root = baseline_root / "Git"

build_root.mkdir()
git_root.mkdir()

ignore = shutil.ignore_patterns(
    ".git",
    "__pycache__",
    "*.o",
    "*.d",
    "*.elf",
    "*.3dsx",
    "*.cia",
    "*.smdh",
    "*.log",
    "build",
    "build_*",
    "fastfile",
    "assets-original",
    "profiles",
    "savegame",
    "screenshots",
)

print("Copying source snapshot...")

shutil.copytree(
    source_root,
    snapshot_root,
    ignore=ignore,
    dirs_exist_ok=False,
)

# Remove this baseline utility from the preserved source snapshot.
snapshot_script = snapshot_root / Path(__file__).name

if snapshot_script.exists():
    snapshot_script.unlink()

print("Saving current linked build...")

current_elf = source_root / "AvP_Gold.elf"
baseline_elf = build_root / "AvP_Gold-YEET33D.elf"
baseline_3dsx = build_root / "AvP_Gold-YEET33D.3dsx"

shutil.copy2(current_elf, baseline_elf)

tool = shutil.which("3dsxtool")

if tool is None:
    print("WARNING: 3dsxtool was not found.")
    print("The ELF was saved, but the 3DSX could not be generated.")
else:
    result = subprocess.run(
        [tool, str(baseline_elf), str(baseline_3dsx)],
        cwd=source_root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )

    if result.returncode != 0:
        fail(
            "3dsxtool failed:\n"
            + result.stdout
        )

    print("Generated AvP_Gold-YEET33D.3dsx")

# Save useful build metadata if it exists.
for filename in (
    "AvP_Gold.map",
    "AvP_Gold.lst",
):
    candidate = source_root / filename

    if candidate.is_file():
        shutil.copy2(candidate, build_root / filename)

print("Capturing Git state...")

(git_root / "HEAD.txt").write_text(
    run_capture(
        ["git", "rev-parse", "HEAD"],
        source_root,
    ),
    encoding="utf-8",
)

(git_root / "STATUS.txt").write_text(
    run_capture(
        ["git", "status", "--short", "--branch"],
        source_root,
    ),
    encoding="utf-8",
)

(git_root / "DIFF.patch").write_text(
    run_capture(
        ["git", "diff", "--binary"],
        source_root,
    ),
    encoding="utf-8",
)

(git_root / "STAGED_DIFF.patch").write_text(
    run_capture(
        ["git", "diff", "--cached", "--binary"],
        source_root,
    ),
    encoding="utf-8",
)

timestamp = datetime.now().astimezone()

readme = f"""\
AvP3DS YEET33D FULL AUDIO BASELINE
==================================

Created:
    {timestamp.isoformat(timespec="seconds")}

Source workspace:
    {source_root}

Baseline purpose
----------------

This snapshot preserves the first hardware-verified AvP3DS build with
the complete normal sound-effect path functioning before music/CDDA
development begins.

Verified hardware state
-----------------------

Gameplay:
- Marine campaign enters controllable gameplay.
- Predator campaign enters controllable gameplay.
- Alien campaign enters controllable gameplay.
- High graphics settings are playable after New 3DS CPU speedup.
- Pause -> Abort Game returns to the front end for all three species.
- Campaign switching after abort works.
- Save and load work on real hardware.

Rendering:
- Citro3D native gameplay renderer works.
- Citro2D front-end state restoration works after leaving gameplay.
- World geometry, models, textures, HUD, depth, and vertex color work.
- Stereoscopic 3D has not yet been implemented.

Controls:
- Circle Pad: movement.
- C-nub: mouse look.
- R: primary attack.
- L: secondary attack.
- A: operate and menu confirmation.
- B: jump.
- ZL: crouch / Alien wall-crawl.
- Y: species-specific former Period action.
- X: Marine jetpack / Predator grapple.
- ZR: species-specific Slash action.
- START: Escape / pause / menu / back.
- SELECT: development safe application exit.

Audio:
- SDL2 audio initializes at 44.1 kHz stereo.
- Permanent common-bank sounds work.
- Dynamic numbered-fastfile sounds work.
- Environmental and placed level sounds are audible.
- Alien swipe, tail, vocal, and other species sounds are audible.
- Predator wristblade and species sounds are audible.
- Marine/NPC voices and weapon sounds are audible.
- Pitch, looping, stopping, and distance attenuation operate.

YEET33D root-cause fix:
- ffread() maps to ffreadb() and returns a BYTE COUNT.
- LoadWavFromFastFile() incorrectly compared that return value to 1.
- The comparison now checks against file_bytes.
- This restored every dynamically loaded WAV from numbered FFL banks.

Not yet implemented
-------------------

Music:
- AvP still uses its legacy CDDA track-selection interface.
- The 3DS CDDA functions remain stubs.
- Music streaming/decoding has not been added.
- This baseline is the rollback point before music implementation.

Lower screen:
- No permanent lower-screen HUD renderer has been established.
- Marine lower-screen artwork currently exists only as a layout concept.
- Existing top-screen HUD remains active.

Known development items
-----------------------

- YEET32B/YEET33C diagnostic reporting may still be present.
- Main-menu Exit Game still reaches the legacy placeholder.
- SELECT is still the temporary development safe-exit control.
- Loading-overlay artwork remains missing.
- Save buffer realloc robustness improvement remains pending.
- PC compatibility of 3DS-padded save files is not established.

Commercial assets
-----------------

Original AvP game assets are NOT included in this baseline.

Continue using the separately maintained, legally user-supplied game
asset directory.

Build command
-------------

make -f Makefile.3ds "$PWD/AvP_Gold.elf" -j4

Package command
---------------

cp AvP_Gold.elf AvP_Gold-YEET33D.elf
3dsxtool AvP_Gold-YEET33D.elf AvP_Gold-YEET33D.3dsx
"""

(baseline_root / "README_BASELINE.txt").write_text(
    readme,
    encoding="utf-8",
)

print("Creating SHA-256 manifest...")

manifest_lines: list[str] = []

for path in sorted(baseline_root.rglob("*")):
    if not path.is_file():
        continue

    if path.name == "SHA256SUMS.txt":
        continue

    relative = path.relative_to(baseline_root).as_posix()
    manifest_lines.append(
        f"{hash_file(path)}  {relative}"
    )

(baseline_root / "SHA256SUMS.txt").write_text(
    "\n".join(manifest_lines) + "\n",
    encoding="utf-8",
)

print("Creating ZIP archive...")

with zipfile.ZipFile(
    zip_path,
    mode="w",
    compression=zipfile.ZIP_DEFLATED,
    compresslevel=9,
) as archive:
    for path in sorted(baseline_root.rglob("*")):
        if not path.is_file():
            continue

        archive_name = (
            Path(BASELINE_NAME)
            / path.relative_to(baseline_root)
        )

        archive.write(path, archive_name.as_posix())

print()
print("YEET33D baseline saved successfully.")
print()
print(f"Folder: {baseline_root}")
print(f"ZIP:    {zip_path}")
print()
print(f"ELF SHA-256: {hash_file(baseline_elf)}")

if baseline_3dsx.exists():
    print(f"3DSX SHA-256: {hash_file(baseline_3dsx)}")
