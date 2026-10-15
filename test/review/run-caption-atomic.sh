#!/bin/bash
# Run the standalone caption crate through the project's pinned Rust wrapper.
# No OBS process, audio device, GPU or Git writes are required.
set -euo pipefail
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
cd "${CAPTION_REVIEW_SOURCE_DIR:-$script_root}"
command -v soldr >/dev/null || { echo "The project's soldr toolchain is required." >&2; exit 2; }
log_dir="$PWD/build_review_captions"
mkdir -p "$log_dir"
printf 'Commit: %s\n' "$(git rev-parse HEAD)"
git diff --check
soldr cargo test -p obs-captions --locked 2>&1 | tee "$log_dir/debug-tests.log"
soldr cargo test -p obs-captions --locked --release 2>&1 | tee "$log_dir/release-tests.log"
soldr cargo clippy -p obs-captions --all-targets --locked -- -D warnings \
  2>&1 | tee "$log_dir/clippy.log"
soldr cargo fmt -p obs-captions -- --check 2>&1 | tee "$log_dir/fmt.log"
printf 'Caption checks passed. Logs: %s\n' "$log_dir"
