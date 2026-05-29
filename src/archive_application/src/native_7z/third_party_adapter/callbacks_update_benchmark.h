// src/archive_application/src/native_7z/third_party_adapter/callbacks_update_benchmark.h
// Role: Benchmark print callback declarations for native backend.

#pragma once

#include "callback_base.h"

namespace z7::app {

class NativeBenchStructuredCallback final : public IBenchCallback, protected CallbackBase {
 public:
  NativeBenchStructuredCallback(const ArchiveBackendHooks& hooks,
                                std::atomic<bool>* cancel_requested,
                                std::function<bool()> wait_while_paused,
                                uint64_t dictionary_size_bytes);

  HRESULT SetEncodeResult(const CBenchInfo& info, bool final) override;
  HRESULT SetDecodeResult(const CBenchInfo& info, bool final) override;

  std::optional<BenchmarkTypedSummary> summary() const;

 private:
  struct BenchTotals {
    CTotalBenchRes values;
    uint64_t unpack_size = 0;
    bool available = false;
  };

  HRESULT check_break() const;
  void update_encode_locked(const CBenchInfo& info, bool final);
  void update_decode_locked(const CBenchInfo& info, bool final);
  BenchmarkTypedMetrics make_metrics_locked(bool final) const;
  void emit_snapshot_locked(BenchmarkSnapshotKind kind,
                            const BenchmarkTypedMetrics& metrics);
  void emit_total_locked();

  const ArchiveBackendHooks& hooks_;
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
  NativeBenchFreqCallback(const ArchiveBackendHooks& hooks,
                          std::atomic<bool>* cancel_requested,
                          std::function<bool()> wait_while_paused);

  HRESULT AddCpuFreq(unsigned num_threads, UInt64 freq, UInt64 usage) override;
  HRESULT FreqsFinished(unsigned num_threads) override;

 private:
  HRESULT check_break() const;
  void emit_frequency_line(const std::string& line) const;

  const ArchiveBackendHooks& hooks_;

  unsigned line_threads_ = 0;
  std::string line_buffer_;
};

class NativeBenchmarkPrintCallback final : public IBenchPrintCallback, protected CallbackBase {
 public:
  NativeBenchmarkPrintCallback(const ArchiveBackendHooks& hooks,
                               std::atomic<bool>* cancel_requested,
                               std::function<bool()> wait_while_paused,
                               BenchmarkTypedParser* parser);

  void Print(const char* s) override;
  void NewLine() override;
  HRESULT CheckBreak() override;

  void flush_pending();

 private:
  void emit_line(const std::string& line);

  const ArchiveBackendHooks& hooks_;
  BenchmarkTypedParser* parser_ = nullptr;

  std::mutex mutex_;
  std::string current_line_;
};

}  // namespace z7::app
