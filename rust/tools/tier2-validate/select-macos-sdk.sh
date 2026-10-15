#!/usr/bin/env bash
# Sourced by validate.sh so the selected Xcode SDK reaches CMake, xcodebuild and clang.
# The helper is exercised with a mocked xcrun by test/review/test_tier2_xcode_sdk.sh.
select_tier2_macos_sdk() {
  local developer sdk
  developer="${DEVELOPER_DIR:-$(xcode-select -p)}" || return 1
  if [[ "$developer" != */Contents/Developer || ! -d "$developer" ]]; then
    echo "Tier 2: full Xcode required (DEVELOPER_DIR=$developer)" >&2
    return 1
  fi

  sdk="$(DEVELOPER_DIR="$developer" xcrun --sdk macosx --show-sdk-path)" || return 1
  if [[ "$sdk" != "$developer"/Platforms/MacOSX.platform/Developer/SDKs/* || \
        ! -f "$sdk/usr/lib/libSystem.B.tbd" ]]; then
    echo "Tier 2: SDK does not belong to selected Xcode: $sdk (developer=$developer)" >&2
    return 1
  fi

  export DEVELOPER_DIR="$developer" SDKROOT="$sdk"
  echo "Tier 2: DEVELOPER_DIR=$DEVELOPER_DIR"
  echo "Tier 2: SDKROOT=$SDKROOT"
}
