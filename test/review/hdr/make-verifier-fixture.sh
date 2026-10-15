#!/usr/bin/env bash
# Format-checker fixture only. This does not exercise OBS or prove HDR picture quality.
set -euo pipefail
file="${1:?usage: make-verifier-fixture.sh /path/to/NEW-fixture.mp4}"
[[ ! -e "$file" ]] || { echo 'Refusing to replace an existing file' >&2; exit 2; }
ffmpeg -nostdin -hide_banner -loglevel error \
  -f lavfi -i 'testsrc2=size=128x72:rate=30' -f lavfi -i 'anullsrc=r=48000:cl=stereo' -t 1 \
  -vf 'format=yuv420p10le,setparams=range=limited:color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc' \
  -c:v libx265 -preset ultrafast -pix_fmt yuv420p10le \
  -x265-params 'log-level=error:pools=1:frame-threads=1:colorprim=9:transfer=18:colormatrix=9:range=limited' \
  -tag:v hvc1 -movflags +write_colr -c:a aac -b:a 128k "$file"
uv run --no-project python "$(dirname "$0")/verify_recording.py" "$file" --allow-missing-ambient
