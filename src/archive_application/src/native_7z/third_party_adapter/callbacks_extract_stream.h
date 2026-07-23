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
    // Returns E_OUTOFMEMORY once the limit is exceeded. Callers decide whether
    // that is a terminal budget error or whether another output strategy is
    // permitted.
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

    // Single-pass nested-open sink. Data stays in memory through memory_limit;
    // the first byte beyond that boundary creates a private temp file, copies
    // the buffered prefix, and continues the same extraction on disk.
    class NativeSpillableOutStream final : public ISequentialOutStream {
    public:
        NativeSpillableOutStream(size_t memory_limit, std::string file_name_hint, bool prefer_file);
        ~NativeSpillableOutStream();

        HRESULT finish();
        HRESULT materialize_to_file();
        bool spilled_to_file() const;
        uint8_t const* memory_data() const;
        size_t memory_size() const;
        std::vector<uint8_t> take_buffer();
        std::filesystem::path const& temp_directory() const;
        std::filesystem::path const& temp_file() const;
        std::string failure_message() const;
        void release_temp_ownership();

        STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
        STDMETHOD_(ULONG, AddRef)() throw() override;
        STDMETHOD_(ULONG, Release)() throw() override;
        STDMETHOD(Write)(void const* data, UInt32 size, UInt32* processed_size) throw() override;

    private:
        HRESULT spill_to_file();
        void cleanup_owned_temp();

        std::atomic<ULONG> ref_count_{1};
        size_t memory_limit_ = 0;
        std::string file_name_hint_;
        bool prefer_file_ = false;
        bool finished_ = false;
        bool owns_temp_ = true;
        std::vector<uint8_t> buffer_;
        std::filesystem::path temp_directory_;
        std::filesystem::path temp_file_;
        NativeFileOutStream* file_stream_ = nullptr;
        std::string failure_message_;
    };

} // namespace z7::app
