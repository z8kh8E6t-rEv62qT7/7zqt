#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "archive_session.h"
#include "main_window/image_preview/ffmpeg_image_decoder.h"

class QWidget;

template <typename T>
class QFutureWatcher;

namespace z7::ui::filemanager {

    class ArchiveImagePreviewWindow;
    class ArchiveProcessRunner;

    struct ArchiveImagePreviewEntry {
        QString path;
        uint32_t archive_index = 0;
    };

    class ArchiveImagePreviewController final : public QObject {
    public:
        explicit ArchiveImagePreviewController(QWidget* owner);
        ~ArchiveImagePreviewController() override;

        static bool is_image_candidate(QString const& path);

        void open(z7::app::ArchiveSessionToken session_token,
                  std::vector<ArchiveImagePreviewEntry> entries,
                  size_t current_position);
        void close_for_session(z7::app::ArchiveSessionToken session_token);
        void close_preview();
        bool is_open() const;
        z7::app::ArchiveSessionToken session_token() const;

    private:
        struct CacheEntry {
            enum class FailureKind {
                kNone,
                kRead,
                kDecode,
            };

            std::shared_ptr<const FfmpegDecodedImage> image;
            QString error;
            FailureKind failure_kind = FailureKind::kNone;
            uint64_t byte_size = 0;
            uint64_t last_used = 0;
        };

        struct Job {
            uint64_t generation = 0;
            size_t position = 0;
            bool background = false;
        };

        QString key_for(size_t position) const;
        bool enqueue(size_t position, bool background, bool front = false);
        void request_current();
        void enqueue_neighbors();
        void start_next_job();
        void finish_read(bool ok, int error_domain, QString const& summary);
        void finish_decode();
        void finish_job_with_cache(CacheEntry entry);
        void show_cached(size_t position, CacheEntry& entry);
        QString localized_failure(CacheEntry const& entry) const;
        void navigate(int direction);
        void cancel_active_job();
        void evict_to_budget();
        bool job_is_current(Job const& job) const;
        bool failure_is_cacheable(z7::app::ArchiveErrorDomain domain) const;

        QWidget* owner_ = nullptr;
        ArchiveImagePreviewWindow* window_ = nullptr;
        z7::app::ArchiveSessionToken session_token_;
        std::vector<ArchiveImagePreviewEntry> entries_;
        size_t current_position_ = 0;
        int navigation_direction_ = 1;
        uint64_t generation_ = 0;
        uint64_t lru_clock_ = 0;
        uint64_t cache_bytes_ = 0;
        bool preview_open_ = false;
        std::deque<Job> queue_;
        std::optional<Job> active_job_;
        QPointer<ArchiveProcessRunner> active_runner_;
        std::shared_ptr<std::optional<z7::app::ReadArchiveEntryResult>> active_read_result_;
        QFutureWatcher<FfmpegImageDecodeResult>* decode_watcher_ = nullptr;
        std::shared_ptr<std::atomic<bool>> decode_cancel_;
        QHash<QString, CacheEntry> cache_;
    };

} // namespace z7::ui::filemanager
