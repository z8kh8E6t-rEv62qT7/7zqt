// src/archive_application/src/native_7z/backend/native_request_validation.h
// Role: Private ArchiveRequest validation overload declarations.

#pragma once

#include <optional>

#include "core/internal.h"

namespace z7::app {

template <typename TRequest>
std::optional<OperationResult> validate_request(const TRequest&) {
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const AddRequest& request);
std::optional<OperationResult> validate_request(const ExtractRequest& request);
std::optional<OperationResult> validate_request(const TestRequest& request);
std::optional<OperationResult> validate_request(const BenchmarkRequest& request);
std::optional<OperationResult> validate_request(const SplitRequest& request);
std::optional<OperationResult> validate_request(const CombineRequest& request);
std::optional<OperationResult> validate_request(const HashRequest& request);
std::optional<OperationResult> validate_request(const DeleteRequest& request);
std::optional<OperationResult> validate_request(const OpenArchiveRequest& request);
std::optional<OperationResult> validate_request(
    const OpenArchiveFromPathRequest& request);
std::optional<OperationResult> validate_request(
    const OpenArchiveFromParentRequest& request);
std::optional<OperationResult> validate_request(
    const CloseArchiveSessionRequest& request);
std::optional<OperationResult> validate_request(const ListRequest& request);
std::optional<OperationResult> validate_request(
    const ArchivePropertiesRequest& request);
std::optional<OperationResult> validate_request(const GetEntryInfoRequest& request);
std::optional<OperationResult> validate_request(const NavigateRequest& request);
std::optional<OperationResult> validate_request(const CopyRequest& request);
std::optional<OperationResult> validate_request(const MoveRequest& request);
std::optional<OperationResult> validate_request(const RenameRequest& request);
std::optional<OperationResult> validate_request(const CreateRequest& request);
std::optional<OperationResult> validate_request(
    const ArchiveCommentRequest& request);
std::optional<OperationResult> validate_request(
    const FilesystemCommentRequest& request);

}  // namespace z7::app
