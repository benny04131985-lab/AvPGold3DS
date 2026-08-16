#!/usr/bin/env python3

from pathlib import Path
import re
import shutil
import subprocess
import wave


EXPECTED_RATE = 44100
EXPECTED_CHANNELS = 2
EXPECTED_WIDTH = 2

source_root = Path.cwd().resolve()
project_root = source_root.parent

fmv_dir = (
    project_root
    / "assets-original"
    / "AvP Classic"
    / "FMVs"
)

music_dir = (
    project_root
    / "assets-original"
    / "AvP Classic"
    / "Music"
)


def find_ffmpeg() -> str | None:
    candidates = [
        shutil.which("ffmpeg"),
        project_root / "tools" / "ffmpeg" / "bin" / "ffmpeg.exe",
        project_root / "tools" / "ffmpeg-unpacked",
    ]

    for candidate in candidates:
        if candidate is None:
            continue

        candidate = Path(candidate)

        if candidate.is_file():
            return str(candidate)

        if candidate.is_dir():
            matches = list(candidate.rglob("ffmpeg.exe"))

            if matches:
                return str(matches[0])

    return None


def valid_output(path: Path) -> tuple[bool, str]:
    if not path.is_file():
        return False, "missing"

    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            rate = wav.getframerate()
            width = wav.getsampwidth()
            frames = wav.getnframes()
    except (wave.Error, EOFError):
        return False, "invalid WAV"

    if channels != EXPECTED_CHANNELS:
        return False, f"{channels} channels"

    if rate != EXPECTED_RATE:
        return False, f"{rate} Hz"

    if width != EXPECTED_WIDTH:
        return False, f"{width * 8}-bit"

    if frames <= 0:
        return False, "empty"

    return True, f"{frames / rate:.2f} seconds"


if not fmv_dir.is_dir():
    raise SystemExit(
        "ERROR: missing FMV directory:\n"
        f"{fmv_dir}"
    )

ffmpeg = find_ffmpeg()

if ffmpeg is None:
    raise SystemExit(
        "ERROR: project-local ffmpeg.exe was not found."
    )

sources: dict[int, Path] = {}

for path in fmv_dir.glob("*.bik"):
    match = re.match(r"^(\d{2})\s", path.name)

    if not match:
        continue

    track = int(match.group(1))

    if 1 <= track <= 15:
        if track in sources:
            raise SystemExit(
                f"ERROR: duplicate source for track {track:02d}:\n"
                f"  {sources[track]}\n"
                f"  {path}"
            )

        sources[track] = path

missing = [
    track
    for track in range(1, 16)
    if track not in sources
]

if missing:
    raise SystemExit(
        "ERROR: missing source BIK track(s): "
        + ", ".join(f"{track:02d}" for track in missing)
    )

music_dir.mkdir(parents=True, exist_ok=True)

print(f"FFmpeg: {ffmpeg}")
print(f"Input:  {fmv_dir}")
print(f"Output: {music_dir}")
print()

converted = 0
skipped = 0

for track in range(1, 16):
    source = sources[track]
    output = music_dir / f"Track{track:02d}.wav"

    valid, description = valid_output(output)

    if valid:
        print(
            f"[{track:02d}/15] Existing valid track: "
            f"{output.name} ({description})"
        )
        skipped += 1
        continue

    if output.exists():
        print(
            f"[{track:02d}/15] Replacing invalid output: "
            f"{description}"
        )
        output.unlink()

    print(
        f"[{track:02d}/15] Converting "
        f"{source.name} -> {output.name}"
    )

    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        str(source),
        "-vn",
        "-ac",
        str(EXPECTED_CHANNELS),
        "-ar",
        str(EXPECTED_RATE),
        "-c:a",
        "pcm_s16le",
        str(output),
    ]

    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )

    if result.returncode != 0:
        if output.exists():
            output.unlink()

        raise SystemExit(
            f"\nERROR: conversion failed for Track {track:02d}:\n"
            + result.stdout
        )

    valid, description = valid_output(output)

    if not valid:
        if output.exists():
            output.unlink()

        raise SystemExit(
            f"ERROR: converted Track {track:02d} failed validation: "
            f"{description}"
        )

    print(
        f"          OK: {description}, "
        f"{output.stat().st_size / (1024 * 1024):.2f} MiB"
    )

    converted += 1

total_bytes = sum(
    (music_dir / f"Track{track:02d}.wav").stat().st_size
    for track in range(1, 16)
)

print()
print("All fifteen AvP music tracks are ready.")
print(f"Converted now: {converted}")
print(f"Already valid: {skipped}")
print(f"Total size:    {total_bytes / (1024 * 1024):.2f} MiB")
print()
print(f"Copy this folder to the active AvP asset root:\n{music_dir}")
