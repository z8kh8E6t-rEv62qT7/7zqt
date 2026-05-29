#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/z7-quicklook-extract-selection-tests.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT

/usr/bin/xcrun swiftc \
  -module-cache-path "$tmpdir/module-cache" \
  -D Z7_TESTING \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookCollectionContainer.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookExportDestination.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookLocalButton.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookLocalClickPairing.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookLocalization.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewController.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewControllerCollection.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewControllerPassword.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewControllerRequests.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewRootView.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewTheme.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookPreviewViewModel.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookRowLayoutMetrics.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookSelectionStrategy.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookTypes.swift" \
  "$repo_root/src/macos_integration/xcode_project/quick_look/Sources/QuickLookVisibleWidthStability.swift" \
  "$repo_root/src/macos_integration/xcode_project/tests/swift/quicklook_extract_selection_tests.swift" \
  -framework AppKit \
  -framework QuickLookUI \
  -framework SwiftUI \
  -framework UniformTypeIdentifiers \
  -o "$tmpdir/quicklook_extract_selection_tests"

"$tmpdir/quicklook_extract_selection_tests"
