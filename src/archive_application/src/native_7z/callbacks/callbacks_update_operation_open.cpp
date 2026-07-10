// src/archive_application/src/native_7z/callbacks/callbacks_update_operation_open.cpp
// Role: Archive-open and scan-stage callback handling for update operations.

#include "core/internal.h"
#include "third_party_adapter/callbacks_update_operation.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    HRESULT NativeUpdateOperationCallback::OpenResult(CCodecs const*,
                                                      CArchiveLink const&,
                                                      wchar_t const* name,
                                                      HRESULT result) {
        if (result != S_OK) {
            std::string const path = update_wide_name_to_utf8(name);
            std::string message = "Open archive failed";
            if (!path.empty()) {
                message += ": " + path;
            }
            emit_log_event(hooks_, OperationStage::kRunning, OutputChannel::kStdErr, message);
        }
        return S_OK;
    }

    HRESULT NativeUpdateOperationCallback::StartScanning() {
        emit_log_event(hooks_, OperationStage::kRunning, OutputChannel::kNone, "Scanning");
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::FinishScanning(CDirItemsStat const& st) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            total_files_ = st.NumFiles + st.NumAltStreams;
            if (st.GetTotalBytes() > 0) {
                totals_known_ = true;
                total_bytes_ = st.GetTotalBytes();
            }
        }
        emit_progress_snapshot();
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::StartOpenArchive(wchar_t const* name) {
        std::string const path = update_wide_name_to_utf8(name);
        if (!path.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            archive_path_ = path;
        }
        if (!path.empty()) {
            emit_log_event(hooks_, OperationStage::kRunning, OutputChannel::kNone, "Opening " + path);
        }
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::ScanError(FString const& path, DWORD) {
        std::string const value = ustring_to_utf8(fs2us(path));
        note_error(value.empty() ? "Scan error" : ("Scan error: " + value));
        return S_FALSE;
    }

    HRESULT NativeUpdateOperationCallback::ScanProgress(CDirItemsStat const& st, FString const& path, bool) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            total_files_ = st.NumFiles + st.NumAltStreams;
            if (st.GetTotalBytes() > 0) {
                totals_known_ = true;
                total_bytes_ = st.GetTotalBytes();
            }
            current_path_ = ustring_to_utf8(fs2us(path));
        }
        emit_progress_snapshot();
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::Open_CheckBreak() {
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::Open_SetTotal(UInt64 const* files, UInt64 const* bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (files != nullptr) {
                total_files_ = *files;
            }
            if (bytes != nullptr) {
                totals_known_ = true;
                total_bytes_ = *bytes;
            }
        }
        emit_progress_snapshot();
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::Open_SetCompleted(UInt64 const* files, UInt64 const* bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (files != nullptr) {
                completed_files_ = *files;
            }
            if (bytes != nullptr) {
                completed_bytes_ = *bytes;
            }
        }
        emit_progress_snapshot();
        return check_break();
    }

    HRESULT NativeUpdateOperationCallback::Open_Finished() {
        return check_break();
    }

#ifndef Z7_NO_CRYPTO
    HRESULT NativeUpdateOperationCallback::provide_password(BSTR* password, bool force_prompt) {
        if (password == nullptr) {
            return E_INVALIDARG;
        }
        *password = nullptr;

        std::string password_value;
        std::string archive_path;
        bool wrong_password = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            password_value = password_;
            archive_path = archive_path_;
            wrong_password = wrong_password_;
        }

        if (wrong_password || force_prompt) {
            if (!hooks_.ask_password) {
                std::lock_guard<std::mutex> lock(mutex_);
                password_requested_ = true;
                wrong_password_ = wrong_password;
                return S_OK;
            }

            PasswordPrompt prompt;
            prompt.archive_path = archive_path;
            prompt.reason_kind =
                wrong_password ? PasswordPromptReason::kWrongPassword : PasswordPromptReason::kPasswordRequired;
            prompt.reason = wrong_password ? "wrong_password" : "password_required";
            PasswordReply const reply = hooks_.ask_password(prompt);
            if (reply.kind != PasswordReplyKind::kProvide) {
                std::lock_guard<std::mutex> lock(mutex_);
                password_requested_ = true;
                wrong_password_ = wrong_password;
                return S_OK;
            }

            password_value = reply.password;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                password_ = password_value;
                password_defined_ = true;
                wrong_password_ = false;
            }
        }

        UString const pass = utf8_to_ustring(password_value);
        const HRESULT pass_res = StringToBstr(pass, password);
        if (pass_res != S_OK) {
            std::lock_guard<std::mutex> lock(mutex_);
            password_requested_ = true;
        }
        return pass_res;
    }

    HRESULT NativeUpdateOperationCallback::CryptoGetTextPassword2(Int32* password_is_defined, BSTR* password) {
        const HRESULT password_res = provide_password(password, false);
        if (password_is_defined != nullptr) {
            bool password_defined = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                password_defined = password_defined_;
            }
            *password_is_defined = BoolToInt(password_res == S_OK && password_defined);
        }
        return password_res;
    }

    HRESULT NativeUpdateOperationCallback::CryptoGetTextPassword(BSTR* password) {
        return CryptoGetTextPassword2(nullptr, password);
    }

    HRESULT NativeUpdateOperationCallback::Open_CryptoGetTextPassword(BSTR* password) {
        return provide_password(password, true);
    }
#endif

} // namespace z7::app
