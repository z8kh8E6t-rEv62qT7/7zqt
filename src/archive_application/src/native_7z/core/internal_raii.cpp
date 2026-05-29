// src/archive_application/src/native_7z/core/internal_raii.cpp
// Role: Out-of-line RAII helper implementations for native backend internals.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

ScopedAtomicBoolReset::ScopedAtomicBoolReset(std::atomic<bool>& value)
    : value_(value) {}

ScopedAtomicBoolReset::~ScopedAtomicBoolReset() {
  value_.store(false);
}

ScopedConditionNotify::ScopedConditionNotify(std::condition_variable& cv)
    : cv_(cv) {}

ScopedConditionNotify::~ScopedConditionNotify() {
  cv_.notify_all();
}

}  // namespace z7::app
