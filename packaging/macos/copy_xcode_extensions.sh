#!/usr/bin/env bash

set -euo pipefail

log() {
  printf '%s\n' "$*"
}

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

require_file() {
  local path="$1"
  local label="$2"
  if [ ! -f "$path" ]; then
    fail "missing ${label}: ${path}"
  fi
}

require_dir() {
  local path="$1"
  local label="$2"
  if [ ! -d "$path" ]; then
    fail "missing ${label}: ${path}"
  fi
}

canonical_dir() {
  local path="$1"
  (cd "$path" && pwd -P)
}

relative_path() {
  local from="$1"
  local to="$2"
  local common="$from"
  local suffix
  local upward=""

  while [ "$common" != "/" ] && [[ "${to}/" != "${common}/"* ]]; do
    common="${common%/*}"
    upward="../${upward}"
  done

  if [ "$common" = "/" ]; then
    suffix="${to#/}"
  elif [ "$to" = "$common" ]; then
    suffix=""
  else
    suffix="${to#"$common"/}"
  fi

  printf '%s%s' "$upward" "$suffix"
}

load_path_lines() {
  local binary="$1"
  otool -L "$binary" 2>/dev/null | sed '1d'
}

rpath_lines() {
  local binary="$1"
  otool -l "$binary" 2>/dev/null |
    awk '
      /LC_RPATH/ { show = 1; next }
      show && /path / {
        sub(/^.*path /, "")
        sub(/ \(offset [0-9]+\)$/, "")
        print
        show = 0
      }
      show && /^Load command/ { show = 0 }
    '
}

is_macho() {
  local path="$1"
  otool -L "$path" >/dev/null 2>&1
}

load_name() {
  local load="$1"
  printf '%s\n' "${load##*/}"
}

trim_load_line() {
  local line="$1"
  line="${line#"${line%%[![:space:]]*}"}"
  line="${line%% (*}"
  printf '%s\n' "$line"
}

append_change() {
  local old_load="$1"
  local new_load="$2"

  if [ "$old_load" = "$new_load" ]; then
    return
  fi

  local existing
  for existing in "${change_old_loads[@]}"; do
    if [ "$existing" = "$old_load" ]; then
      return
    fi
  done

  change_old_loads+=("$old_load")
  change_args+=(-change "$old_load" "$new_load")
}

qt_framework_suffix() {
  local load="$1"
  if [[ "$load" =~ (Qt[^/]+\.framework/.+)$ ]]; then
    printf '%s\n' "${BASH_REMATCH[1]}"
    return 0
  fi
  return 1
}

runtime_target_name() {
  local name="$1"
  case "$name" in
    libc++.1.dylib)
      printf 'libc++.1.dylib\n'
      ;;
    libc++abi.dylib|libc++abi.1.dylib)
      printf 'libc++abi.1.dylib\n'
      ;;
    libunwind.dylib|libunwind.1.dylib)
      printf 'libunwind.1.dylib\n'
      ;;
    *)
      return 1
      ;;
  esac
}

