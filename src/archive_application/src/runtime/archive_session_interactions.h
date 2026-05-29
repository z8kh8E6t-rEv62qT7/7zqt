#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ports/archive_backend_port.h"

namespace z7::app {

class ArchiveSessionInteractionBroker final {
 public:
  explicit ArchiveSessionInteractionBroker(std::shared_ptr<IArchiveDelegate> delegate);

  ArchiveBackendHooks make_hooks(const ArchiveRequest& request,
                                  NativeEventCallback on_event);
  std::optional<OperationResult> missing_interaction_result() const;

 private:
  void mark_missing_interaction(std::string_view method_name);
  OverwriteDecision request_overwrite(const OverwritePrompt& prompt);
  PasswordReply request_password(const PasswordPrompt& prompt);
  ChoiceReply request_choice(const ChoicePrompt& prompt);
  MemoryLimitReply request_memory_limit(const MemoryLimitPrompt& prompt);
  bool forward_list_batch(std::vector<ArchiveListEntry>&& batch);

  std::shared_ptr<IArchiveDelegate> delegate_;
  mutable std::mutex password_cache_mutex_;
  std::optional<std::string> cached_password_;
  std::optional<OperationResult> missing_interaction_;
};

}  // namespace z7::app
