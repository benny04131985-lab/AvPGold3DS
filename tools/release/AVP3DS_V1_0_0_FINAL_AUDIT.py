#!/usr/bin/env python3
"""
AVP3DS_V1_0_0_FINAL_AUDIT.py
=============================

READ-ONLY ship/Git preflight for the finished AvP3DS v1.0.0 candidate.

Run from:
    C:/Projects/AVP3DS_Stereo/Source
or:
    /c/Projects/AVP3DS_Stereo/Source

Writes ONLY:
    AVP3DS_V1_0_0_FINAL_AUDIT_<timestamp>.txt
    AVP3DS_V1_0_0_FINAL_AUDIT_<timestamp>.json

No source/build files are modified.
"""

from __future__ import annotations

import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

VERSION = "1.0.0"
MILESTONE = "AVP3DS-V1.0.0-FINALSHIP1-PREFLIGHT"
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

REQUIRED_MARKERS = {
    "AVP-SHIPFINAL1-VOICEHALF-MENUSAFE":
        ["src/avp/psnd.c", "src/avp/win95/frontend/avp_menus.c",
         "src/avp/win95/frontend/avp_menudata.c"],
    "AVP-SHIPFINAL2-MOVESCALE65":
        ["src/avp/pmove.c"],
    "AVP-STEREO-S2C2A1-CMDBUF-512K":
        ["src/main_3ds.c"],
    "AVP-STEREO-S2C2-FLAT-SIGHTS":
        ["src/main_3ds.c", "src/avp"],
    "AVP-STEREO-S2C1A2-FLAT-STATES":
        ["src/main.c", "src/main_3ds.c"],
    "AVP-STEREO-S2B-DEPTH-WARP":
        ["src/main_3ds.c"],
    "AVP-MARINE-HUD-DOWN12":
        ["src/main_3ds.c", "src/avp"],
    "PRED-HUD1H-UPPER-SUPPRESS":
        ["src/main_3ds.c", "src/avp"],
    "ALIEN-HUD1C3-NATIVE-BACKDROP":
        ["src/main_3ds.c", "src/avp"],
    "AVP-EXIT1A2-FRONTEND-LOOP-FIX":
        ["src/main.c", "src/main_3ds.c"],
}

PRE_RELEASE_DIAGNOSTIC_TOKENS = [
    "AVP-SHIP1-PERFLOG1A3",
    "AVP-PERFLOG-UNIQUEPATH1",
    "AVP-HEADROOM2-SUBBUCKET1A3",
    "YEET28 BENCH",
    "HEADROOM2A3",
    "AvP3DS_BenchmarkFrameStart",
    "AvP3DS_HeadroomTick",
    "AvP3DS_HeadroomAccumulate",
    "AVP_SHIP1_PERF_",
]

MUST_BE_ABSENT = [
    "YEET32B AUD",
    "sdmc:/avp_save_trace.log",
]

BLOCKED_DIR_NAMES = {
    "retaildata", "retail_data", "gamedata", "game_data",
    "originaldata", "original_data", "cddata", "cd_data",
    "userretaildata", "user_retail_data",
}

DEV_DIR_NAMES = {
    ".git", ".svn", ".hg", ".vs", ".idea", "__pycache__",
    "dist", "ship_freeze", "baselines", "checkpoints",
}

DEV_FILE_SUFFIXES = {
    ".3dsx", ".elf", ".cia", ".smdh", ".log", ".map", ".o", ".obj",
    ".dll", ".so", ".dylib", ".exe", ".bak", ".orig", ".rej", ".tmp",
}

