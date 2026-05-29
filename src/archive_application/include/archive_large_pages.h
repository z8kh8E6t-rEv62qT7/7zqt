#pragma once

namespace z7::app {

bool large_pages_runtime_supported();
bool probe_large_pages_runtime();
void set_large_pages_runtime_enabled(bool enabled);
#ifdef Z7_TESTING
void set_large_pages_probe_override_for_test(int value);
#endif

}  // namespace z7::app
