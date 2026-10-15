#!/usr/bin/env -S uv run --script
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect an actual recording, including HEVC VUI and MP4 sample-entry tags.
Passing is a file-format gate, not proof of iPhone or Meta upload playback.
"""
from __future__ import annotations
import argparse
import json
import re
import struct
import subprocess
from fractions import Fraction
from pathlib import Path

TARGET = {"codec_name": "hevc", "profile": "Main 10", "codec_tag_string": "hvc1", "pix_fmt": "yuv420p10le",
          "color_primaries": "bt2020", "color_transfer": "arib-std-b67", "color_space": "bt2020nc", "color_range": "tv"}
VUI = {"general_profile_idc": 2, "chroma_format_idc": 1, "bit_depth_luma_minus8": 2,
       "bit_depth_chroma_minus8": 2, "colour_primaries": 9, "transfer_characteristics": 18,
       "matrix_coefficients": 9, "video_full_range_flag": 0}


def inspect_probe(probe: dict) -> list[str]:
    errors: list[str] = []
    streams = probe.get("streams", [])
    videos = [s for s in streams if s.get("codec_type") == "video"]
    if len(videos) != 1:
        return ["Expected exactly one video stream"]
    video = videos[0]
    for key, value in TARGET.items():
        if video.get(key) != value:
            errors.append(f"{key}: expected {value!r}, got {video.get(key)!r}")
    for dim in ("width", "height"):
        n = video.get(dim, 0)
        if not isinstance(n, int) or n <= 0 or n % 2:
            errors.append(f"{dim}: requires a positive even value")
    try:
        fps = Fraction(video.get("avg_frame_rate", "0/1"))
        if not 0 < fps <= 60:
            errors.append("Frame rate must be positive and at most 60 fps")
    except (TypeError, ValueError, ZeroDivisionError):
        errors.append("Invalid frame-rate declaration")
    audio = [s for s in streams if s.get("codec_type") == "audio"]
    if len(audio) > 1:
        errors.append("Use a single mixed audio track for the upload copy")
    for stream in audio:
        if stream.get("codec_name") != "aac" or stream.get("channels") not in (1, 2):
            errors.append("Upload-copy audio must be mono/stereo AAC")
        if str(stream.get("sample_rate")) not in ("44100", "48000"):
            errors.append("Use 44.1 or 48 kHz AAC")
    return errors


def inspect_vui(log: str) -> list[str]:
    errors = []
    for name, value in VUI.items():
        found = re.findall(r"\b" + re.escape(name) + r"\s+[01]+\s*=\s*(\d+)", log)
        if not found or any(int(v) != value for v in found):
            errors.append(f"HEVC header {name}: expected {value}, found {found or 'no explicit value'}")
    return errors


def sample_entries(path: Path) -> list[dict]:
    """Bounded seek-only MP4 walk. Never scan mdat bytes for false box names."""
    entries: list[dict] = []
    seen = 0
    with path.open("rb") as file:
        length = path.stat().st_size
        def boxes(start: int, end: int, depth: int, entry: dict | None = None):
            nonlocal seen
            if depth > 16:
                raise ValueError("Excessive MP4 nesting")
            while start < end:
                seen += 1
                if seen > 100000 or end - start < 8:
                    raise ValueError("Malformed MP4 box list")
                file.seek(start)
                raw = file.read(8)
                if len(raw) != 8:
                    raise ValueError("Truncated MP4 box")
                size, kind = struct.unpack(">I4s", raw)
                header = 8
                if size == 1:
                    raw = file.read(8)
                    if len(raw) != 8:
                        raise ValueError("Truncated extended size")
                    size, = struct.unpack(">Q", raw)
                    header = 16
                elif size == 0:
                    size = end - start
                if size < header or size > end - start:
                    raise ValueError("MP4 box exceeds its parent")
                payload, stop = start + header, start + size
                if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl"):
                    boxes(payload, stop, depth + 1, entry)
                elif kind == b"stsd":
                    if stop - payload < 8:
                        raise ValueError("Truncated stsd")
                    boxes(payload + 8, stop, depth + 1)
                elif kind == b"hvc1":
                    if stop - payload < 78:
                        raise ValueError("Truncated visual sample entry")
                    child = {"colr": [], "amve": [], "hvcC": 0}
                    entries.append(child)
                    boxes(payload + 78, stop, depth + 1, child)
                elif entry is not None and kind == b"hvcC":
                    entry["hvcC"] += 1
                elif entry is not None and kind in (b"colr", b"amve"):
                    if stop - payload > 1024:
                        raise ValueError("Oversized colour/ambient box")
                    file.seek(payload)
                    entry[kind.decode()].append(file.read(stop - payload))
                start = stop
        boxes(0, length, 0)
    return entries


def inspect_container(path: Path, require_ambient: bool = True) -> list[str]:
    errors = []
    entries = sample_entries(path)
    if len(entries) != 1:
        return ["Expected one hvc1 visual sample entry"]
    entry = entries[0]
    if entry["hvcC"] != 1:
        errors.append("Expected one HEVC configuration box")
    if entry["colr"] != [b"nclx" + struct.pack(">HHHB", 9, 18, 9, 0)]:
        errors.append("MP4 nclx must explicitly match limited-range BT.2020 HLG (9/18/9)")
    ambient = struct.pack(">IHH", 3140000, 15635, 16450)
    if (require_ambient or entry["amve"]) and entry["amve"] != [ambient]:
        errors.append("Missing/different authored 314 lux D65 amve; use the HDR Hybrid MP4 recording path")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    parser.add_argument("--allow-missing-ambient", action="store_true", help="general HLG fixture validation only")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    path = args.recording.resolve(strict=True)
    probe_run = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)],
                               text=True, capture_output=True, check=True, timeout=60)
    probe = json.loads(probe_run.stdout)
    trace = subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-v", "info", "-i", str(path), "-map", "0:v:0", "-an",
                            "-c:v", "copy", "-frames:v", "1", "-bsf:v", "trace_headers", "-f", "null", "-"],
                           text=True, capture_output=True, check=True, timeout=60)
    errors = inspect_probe(probe) + inspect_vui(trace.stderr) + inspect_container(path, not args.allow_missing_ambient)
    # Decode real frames as well as reading headers. A valid first header does
    # not establish that a truncated/corrupt bitstream is decodable.
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-xerror", "-i", str(path), "-map", "0:v:0", "-an",
                    "-frames:v", "30", "-f", "null", "-"], check=True, timeout=120)
    report = {"file": str(path), "file_gate": "FAIL" if errors else "PASS", "errors": errors,
              "iphone_playback": "NOT TESTED", "instagram_upload": "NOT TESTED", "facebook_upload": "NOT TESTED",
              "note": "First video headers and 30 decoded frames checked; no whole-file quality or HDR display proof."}
    text = json.dumps(report, indent=2)
    print(text)
    if args.report:
        args.report.write_text(text + "\n")
    raise SystemExit(bool(errors))


if __name__ == "__main__":
    main()
