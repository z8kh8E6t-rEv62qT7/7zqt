// src/archive_application/src/native_7z/backend/native_request_validation.h
// Role: Private ArchiveRequest validation overload declarations.

#pragma once

#include <optional>

#include "core/internal.h"

namespace z7::app {

    template <typename TRequest>
    std::optional<OperationResult> validate_request(TRequest const&) {
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(AddRequest const& request);
    std::optional<OperationResult> validate_request(ExtractRequest const& request);
    std::optional<OperationResult> validate_request(TestRequest const& request);
    std::optional<OperationResult> validate_request(BenchmarkRequest const& request);
    std::optional<OperationResult> validate_request(SplitRequest const& request);
    std::optional<OperationResult> validate_request(CombineRequest const& request);
    std::optional<OperationResult> validate_request(HashRequest const& request);
    std::optional<OperationResult> validate_request(DeleteRequest const& request);
    std::optional<OperationResult> validate_request(OpenArchiveRequest const& request);
    std::optional<OperationResult> validate_request(OpenArchiveFromPathRequest const& request);
    std::optional<OperationResult> validate_request(OpenArchiveFromParentRequest const& request);
    std::optional<OperationResult> validate_request(SetArchiveSessionFilenameCodePageRequest const& request);
    std::optional<OperationResult> validate_request(CloseArchiveSessionRequest const& request);
    std::optional<OperationResult> validate_request(ListRequest const& request);
    std::optional<OperationResult> validate_request(ArchivePropertiesRequest const& request);
    std::optional<OperationResult> validate_request(GetEntryInfoRequest const& request);
    std::optional<OperationResult> validate_request(ReadArchiveEntryRequest const& request);
    std::optional<OperationResult> validate_request(NavigateRequest const& request);
    std::optional<OperationResult> validate_request(CopyRequest const& request);
    std::optional<OperationResult> validate_request(MoveRequest const& request);
    std::optional<OperationResult> validate_request(RenameRequest const& request);
    std::optional<OperationResult> validate_request(CreateRequest const& request);
    std::optional<OperationResult> validate_request(ArchiveCommentRequest const& request);
    std::optional<OperationResult> validate_request(FilesystemCommentRequest const& request);

} // namespace z7::app
