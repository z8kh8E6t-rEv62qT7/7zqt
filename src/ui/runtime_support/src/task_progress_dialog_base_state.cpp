#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include "archive_progress_format.h"
#include "official_lang_catalog.h"
#include "task_progress_dialog_base.h"

namespace z7::ui::runtime_support
{

    void TaskProgressDialogBase::set_header(QString const& text)
    {
        header_text_ = text.trimmed();
        operation_text_.clear();
        target_text_.clear();

        int const colon = header_text_.indexOf(QLatin1Char(':'));
        if (colon >= 0)
        {
            operation_text_ = header_text_.left(colon).trimmed();
            target_text_ = header_text_.mid(colon + 1).trimmed();
        }
        else
        {
            operation_text_ = header_text_;
        }

        update_title();
    }

    void TaskProgressDialogBase::set_stage(QString const& text)
    {
        stage_text_ = text.trimmed();
        QString const display = display_stage_text();
        if (status_label_ != nullptr)
        {
            status_label_->setText(display);
            status_label_->setVisible(!display.isEmpty());
        }
        update_title();
    }

    void TaskProgressDialogBase::set_pause_available(bool available)
    {
        pause_available_ = available;
        if (pause_button_ != nullptr)
        {
            pause_button_->setEnabled(running_ && pause_available_);
        }
    }

    void TaskProgressDialogBase::set_running(bool running)
    {
        running_ = running;

        if (running_)
        {
            started_ms_ = z7::ui::archive_support::now_msecs();
            elapsed_ms_ = 0;
            paused_started_ms_ = -1;
            paused_total_ms_ = 0;
            set_paused(false);
            set_backgrounded(false);

            if (metrics_widget_ != nullptr)
            {
                metrics_widget_->setVisible(true);
            }
            if (status_label_ != nullptr)
            {
                QString const display = display_stage_text();
                status_label_->setText(display);
                status_label_->setVisible(!display.isEmpty());
            }
            if (progress_bar_ != nullptr)
            {
                progress_bar_->setVisible(true);
            }
            if (log_view_ != nullptr)
            {
                log_view_->setVisible(false);
            }
            if (result_messages_view_ != nullptr)
            {
                result_messages_view_->setVisible(false);
            }
            if (background_button_ != nullptr)
            {
                background_button_->setVisible(true);
                background_button_->setEnabled(true);
            }
            if (pause_button_ != nullptr)
            {
                pause_button_->setVisible(true);
                pause_button_->setEnabled(pause_available_);
            }
            if (cancel_button_ != nullptr)
            {
                cancel_button_->setVisible(true);
                cancel_button_->setEnabled(true);
            }
            if (close_button_ != nullptr)
            {
                close_button_->setVisible(false);
                close_button_->setEnabled(false);
            }
            if (refresh_timer_ != nullptr)
            {
                refresh_timer_->start();
            }
        }
        else
        {
            if (refresh_timer_ != nullptr)
            {
                refresh_timer_->stop();
            }
            if (background_button_ != nullptr)
            {
                background_button_->setEnabled(false);
            }
            if (pause_button_ != nullptr)
            {
                pause_button_->setEnabled(false);
            }
            if (cancel_button_ != nullptr)
            {
                cancel_button_->setEnabled(false);
            }
        }

        refresh_metrics();
        update_title();
    }

    void TaskProgressDialogBase::set_test_mode(bool enabled)
    {
        test_mode_ = enabled;
    }

    void TaskProgressDialogBase::set_failure_result_mode()
    {
        running_ = false;
        paused_ = false;
        backgrounded_ = false;

        if (refresh_timer_ != nullptr)
        {
            refresh_timer_->stop();
        }
        percent_ = 100;
        stage_text_.clear();
        if (metrics_widget_ != nullptr)
        {
            metrics_widget_->setVisible(true);
        }
        if (status_label_ != nullptr)
        {
            QString const display = display_stage_text();
            status_label_->setText(display);
            status_label_->setVisible(!display.isEmpty());
        }
        if (current_path_label_ != nullptr)
        {
            current_path_label_->setVisible(false);
        }
        if (current_file_label_ != nullptr)
        {
            current_file_label_->setVisible(false);
        }
        if (progress_bar_ != nullptr)
        {
            progress_bar_->setVisible(true);
            progress_bar_->setRange(0, 100);
            progress_bar_->setValue(100);
        }
        if (log_view_ != nullptr)
        {
            log_view_->setVisible(false);
        }
        if (result_messages_view_ != nullptr)
        {
            result_messages_view_->setVisible(true);
        }
        if (background_button_ != nullptr)
        {
            background_button_->setVisible(false);
        }
        if (pause_button_ != nullptr)
        {
            pause_button_->setVisible(false);
        }
        if (cancel_button_ != nullptr)
        {
            cancel_button_->setVisible(false);
        }
        if (close_button_ != nullptr)
        {
            close_button_->setText(L(401));
            close_button_->setVisible(true);
            close_button_->setEnabled(true);
            close_button_->setFocus();
        }

        refresh_metrics();
        update_title();
    }

