// src/archive_application/src/native_7z/operations/nested_writeback.h
// Role: Shared nested-archive writeback pipeline for update/delete requests.

#pragma once

#include "core/internal_results.h"

namespace z7::app {

    class NativeArchiveBackend;

    AddResult run_add_with_nested_writeback(NativeArchiveBackend& backend,
                                            AddRequest const& request,
                                            ArchiveBackendHooks const& hooks);

    DeleteResult run_delete_with_nested_writeback(NativeArchiveBackend& backend,
                                                  DeleteRequest const& request,
                                                  ArchiveBackendHooks const& hooks);

} // namespace z7::app
