// src/archive_application/src/native_7z/operations/operations_add_input_items.h
// Role: Add request input_items validation and direct mapping helpers.

#pragma once

#include "core/internal.h"

namespace z7::app {

std::optional<OperationResult> prepare_add_request_for_execution(
    const AddRequest& request,
    AddRequest* out_request);

}  // namespace z7::app