    void TaskProgressDialogBase::set_cancel_confirmation_handler(std::function<int()> const& handler)
    {
        cancel_confirmation_handler_ = handler;
    }

    void TaskProgressDialogBase::set_result_mode_impl()
    {
        running_ = false;
        paused_ = false;
        backgrounded_ = false;

        if (refresh_timer_ != nullptr)
        {
            refresh_timer_->stop();
        }

        setWindowTitle(strip_mnemonic(L(3302)));

        if (metrics_widget_ != nullptr)
        {
            metrics_widget_->setVisible(false);
        }
        if (status_label_ != nullptr)
        {
            status_label_->setVisible(false);
        }
        if (current_path_label_ != nullptr)
        {
            current_path_label_->setVisible(false);
        }
        if (current_file_label_ != nullptr)
        {
            current_file_label_->setVisible(false);
        }
        if (progress_bar_ != nullptr)
        {
            progress_bar_->setVisible(false);
        }
        if (background_button_ != nullptr)
        {
            background_button_->setVisible(false);
        }
        if (pause_button_ != nullptr)
        {
            pause_button_->setVisible(false);
        }
        if (cancel_button_ != nullptr)
        {
            cancel_button_->setVisible(false);
        }
        if (log_view_ != nullptr)
        {
            log_view_->setVisible(true);
        }
        if (result_messages_view_ != nullptr)
        {
            result_messages_view_->setVisible(false);
        }
        if (close_button_ != nullptr)
        {
            close_button_->setText(L(401));
            close_button_->setVisible(true);
            close_button_->setEnabled(true);
            close_button_->setFocus();
        }
    }

    void TaskProgressDialogBase::set_paused(bool paused)
    {
        if (paused_ == paused)
        {
            return;
        }

        qint64 const now = z7::ui::archive_support::now_msecs();
        if (paused)
        {
            paused_started_ms_ = now;
        }
        else if (paused_started_ms_ >= 0)
        {
            paused_total_ms_ += now - paused_started_ms_;
            paused_started_ms_ = -1;
        }
        paused_ = paused;

        if (pause_button_ != nullptr)
        {
            pause_button_->setText(paused_ ? L(411) : L(446));
        }
        update_title();
    }

    void TaskProgressDialogBase::set_backgrounded(bool backgrounded)
    {
        if (backgrounded_ == backgrounded)
        {
            return;
        }

        backgrounded_ = backgrounded;
        if (background_button_ != nullptr)
        {
            background_button_->setText(backgrounded_ ? L(445) : L(444));
        }
        update_title();
    }

    void TaskProgressDialogBase::update_title()
    {
        if (behavior_.freeze_title_after_result_mode
            && !running_
            && close_button_ != nullptr
            && close_button_->isVisible())
        {
            return;
        }

        QString title;
        if (paused_)
        {
            title += strip_mnemonic(L(447)) + QLatin1Char(' ');
        }
        if (percent_ >= 0)
        {
            title += QStringLiteral("%1% ").arg(percent_);
        }
        if (backgrounded_)
        {
            title += strip_mnemonic(L(445)) + QLatin1Char(' ');
        }

        QString const stage = display_stage_text();
        if (!stage.isEmpty())
        {
            title += stage;
        }
        else if (!operation_text_.isEmpty())
        {
            title += operation_text_;
        }
        else
        {
            title += strip_mnemonic(L(3304));
        }
        if (!target_text_.isEmpty())
        {
            title += QStringLiteral(" %1").arg(QDir::toNativeSeparators(target_text_));
        }

        setWindowTitle(title.trimmed());
    }

    QString TaskProgressDialogBase::display_stage_text() const
    {
        if (stage_text_.isEmpty())
        {
            return operation_text_;
        }

        if (stage_text_.compare(QStringLiteral("Running"), Qt::CaseInsensitive) == 0)
        {
            if (behavior_.running_stage_uses_test_caption && test_mode_)
            {
                return strip_mnemonic(L(3302));
            }
            if (!operation_text_.isEmpty())
            {
                return operation_text_;
            }
        }

        return stage_text_;
    }

} // namespace z7::ui::runtime_support
