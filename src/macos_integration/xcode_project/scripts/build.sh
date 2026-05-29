#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./scripts/dev_build.sh dev
  ./scripts/dev_build.sh release
EOF
}

if [ "$#" -ne 1 ]; then
  usage >&2
  exit 1
fi

profile="$1"
case "$profile" in
  dev)
    cmake_profile="dev"
    xcode_configuration="Debug"
    ;;
  release)
    cmake_profile="release"
    xcode_configuration="Release"
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
repo_root="$(cd "$project_dir/../../.." && pwd)"
cmake_root="$repo_root/build/$cmake_profile"

required_paths=(
  "$cmake_root/bin/7zFM"
  "$cmake_root/bin/7zG"
  "$cmake_root/lib/libz7_shared_runtime.dylib"
  "$cmake_root/lib/libz7_third_party.dylib"
)

for required_path in "${required_paths[@]}"; do
  if [ ! -e "$required_path" ]; then
    printf 'Missing required CMake build output: %s\n' "$required_path" >&2
    printf 'Build the matching CMake profile first; dev_build.sh will not invoke CMake for you.\n' >&2
    exit 1
  fi
done

xcodegen generate \
  --spec "$project_dir/project.yml" \
  --project "$project_dir" \
  --project-root "$project_dir"

xcodebuild \
  -project "$project_dir/Z7MacOSIntegration.xcodeproj" \
  -scheme MacOSIntegrationExtensions \
  -configuration "$xcode_configuration" \
  -destination "generic/platform=macOS" \
  ARCHS=arm64 \
  ONLY_ACTIVE_ARCH=YES \
  EXCLUDED_ARCHS=x86_64 \
  build
