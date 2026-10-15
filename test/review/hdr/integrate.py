#!/usr/bin/env -S uv run --script
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage reviewed native edits in a disposable worktree of the pinned HDR base.

Large existing sources are kept byte-identical in this handoff commit; this
script verifies their Git blob IDs and emits/applies a regular unified diff.
It never fetches, commits, checks out, or pushes. Default: print the patch only.
"""
from __future__ import annotations
import argparse
import difflib
import hashlib
import re
import subprocess
from pathlib import Path

BASE = "72c48c4ecebacfb0e7da6b8a8dbc7cee97aef258"
PINS = {
    "libobs/obs-encoder.c": "f353f87891665fdde312c3ebf5aa95acbc9c3ce7",
    "libobs/obs-internal.h": "bff74f7941ea354fbc4d7c180c958173ef6383c0",
    "libobs/obs-video.c": "ad843195eedd56cb1c6a0438ab62963cc06898d2",
    "libobs/data/format_conversion.effect": "6d1088c75b2e3815013c752e86e69fa3804eed4e",
    "plugins/mac-videotoolbox/encoder.c": "32f39f1bca7cb321977f1862228243a7a811af8d",
    "plugins/mac-videotoolbox/data/locale/en-US.ini": "12a57d9a2135252300a6aa68366a64de2dcd4bc1",
    "plugins/obs-outputs/mp4-mux.c": "367eed4aa89045e50467405930b41ba52483b814",
}


def blob_id(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f"Expected one anchor, found {count}: {old[:100]!r}")
    return text.replace(old, new, 1)


def function(text: str, name: str, edit) -> str:
    # Existing C/effect function definitions have column-zero closing braces.
    pattern = rf"^[^\n;]*\b{re.escape(name)}\([^;]*?\n\{{\n.*?^\}}"
    matches = list(re.finditer(pattern, text, re.M | re.S))
    if len(matches) != 1:
        raise ValueError(f"Cannot uniquely locate function {name}: {len(matches)}")
    m = matches[0]
    return text[:m.start()] + edit(m.group()) + text[m.end():]


def transform(root: Path, path: str, text: str) -> str:
    if path == "libobs/obs-internal.h":
        text = once(text, '#include "obs-video-stats.h"', '#include "obs-video-stats.h"\n#include "hdr-recording-profile.h"')
        return once(text, 'struct obs_core_video_mix {\n\tstruct obs_view *view;',
                    'struct obs_core_video_mix {\n\tstruct obs_view *view;\n\tstruct hdr_recording_profile hdr_recording;')
    if path == "libobs/obs-encoder.c":
        text = '#include "hdr-recording-settings.h"\n' + text
        text = function(text, "maybe_set_up_gpu_rescale", lambda _: '#include "hdr-recording-mix.inc"')
        text = once(text, 'static THREAD_LOCAL bool can_reroute = false;',
                    'static void maybe_clear_encoder_core_video_mix(obs_encoder_t *encoder);\n\nstatic THREAD_LOCAL bool can_reroute = false;')
        def init(f):
            f = once(f, '\tmaybe_set_up_gpu_rescale(encoder);', '\tif (!maybe_set_up_gpu_rescale(encoder))\n\t\treturn false;')
            return once(f, '\tif (!encoder->context.data)\n\t\treturn false;',
                        '\tif (!encoder->context.data) {\n\t\tif (encoder->orig_info.type == OBS_ENCODER_VIDEO)\n\t\t\tmaybe_clear_encoder_core_video_mix(encoder);\n\t\treturn false;\n\t}')
        return function(text, "obs_encoder_initialize_internal", init)
    if path == "libobs/obs-video.c":
        text = once(text, '#include "obs-internal.h"', '#include "obs-internal.h"\n#include "hdr-recording-effect.h"')
        def convert(f):
            f = once(f, '\tgs_effect_t *effect = obs->video.conversion_effect;',
                        '\tgs_effect_t *effect = obs->video.conversion_effect;\n\thdr_recording_set_effect(effect, &video->hdr_recording);')
            f = once(f, 'const float hdr_nominal_peak_level = obs->video.hdr_nominal_peak_level;',
                        'const float hdr_nominal_peak_level = video->hdr_recording.enabled ? video->hdr_recording.peak_nits : obs->video.hdr_nominal_peak_level;')
            return once(f, 'const float multiplier = obs_get_video_sdr_white_level() / 10000.f;',
                        'const float multiplier = (video->hdr_recording.enabled ? video->hdr_recording.white_nits : obs_get_video_sdr_white_level()) / 10000.f;')
        return function(text, "render_convert_texture", convert)
    if path == "libobs/data/format_conversion.effect":
        # Inline in the already-packaged resource; no additional runtime asset.
        shader = (root / "libobs/data/hdr-recording.effect").read_text()
        text = once(text, '#include "color.effect"', '#include "color.effect"\n\n' + shader)
        for name in ("PS_P010_HLG_Y_709_2020", "PS_P010_HLG_UV_709_2020_WideWide"):
            text = function(text, name, lambda f: once(f, '\trgb = linear_to_hlg(rgb, hdr_lw);',
                                                        '\trgb = hdr_recording_grade(rgb);\n\trgb = linear_to_hlg(rgb, hdr_lw);'))
        return text
    if path == "plugins/mac-videotoolbox/encoder.c":
        text = once(text, '#include <obs-module.h>', '#include <obs-module.h>\n#include "hdr-recording-properties.h"')
        text = once(text, 'struct vt_encoder {\n\tobs_encoder_t *encoder;',
                    'struct vt_encoder {\n\tobs_encoder_t *encoder;\n\tstruct hdr_recording_profile hdr_recording;')
        text = once(text, 'static OSStatus session_set_colorspace(', '#include "hdr-recording-session.inc"\n\nstatic OSStatus session_set_colorspace(')
        def params(f):
            f = once(f, '\tconst char *codec = obs_encoder_get_codec(enc->encoder);', '''	struct hdr_recording_profile profile;
	if (!hdr_recording_read(settings, &profile)) {
		obs_encoder_set_last_error(enc->encoder, obs_module_text("HDRRecording.Invalid"));
		return false;
	}
	if (enc->session && !hdr_recording_profile_equal(&profile, &enc->hdr_recording)) {
		obs_encoder_set_last_error(enc->encoder, obs_module_text("HDRRecording.Restart"));
		return false;
	}
	const char *codec = obs_encoder_get_codec(enc->encoder);
	if (profile.enabled && (strcmp(codec, "hevc") != 0 || voi->format != VIDEO_FORMAT_P010 ||
				voi->colorspace != VIDEO_CS_2100_HLG || voi->range != VIDEO_RANGE_PARTIAL ||
				!voi->fps_num || !voi->fps_den || (uint64_t)voi->fps_num > 60ULL * voi->fps_den)) {
		obs_encoder_set_last_error(enc->encoder, obs_module_text("HDRRecording.Format"));
		return false;
	}
	enc->hdr_recording = profile;''')
            return once(f, 'enc->profile = obs_data_get_string(settings, "profile");',
                        'enc->profile = profile.enabled ? "main10" : obs_data_get_string(settings, "profile");')
        text = function(text, "update_params", params)
        text = function(text, "vt_update", lambda f: once(f, '\tupdate_params(enc, settings);', '\tif (!update_params(enc, settings))\n\t\treturn false;'))
        def create(f):
            f = once(f, 'log_osstatus(LOG_ERROR, enc, "VTCompressionSessionCreate", code);\n\t}',
                        'log_osstatus(LOG_ERROR, enc, "VTCompressionSessionCreate", code);\n\t\treturn code;\n\t}\n\t/* Own the session now so every subsequent failure is cleaned by vt_destroy. */\n\tenc->session = s;')
            return once(f, 'code = session_set_colorspace(s, enc->colorspace);',
                        'code = enc->hdr_recording.enabled ? vt_hdr_recording_set_colorspace(s) : session_set_colorspace(s, enc->colorspace);')
        text = function(text, "create_encoder", create)
        def cached(f):
            f = once(f, '\tif (!pool)\n\t\treturn kCVReturnError;', '\tif (!pool)\n\t\treturn false;')
            return once(f, '\tif (pq || hlg) {', '\tif ((pq || hlg) && !enc->hdr_recording.enabled) {')
        text = function(text, "get_cached_pixel_buffer", cached)
        text = function(text, "vt_properties_h26x", lambda f: once(f, '\treturn props;',
            '\tif (encoder_type_data->codec_type == kCMVideoCodecType_HEVC)\n\t\tvt_hdr_recording_properties(props);\n\treturn props;'))
        return function(text, "vt_defaults", lambda f: f[:-1] + '\thdr_recording_defaults(settings);\n}')
    if path.endswith("/locale/en-US.ini"):
        return text.rstrip() + '\n' + (root / "test/review/hdr/locale-additions.ini").read_text()
    if path == "plugins/obs-outputs/mp4-mux.c":
        text = '#include "../../libobs/hdr-recording-settings.h"\n' + text
        text = once(text, 'static size_t mp4_write_clli(', '#include "mp4-hdr-recording.inc"\n\nstatic size_t mp4_write_clli(')
        for name in ("mp4_write_clli", "mp4_write_mdcv"):
            text = function(text, name, lambda f: once(f, '\n{\n', '\n{\n\tif (mp4_hdr_recording_enabled(enc))\n\t\treturn 0;\n'))
        return function(text, "mp4_write_hvc1", lambda f: once(f, '\tmp4_write_colr(mux, enc);',
                        '\tmp4_write_colr(mux, enc);\n\tmp4_write_hdr_recording_ambient(mux, enc);'))
    raise ValueError(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="apply the verified patch in a clean detached worktree")
    parser.add_argument("--patch", type=Path, help="also save the regular unified diff")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    originals, updated = {}, {}
    for path, sha in PINS.items():
        data = (root / path).read_bytes()
        if blob_id(data) != sha:
            raise SystemExit(f"REFUSED: {path} differs from pinned base {BASE}; inspect/rebase the edits manually")
        originals[path] = data.decode("utf-8")
        updated[path] = transform(root, path, originals[path])
    patch = "".join("".join(difflib.unified_diff(originals[p].splitlines(True), updated[p].splitlines(True),
                                                fromfile="a/" + p, tofile="b/" + p)) for p in PINS)
    if args.patch:
        args.patch.write_text(patch)
    if not args.apply:
        print(patch, end="")
        return
    if subprocess.run(["git", "symbolic-ref", "-q", "HEAD"], cwd=root, capture_output=True).returncode == 0:
        raise SystemExit("REFUSED: use a detached disposable worktree; no existing branch will be edited")
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root).strip():
        raise SystemExit("REFUSED: tracked worktree changes already exist")
    subprocess.run(["git", "apply", "--check", "-"], cwd=root, input=patch.encode(), check=True)
    subprocess.run(["git", "apply", "-"], cwd=root, input=patch.encode(), check=True)
    print("Applied verified native integration edits. No commits, branch updates or pushes performed.")


if __name__ == "__main__":
    main()
