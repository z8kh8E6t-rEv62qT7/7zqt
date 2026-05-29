// drag_drop_native_macos_concurrency.cpp
// Role: PromiseWriteConcurrencyPolicy factory.
// All env-var, QWidget-property, and runtime override inputs removed (§3.1);
// limits and thresholds are now compiled-in constants only.

#include "internal.h"

namespace z7::macos_integration::native_drag::detail {

PromiseWriteConcurrencyPolicy PromiseWriteConcurrencyPolicy::from_defaults() {
  return PromiseWriteConcurrencyPolicy{};
}

}  // namespace z7::macos_integration::native_drag::detail
