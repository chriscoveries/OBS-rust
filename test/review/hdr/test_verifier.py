#!/usr/bin/env -S uv run --script
# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import struct
import tempfile
import unittest
from pathlib import Path
from verify_recording import TARGET, VUI, inspect_probe, inspect_vui, inspect_container


def box(kind, payload):
    return struct.pack(">I4s", len(payload) + 8, kind) + payload


def movie(trc=18, ambient=True):
    entry = box(b"hvcC", b"\x01" * 24) + box(b"colr", b"nclx" + struct.pack(">HHHB", 9, trc, 9, 0))
    if ambient:
        entry += box(b"amve", struct.pack(">IHH", 3140000, 15635, 16450))
    return box(b"moov", box(b"trak", box(b"mdia", box(b"minf", box(b"stbl", box(b"stsd", b"\0" * 4 + struct.pack(">I", 1) + box(b"hvc1", b"\0" * 78 + entry)))))))


class VerifyTests(unittest.TestCase):
    def setUp(self):
        self.probe = {"streams": [{**TARGET, "codec_type": "video", "width": 1080, "height": 1920, "avg_frame_rate": "60000/1001"}]}
    def test_valid_probe(self):
        self.assertEqual(inspect_probe(self.probe), [])
    def test_wrong_tags_and_depth(self):
        for key in TARGET:
            with self.subTest(key=key):
                probe = copy.deepcopy(self.probe)
                probe["streams"][0][key] = "wrong"
                self.assertTrue(inspect_probe(probe))
    def test_invalid_geometry_and_fps(self):
        for key, value in [("width", 1079), ("height", 0), ("avg_frame_rate", "120/1"), ("avg_frame_rate", "0/0")]:
            with self.subTest(key=key):
                probe = copy.deepcopy(self.probe)
                probe["streams"][0][key] = value
                self.assertTrue(inspect_probe(probe))
    def test_audio(self):
        self.probe["streams"].append({"codec_type": "audio", "codec_name": "aac", "channels": 2, "sample_rate": "48000"})
        self.assertEqual(inspect_probe(self.probe), [])
        self.probe["streams"][-1]["channels"] = 6
        self.assertTrue(inspect_probe(self.probe))
    def test_vui_disagrees_with_container(self):
        valid = "\n".join(f"{key} 0010 = {value}" for key, value in VUI.items())
        self.assertEqual(inspect_vui(valid), [])
        for key in VUI:
            with self.subTest(key=key):
                self.assertTrue(inspect_vui(valid.replace(f"{key} 0010 = {VUI[key]}", f"{key} 0010 = 255")))
        self.assertTrue(inspect_vui(""))
    def test_mp4_boxes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "fixture.mp4"
            path.write_bytes(movie())
            self.assertEqual(inspect_container(path), [])
            path.write_bytes(movie(trc=1))
            self.assertTrue(inspect_container(path))
            path.write_bytes(movie(ambient=False))
            self.assertTrue(inspect_container(path))
            self.assertEqual(inspect_container(path, False), [])
            path.write_bytes(box(b"mdat", movie()))
            self.assertTrue(inspect_container(path))  # no blind signature scan
    def test_truncated_box(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.mp4"
            path.write_bytes(movie()[:-1])
            with self.assertRaises(ValueError):
                inspect_container(path)


if __name__ == "__main__":
    unittest.main()
