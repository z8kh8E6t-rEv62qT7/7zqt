#include "password_relay_delegate.h"

#include <QByteArray>

namespace z7::macos_integration::capi_internal {
namespace {

class QuickLookPasswordRelayDelegate final : public z7::app::IArchiveDelegate {
 public:
  QuickLookPasswordRelayDelegate(
      std::weak_ptr<z7_mi_session_state> state,
      QString archive_path_abs,
      QString effective_archive_type,
      QStringList nested_chain,
      std::shared_ptr<AsyncTaskState> task_state,
      std::shared_ptr<z7::app::IArchiveDelegate> forward)
      : state_(std::move(state)),
        archive_path_abs_(std::move(archive_path_abs)),
        effective_archive_type_(std::move(effective_archive_type)),
        nested_chain_(std::move(nested_chain)),
        task_state_(std::move(task_state)),
        forward_(std::move(forward)) {}

  std::optional<z7::app::OverwriteDecision> request_overwrite(
      const z7::app::OverwritePrompt& prompt) override {
    if (forward_) {
      return forward_->request_overwrite(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    const auto state = state_.lock();
    if (!state) {
      return std::nullopt;
    }

    const QString cache_key =
        nested_session_cache_key(archive_path_abs_,
                                 effective_archive_type_,
                                 nested_chain_);
    const bool wrong_password =
        retry_count_ > 0 ||
        prompt.reason_kind == z7::app::PasswordPromptReason::kWrongPassword;
    if (wrong_password) {
      std::lock_guard<std::mutex> lock(state->mutex);
      auto it = state->password_cache.find(cache_key);
      if (it != state->password_cache.end()) {
        std::string& cached = it.value();
        std::fill(cached.begin(), cached.end(), '\0');
        state->password_cache.erase(it);
      }
    } else {
      std::lock_guard<std::mutex> lock(state->mutex);
      const auto it = state->password_cache.constFind(cache_key);
      if (it != state->password_cache.cend() && !it.value().empty()) {
        z7::app::PasswordReply reply;
        reply.kind = z7::app::PasswordReplyKind::kProvide;
        reply.password = it.value();
        return reply;
      }
    }

    z7_mi_password_prompt_callback_t callback = nullptr;
    void* user_data = nullptr;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      callback = state->password_prompt_callback;
      user_data = state->password_prompt_user_data;
    }
    if (callback == nullptr) {
      if (task_state_) {
        task_state_->password_prompt_missing_callback.store(true);
      }
      return std::nullopt;
    }

    const QString reason_key =
        wrong_password ? QStringLiteral("wrong_password")
        : nested_chain_.isEmpty() ? QStringLiteral("password_required")
                                  : QStringLiteral("nested_password_required");
    if (wrong_password && retry_count_ >= 3) {
      if (task_state_) {
        task_state_->password_prompt_canceled.store(true);
      }
      z7::app::PasswordReply reply;
      reply.kind = z7::app::PasswordReplyKind::kCancel;
      return reply;
    }
    ++retry_count_;

    std::shared_ptr<PromptSlot> slot;
    std::shared_ptr<PromptDispatchEntry> dispatch;
    bool created = false;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      const auto it = state->pending_prompts.find(cache_key);
      if (it != state->pending_prompts.end() && it.value()) {
        std::lock_guard<std::mutex> slot_lock(it.value()->mutex);
        if (!it.value()->done && it.value()->reason_key == reason_key.toStdString()) {
          slot = it.value();
        }
      }
      if (!slot) {
        slot = std::make_shared<PromptSlot>();
        slot->reason_key = reason_key.toStdString();
        dispatch = std::make_shared<PromptDispatchEntry>();
        dispatch->slot = slot;
        dispatch->archive_path_abs = archive_path_abs_;
        dispatch->nested_chain = nested_chain_;
        dispatch->reason_key = reason_key;
        created = true;
        const auto existing = state->pending_prompts.find(cache_key);
        if (existing != state->pending_prompts.end() && existing.value()) {
          state->orphan_prompt_slots.push_back(slot);
        } else {
          state->pending_prompts.insert(cache_key, slot);
        }
      }
      slot->waiter_count.fetch_add(1);
    }
    bool waiter_registered = true;
    auto release_waiter = [&]() {
      if (!waiter_registered || !slot) {
        return;
      }
      waiter_registered = false;
      if (slot->waiter_count.fetch_sub(1) != 1) {
        return;
      }
      std::lock_guard<std::mutex> lock(state->mutex);
      const auto it = state->pending_prompts.find(cache_key);
      if (it != state->pending_prompts.end() && it.value() == slot) {
        state->pending_prompts.erase(it);
      }
    };

    if (created) {
      queue_password_prompt_dispatch(state, dispatch);
      std::unique_lock<std::mutex> dispatch_lock(dispatch->mutex);
      dispatch->cv.wait(dispatch_lock, [&dispatch]() {
        return dispatch->dispatched || dispatch->canceled;
      });
      const bool dispatch_canceled = dispatch->canceled;
      dispatch_lock.unlock();
      if (dispatch_canceled) {
        if (task_state_) {
          task_state_->password_prompt_canceled.store(true);
        }
        release_waiter();
        z7::app::PasswordReply reply;
        reply.kind = z7::app::PasswordReplyKind::kCancel;
        return reply;
      }

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        callback = state->password_prompt_callback;
        user_data = state->password_prompt_user_data;
      }
      if (callback == nullptr) {
        if (task_state_) {
          task_state_->password_prompt_missing_callback.store(true);
          task_state_->password_prompt_canceled.store(true);
        }
        finish_prompt_slot(slot, true);
        advance_password_prompt_dispatch(state, dispatch);
        release_waiter();
        z7::app::PasswordReply reply;
        reply.kind = z7::app::PasswordReplyKind::kCancel;
        return reply;
      }

      auto* handle = new z7_mi_password_prompt();
      handle->slot = slot;
      handle->dispatch = dispatch;
      handle->state = state;

      const QByteArray archive_path_utf8 = archive_path_abs_.toUtf8();
      const QByteArray reason_key_utf8 = reason_key.toUtf8();
      std::vector<QByteArray> chain_storage;
      std::vector<const char*> chain_ptrs;
      chain_storage.reserve(static_cast<size_t>(nested_chain_.size()));
      chain_ptrs.reserve(static_cast<size_t>(nested_chain_.size()));
      for (const QString& entry : nested_chain_) {
        chain_storage.push_back(entry.toUtf8());
      }
      for (const QByteArray& value : chain_storage) {
        chain_ptrs.push_back(value.constData());
      }

      callback(handle,
               archive_path_utf8.constData(),
               chain_ptrs.empty() ? nullptr : chain_ptrs.data(),
               chain_ptrs.size(),
               reason_key_utf8.constData(),
               user_data);
    }