resolve_loader_path() {
  local binary_dir="$1"
  local load="$2"
  local suffix

  case "$load" in
    @loader_path/*)
      suffix="${load#@loader_path/}"
      (cd "$binary_dir" && cd "$(dirname "$suffix")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$suffix")")
      ;;
    *)
      return 1
      ;;
  esac
}

resolve_rpath() {
  local binary_dir="$1"
  local rpath="$2"
  local suffix

  case "$rpath" in
    @loader_path/*)
      suffix="${rpath#@loader_path/}"
      (cd "$binary_dir" && cd "$suffix" && pwd -P)
      ;;
    @executable_path/*)
      suffix="${rpath#@executable_path/}"
      (cd "$binary_dir" && cd "$suffix" && pwd -P)
      ;;
    /*)
      (cd "$rpath" && pwd -P)
      ;;
    *)
      return 1
      ;;
  esac
}

has_frameworks_rpath() {
  local binary="$1"
  local real_binary
  local binary_dir
  local rpath
  local resolved

  real_binary="$(cd "$(dirname "$binary")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$binary")")"
  binary_dir="$(dirname "$real_binary")"
  while IFS= read -r rpath; do
    [ -n "$rpath" ] || continue
    if resolved="$(resolve_rpath "$binary_dir" "$rpath" 2>/dev/null)" &&
       [ "$resolved" = "$frameworks_dir" ]; then
      return 0
    fi
  done < <(rpath_lines "$binary")
  return 1
}

frameworks_load_prefix() {
  local binary="$1"
  local real_binary
  local binary_dir
  local frameworks_relative

  if has_frameworks_rpath "$binary"; then
    printf '@rpath\n'
    return
  fi

  real_binary="$(cd "$(dirname "$binary")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$binary")")"
  binary_dir="$(dirname "$real_binary")"
  frameworks_relative="$(relative_path "$binary_dir" "$frameworks_dir")"
  printf '@loader_path/%s\n' "$frameworks_relative"
}

rewrite_binary_rpaths() {
  local binary="$1"
  local rpath

  while IFS= read -r rpath; do
    [ -n "$rpath" ] || continue
    case "$rpath" in
      /opt/homebrew/*|/usr/local/*|*/build/release/lib|*/build/dev/lib)
        log "deleting rpath from ${binary}: ${rpath}"
        install_name_tool -delete_rpath "$rpath" "$binary"
        ;;
    esac
  done < <(rpath_lines "$binary")
}

