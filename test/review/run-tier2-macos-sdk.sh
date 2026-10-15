#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash test/review/test_tier2_xcode_sdk.sh
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'Native Xcode/Tier 2 test requires macOS; mock test passed.'
  exit 0
fi
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode_26.5.app/Contents/Developer}"
command -v brew >/dev/null
brew list cmocka >/dev/null || brew install cmocka
bash rust/tools/tier2-validate/validate.sh
