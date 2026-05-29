// src/archive_application/src/native_7z/backend/native_backend_identity.cpp
// Role: Native backend identity and capability reporting.

#include "core/internal.h"

namespace z7::app {

const char* NativeArchiveBackend::backend_name() const { return "native_backend"; }

BackendCapabilities NativeArchiveBackend::capabilities() const {
  BackendCapabilities caps;
  caps.supports_split = true;
  caps.supports_combine = true;
  caps.supports_typed_benchmark = true;
  return caps;
}

}  // namespace z7::app