rewrite_binary_loads() {
  local binary="$1"
  local frameworks_prefix
  local line
  local load
  local name
  local new_name
  local suffix

  frameworks_prefix="$(frameworks_load_prefix "$binary")"

  change_args=()
  change_old_loads=()

  while IFS= read -r line; do
    load="$(trim_load_line "$line")"
    [ -n "$load" ] || continue
    name="$(load_name "$load")"

    if new_name="$(runtime_target_name "$name")"; then
      append_change "$load" "${frameworks_prefix}/${new_name}"
      continue
    fi

    case "$name" in
      libz7_shared_runtime.dylib|libz7_third_party.dylib)
        append_change "$load" "${frameworks_prefix}/${name}"
        continue
        ;;
    esac

    if suffix="$(qt_framework_suffix "$load")"; then
      case "$load" in
        /opt/homebrew/*|/usr/local/*|@rpath/*)
          append_change "$load" "${frameworks_prefix}/${suffix}"
          ;;
      esac
    fi
  done < <(load_path_lines "$binary")

  if [ "${#change_args[@]}" -gt 0 ]; then
    log "rewriting loads: ${binary}"
    install_name_tool "${change_args[@]}" "$binary"
  fi

  rewrite_binary_rpaths "$binary"
}

collect_macho_files() {
  local root="$1"
  local path
  while IFS= read -r -d '' path; do
    if is_macho "$path"; then
      printf '%s\0' "$path"
    fi
  done < <(find "$root" -type f -print0)
}

validate_binary_loads() {
  local binary="$1"
  local real_binary
  local binary_dir
  local rpath
  local line
  local load
  local name
  local suffix
  local expected
  local resolved

  real_binary="$(cd "$(dirname "$binary")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$binary")")"
  binary_dir="$(dirname "$real_binary")"

  while IFS= read -r rpath; do
    [ -n "$rpath" ] || continue
    case "$rpath" in
      /opt/homebrew/*|/usr/local/*|*/build/release/lib|*/build/dev/lib)
        fail "non-deploy rpath remains in ${binary}: ${rpath}"
        ;;
    esac
  done < <(rpath_lines "$binary")

  while IFS= read -r line; do
    load="$(trim_load_line "$line")"
    [ -n "$load" ] || continue
    name="$(load_name "$load")"

    case "$load" in
      /usr/lib/libc++*|/usr/lib/libc++abi*|/usr/lib/libunwind*)
        fail "system LLVM C++ runtime load remains in ${binary}: ${load}"
        ;;
      /opt/homebrew/*|/usr/local/*)
        if qt_framework_suffix "$load" >/dev/null ||
           runtime_target_name "$name" >/dev/null 2>&1; then
          fail "absolute Homebrew load remains in ${binary}: ${load}"
        fi
        ;;
    esac

    if suffix="$(qt_framework_suffix "$load")"; then
      case "$load" in
        @loader_path/*)
          expected="${frameworks_dir}/${suffix}"
          resolved="$(resolve_loader_path "$binary_dir" "$load")"
          if [ "$resolved" != "$expected" ]; then
            fail "Qt load does not resolve into app Frameworks for ${binary}: ${load} -> ${resolved}, expected ${expected}"
          fi
          require_file "$resolved" "resolved Qt framework binary"
          ;;
        @rpath/*)
          expected="${frameworks_dir}/${suffix}"
          if ! has_frameworks_rpath "$binary"; then
            fail "Qt @rpath load has no app Frameworks rpath for ${binary}: ${load}"
          fi
          require_file "$expected" "resolved Qt framework binary"
          ;;
      esac
      continue
    fi

    case "$name" in
      libz7_shared_runtime.dylib|libz7_third_party.dylib|libc++.1.dylib|libc++abi.1.dylib|libunwind.1.dylib)
        case "$load" in
          @loader_path/*)
            expected="${frameworks_dir}/${name}"
            resolved="$(resolve_loader_path "$binary_dir" "$load")"
            if [ "$resolved" != "$expected" ]; then
              fail "runtime load does not resolve into app Frameworks for ${binary}: ${load} -> ${resolved}, expected ${expected}"
            fi
            require_file "$resolved" "resolved bundled runtime"
            ;;
          @rpath/*)
            expected="${frameworks_dir}/${name}"
            if ! has_frameworks_rpath "$binary"; then
              fail "runtime @rpath load has no app Frameworks rpath for ${binary}: ${load}"
            fi
            require_file "$expected" "resolved bundled runtime"
            ;;
          *)
            fail "runtime load is not @loader_path-relative in ${binary}: ${load}"
            ;;
        esac
        ;;
    esac
  done < <(load_path_lines "$binary")
}

codesign_bundle() {
  local path="$1"
  local label="$2"

  log "codesigning ${label}: ${path}"
  codesign --force --sign - "$path"
}

codesign_bundle_with_entitlements() {
  local path="$1"
  local entitlements="$2"
  local label="$3"

  require_file "$entitlements" "${label} entitlements"
  log "codesigning ${label}: ${path}"
  codesign --force --sign - --entitlements "$entitlements" "$path"
}

assert_sandbox_entitlement() {
  local path="$1"
  local label="$2"
  local entitlements

  entitlements="$(codesign -d --entitlements :- "$path" 2>/dev/null || true)"
  if ! printf '%s\n' "$entitlements" |
       awk '
         /<key>com\.apple\.security\.app-sandbox<\/key>/ {
           getline
           if ($0 ~ /<true\/>/) {
             found = 1
           }
         }
         END { exit found ? 0 : 1 }
       '; then
    fail "${label} is missing com.apple.security.app-sandbox entitlement: ${path}"
  fi
}

repo_root="$(canonical_dir "$(dirname "$0")/../..")"
xcode_project="${repo_root}/src/macos_integration/xcode_project/Z7MacOSIntegration.xcodeproj"
target_app="${repo_root}/build/release/deploy/7zFM.app"
target_contents="${target_app}/Contents"
target_plugins="${target_contents}/PlugIns"
frameworks_dir="${target_contents}/Frameworks"

finder_extension="MacOSIntegrationFinderSyncExtension.appex"
quicklook_extension="MacOSIntegrationQuickLookExtension.appex"
extensions=("$finder_extension" "$quicklook_extension")
finder_entitlements="${repo_root}/src/macos_integration/xcode_project/finder_sync/Resources/MacOSIntegrationFinderSyncExtension.entitlements"
quicklook_entitlements="${repo_root}/src/macos_integration/xcode_project/quick_look/Resources/MacOSIntegrationQuickLookExtension.entitlements"

require_dir "$xcode_project" "Xcode project"
require_dir "$target_app" "CMake deploy app"
require_dir "$frameworks_dir" "CMake deploy app Frameworks directory"
require_file "${frameworks_dir}/QtCore.framework/Versions/A/QtCore" "bundled QtCore"
require_file "${frameworks_dir}/libz7_shared_runtime.dylib" "bundled shared runtime"
require_file "${frameworks_dir}/libc++.1.dylib" "bundled libc++"
require_file "${frameworks_dir}/libc++abi.1.dylib" "bundled libc++abi"
require_file "${frameworks_dir}/libunwind.1.dylib" "bundled libunwind"

build_settings="$(
  xcodebuild \
    -project "$xcode_project" \
    -scheme MacOSIntegrationExtensions \
    -configuration Release \
    -destination "generic/platform=macOS" \
    -showBuildSettings
)"

built_products_dir="$(
  printf '%s\n' "$build_settings" |
    awk -F' = ' '/^[[:space:]]*BUILT_PRODUCTS_DIR = / { print $2; exit }'
)"
if [ -z "$built_products_dir" ]; then
  fail "could not read BUILT_PRODUCTS_DIR from xcodebuild -showBuildSettings"
fi

source_plugins="${built_products_dir}/MacOSIntegrationHostApp.app/Contents/PlugIns"
require_dir "$source_plugins" "Xcode Release MacOSIntegrationHostApp PlugIns directory"
for extension in "${extensions[@]}"; do
  require_dir "${source_plugins}/${extension}" "Xcode Release ${extension}"
done

mkdir -p "$target_plugins"
for extension in "${extensions[@]}"; do
  log "copying ${extension}"
  rm -rf "${target_plugins:?}/${extension}"
  ditto "${source_plugins}/${extension}" "${target_plugins}/${extension}"
done

declare -a macho_files=()
for extension in "${extensions[@]}"; do
  while IFS= read -r -d '' macho; do
    macho_files+=("$macho")
  done < <(collect_macho_files "${target_plugins}/${extension}")
done

if [ "${#macho_files[@]}" -eq 0 ]; then
  fail "no Mach-O files found in copied macOS integration extensions"
fi

declare -a change_args=()
declare -a change_old_loads=()
for macho in "${macho_files[@]}"; do
  rewrite_binary_loads "$macho"
done

finder_target="${target_plugins}/${finder_extension}"
quicklook_target="${target_plugins}/${quicklook_extension}"
finder_broker="${finder_target}/Contents/XPCServices/BrokerService.xpc"
quicklook_broker="${quicklook_target}/Contents/XPCServices/BrokerService.xpc"

require_dir "$finder_broker" "Finder Sync BrokerService"
require_dir "$quicklook_broker" "Quick Look BrokerService"

codesign_bundle "$finder_broker" "Finder Sync BrokerService"
codesign_bundle "$quicklook_broker" "Quick Look BrokerService"
codesign_bundle_with_entitlements "$finder_target" "$finder_entitlements" "Finder Sync extension"
codesign_bundle_with_entitlements "$quicklook_target" "$quicklook_entitlements" "Quick Look extension"
assert_sandbox_entitlement "$finder_target" "Finder Sync extension"
assert_sandbox_entitlement "$quicklook_target" "Quick Look extension"

log "codesigning final app: ${target_app}"
codesign --force --sign - "$target_app"
codesign --verify --deep --strict "$target_app"

for macho in "${macho_files[@]}"; do
  validate_binary_loads "$macho"
done

for broker in \
  "${target_plugins}/${finder_extension}/Contents/XPCServices/BrokerService.xpc/Contents/MacOS/BrokerService" \
  "${target_plugins}/${quicklook_extension}/Contents/XPCServices/BrokerService.xpc/Contents/MacOS/BrokerService"; do
  require_file "$broker" "BrokerService executable"
  log "verified BrokerService loads:"
  otool -L "$broker"
done

log "macOS integration extensions copied and linked into ${target_app}"
