// src/archive_application/src/runtime/archive_session_helpers.h
// Role: Internal helper APIs shared by archive_session split units.

#pragma once

#include <chrono>
#include <optional>

#include "archive_session.h"

namespace z7::app::archive_session_helpers {

using Clock = std::chrono::steady_clock;

inline constexpr std::chrono::milliseconds kDefaultProgressInterval(100);
inline constexpr std::chrono::milliseconds kBenchmarkProgressInterval(250);

bool is_benchmark_request(const ArchiveRequest& request);
bool is_wrong_password_prompt(const PasswordPrompt& prompt);
OperationOutcome make_outcome(const OperationResult& base, OperationPayload payload);

std::optional<OperationResult> maybe_block_benchmark_for_memory_limit(
    const ArchiveRequest& request,
    IArchiveDelegate* delegate);

}  // namespace z7::app::archive_session_helpers
