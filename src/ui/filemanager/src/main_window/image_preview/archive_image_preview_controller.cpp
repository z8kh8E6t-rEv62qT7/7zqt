#include "main_window/image_preview/archive_image_preview_controller.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

#include "archive_process_runner.h"
#include "custom_localization.h"
#include "main_window/image_preview/archive_image_preview_window.h"

namespace z7::ui::filemanager {
    namespace {

        constexpr uint64_t kMaximumRawEntryBytes = 128ull * 1024ull * 1024ull;

        QSet<QString> const& candidate_suffixes() {
            static QSet<QString> const suffixes = {
                QStringLiteral("apng"), QStringLiteral("avif"), QStringLiteral("bmp"),  QStringLiteral("dds"),
                QStringLiteral("dib"),  QStringLiteral("dpx"),  QStringLiteral("exr"),  QStringLiteral("gif"),
                QStringLiteral("hdr"),  QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("ico"),
                QStringLiteral("jfif"), QStringLiteral("jpe"),  QStringLiteral("jpeg"), QStringLiteral("jpg"),
                QStringLiteral("jxl"),  QStringLiteral("pam"),  QStringLiteral("pbm"),  QStringLiteral("pcx"),
                QStringLiteral("pfm"),  QStringLiteral("pgm"),  QStringLiteral("png"),  QStringLiteral("pnm"),
                QStringLiteral("ppm"),  QStringLiteral("psd"),  QStringLiteral("qoi"),  QStringLiteral("ras"),
                QStringLiteral("sgi"),  QStringLiteral("svg"),  QStringLiteral("tga"),  QStringLiteral("tif"),
                QStringLiteral("tiff"), QStringLiteral("webp"), QStringLiteral("xbm"),  QStringLiteral("xpm")};
            return suffixes;
        }

    } // namespace

    ArchiveImagePreviewController::ArchiveImagePreviewController(QWidget* owner) : QObject(owner), owner_(owner) {
        window_ = new ArchiveImagePreviewWindow(owner);
        decode_watcher_ = new QFutureWatcher<FfmpegImageDecodeResult>(this);
        connect(window_, &ArchiveImagePreviewWindow::previous_requested, this, [this]() { navigate(-1); });
        connect(window_, &ArchiveImagePreviewWindow::next_requested, this, [this]() { navigate(1); });
        connect(window_, &ArchiveImagePreviewWindow::preview_closed, this, [this]() {
            preview_open_ = false;
            queue_.clear();
            cancel_active_job();
        });
        connect(decode_watcher_, &QFutureWatcherBase::finished, this, [this]() { finish_decode(); });
    }

    ArchiveImagePreviewController::~ArchiveImagePreviewController() {
        cancel_active_job();
        if (decode_watcher_ != nullptr && decode_watcher_->isRunning()) {
            decode_watcher_->waitForFinished();
        }
        delete window_;
    }

    bool ArchiveImagePreviewController::is_image_candidate(QString const& path) {
        QString const suffix = QFileInfo(path).suffix().toLower();
        return !suffix.isEmpty() && candidate_suffixes().contains(suffix);
    }

    void ArchiveImagePreviewController::open(z7::app::ArchiveSessionToken session_token,
                                             std::vector<ArchiveImagePreviewEntry> entries,
                                             size_t current_position) {
        if (!session_token.is_valid() || entries.empty() || current_position >= entries.size()) {
            return;
        }
        if (session_token_ != session_token) {
            cache_.clear();
            cache_bytes_ = 0;
        }
        cancel_active_job();
        queue_.clear();
        ++generation_;
        session_token_ = session_token;
        entries_ = std::move(entries);
        current_position_ = current_position;
        navigation_direction_ = 1;
        preview_open_ = true;
        window_->show_entry(QFileInfo(entries_[current_position_].path).fileName(),
                            static_cast<int>(current_position_),
                            static_cast<int>(entries_.size()));
        window_->show_loading();
        request_current();
    }

    void ArchiveImagePreviewController::close_for_session(z7::app::ArchiveSessionToken session_token) {
        if (session_token_ == session_token) {
            close_preview();
            entries_.clear();
            cache_.clear();
            cache_bytes_ = 0;
            session_token_ = {};
        }
    }

    void ArchiveImagePreviewController::close_preview() {
        queue_.clear();
        cancel_active_job();
        preview_open_ = false;
        if (window_ != nullptr) {
            window_->close();
        }
    }

    bool ArchiveImagePreviewController::is_open() const {
        return preview_open_ && window_ != nullptr && window_->isVisible();
    }

    z7::app::ArchiveSessionToken ArchiveImagePreviewController::session_token() const {
        return session_token_;
    }

    QString ArchiveImagePreviewController::key_for(size_t position) const {
        if (position >= entries_.size()) {
            return {};
        }
        return QStringLiteral("%1:%2").arg(session_token_.value).arg(entries_[position].archive_index);
    }

