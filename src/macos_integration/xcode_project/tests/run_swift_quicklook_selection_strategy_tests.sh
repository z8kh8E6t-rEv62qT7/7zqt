#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/z7-quicklook-selection-tests.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT

/usr/bin/xcrun swiftc \
  -module-cache-path "$tmpdir/module-cache" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookSelectionStrategy.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookLocalClickPairing.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookExportDestination.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookRowLayoutMetrics.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookVisibleWidthStability.swift" \
  "$repo_root/src/macos_integration/xcode_project/tests/swift/quicklook_selection_strategy_tests.swift" \
  -o "$tmpdir/quicklook_selection_strategy_tests"

"$tmpdir/quicklook_selection_strategy_tests"
