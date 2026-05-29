#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
build_root="$repo_root/build/dev"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/z7-broker-service-tests.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT

required_paths=(
  "$build_root/lib/libz7_shared_runtime.dylib"
  "$build_root/tests/support/fake_launcher_binary/z7_fake_open_tracker"
  "$repo_root/src/macos_integration/include/macos_integration_c_api.h"
  "/opt/homebrew/bin/7zz"
)

for required_path in "${required_paths[@]}"; do
  if [ ! -e "$required_path" ]; then
    printf 'Missing required dependency: %s\n' "$required_path" >&2
    exit 1
  fi
done

cp "$build_root/tests/support/fake_launcher_binary/z7_fake_open_tracker" "$tmpdir/7zG"

export Z7_TEST_PORTABLE_SETTINGS_ROOT="$tmpdir/settings"

/usr/bin/xcrun clang++ \
  -x objective-c++ \
  -fobjc-arc \
  -std=gnu++20 \
  -DZ7_TESTING \
  -I"$repo_root/src/macos_integration/include" \
  -I"$repo_root/src/macos_integration/xcode_project/BrokerService" \
  -I"$repo_root/src/macos_integration/xcode_project/Shared" \
  -I/opt/homebrew/lib/QtCore.framework/Headers \
  "$repo_root/src/macos_integration/xcode_project/tests/objc/broker_service_tests.m" \
  "$repo_root/src/macos_integration/xcode_project/BrokerService/BrokerService.mm" \
  "$repo_root/src/macos_integration/xcode_project/BrokerService/BrokerServiceMenu.mm" \
  "$repo_root/src/macos_integration/xcode_project/BrokerService/BrokerServiceQuickLook.mm" \
  "$repo_root/src/macos_integration/xcode_project/BrokerService/BrokerServiceRecords.mm" \
  "$repo_root/src/macos_integration/xcode_project/BrokerService/BrokerServiceUtilities.mm" \
  "$repo_root/src/macos_integration/xcode_project/BrokerService/BrokerServiceXPCProtocol.mm" \
  "$repo_root/src/macos_integration/xcode_project/Shared/BrokerMenuDTO.mm" \
  "$repo_root/src/macos_integration/xcode_project/Shared/BrokerPasswordDTO.mm" \
  "$repo_root/src/macos_integration/xcode_project/Shared/BrokerQuickLookDTO.mm" \
  "$repo_root/src/macos_integration/xcode_project/Shared/BrokerXPCProtocol.mm" \
  -L"$build_root/lib" \
  -F/opt/homebrew/lib \
  -Wl,-rpath,"$build_root/lib" \
  -Wl,-rpath,/opt/homebrew/lib \
  -lz7_shared_runtime \
  -framework QtCore \
  -framework Foundation \
  -framework CoreFoundation \
  -o "$tmpdir/broker_service_tests"

"$tmpdir/broker_service_tests" "$@"