    bool ArchiveImagePreviewController::enqueue(size_t position, bool background, bool front) {
        if (position >= entries_.size() || cache_.contains(key_for(position))) {
            return false;
        }
        if (active_job_.has_value() && active_job_->generation == generation_ && active_job_->position == position) {
            // A foreground navigation can overtake a password-silent prefetch.
            // Keep one foreground retry queued while the prefetch is canceled.
            if (background || !active_job_->background) {
                return false;
            }
        }
        if (std::any_of(queue_.begin(), queue_.end(), [&](Job const& job) {
                return job.generation == generation_ && job.position == position;
            })) {
            return false;
        }
        Job job{generation_, position, background};
        if (front) {
            queue_.push_front(job);
        } else {
            queue_.push_back(job);
        }
        return true;
    }

    void ArchiveImagePreviewController::request_current() {
        QString const key = key_for(current_position_);
        auto cached = cache_.find(key);
        if (cached != cache_.end()) {
            show_cached(current_position_, cached.value());
            enqueue_neighbors();
            start_next_job();
            return;
        }
        enqueue(current_position_, false, true);
        start_next_job();
    }

    void ArchiveImagePreviewController::enqueue_neighbors() {
        if (entries_.empty() || entries_.size() == 1) {
            return;
        }
        size_t const next = (current_position_ + 1) % entries_.size();
        size_t const previous = (current_position_ + entries_.size() - 1) % entries_.size();
        if (navigation_direction_ >= 0) {
            enqueue(next, true);
            enqueue(previous, true);
        } else {
            enqueue(previous, true);
            enqueue(next, true);
        }
    }

    void ArchiveImagePreviewController::start_next_job() {
        if (active_job_.has_value() || (decode_watcher_ != nullptr && decode_watcher_->isRunning())) {
            return;
        }
        while (!queue_.empty()) {
            Job const job = queue_.front();
            queue_.pop_front();
            if (job.generation != generation_ || job.position >= entries_.size()) {
                continue;
            }
            auto cached = cache_.find(key_for(job.position));
            if (cached != cache_.end()) {
                if (!job.background && job.position == current_position_) {
                    show_cached(job.position, cached.value());
                    enqueue_neighbors();
                }
                continue;
            }

            active_job_ = job;
            active_read_result_ = std::make_shared<std::optional<z7::app::ReadArchiveEntryResult>>();
            auto* runner = new ArchiveProcessRunner(this);
            active_runner_ = runner;
            runner->set_prompt_parent_provider([this]() { return owner_; });
            if (job.background) {
                runner->set_password_prompt_handler([](z7::app::PasswordPrompt const&) {
                    return z7::app::PasswordReply{z7::app::PasswordReplyKind::kCancel, {}};
                });
            }
            connect(runner,
                    &ArchiveProcessRunner::finished,
                    this,
                    [this](bool ok, int, int error_domain, QString const& summary) {
                        finish_read(ok, error_domain, summary);
                    });
            if (!runner->start_read_archive_entry(session_token_,
                                                  entries_[job.position].archive_index,
                                                  kMaximumRawEntryBytes,
                                                  active_read_result_)) {
                finish_read(false,
                            static_cast<int>(z7::app::ArchiveErrorDomain::kBackendUnavailable),
                            QStringLiteral("Unable to start archive entry read"));
            }
            return;
        }
    }

    void ArchiveImagePreviewController::finish_read(bool ok, int error_domain, QString const& summary) {
        QPointer<ArchiveProcessRunner> const completed_runner = active_runner_;
        active_runner_ = nullptr;
        if (completed_runner != nullptr) {
            completed_runner->deleteLater();
        }
        if (!active_job_.has_value()) {
            return;
        }
        Job const job = *active_job_;
        if (job.generation != generation_) {
            active_job_.reset();
            active_read_result_.reset();
            start_next_job();
            return;
        }
        z7::app::ArchiveErrorDomain const domain = static_cast<z7::app::ArchiveErrorDomain>(error_domain);
        if (!ok || !active_read_result_ || !active_read_result_->has_value()
            || !active_read_result_->value().bytes) {
            CacheEntry failure;
            failure.error = summary;
            failure.failure_kind = CacheEntry::FailureKind::kRead;
            failure.last_used = ++lru_clock_;
            active_read_result_.reset();
            if (failure_is_cacheable(domain)) {
                finish_job_with_cache(std::move(failure));
            } else {
                if (!job.background && job_is_current(job) && preview_open_) {
                    window_->show_error(localized_failure(failure));
                }
                active_job_.reset();
                start_next_job();
            }
            return;
        }

        std::shared_ptr<const std::vector<uint8_t>> bytes = active_read_result_->value().bytes;
        active_read_result_.reset();
        decode_cancel_ = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> const cancel = decode_cancel_;
        decode_watcher_->setFuture(QtConcurrent::run([bytes = std::move(bytes), cancel]() {
            return FfmpegImageDecoder::decode(bytes, cancel.get());
        }));
    }

