// src/archive_application/src/native_7z/callbacks/callbacks_extract_stream.cpp
// Role: Output stream callback implementation for archive extraction.

#include "third_party_adapter/callbacks_extract_stream.h"

#include <array>

#if defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <winioctl.h>
#endif

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    namespace {

        std::string current_io_error_message(std::string_view action, fs::path const& path) {
#ifdef _WIN32
            std::error_code const ec(static_cast<int>(::GetLastError()), std::system_category());
#else
            std::error_code const ec(errno, std::generic_category());
#endif
            return std::string(action) + ": " + path.generic_string() + (ec ? std::string("; ") + ec.message() : "");
        }

    } // namespace

    ExtractBudgetTracker::ExtractBudgetTracker(std::optional<ExtractBudget> budget) : budget_(std::move(budget)) {}

    bool ExtractBudgetTracker::enabled() const {
        return budget_.has_value();
    }

    bool ExtractBudgetTracker::trigger_locked(std::string reason) {
        if (!triggered_) {
            triggered_ = true;
            trigger_reason_ = std::move(reason);
        }
        return false;
    }

    bool ExtractBudgetTracker::try_reserve_file() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!budget_.has_value() || !budget_->max_files.has_value()) {
            return true;
        }
        uint64_t const limit = *budget_->max_files;
        if (reserved_files_ >= limit) {
            return trigger_locked("max_files limit exceeded (" + std::to_string(limit) + ")");
        }
        ++reserved_files_;
        return true;
    }

    void ExtractBudgetTracker::release_file() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (budget_.has_value() && budget_->max_files.has_value() && reserved_files_ != 0) {
            --reserved_files_;
        }
    }

    bool ExtractBudgetTracker::can_accept_declared_bytes(uint64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!budget_.has_value() || !budget_->max_bytes.has_value()) {
            return true;
        }
        uint64_t const limit = *budget_->max_bytes;
        if (reserved_bytes_ > limit || bytes > limit - reserved_bytes_) {
            return trigger_locked("max_bytes limit exceeded (" + std::to_string(limit) + ")");
        }
        return true;
    }

    bool ExtractBudgetTracker::try_reserve_bytes(uint64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!budget_.has_value() || !budget_->max_bytes.has_value()) {
            return true;
        }
        uint64_t const limit = *budget_->max_bytes;
        if (reserved_bytes_ > limit || bytes > limit - reserved_bytes_) {
            return trigger_locked("max_bytes limit exceeded (" + std::to_string(limit) + ")");
        }
        reserved_bytes_ += bytes;
        return true;
    }

    void ExtractBudgetTracker::release_bytes(uint64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!budget_.has_value() || !budget_->max_bytes.has_value()) {
            return;
        }
        reserved_bytes_ = bytes > reserved_bytes_ ? 0 : reserved_bytes_ - bytes;
    }

    bool ExtractBudgetTracker::triggered() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return triggered_;
    }

    std::string ExtractBudgetTracker::trigger_reason() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return trigger_reason_;
    }

    BudgetExceededAction ExtractBudgetTracker::policy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return budget_.has_value() ? budget_->on_exceeded : BudgetExceededAction::kFailAndRollback;
    }

    NativeFileOutStream::NativeFileOutStream(fs::path path, std::shared_ptr<ExtractBudgetTracker> budget_tracker) :
        path_(std::move(path)), budget_tracker_(std::move(budget_tracker)) {}

    HRESULT NativeFileOutStream::open() {
        FString const native_path = us2fs(utf8_to_ustring(path_.string()));
        if (!file_.Create_NEW(native_path)) {
            failure_message_ = current_io_error_message("Cannot create temporary output", path_);
            return E_FAIL;
        }
        is_open_ = true;
        return S_OK;
    }

    HRESULT NativeFileOutStream::Close() {
        if (!is_open_) {
            return S_OK;
        }
        bool const pending_ok = flush_pending_zero_run(true);
        bool const length_ok = pending_ok && file_.SetLength(bytes_written_);
        if (!length_ok) {
            failure_message_ = current_io_error_message("Cannot set extracted output length", path_);
        }
        bool const close_ok = file_.Close();
        if (!close_ok && failure_message_.empty()) {
            failure_message_ = current_io_error_message("Cannot close extracted output", path_);
        }
#if defined(__APPLE__)
        bool const sparse_ok = close_ok && punch_sparse_ranges();
        if (!sparse_ok && failure_message_.empty()) {
            failure_message_ = current_io_error_message("Cannot deallocate sparse output ranges", path_);
        }
#else
        bool const sparse_ok = true;
#endif
        is_open_ = false;
        return length_ok && close_ok && sparse_ok ? S_OK : E_FAIL;
    }

    uint64_t NativeFileOutStream::bytes_written() const {
        return bytes_written_;
    }

    std::string NativeFileOutStream::failure_message() const {
        return failure_message_;
    }

    bool NativeFileOutStream::flush_pending_zero_run(bool trailing) {
        if (pending_zero_bytes_ == 0) {
            return true;
        }
        bool use_hole = pending_zero_bytes_ >= 4096;
#ifdef _WIN32
        if (use_hole && !sparse_mode_enabled_) {
            DWORD bytes_returned = 0;
            sparse_mode_enabled_ = file_.DeviceIoControl(FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes_returned);
        }
        use_hole = use_hole && sparse_mode_enabled_;
#endif
        if (use_hole) {
#if defined(__APPLE__)
            off_t const range_start = file_.seekToCur();
            if (range_start < 0) {
                return false;
            }
            sparse_ranges_.emplace_back(static_cast<uint64_t>(range_start), pending_zero_bytes_);
#endif
            if (!trailing) {
#ifdef _WIN32
                UInt64 new_position = 0;
                if (!file_.Seek(static_cast<Int64>(pending_zero_bytes_), FILE_CURRENT, new_position)) {
                    return false;
                }
#else
                if (pending_zero_bytes_ > static_cast<uint64_t>(std::numeric_limits<off_t>::max())
                    || file_.seek(static_cast<off_t>(pending_zero_bytes_), SEEK_CUR) < 0) {
                    return false;
                }
#endif
            }
            pending_zero_bytes_ = 0;
            return true;
        }

        static constexpr std::array<unsigned char, 4096> kZeroBuffer{};
        while (pending_zero_bytes_ != 0) {
            size_t const chunk =
                static_cast<size_t>(std::min<uint64_t>(pending_zero_bytes_, static_cast<uint64_t>(kZeroBuffer.size())));
            if (!file_.WriteFull(kZeroBuffer.data(), chunk)) {
                return false;
            }
            pending_zero_bytes_ -= chunk;
        }
        return true;
    }

