// src/archive_application/src/native_7z/operations/operations_add_input_items.h
// Role: Add request input_items staging helpers.

#pragma once

#include "core/internal.h"

namespace z7::app {

struct ScopedAddInputTree final {
  fs::path path;

  ScopedAddInputTree() = default;
  ScopedAddInputTree(const ScopedAddInputTree&) = delete;
  ScopedAddInputTree& operator=(const ScopedAddInputTree&) = delete;

  ~ScopedAddInputTree();
};

std::optional<OperationResult> prepare_add_request_for_execution(
    const AddRequest& request,
    ScopedAddInputTree* staging_tree,
    AddRequest* out_request);

}  // namespace z7::app
