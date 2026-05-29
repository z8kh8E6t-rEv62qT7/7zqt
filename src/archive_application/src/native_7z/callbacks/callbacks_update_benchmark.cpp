// src/archive_application/src/native_7z/callbacks/callbacks_update_benchmark.cpp
// Role: Benchmark print callback implementation for native backend.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_update_benchmark.h"

namespace z7::app {
namespace {

uint64_t to_mips(uint64_t ips) {
  return (ips + 500000ULL) / 1000000ULL;
}

uint32_t bytes_to_dict_log(uint64_t bytes) {
  if (bytes == 0) {
    return 0;
  }
  uint32_t log = 0;
  while ((bytes >> 1) != 0) {
    bytes >>= 1;
    ++log;
  }
  return log;
}

}  // namespace

NativeBenchStructuredCallback::NativeBenchStructuredCallback(
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused,
    uint64_t dictionary_size_bytes)
    : CallbackBase(cancel_requested, std::move(wait_while_paused)),
      hooks_(hooks),
      dictionary_size_bytes_(dictionary_size_bytes) {}

HRESULT NativeBenchStructuredCallback::check_break() const {
  return should_abort() ? E_ABORT : S_OK;
}

void NativeBenchStructuredCallback::update_encode_locked(const CBenchInfo& info,
                                                         bool final) {
  BenchTotals& out = final ? final_encode_ : current_encode_;
  uint64_t dict = dictionary_size_bytes_;
  if (!final && dict > info.UnpackSize) {
    dict = info.UnpackSize;
  }
  out.values.Rating = info.GetRating_LzmaEnc(dict);
  out.values.NumIterations2 = 1;
  out.values.Generate_From_BenchInfo(info);
  out.unpack_size = info.Get_UnpackSize_Full();
  out.available = true;
  if (final) {
    ++final_encode_updates_;
  }
}

void NativeBenchStructuredCallback::update_decode_locked(const CBenchInfo& info,
                                                         bool final) {
  BenchTotals& out = final ? final_decode_ : current_decode_;
  out.values.Rating = info.GetRating_LzmaDec();
  out.values.NumIterations2 = 1;
  out.values.Generate_From_BenchInfo(info);
  out.unpack_size = info.Get_UnpackSize_Full();
  out.available = true;
  if (final) {
    ++final_decode_updates_;
  }
}

BenchmarkTypedMetrics NativeBenchStructuredCallback::make_metrics_locked(bool final) const {
  const BenchTotals& enc = final ? final_encode_ : current_encode_;
  const BenchTotals& dec = final ? final_decode_ : current_decode_;

  BenchmarkTypedMetrics metrics;
  if (dictionary_size_bytes_ != 0) {
    metrics.has_dictionary_log = true;
    metrics.dictionary_log = bytes_to_dict_log(dictionary_size_bytes_);
  }

  if (enc.available) {
    metrics.has_compress_size = true;
    metrics.compress_size_bytes = enc.unpack_size;
    metrics.compress_speed_kb_per_sec = enc.values.Speed >> 10;
    metrics.compress_cpu_usage_percent =
        Benchmark_GetUsage_Percents(enc.values.Usage);
    metrics.compress_rpu_mips = to_mips(enc.values.RPU);
    metrics.compress_rating_mips = to_mips(enc.values.Rating);
  }

  if (dec.available) {
    metrics.has_decompress_size = true;
    metrics.decompress_size_bytes = dec.unpack_size;
    metrics.decompress_speed_kb_per_sec = dec.values.Speed >> 10;
    metrics.decompress_cpu_usage_percent =
        Benchmark_GetUsage_Percents(dec.values.Usage);
    metrics.decompress_rpu_mips = to_mips(dec.values.RPU);
    metrics.decompress_rating_mips = to_mips(dec.values.Rating);
  }

  return metrics;
}

void NativeBenchStructuredCallback::emit_snapshot_locked(
    BenchmarkSnapshotKind kind,
    const BenchmarkTypedMetrics& metrics) {
  BenchmarkTypedSnapshot snapshot;
  snapshot.kind = kind;
  snapshot.metrics = metrics;

  emit_progress_event(hooks_,
                      OperationStage::kRunning,
                      -1,
                      false,
                      0,
                      0,
                      0,
                      0,
                      0,
                      {},
                      {},
                      std::nullopt,
                      snapshot,
                      summary_);
}

void NativeBenchStructuredCallback::emit_total_locked() {
  if (!final_encode_.available || !final_decode_.available) {
    return;
  }

  CTotalBenchRes total = final_encode_.values;
  total.Update_With_Res(final_decode_.values);
  if (total.NumIterations2 == 0) {
    return;
  }

  const uint64_t avg_usage = total.Usage / total.NumIterations2;
  const uint64_t avg_rpu = total.RPU / total.NumIterations2;
  const uint64_t avg_rating = total.Rating / total.NumIterations2;

  summary_.has_total_rating = true;
  summary_.total_cpu_usage_percent = Benchmark_GetUsage_Percents(avg_usage);
  summary_.total_rpu_mips = to_mips(avg_rpu);
  summary_.total_rating_mips = to_mips(avg_rating);

  BenchmarkTypedSnapshot snapshot;
  snapshot.kind = BenchmarkSnapshotKind::kTotalRating;
  snapshot.total_cpu_usage_percent = summary_.total_cpu_usage_percent;
  snapshot.total_rpu_mips = summary_.total_rpu_mips;
  snapshot.total_rating_mips = summary_.total_rating_mips;

  emit_progress_event(hooks_,
                      OperationStage::kRunning,
                      -1,
                      false,
                      0,
                      0,
                      0,
                      0,
                      0,
                      {},
                      {},
                      std::nullopt,
                      snapshot,
                      summary_);
}

HRESULT NativeBenchStructuredCallback::SetEncodeResult(const CBenchInfo& info, bool final) {
  if (check_break() != S_OK) {
    return E_ABORT;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  update_encode_locked(info, final);
  const BenchmarkTypedMetrics metrics = make_metrics_locked(final);
  emit_snapshot_locked(final ? BenchmarkSnapshotKind::kAveragePass
                             : BenchmarkSnapshotKind::kDictionaryPass,
                       metrics);
  if (final) {
    summary_.has_average_metrics = true;
    summary_.average_metrics = metrics;
    const uint64_t completed = std::min(final_encode_updates_, final_decode_updates_);
    if (completed > total_emitted_updates_) {
      emit_total_locked();
      total_emitted_updates_ = completed;
    }
  }
  return S_OK;
}

HRESULT NativeBenchStructuredCallback::SetDecodeResult(const CBenchInfo& info, bool final) {
  if (check_break() != S_OK) {
    return E_ABORT;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  update_decode_locked(info, final);
  const BenchmarkTypedMetrics metrics = make_metrics_locked(final);
  emit_snapshot_locked(final ? BenchmarkSnapshotKind::kAveragePass
                             : BenchmarkSnapshotKind::kDictionaryPass,
                       metrics);
  if (final) {
    summary_.has_average_metrics = true;
    summary_.average_metrics = metrics;
    const uint64_t completed = std::min(final_encode_updates_, final_decode_updates_);
    if (completed > total_emitted_updates_) {
      emit_total_locked();
      total_emitted_updates_ = completed;
    }
  }
  return S_OK;
}

std::optional<BenchmarkTypedSummary> NativeBenchStructuredCallback::summary() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return summary_;
}

NativeBenchFreqCallback::NativeBenchFreqCallback(
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused)
    : CallbackBase(cancel_requested, std::move(wait_while_paused)),
      hooks_(hooks) {}

HRESULT NativeBenchFreqCallback::check_break() const {
  return should_abort() ? E_ABORT : S_OK;
}

void NativeBenchFreqCallback::emit_frequency_line(const std::string& line) const {
  if (line.empty()) {
    return;
  }
  BenchmarkTypedSnapshot snapshot;
  snapshot.kind = BenchmarkSnapshotKind::kFrequency;
  snapshot.frequency_line = line;
  emit_log_event(hooks_,
                 OperationStage::kRunning,
                 OutputChannel::kNone,
                 line,
                 snapshot,
                 std::nullopt);
}

HRESULT NativeBenchFreqCallback::AddCpuFreq(unsigned num_threads,
                                            UInt64 freq,
                                            UInt64 usage) {
  if (check_break() != S_OK) {
    return E_ABORT;
  }

  if (line_threads_ != num_threads) {
    if (!line_buffer_.empty()) {
      emit_frequency_line(line_buffer_);
      line_buffer_.clear();
    }
    line_threads_ = num_threads;
    line_buffer_ = std::to_string(num_threads) + "T Frequency (MHz):";
  }

  line_buffer_.push_back(' ');
  if (num_threads != 1) {
    line_buffer_ += std::to_string(Benchmark_GetUsage_Percents(usage));
    line_buffer_.push_back('%');
    line_buffer_.push_back(' ');
  }
  line_buffer_ += std::to_string(to_mips(freq));
  return S_OK;
}

HRESULT NativeBenchFreqCallback::FreqsFinished(unsigned /*num_threads*/) {
  if (check_break() != S_OK) {
    return E_ABORT;
  }
  if (!line_buffer_.empty()) {
    emit_frequency_line(line_buffer_);
    line_buffer_.clear();
  }
  return S_OK;
}

NativeBenchmarkPrintCallback::NativeBenchmarkPrintCallback(
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused,
    BenchmarkTypedParser* parser)
    : CallbackBase(cancel_requested, std::move(wait_while_paused)),
      hooks_(hooks),
      parser_(parser) {}

void NativeBenchmarkPrintCallback::Print(const char* s) {
  if (s == nullptr) {
    return;
  }

  std::vector<std::string> ready_lines;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const char* p = s; *p != 0; ++p) {
      const char ch = *p;
      if (ch == '\r' || ch == '\n') {
        if (!current_line_.empty()) {
          ready_lines.push_back(current_line_);
          current_line_.clear();
        }
        continue;
      }
      current_line_.push_back(ch);
    }
  }

