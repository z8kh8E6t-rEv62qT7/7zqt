// src/archive_application/src/native_7z/third_party_adapter/callbacks_update_benchmark.h
// Role: Benchmark print callback declarations for native backend.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "archive_types_benchmark.h"
#include "callback_base.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    struct ArchiveBackendHooks;
    class BenchmarkTypedParser;

    class NativeBenchStructuredCallback final : public IBenchCallback, protected CallbackBase {
    public:
        NativeBenchStructuredCallback(ArchiveBackendHooks const& hooks,
                                      std::atomic<bool>* cancel_requested,
                                      std::function<bool()> wait_while_paused,
                                      uint64_t dictionary_size_bytes);

        HRESULT SetEncodeResult(CBenchInfo const& info, bool final) override;
        HRESULT SetDecodeResult(CBenchInfo const& info, bool final) override;

        std::optional<BenchmarkTypedSummary> summary() const;

    private:
        struct BenchTotals {
            CTotalBenchRes values;
            uint64_t unpack_size = 0;
            bool available = false;
        };

        HRESULT check_break() const;
        void update_encode_locked(CBenchInfo const& info, bool final);
        void update_decode_locked(CBenchInfo const& info, bool final);
        BenchmarkTypedMetrics make_metrics_locked(bool final) const;
        void emit_snapshot_locked(BenchmarkSnapshotKind kind, BenchmarkTypedMetrics const& metrics);
        void emit_total_locked();

        ArchiveBackendHooks const& hooks_;
        uint64_t dictionary_size_bytes_ = 0;

        mutable std::mutex mutex_;
        BenchTotals current_encode_;
        BenchTotals current_decode_;
        BenchTotals final_encode_;
        BenchTotals final_decode_;
        uint64_t final_encode_updates_ = 0;
        uint64_t final_decode_updates_ = 0;
        uint64_t total_emitted_updates_ = 0;
        BenchmarkTypedSummary summary_;
    };

    class NativeBenchFreqCallback final : public IBenchFreqCallback, protected CallbackBase {
    public:
        NativeBenchFreqCallback(ArchiveBackendHooks const& hooks,
                                std::atomic<bool>* cancel_requested,
                                std::function<bool()> wait_while_paused);

        HRESULT AddCpuFreq(unsigned num_threads, UInt64 freq, UInt64 usage) override;
        HRESULT FreqsFinished(unsigned num_threads) override;

    private:
        HRESULT check_break() const;
        void emit_frequency_line(std::string const& line) const;

        ArchiveBackendHooks const& hooks_;

        unsigned line_threads_ = 0;
        std::string line_buffer_;
    };

    class NativeBenchmarkPrintCallback final : public IBenchPrintCallback, protected CallbackBase {
    public:
        NativeBenchmarkPrintCallback(ArchiveBackendHooks const& hooks,
                                     std::atomic<bool>* cancel_requested,
                                     std::function<bool()> wait_while_paused,
                                     BenchmarkTypedParser* parser);

        void Print(char const* s) override;
        void NewLine() override;
        HRESULT CheckBreak() override;

        void flush_pending();

    private:
        void emit_line(std::string const& line);

        ArchiveBackendHooks const& hooks_;
        BenchmarkTypedParser* parser_ = nullptr;

        std::mutex mutex_;
        std::string current_line_;
    };

} // namespace z7::app
