// src/archive_application/src/native_7z/core/core_large_pages.cpp
// Role: Runtime bridge for platform large-page allocator controls.

#include "archive_large_pages.h"

#include <optional>

#if defined(__APPLE__) && defined(Z7_LARGE_PAGES)
#include "C/Alloc.h"
#include <mach/vm_statistics.h>
#endif

namespace z7::app {
namespace {

#if defined(__APPLE__) && defined(Z7_LARGE_PAGES) && defined(VM_FLAGS_SUPERPAGE_SIZE_ANY)
constexpr UInt32 kLargePagesEnabledFlags =
    Z7_LARGE_PAGES_FLAG_USE_HUGEPAGE |
    Z7_LARGE_PAGES_FLAG_DIRECT_PAGE_SIZE |
    Z7_LARGE_PAGES_FLAG_DIRECT_THRESHOLD;
constexpr size_t kMacLargePageSize = static_cast<size_t>(2) << 20;
constexpr size_t kMacLargePageThreshold = static_cast<size_t>(1) << 20;

bool g_large_pages_runtime_enabled = false;
#ifdef Z7_TESTING
std::optional<bool> g_large_pages_probe_override;
#endif

void apply_large_pages_runtime_mode(bool enabled, bool fail_stop) {
  UInt32 flags = 0;
  size_t page_size = 0;
  size_t threshold = 0;
  if (enabled) {
    flags = kLargePagesEnabledFlags;
    if (fail_stop) {
      flags |= Z7_LARGE_PAGES_FLAG_FAIL_STOP;
    }
    page_size = kMacLargePageSize;
    threshold = kMacLargePageThreshold;
  }
  z7_LargePage_Set(flags, page_size, threshold);
}
#endif

}  // namespace

bool large_pages_runtime_supported() {
#if defined(__APPLE__) && defined(Z7_LARGE_PAGES) && defined(VM_FLAGS_SUPERPAGE_SIZE_ANY)
  return true;
#else
  return false;
#endif
}

bool probe_large_pages_runtime() {
#if defined(__APPLE__) && defined(Z7_LARGE_PAGES) && defined(VM_FLAGS_SUPERPAGE_SIZE_ANY)
#ifdef Z7_TESTING
  if (g_large_pages_probe_override.has_value()) {
    return *g_large_pages_probe_override;
  }
#endif
  const bool was_enabled = g_large_pages_runtime_enabled;
  apply_large_pages_runtime_mode(true, true);
  void* allocation = BigAlloc(kMacLargePageSize);
  const bool ok = allocation != nullptr;
  if (allocation != nullptr) {
    BigFree(allocation);
  }
  apply_large_pages_runtime_mode(was_enabled, false);
  return ok;
#else
  return false;
#endif
}

void set_large_pages_runtime_enabled(bool enabled) {
#if defined(__APPLE__) && defined(Z7_LARGE_PAGES) && defined(VM_FLAGS_SUPERPAGE_SIZE_ANY)
  g_large_pages_runtime_enabled = enabled;
  apply_large_pages_runtime_mode(enabled, false);
#else
  (void)enabled;
#endif
}

#ifdef Z7_TESTING
void set_large_pages_probe_override_for_test(int value) {
#if defined(__APPLE__) && defined(Z7_LARGE_PAGES) && defined(VM_FLAGS_SUPERPAGE_SIZE_ANY)
  if (value < 0) {
    g_large_pages_probe_override.reset();
  } else {
    g_large_pages_probe_override = (value != 0);
  }
#else
  (void)value;
#endif
}
#endif

}  // namespace z7::app
