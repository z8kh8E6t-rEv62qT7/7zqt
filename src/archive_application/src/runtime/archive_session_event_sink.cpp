#include "archive_session_event_sink.h"

#include <algorithm>
#include <utility>

#include "archive_session_helpers.h"

namespace z7::app {

ArchiveSessionEventSink::ArchiveSessionEventSink(
    std::shared_ptr<IArchiveDelegate> delegate,
    const ArchiveRequest& request,
    StateSetter set_state)
    : delegate_(std::move(delegate)),
      set_state_(std::move(set_state)),
      progress_interval_(archive_session_helpers::is_benchmark_request(request)
                             ? archive_session_helpers::kBenchmarkProgressInterval
                             : archive_session_helpers::kDefaultProgressInterval) {}

void ArchiveSessionEventSink::reset_diagnostics() {
  std::lock_guard<std::mutex> lock(mutex_);
  collected_diagnostics_.clear();
}

void ArchiveSessionEventSink::on_event(const OperationEvent& event) {
  if (event.kind == OperationEventKind::kLifecycle) {
    on_lifecycle_event(event);
    return;
  }
  if (event.kind == OperationEventKind::kLog) {
    on_log_event(event);
    return;
  }
  if (event.kind == OperationEventKind::kProgress) {
    on_progress_event(event);
  }
}

ProgressSnapshot ArchiveSessionEventSink::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void ArchiveSessionEventSink::merge_diagnostics(OperationOutcome& outcome) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const std::string& diagnostic : collected_diagnostics_) {
    if (diagnostic.empty()) {
      continue;
    }
    if (std::find(outcome.diagnostics.begin(),
                  outcome.diagnostics.end(),
                  diagnostic) == outcome.diagnostics.end()) {
      outcome.diagnostics.push_back(diagnostic);
    }
  }
}

void ArchiveSessionEventSink::on_lifecycle_event(const OperationEvent& event) {
  if (delegate_) {
    delegate_->on_lifecycle(event.stage, event.message);
  }
  if (event.stage == OperationStage::kRunning && set_state_) {
    set_state_(ArchiveSessionState::kRunning);
  }
}

void ArchiveSessionEventSink::on_log_event(const OperationEvent& event) {
  if (event.output_channel == OutputChannel::kStdErr && !event.message.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (collected_diagnostics_.size() < 64) {
      collected_diagnostics_.push_back(event.message);
    }
  }
  if (!delegate_) {
    return;
  }
  ArchiveLog log;
  log.stage = event.stage;
  log.channel = event.output_channel;
  log.message = event.message;
  log.benchmark_snapshot = event.benchmark_snapshot;
  log.benchmark_summary = event.benchmark_summary;
  delegate_->on_log(log);
}

void ArchiveSessionEventSink::on_progress_event(const OperationEvent& event) {
  ProgressSnapshot current;
  current.stage = event.stage;
  current.percent = event.percent;
  current.totals_known = event.totals_known;
  current.total_bytes = event.total_bytes;
  current.completed_bytes = event.completed_bytes;
  current.total_files = event.total_files;
  current.completed_files = event.completed_files;
  current.error_count = event.error_count;
  current.current_path = event.current_path;
  current.message = event.message;
  current.ratio_info = event.ratio_info;
  current.benchmark_snapshot = event.benchmark_snapshot;
  current.benchmark_summary = event.benchmark_summary;

  bool should_emit = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = current;
    const archive_session_helpers::Clock::time_point now =
        archive_session_helpers::Clock::now();
    const bool has_terminal_percent = current.percent >= 100;
    if (last_progress_emit_ == archive_session_helpers::Clock::time_point{} ||
        now - last_progress_emit_ >= progress_interval_ ||
        has_terminal_percent) {
      last_progress_emit_ = now;
      should_emit = true;
    }
  }
  if (should_emit && delegate_) {
    delegate_->on_progress(current);
  }
}

}  // namespace z7::app