#if defined(__APPLE__)
    bool NativeFileOutStream::punch_sparse_ranges() {
        if (sparse_ranges_.empty()) {
            return true;
        }
        int const fd = ::open(path_.c_str(), O_RDWR);
        if (fd < 0) {
            return false;
        }
        struct stat file_info{};
        if (::fstat(fd, &file_info) != 0 || file_info.st_blksize <= 0) {
            (void)::close(fd);
            return false;
        }
        uint64_t const block_size = static_cast<uint64_t>(file_info.st_blksize);
        bool ok = true;
        for (auto const& [offset, length] : sparse_ranges_) {
            if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())
                || length > static_cast<uint64_t>(std::numeric_limits<off_t>::max())
                || offset + length < offset
                || offset + length > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
                ok = false;
                break;
            }
            uint64_t const range_end = offset + length;
            uint64_t const aligned_offset =
                offset % block_size == 0 ? offset : offset + (block_size - offset % block_size);
            uint64_t const aligned_end = range_end - range_end % block_size;
            if (aligned_end <= aligned_offset) {
                continue;
            }
            fpunchhole_t range{};
            range.fp_offset = static_cast<off_t>(aligned_offset);
            range.fp_length = static_cast<off_t>(aligned_end - aligned_offset);
            if (::fcntl(fd, F_PUNCHHOLE, &range) != 0) {
                ok = false;
                break;
            }
        }
        int const close_result = ::close(fd);
        sparse_ranges_.clear();
        return ok && close_result == 0;
    }
