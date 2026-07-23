#include "main_window/image_preview/archive_image_preview_window.h"

#include <QCloseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include "custom_localization.h"

namespace z7::ui::filemanager {

    class ArchiveImagePreviewWindow::ImageCanvas final : public QWidget {
    public:
        explicit ImageCanvas(QWidget* parent) : QWidget(parent) {
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            setMinimumSize(1, 1);
        }

        void set_image(QImage image) {
            image_ = std::move(image);
            update();
        }

        void clear_image() {
            image_ = QImage();
            update();
        }

    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter painter(this);
            painter.fillRect(rect(), palette().brush(QPalette::Window));
            if (image_.isNull()) {
                return;
            }
            QSize const fitted = image_.size().scaled(size(), Qt::KeepAspectRatio);
            QRect const target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(target, image_);
        }

    private:
        QImage image_;
    };

    ArchiveImagePreviewWindow::ArchiveImagePreviewWindow(QWidget* owner) : QWidget(nullptr), owner_(owner) {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setWindowFlag(Qt::Window, true);
        setWindowModality(Qt::NonModal);

        canvas_ = new ImageCanvas(this);
        status_label_ = new QLabel(this);
        status_label_->setTextFormat(Qt::PlainText);
        status_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        previous_button_ = new QPushButton(z7::ui::runtime_support::J(QStringLiteral("ui.archive.image_preview.previous")),
                                           this);
        next_button_ = new QPushButton(z7::ui::runtime_support::J(QStringLiteral("ui.archive.image_preview.next")),
                                       this);

        auto* navigation = new QHBoxLayout;
        navigation->addWidget(previous_button_);
        navigation->addWidget(next_button_);
        navigation->addWidget(status_label_, 1);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->addWidget(canvas_, 1);
        layout->addLayout(navigation);

        animation_timer_ = new QTimer(this);
        animation_timer_->setSingleShot(true);
        connect(animation_timer_, &QTimer::timeout, this, &ArchiveImagePreviewWindow::advance_animation);
        connect(previous_button_, &QPushButton::clicked, this, &ArchiveImagePreviewWindow::previous_requested);
        connect(next_button_, &QPushButton::clicked, this, &ArchiveImagePreviewWindow::next_requested);
    }

    void ArchiveImagePreviewWindow::show_entry(QString const& file_name, int position, int count) {
        setWindowTitle(QStringLiteral("%1 (%2/%3)").arg(file_name).arg(position + 1).arg(count));
        if (!isVisible()) {
            show_initially_clamped();
        } else {
            show();
            raise();
            activateWindow();
        }
    }

    void ArchiveImagePreviewWindow::show_loading() {
        status_label_->setText(z7::ui::runtime_support::J(QStringLiteral("ui.archive.image_preview.loading")));
    }

    void ArchiveImagePreviewWindow::show_image(std::shared_ptr<const FfmpegDecodedImage> image) {
        image_ = std::move(image);
        frame_index_ = 0;
        if (!image_ || image_->frames.empty()) {
            show_error(z7::ui::runtime_support::J(QStringLiteral("ui.archive.image_preview.decode_failed")));
            return;
        }
        status_label_->setText(
            image_->animation_truncated
                ? z7::ui::runtime_support::J(QStringLiteral("ui.archive.image_preview.animation_too_large"))
                : QString());
        display_frame(0);
        restart_animation_if_active();
    }

    void ArchiveImagePreviewWindow::show_error(QString const& message) {
        animation_timer_->stop();
        image_.reset();
        frame_index_ = 0;
        canvas_->clear_image();
        status_label_->setText(message.isEmpty()
                                   ? z7::ui::runtime_support::J(
                                         QStringLiteral("ui.archive.image_preview.decode_failed"))
                                   : message);
    }

    void ArchiveImagePreviewWindow::closeEvent(QCloseEvent* event) {
        animation_timer_->stop();
        emit preview_closed();
        QWidget::closeEvent(event);
    }

    void ArchiveImagePreviewWindow::keyPressEvent(QKeyEvent* event) {
        if (event->modifiers() == Qt::NoModifier) {
            switch (event->key()) {
                case Qt::Key_Left:
                    emit previous_requested();
                    event->accept();
                    return;
                case Qt::Key_Right:
                    emit next_requested();
                    event->accept();
                    return;
                case Qt::Key_Escape:
                case Qt::Key_Space:
                    close();
                    event->accept();
                    return;
                default:
                    break;
            }
        }
        QWidget::keyPressEvent(event);
    }

    bool ArchiveImagePreviewWindow::event(QEvent* event) {
        if (event != nullptr && event->type() == QEvent::WindowDeactivate) {
            window_active_ = false;
            animation_timer_->stop();
        } else if (event != nullptr && event->type() == QEvent::WindowActivate) {
            window_active_ = true;
            restart_animation_if_active();
        }
        return QWidget::event(event);
    }

    void ArchiveImagePreviewWindow::show_initially_clamped() {
        if (!initial_geometry_applied_) {
            QScreen* screen = owner_ != nullptr ? QGuiApplication::screenAt(owner_->frameGeometry().center()) : nullptr;
            if (screen == nullptr) {
                screen = QGuiApplication::primaryScreen();
            }
            QRect const available = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 900, 700);
            QSize const target(qMin(900, available.width()), qMin(700, available.height()));
            resize(target);
            move(available.center() - QPoint(target.width() / 2, target.height() / 2));
            initial_geometry_applied_ = true;
        }
        show();
        raise();
        activateWindow();
    }

    void ArchiveImagePreviewWindow::display_frame(size_t index) {
        if (!image_ || index >= image_->frames.size()) {
            return;
        }
        canvas_->set_image(image_->frames[index].image);
    }

    void ArchiveImagePreviewWindow::restart_animation_if_active() {
        animation_timer_->stop();
        if (!window_active_ || !isVisible() || !image_ || image_->frames.size() <= 1) {
            return;
        }
        animation_timer_->start(image_->frames[frame_index_].duration_ms);
    }

    void ArchiveImagePreviewWindow::advance_animation() {
        if (!image_ || image_->frames.size() <= 1) {
            return;
        }
        frame_index_ = (frame_index_ + 1) % image_->frames.size();
        display_frame(frame_index_);
        restart_animation_if_active();
    }

} // namespace z7::ui::filemanager