  for (const std::string& line : ready_lines) {
    emit_line(line);
  }
}

void NativeBenchmarkPrintCallback::NewLine() {
  std::string line;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    line.swap(current_line_);
  }
  emit_line(line);
}

HRESULT NativeBenchmarkPrintCallback::CheckBreak() {
  return should_abort() ? E_ABORT : S_OK;
}

void NativeBenchmarkPrintCallback::flush_pending() {
  NewLine();
}

void NativeBenchmarkPrintCallback::emit_line(const std::string& line) {
  if (line.empty()) {
    return;
  }

  std::optional<BenchmarkTypedSnapshot> typed_snapshot;
  std::optional<BenchmarkTypedSummary> typed_summary;
  if (parser_ != nullptr) {
    typed_snapshot = parser_->consume_line(line);
    if (typed_snapshot.has_value()) {
      typed_summary = parser_->summary();
    }
  }

  emit_log_event(hooks_,
                 OperationStage::kRunning,
                 OutputChannel::kNone,
                 line,
                 typed_snapshot,
                 typed_summary);
  if (typed_snapshot.has_value() || typed_summary.has_value()) {
    int percent = -1;
    if (typed_snapshot.has_value() &&
        typed_snapshot->kind == BenchmarkSnapshotKind::kTotalRating) {
      percent = 100;
    }
    emit_progress_event(hooks_,
                        OperationStage::kRunning,
                        percent,
                        false,
                        0,
                        0,
                        0,
                        0,
                        0,
                        {},
                        {},
                        std::nullopt,
                        typed_snapshot,
                        typed_summary);
  }
}

}  // namespace z7::app
