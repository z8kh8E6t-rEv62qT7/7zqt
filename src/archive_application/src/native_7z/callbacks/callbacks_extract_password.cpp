// src/archive_application/src/native_7z/callbacks/callbacks_extract_password.cpp
// Role: Extract callback password request handling.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

STDMETHODIMP NativeExtractCallback::CryptoGetTextPassword(BSTR* password) throw() {
  if (password == nullptr) {
    return E_INVALIDARG;
  }
  *password = nullptr;

  std::string password_value;
  bool wrong_password = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    password_value = password_;
    wrong_password = wrong_password_;
  }

  if (password_value.empty() || wrong_password) {
    if (hooks_.ask_password) {
      PasswordPrompt prompt;
      prompt.archive_path = archive_path_;
      prompt.reason_kind = wrong_password
                               ? PasswordPromptReason::kWrongPassword
                               : PasswordPromptReason::kPasswordRequired;
      prompt.reason = wrong_password ? "wrong_password" : "password_required";
      const PasswordReply reply = hooks_.ask_password(prompt);
      if (reply.kind == PasswordReplyKind::kProvide && !reply.password.empty()) {
        password_value = reply.password;
        std::lock_guard<std::mutex> lock(mutex_);
        password_ = password_value;
        wrong_password_ = false;
      } else {
        std::lock_guard<std::mutex> lock(mutex_);
        password_requested_ = true;
        return E_ABORT;
      }
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      password_requested_ = true;
      wrong_password_ = true;
      return E_ABORT;
    }
  }

  if (!password_value.empty()) {
    const UString pass = utf8_to_ustring(password_value);
    const HRESULT pass_res = StringToBstr(pass, password);
    if (pass_res == S_OK && *password != nullptr) {
      return pass_res;
    }
    return pass_res;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    password_requested_ = true;
  }
  return E_ABORT;
}

}  // namespace z7::app
