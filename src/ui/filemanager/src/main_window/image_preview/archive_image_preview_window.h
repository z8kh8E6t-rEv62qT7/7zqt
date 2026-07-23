#pragma once

#include <QWidget>
#include <memory>

#include "main_window/image_preview/ffmpeg_image_decoder.h"

class QLabel;
class QPushButton;
class QTimer;

namespace z7::ui::filemanager {

    class ArchiveImagePreviewWindow final : public QWidget {
        Q_OBJECT

    public:
        explicit ArchiveImagePreviewWindow(QWidget* owner);

        void show_entry(QString const& file_name, int position, int count);
        void show_loading();
        void show_image(std::shared_ptr<const FfmpegDecodedImage> image);
        void show_error(QString const& message);

    signals:
        void previous_requested();
        void next_requested();
        void preview_closed();

    protected:
        void closeEvent(QCloseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        bool event(QEvent* event) override;

    private:
        class ImageCanvas;

        void show_initially_clamped();
        void display_frame(size_t index);
        void restart_animation_if_active();
        void advance_animation();

        QWidget* owner_ = nullptr;
        ImageCanvas* canvas_ = nullptr;
        QLabel* status_label_ = nullptr;
        QPushButton* previous_button_ = nullptr;
        QPushButton* next_button_ = nullptr;
        QTimer* animation_timer_ = nullptr;
        std::shared_ptr<const FfmpegDecodedImage> image_;
        size_t frame_index_ = 0;
        bool window_active_ = true;
        bool initial_geometry_applied_ = false;
    };

} // namespace z7::ui::filemanager
