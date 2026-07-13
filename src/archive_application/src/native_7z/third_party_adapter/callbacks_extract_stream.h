// src/archive_application/src/native_7z/third_party_adapter/callbacks_extract_stream.h
// Role: Output stream callback declarations for archive extraction.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "archive_types_extract.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    // Request-scoped extraction budget. One tracker is shared by every archive in
    // a multi-archive request so limits and rollback semantics cannot reset at an
    // archive boundary.
    class ExtractBudgetTracker final {
    public:
        explicit ExtractBudgetTracker(std::optional<ExtractBudget> budget);

        bool enabled() const;
        bool try_reserve_file();
        void release_file();
        bool can_accept_declared_bytes(uint64_t bytes);
        bool try_reserve_bytes(uint64_t bytes);
        void release_bytes(uint64_t bytes);

        bool triggered() const;
        std::string trigger_reason() const;
        BudgetExceededAction policy() const;

    private:
        bool trigger_locked(std::string reason);

        std::optional<ExtractBudget> budget_;
        mutable std::mutex mutex_;
        uint64_t reserved_files_ = 0;
        uint64_t reserved_bytes_ = 0;
        bool triggered_ = false;
        std::string trigger_reason_;
    };

    class NativeFileOutStream final : public ISequentialOutStream {
    public:
        NativeFileOutStream(std::filesystem::path path, std::shared_ptr<ExtractBudgetTracker> budget_tracker);

        HRESULT open();
        HRESULT Close();
        uint64_t bytes_written() const;
        std::string failure_message() const;

        STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
        STDMETHOD_(ULONG, AddRef)() throw() override;
        STDMETHOD_(ULONG, Release)() throw() override;

        STDMETHOD(Write)(void const* data, UInt32 size, UInt32* processed_size) throw() override;

    private:
        bool flush_pending_zero_run(bool trailing);
#if defined(__APPLE__)
        bool punch_sparse_ranges();
#endif

        std::atomic<ULONG> ref_count_{1};
        std::filesystem::path path_;
        NWindows::NFile::NIO::COutFile file_;
        std::shared_ptr<ExtractBudgetTracker> budget_tracker_;
        uint64_t bytes_written_ = 0;
        uint64_t pending_zero_bytes_ = 0;
        bool is_open_ = false;
#ifdef _WIN32
        bool sparse_mode_enabled_ = false;
#endif
#if defined(__APPLE__)
        std::vector<std::pair<uint64_t, uint64_t>> sparse_ranges_;
#endif
        std::string failure_message_;
    };

    // Writes extracted data into a caller-owned buffer up to max_size.
    // Returns E_OUTOFMEMORY once the limit is exceeded, which signals the
    // caller to abandon the in-memory strategy and fall back to a temp file.
    class NativeBufferOutStream final : public ISequentialOutStream {
    public:
        NativeBufferOutStream(std::vector<uint8_t>& sink, size_t max_size);

        STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
        STDMETHOD_(ULONG, AddRef)() throw() override;
        STDMETHOD_(ULONG, Release)() throw() override;

        STDMETHOD(Write)(void const* data, UInt32 size, UInt32* processed_size) throw() override;

    private:
        std::atomic<ULONG> ref_count_{1};
        std::vector<uint8_t>& sink_;
        size_t max_size_;
    };

} // namespace z7::app