    std::unique_lock<std::mutex> slot_lock(slot->mutex);
    slot->cv.wait(slot_lock, [&slot]() { return slot->done; });
    const bool canceled = slot->canceled;
    std::string password = slot->password;
    slot_lock.unlock();

    release_waiter();

    if (canceled) {
      if (task_state_) {
        task_state_->password_prompt_canceled.store(true);
      }
      z7::app::PasswordReply reply;
      reply.kind = z7::app::PasswordReplyKind::kCancel;
      return reply;
    }

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->password_cache.insert(cache_key, password);
    }

    z7::app::PasswordReply reply;
    reply.kind = z7::app::PasswordReplyKind::kProvide;
    reply.password = std::move(password);
    return reply;
  }

  std::optional<z7::app::ChoiceReply> request_choice(
      const z7::app::ChoicePrompt& prompt) override {
    if (forward_) {
      return forward_->request_choice(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    if (forward_) {
      return forward_->request_memory_limit(prompt);
    }
    return std::nullopt;
  }

  void on_lifecycle(z7::app::OperationStage stage,
                    std::string_view message) override {
    if (forward_) {
      forward_->on_lifecycle(stage, message);
    }
  }

  void on_log(const z7::app::ArchiveLog& log) override {
    if (forward_) {
      forward_->on_log(log);
    }
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    if (forward_) {
      forward_->on_progress(progress);
    }
  }

  bool on_list_entries_batch(std::vector<z7::app::ArchiveListEntry>&& batch) override {
    if (forward_) {
      return forward_->on_list_entries_batch(std::move(batch));
    }
    return true;
  }

  void on_finished(const z7::app::OperationOutcome& outcome) override {
    if (forward_) {
      forward_->on_finished(outcome);
    }
  }

 private:
  std::weak_ptr<z7_mi_session_state> state_;
  QString archive_path_abs_;
  QString effective_archive_type_;
  QStringList nested_chain_;
  std::shared_ptr<AsyncTaskState> task_state_;
  std::shared_ptr<z7::app::IArchiveDelegate> forward_;
  int retry_count_ = 0;
};

}  // namespace

std::shared_ptr<z7::app::IArchiveDelegate> make_quicklook_password_relay_delegate(
    const std::shared_ptr<z7_mi_session_state>& state,
    const QString& archive_path_abs,
    const QString& effective_archive_type,
    const QStringList& nested_chain,
    const std::shared_ptr<AsyncTaskState>& task_state,
    std::shared_ptr<z7::app::IArchiveDelegate> forward) {
  return std::make_shared<QuickLookPasswordRelayDelegate>(
      state,
      archive_path_abs,
      effective_archive_type,
      nested_chain,
      task_state,
      std::move(forward));
}

}  // namespace z7::macos_integration::capi_internal
