#!/usr/bin/env bash
# Pure mock test, runnable on Linux and macOS without Xcode or sudo.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$root/rust/tools/tier2-validate/select-macos-sdk.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin"
cat > "$tmp/bin/xcrun" <<'SH'
#!/usr/bin/env bash
[[ "$*" == '--sdk macosx --show-sdk-path' ]] || exit 2
printf '%s\n' "$MOCK_SDK_PATH"
SH
chmod +x "$tmp/bin/xcrun"
export PATH="$tmp/bin:$PATH"
xcode="$tmp/Xcode_26.5.app/Contents/Developer"
valid="$xcode/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
clt="$tmp/CommandLineTools/SDKs/MacOSX.sdk"
mkdir -p "$valid/usr/lib" "$clt/usr/lib"
: > "$valid/usr/lib/libSystem.B.tbd"
: > "$clt/usr/lib/libSystem.B.tbd"
export DEVELOPER_DIR="$xcode" MOCK_SDK_PATH="$valid"
select_tier2_macos_sdk
[[ "$SDKROOT" == "$valid" ]]
[[ "$DEVELOPER_DIR" == "$xcode" ]]

# Fail closed instead of silently linking against Command Line Tools.
export MOCK_SDK_PATH="$clt"
if (select_tier2_macos_sdk >/dev/null 2>&1); then
  echo "ERROR: accepted mismatched Command Line Tools SDK" >&2
  exit 1
fi

# Fail before expensive builds when full Xcode is missing.
export DEVELOPER_DIR="$tmp/CommandLineTools"
if (select_tier2_macos_sdk >/dev/null 2>&1); then
  echo "ERROR: accepted Command Line Tools as Xcode" >&2
  exit 1
fi

# Reject an absent SDK even when its claimed path sits inside Xcode.
export DEVELOPER_DIR="$xcode" MOCK_SDK_PATH="$xcode/Platforms/MacOSX.platform/Developer/SDKs/Missing.sdk"
if (select_tier2_macos_sdk >/dev/null 2>&1); then
  echo "ERROR: accepted missing SDK" >&2
  exit 1
fi

echo 'PASS: Xcode SDK selection, CLT fallback refusal, and missing-path guards'
