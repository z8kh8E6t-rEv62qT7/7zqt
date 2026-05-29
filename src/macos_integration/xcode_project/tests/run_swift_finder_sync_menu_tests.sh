#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/z7-finder-sync-menu-tests.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT

/usr/bin/xcrun swiftc \
  -module-cache-path "$tmpdir/module-cache" \
  -D Z7_TESTING \
  "$repo_root/src/macos_integration/xcode_project/finder_sync/Sources/FinderSync.swift" \
  "$repo_root/src/macos_integration/xcode_project/tests/swift/finder_sync_menu_tests.swift" \
  -framework AppKit \
  -framework FinderSync \
  -framework Foundation \
  -o "$tmpdir/finder_sync_menu_tests"

"$tmpdir/finder_sync_menu_tests"
