#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"

"$script_dir/run_swift_quicklook_selection_strategy_tests.sh"
"$script_dir/run_swift_quicklook_extract_selection_tests.sh"
"$script_dir/run_swift_finder_sync_menu_tests.sh"
"$script_dir/run_objc_broker_service_tests.sh"
"$script_dir/run_objc_quicklook_c_api_tests.sh"