#endif

    STDMETHODIMP NativeFileOutStream::QueryInterface(REFIID iid, void** out_object) throw() {
        if (out_object == nullptr) {
            return E_INVALIDARG;
        }
        *out_object = nullptr;

        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
            *out_object = static_cast<ISequentialOutStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) NativeFileOutStream::AddRef() throw() {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) NativeFileOutStream::Release() throw() {
        const ULONG next = --ref_count_;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    STDMETHODIMP NativeFileOutStream::Write(void const* data, UInt32 size, UInt32* processed_size) throw() {
        if (processed_size != nullptr) {
            *processed_size = 0;
        }
        if (size == 0) {
            return S_OK;
        }
        if (!is_open_) {
            return E_FAIL;
        }
        if (size > std::numeric_limits<uint64_t>::max() - bytes_written_) {
            failure_message_ = "Extracted output size overflow: " + path_.generic_string();
            return E_FAIL;
        }

        if (budget_tracker_ != nullptr && !budget_tracker_->try_reserve_bytes(size)) {
            return E_ABORT;
        }

        bool write_ok = true;
        auto const* bytes = static_cast<unsigned char const*>(data);
        size_t cursor = 0;
        while (write_ok && cursor < size) {
            size_t zero_start = cursor;
            while (zero_start < size && bytes[zero_start] != 0) {
                ++zero_start;
            }
            if (zero_start != cursor) {
                write_ok = flush_pending_zero_run(false) && file_.WriteFull(bytes + cursor, zero_start - cursor);
                cursor = zero_start;
                continue;
            }

            size_t zero_end = zero_start;
            while (zero_end < size && bytes[zero_end] == 0) {
                ++zero_end;
            }
            size_t const zero_length = zero_end - zero_start;
            pending_zero_bytes_ += zero_length;
            cursor = zero_end;
        }
        if (!write_ok) {
            failure_message_ = current_io_error_message("Cannot write extracted output", path_);
            if (budget_tracker_ != nullptr) {
                budget_tracker_->release_bytes(size);
            }
            return E_FAIL;
        }
        bytes_written_ += size;
        if (processed_size != nullptr) {
            *processed_size = size;
        }
        return S_OK;
    }

    NativeBufferOutStream::NativeBufferOutStream(std::vector<uint8_t>& sink, size_t max_size) :
        sink_(sink), max_size_(max_size) {}

    STDMETHODIMP NativeBufferOutStream::QueryInterface(REFIID iid, void** out_object) throw() {
        if (out_object == nullptr) {
            return E_INVALIDARG;
        }
        *out_object = nullptr;

        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
            *out_object = static_cast<ISequentialOutStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) NativeBufferOutStream::AddRef() throw() {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) NativeBufferOutStream::Release() throw() {
        const ULONG next = --ref_count_;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    STDMETHODIMP NativeBufferOutStream::Write(void const* data, UInt32 size, UInt32* processed_size) throw() {
        if (processed_size != nullptr) {
            *processed_size = 0;
        }
        if (size == 0) {
            return S_OK;
        }
        size_t const current = sink_.size();
        if (current > max_size_ || size > max_size_ - current) {
            return E_OUTOFMEMORY;
        }
        auto const* bytes = static_cast<uint8_t const*>(data);
        sink_.insert(sink_.end(), bytes, bytes + size);
        if (processed_size != nullptr) {
            *processed_size = size;
        }
        return S_OK;
    }

} // namespace z7::app