SUSPICIOUS_RETAIL_EXTS = {".ffl", ".rif", ".smk", ".bik", ".mp3", ".ogg", ".flac"}
WARN_SIZE = 50 * 1024 * 1024
FAIL_SIZE = 100 * 1024 * 1024


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def run(cmd: list[str], timeout: int = 20) -> tuple[int, str]:
    try:
        p = subprocess.run(
            cmd,
            cwd=str(ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        return p.returncode, p.stdout.rstrip()
    except Exception as exc:
        return 999, str(exc)


def discover_license_files() -> list[str]:
    out = []
    for p in ROOT.iterdir():
        if not p.is_file():
            continue
        n = p.name.lower()
        if n.startswith("license") or n.startswith("copying") or n.startswith("notice"):
            out.append(p.name)
    return sorted(out)


def collect_source_text() -> str:
    joined = []
    for rel in KEY_FILES:
        p = ROOT / rel
        if p.is_file() and p.suffix.lower() in {".c", ".cpp", ".h", ".txt"}:
            joined.append(read(p))
    return "\n".join(joined)


def marker_present(marker: str, paths: list[str]) -> tuple[bool, list[str]]:
    found = []
    for raw in paths:
        p = ROOT / raw
        if p.is_file():
            if marker in read(p):
                found.append(raw)
        elif p.is_dir():
            for child in p.rglob("*"):
                if not child.is_file() or child.suffix.lower() not in {".c", ".cpp", ".h", ".hpp"}:
                    continue
                try:
                    if marker in read(child):
                        found.append(child.relative_to(ROOT).as_posix())
                        break
                except Exception:
                    pass
    return bool(found), found


def scan_tree() -> dict:
    blocked_dirs, dev_files, suspicious, huge, warnings = [], [], [], [], []
    total_files = 0

    for dirpath, dirnames, filenames in os.walk(ROOT):
        d = Path(dirpath)
        rel_dir = d.relative_to(ROOT)
        keep = []
        for name in dirnames:
            low = name.lower()
            rel = (rel_dir / name).as_posix()
            if low in BLOCKED_DIR_NAMES:
                blocked_dirs.append(rel)
                continue
            if low in DEV_DIR_NAMES:
                continue
            if rel_dir == Path(".") and (
                low in {"build", "build_3ds"} or low.startswith("cmake-build-")
            ):
                continue
            keep.append(name)
        dirnames[:] = keep

        for name in filenames:
            p = d / name
            rel = p.relative_to(ROOT)
            total_files += 1
            try:
                size = p.stat().st_size
            except OSError:
                continue

            lowname = name.lower()
            suffix = p.suffix.lower()

            if suffix in DEV_FILE_SUFFIXES or lowname.endswith("~"):
                dev_files.append(rel.as_posix())

            if suffix in SUSPICIOUS_RETAIL_EXTS:
                suspicious.append({
                    "path": rel.as_posix(),
                    "bytes": size,
                    "reason": f"suspicious retail/media extension {suffix}",
                })

            if size >= FAIL_SIZE:
                huge.append({"path": rel.as_posix(), "bytes": size})
            elif size >= WARN_SIZE:
                warnings.append({"path": rel.as_posix(), "bytes": size})

    return {
        "total_files_seen": total_files,
        "blocked_retail_dirs_present": sorted(blocked_dirs),
        "development_artifacts": sorted(dev_files),
        "suspicious_media_or_retail_files": suspicious,
        "files_ge_100MiB": huge,
        "files_ge_50MiB": warnings,
    }


def binary_contains(path: Path, token: str) -> bool:
    try:
        return token.encode("utf-8") in path.read_bytes()
    except Exception:
        return False


def main() -> int:
    if "avp3ds_stereo" not in ROOT.as_posix().lower():
        print("ERROR: run from C:/Projects/AVP3DS_Stereo/Source")
        return 1

    now = dt.datetime.now().astimezone()
    stamp = now.strftime("%Y%m%d_%H%M%S")
    txt_path = ROOT / f"AVP3DS_V1_0_0_FINAL_AUDIT_{stamp}.txt"
    json_path = ROOT / f"AVP3DS_V1_0_0_FINAL_AUDIT_{stamp}.json"

    checks, failures, warnings, infos = [], [], [], []

    def record(status: str, name: str, detail: str = ""):
        checks.append({"status": status, "name": name, "detail": detail})
        item = name + (f": {detail}" if detail else "")
        if status == "FAIL":
            failures.append(item)
        elif status == "WARN":
            warnings.append(item)
        elif status == "INFO":
            infos.append(item)

    for rel in KEY_FILES:
        p = ROOT / rel
        record("PASS" if p.is_file() else "FAIL",
               f"required file {rel.as_posix()}",
               str(p) if p.is_file() else "missing")

    marker_results = {}
    for marker, paths in REQUIRED_MARKERS.items():
        ok, found = marker_present(marker, paths)
        marker_results[marker] = found
        record("PASS" if ok else "FAIL", f"marker {marker}",
               ", ".join(found) if found else "not found")

    pmove = ROOT / "src/avp/pmove.c"
    if pmove.is_file():
        t = read(pmove)
        scale_ok = (
            t.count("forwardSpeed = (forwardSpeed * 13) / 20;") == 1
            and t.count("strafeSpeed = (strafeSpeed * 13) / 20;") == 1
        )
        record("PASS" if scale_ok else "FAIL",
               "final 65% movement scale",
               "forward+strafe 13/20 exactly once" if scale_ok
               else "expected exact 13/20 forward+strafe scale not found")

    joined = collect_source_text()
    for token in MUST_BE_ABSENT:
        record("PASS" if token not in joined else "FAIL",
               f"removed debug token {token}",
               "absent" if token not in joined else "still present")

    diag_presence = {}
    for token in PRE_RELEASE_DIAGNOSTIC_TOKENS:
        present = token in joined
        diag_presence[token] = present
        if present:
            record("INFO", f"release cleanup pending: {token}",
                   "present in validation source; finalizer will deactivate callsites")

    artifacts = {}
    newest_source_mtime = 0.0
    for rel in KEY_FILES:
        p = ROOT / rel
        if p.is_file():
            newest_source_mtime = max(newest_source_mtime, p.stat().st_mtime)

    for name in ("AvP_Gold.elf", "AvP_Gold.3dsx", "AvP_Gold.smdh"):
        p = ROOT / name
        if p.is_file():
            st = p.stat()
            artifacts[name] = {
                "bytes": st.st_size,
                "mtime": dt.datetime.fromtimestamp(st.st_mtime, tz=now.tzinfo).isoformat(),
                "sha256": sha256(p),
            }
            record("PASS", f"build artifact {name}",
                   f"{st.st_size} bytes sha256={artifacts[name]['sha256']}")
        elif name != "AvP_Gold.smdh":
            record("WARN", f"build artifact {name}", "not present; finalizer will clean-build")

    x3 = ROOT / "AvP_Gold.3dsx"
    if x3.is_file():
        fresh = x3.stat().st_mtime >= newest_source_mtime
        record("PASS" if fresh else "WARN",
               "current 3DSX newer than audited key source",
               "fresh candidate" if fresh else "current 3DSX predates source; finalizer will rebuild")
        for tok in ("YEET32B AUD", "avp_save_trace.log"):
            present = binary_contains(x3, tok)
            record("PASS" if not present else "WARN",
                   f"candidate binary old debug string {tok}",
                   "absent" if not present else "present in current candidate")

    tools = {}
    for tool in ("make", "arm-none-eabi-gcc", "arm-none-eabi-nm"):
        resolved = shutil.which(tool)
        tools[tool] = resolved
        record("PASS" if resolved else "FAIL", f"tool {tool}", resolved or "not in PATH")

    devkitpro = os.environ.get("DEVKITPRO", "")
    devkitarm = os.environ.get("DEVKITARM", "")
    record("PASS" if devkitpro else "WARN", "DEVKITPRO", devkitpro or "unset")
    record("PASS" if devkitarm else "WARN", "DEVKITARM", devkitarm or "unset")

    git = {}
    if (ROOT / ".git").exists() and shutil.which("git"):
        for key, args in {
            "branch": ["git", "branch", "--show-current"],
            "head": ["git", "rev-parse", "HEAD"],
            "status": ["git", "status", "--porcelain"],
        }.items():
            rc, out = run(args)
            git[key] = out if rc == 0 else f"<rc={rc}> {out}"
        record("INFO", "git repository",
               f"branch={git.get('branch','')} HEAD={git.get('head','')}")
        if git.get("status", "").strip():
            record("INFO", "development tree has local changes",
                   "expected; public freeze will be a clean copy")
    else:
        record("INFO", "git repository",
               "no .git at this root; finalizer can still stage a GitHub tree")

    license_files = discover_license_files()
    record("PASS" if license_files else "WARN",
           "license/notice files",
           ", ".join(license_files) if license_files else "none found at repo root")

    tree_scan = scan_tree()
    if tree_scan["blocked_retail_dirs_present"]:
        record("INFO", "retail-data roots found",
               ", ".join(tree_scan["blocked_retail_dirs_present"]) + " — finalizer excludes them")

    suspicious = tree_scan["suspicious_media_or_retail_files"]
    if suspicious:
        record("WARN", "suspicious retail/media files",
               f"{len(suspicious)} file(s); inspect report before public upload")

    if tree_scan["files_ge_100MiB"]:
        record("WARN", "files >=100 MiB",
               f"{len(tree_scan['files_ge_100MiB'])} file(s)")

    key_hashes = {}
    for rel in KEY_FILES:
        p = ROOT / rel
        if p.is_file():
            key_hashes[rel.as_posix()] = {
                "sha256": sha256(p),
                "bytes": p.stat().st_size,
                "mtime": dt.datetime.fromtimestamp(p.stat().st_mtime, tz=now.tzinfo).isoformat(),
            }

    ready = not failures

    manifest = {
        "milestone": MILESTONE,
        "version": VERSION,
        "created": now.isoformat(),
        "root": str(ROOT),
        "ready_for_finalizer": ready,
        "failures": failures,
        "warnings": warnings,
        "info": infos,
        "checks": checks,
        "required_markers": marker_results,
        "diagnostic_tokens_present": diag_presence,
        "key_source": key_hashes,
        "artifacts": artifacts,
        "tools": tools,
        "git": git,
        "license_files": license_files,
        "tree_scan": tree_scan,
    }

    json_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    lines = [
        "=" * 96,
        f"AVP3DS v{VERSION} FINAL SHIP / GIT OBJECTIVE AUDIT",
        "=" * 96,
        f"Milestone: {MILESTONE}",
        f"Created:   {now.isoformat()}",
        f"Root:      {ROOT}",
        "Mode:      READ-ONLY",
        "",
        "RESULT",
        "------",
        "READY FOR FINALIZER" if ready else "NOT READY — FAILURES PRESENT",
        "",
        "CHECKS",
        "------",
    ]
    for c in checks:
        detail = f" — {c['detail']}" if c["detail"] else ""
        lines.append(f"[{c['status']}] {c['name']}{detail}")

    lines += ["", "KEY SOURCE SHA-256", "-----------------"]
    for rel, meta in sorted(key_hashes.items()):
        lines.append(f"{meta['sha256']}  {rel}")

    lines += ["", "CURRENT BUILD ARTIFACTS", "-----------------------"]
    if artifacts:
        for name, meta in sorted(artifacts.items()):
            lines.append(f"{meta['sha256']}  {name}  ({meta['bytes']} bytes)")
    else:
        lines.append("(none)")

    lines += [
        "",
        "PUBLIC-TREE SAFETY CENSUS",
        "-------------------------",
        f"Files seen: {tree_scan['total_files_seen']}",
        "Retail-data roots present: " + (", ".join(tree_scan["blocked_retail_dirs_present"]) or "none"),
        f"Development artifacts seen: {len(tree_scan['development_artifacts'])}",
        f"Suspicious retail/media files: {len(suspicious)}",
        f">=50 MiB files: {len(tree_scan['files_ge_50MiB'])}",
        f">=100 MiB files: {len(tree_scan['files_ge_100MiB'])}",
    ]

    if suspicious:
        lines += ["", "SUSPICIOUS RETAIL/MEDIA FILES"]
        for item in suspicious[:200]:
            lines.append(f"{item['bytes']:12d}  {item['path']}  [{item['reason']}]")

    lines += [
        "",
        "RELEASE CLEANUP POLICY",
        "----------------------",
        "The hardware-validation tree intentionally still contains YEET28/HEADROOM2",
        "profiling support. The final ship builder deactivates those callsites only",
        "inside its clean frozen source/build snapshot. The proven development tree",
        "is not mutated.",
        "",
        "Git/GitHub policy:",
        "  * stage a frozen clean public tree",
        "  * stage release binaries separately",
        "  * never copy retail/commercial game data",
        "  * never commit/push/tag/publish automatically",
        "  * preserve an existing .git directory only when --github-clone is supplied",
        "",
        f"Machine manifest: {json_path.name}",
    ]

    txt_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print("=" * 80)
    print(f"AVP3DS v{VERSION} FINAL AUDIT")
    print("=" * 80)
    print("READ-ONLY: no source/build files changed.")
    print(f"Result: {'READY FOR FINALIZER' if ready else 'FAIL'}")
    print(f"Failures: {len(failures)}  Warnings: {len(warnings)}")
    print(f"Report:   {txt_path.name}")
    print(f"Manifest: {json_path.name}")
    return 0 if ready else 2


if __name__ == "__main__":
    sys.exit(main())
