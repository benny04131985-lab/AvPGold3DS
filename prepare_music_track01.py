#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import wave


source_root = Path.cwd().resolve()
project_root = source_root.parent

source_root = Path.cwd().resolve()
project_root = source_root.parent

source_bik = (
    project_root
    / "assets-original"
    / "AvP Classic"
    / "FMVs"
    / "01 Marine Music 1.bik"
)

music_dir = (
    project_root
    / "assets-original"
    / "AvP Classic"
    / "Music"
)

output_wav = music_dir / "Track01.wav"


def find_ffmpeg():
    found = shutil.which("ffmpeg")

    if found:
        return found

    candidates = [
        Path("C:/ffmpeg/bin/ffmpeg.exe"),
        Path("C:/Program Files/ffmpeg/bin/ffmpeg.exe"),
        source_root / "tools" / "ffmpeg" / "bin" / "ffmpeg.exe",
        project_root / "tools" / "ffmpeg" / "bin" / "ffmpeg.exe",
    ]

    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)

    return None


if not source_bik.is_file():
    raise SystemExit(
        "ERROR: missing source BIK:\n"
        f"{source_bik}"
    )

ffmpeg = find_ffmpeg()

if ffmpeg is None:
    raise SystemExit(
        "ERROR: ffmpeg was not found.\n"
        "No files were changed."
    )

music_dir.mkdir(parents=True, exist_ok=True)

command = [
    ffmpeg,
    "-y",
    "-i",
    str(source_bik),
    "-vn",
    "-ac",
    "2",
    "-ar",
    "44100",
    "-c:a",
    "pcm_s16le",
    str(output_wav),
]

print("Converting AvP music Track 01...")
print(f"Input:  {source_bik}")
print(f"Output: {output_wav}")
print()

result = subprocess.run(
    command,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    check=False,
)

if result.returncode != 0:
    if output_wav.exists():
        output_wav.unlink()

    raise SystemExit(
        "ERROR: ffmpeg conversion failed:\n\n"
        + result.stdout
    )

with wave.open(str(output_wav), "rb") as wav:
    channels = wav.getnchannels()
    rate = wav.getframerate()
    width = wav.getsampwidth()
    frames = wav.getnframes()

if channels != 2:
    raise SystemExit(
        f"ERROR: expected stereo WAV, got {channels} channel(s)"
    )

if rate != 44100:
    raise SystemExit(
        f"ERROR: expected 44100 Hz WAV, got {rate} Hz"
    )

if width != 2:
    raise SystemExit(
        f"ERROR: expected 16-bit WAV, got {width * 8}-bit"
    )

duration = frames / rate

print()
print("Track 01 conversion succeeded.")
print(f"Format:   {rate} Hz, stereo, 16-bit PCM")
print(f"Duration: {duration:.2f} seconds")
print(f"Size:     {output_wav.stat().st_size / (1024 * 1024):.2f} MiB")
