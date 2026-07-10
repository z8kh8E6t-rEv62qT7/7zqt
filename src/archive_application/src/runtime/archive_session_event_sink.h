#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "archive_session.h"
#include "archive_session_helpers.h"

namespace z7::app {

    class ArchiveSessionEventSink final {
    public:
        using StateSetter = std::function<void(ArchiveSessionState)>;

        ArchiveSessionEventSink(std::shared_ptr<IArchiveDelegate> delegate,
                                ArchiveRequest const& request,
                                StateSetter set_state);

        void reset_diagnostics();
        void on_event(OperationEvent const& event);
        ProgressSnapshot snapshot() const;
        void merge_diagnostics(OperationOutcome& outcome) const;

    private:
        void on_lifecycle_event(OperationEvent const& event);
        void on_log_event(OperationEvent const& event);
        void on_progress_event(OperationEvent const& event);

        std::shared_ptr<IArchiveDelegate> delegate_;
        StateSetter set_state_;
        std::chrono::milliseconds progress_interval_;

        mutable std::mutex mutex_;
        ProgressSnapshot snapshot_;
        std::vector<std::string> collected_diagnostics_;
        archive_session_helpers::Clock::time_point last_progress_emit_{};
    };

} // namespace z7::app