    void ArchiveImagePreviewController::finish_decode() {
        if (!active_job_.has_value()) {
            return;
        }
        Job const job = *active_job_;
        FfmpegImageDecodeResult const result = decode_watcher_->result();
        decode_cancel_.reset();
        if (job.generation != generation_ || result.canceled) {
            active_job_.reset();
            start_next_job();
            return;
        }
        CacheEntry entry;
        entry.image = result.image;
        entry.error = result.error;
        entry.failure_kind = result.image ? CacheEntry::FailureKind::kNone : CacheEntry::FailureKind::kDecode;
        entry.byte_size = result.image ? result.image->byte_size : 0;
        entry.last_used = ++lru_clock_;
        finish_job_with_cache(std::move(entry));
    }

    void ArchiveImagePreviewController::finish_job_with_cache(CacheEntry entry) {
        if (!active_job_.has_value()) {
            return;
        }
        Job const job = *active_job_;
        QString const key = key_for(job.position);
        auto old = cache_.find(key);
        if (old != cache_.end()) {
            cache_bytes_ -= old->byte_size;
        }
        cache_bytes_ += entry.byte_size;
        cache_.insert(key, std::move(entry));
        active_job_.reset();
        evict_to_budget();

        auto cached = cache_.find(key);
        if (cached != cache_.end() && job_is_current(job) && preview_open_) {
            show_cached(job.position, cached.value());
            enqueue_neighbors();
        }
        start_next_job();
    }

    void ArchiveImagePreviewController::show_cached(size_t position, CacheEntry& entry) {
        entry.last_used = ++lru_clock_;
        window_->show_entry(QFileInfo(entries_[position].path).fileName(),
                            static_cast<int>(position),
                            static_cast<int>(entries_.size()));
        if (entry.image) {
            window_->show_image(entry.image);
        } else {
            window_->show_error(localized_failure(entry));
        }
    }

    QString ArchiveImagePreviewController::localized_failure(CacheEntry const& entry) const {
        QString key;
        switch (entry.failure_kind) {
            case CacheEntry::FailureKind::kRead:
                key = QStringLiteral("ui.archive.image_preview.read_failed");
                break;
            case CacheEntry::FailureKind::kDecode:
                key = QStringLiteral("ui.archive.image_preview.decode_failed");
                break;
            case CacheEntry::FailureKind::kNone:
                break;
        }
        QString const localized = key.isEmpty() ? QString() : z7::ui::runtime_support::J(key).trimmed();
        QString const detail = entry.error.trimmed();
        if (localized.isEmpty()) {
            return detail;
        }
        if (detail.isEmpty() || detail == localized) {
            return localized;
        }
        return localized + QLatin1Char(' ') + detail;
    }

    void ArchiveImagePreviewController::navigate(int direction) {
        if (!preview_open_ || entries_.size() <= 1 || direction == 0) {
            return;
        }
        navigation_direction_ = direction > 0 ? 1 : -1;
        current_position_ = direction > 0 ? (current_position_ + 1) % entries_.size()
                                          : (current_position_ + entries_.size() - 1) % entries_.size();
        window_->show_entry(QFileInfo(entries_[current_position_].path).fileName(),
                            static_cast<int>(current_position_),
                            static_cast<int>(entries_.size()));
        window_->show_loading();
        queue_.clear();
        if (active_job_.has_value()
            && (active_job_->position != current_position_ || active_job_->background)) {
            cancel_active_job();
        }
        request_current();
    }

    void ArchiveImagePreviewController::cancel_active_job() {
        if (active_runner_ != nullptr) {
            active_runner_->cancel();
        }
        if (decode_cancel_) {
            decode_cancel_->store(true);
        }
    }

    void ArchiveImagePreviewController::evict_to_budget() {
        QString const pinned = key_for(current_position_);
        while (cache_bytes_ > FfmpegImageDecoder::kMaximumDecodedBytes) {
            auto victim = cache_.end();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it.key() == pinned || it->byte_size == 0) {
                    continue;
                }
                if (victim == cache_.end() || it->last_used < victim->last_used) {
                    victim = it;
                }
            }
            if (victim == cache_.end()) {
                break;
            }
            cache_bytes_ -= victim->byte_size;
            cache_.erase(victim);
        }
    }

    bool ArchiveImagePreviewController::job_is_current(Job const& job) const {
        return job.generation == generation_ && job.position == current_position_;
    }

    bool ArchiveImagePreviewController::failure_is_cacheable(z7::app::ArchiveErrorDomain domain) const {
        return domain == z7::app::ArchiveErrorDomain::kBudgetExceeded
            || domain == z7::app::ArchiveErrorDomain::kUnsupportedFormat
            || domain == z7::app::ArchiveErrorDomain::kInvalidArguments;
    }

} // namespace z7::ui::filemanager
