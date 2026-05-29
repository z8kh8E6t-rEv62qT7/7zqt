#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "archive_session_event_sink.h"
#include "archive_session_interactions.h"
#include "ports/archive_backend_port.h"

namespace z7::app {

class ArchiveSessionCore final
    : public std::enable_shared_from_this<ArchiveSessionCore> {
 public:
  ArchiveSessionCore(const ArchiveRequest& request,
                     std::shared_ptr<IArchiveDelegate> delegate,
                     std::unique_ptr<INativeArchiveBackend> backend);
  ~ArchiveSessionCore();

  bool start();
  void cancel();
  void pause();
  void resume();
  ArchiveSessionState state() const;
  ProgressSnapshot snapshot() const;

 private:
  void run_worker();
  void set_state(ArchiveSessionState expected);

  ArchiveRequest request_;
  std::shared_ptr<IArchiveDelegate> delegate_;
  std::unique_ptr<INativeArchiveBackend> backend_;
  ArchiveSessionEventSink event_sink_;
  ArchiveSessionInteractionBroker interaction_broker_;
  std::thread worker_;
  std::atomic<ArchiveSessionState> state_{ArchiveSessionState::kPending};
};

}  // namespace z7::app
