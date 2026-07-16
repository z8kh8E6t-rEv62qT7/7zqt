// src/archive_application/src/native_7z/callbacks/callbacks_extract_password.cpp
// Role: Extract callback password request handling.

#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    STDMETHODIMP NativeExtractCallback::CryptoGetTextPassword(BSTR* password) throw() {
        if (password == nullptr) {
            return E_INVALIDARG;
        }
        *password = nullptr;

        std::string password_value;
        bool password_defined = false;
        bool password_retry_required = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            password_value = password_;
            password_defined = password_defined_;
            password_retry_required = password_retry_required_;
        }

        if (!password_defined || password_retry_required) {
            if (hooks_.ask_password) {
                PasswordPrompt prompt;
                prompt.archive_path = archive_path_;
                prompt.reason_kind = password_retry_required ? PasswordPromptReason::kWrongPassword
                                                             : PasswordPromptReason::kPasswordRequired;
                prompt.reason = password_retry_required ? "wrong_password" : "password_required";
                PasswordReply reply;
                try {
                    reply = hooks_.ask_password(prompt);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    password_requested_ = true;
                    return E_FAIL;
                }
                if (reply.kind == PasswordReplyKind::kProvide) {
                    password_value = reply.password;
                    password_defined = true;
                    std::lock_guard<std::mutex> lock(mutex_);
                    password_ = password_value;
                    password_defined_ = true;
                    password_retry_required_ = false;
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    password_requested_ = true;
                    return E_ABORT;
                }
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                password_requested_ = true;
                password_retry_required_ = true;
                wrong_password_ = true;
                return E_ABORT;
            }
        }

        if (password_defined) {
            UString const pass = utf8_to_ustring(password_value);
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

} // namespace z7::app
