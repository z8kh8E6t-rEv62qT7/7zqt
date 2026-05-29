#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
build_root="$repo_root/build/dev"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/z7-quicklook-objc-tests.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT

required_paths=(
  "$build_root/lib/libz7_shared_runtime.dylib"
  "$repo_root/src/macos_integration/include/macos_integration_c_api.h"
  "/opt/homebrew/bin/7zz"
)

for required_path in "${required_paths[@]}"; do
  if [ ! -e "$required_path" ]; then
    printf 'Missing required dependency: %s\n' "$required_path" >&2
    exit 1
  fi
done

/usr/bin/xcrun clang++ \
  -x objective-c++ \
  -fobjc-arc \
  -std=gnu++20 \
  -I"$repo_root/src/macos_integration/include" \
  "$repo_root/src/macos_integration/xcode_project/tests/objc/quicklook_c_api_tests.m" \
  -L"$build_root/lib" \
  -F/opt/homebrew/lib \
  -Wl,-rpath,"$build_root/lib" \
  -Wl,-rpath,/opt/homebrew/lib \
  -lz7_shared_runtime \
  -framework QtCore \
  -framework Foundation \
  -framework CoreFoundation \
  -o "$tmpdir/quicklook_c_api_tests"

"$tmpdir/quicklook_c_api_tests" "$@"
