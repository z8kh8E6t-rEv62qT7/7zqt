#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <algorithm>
#include <limits>

#include "archive_progress_format.h"
#include "task_progress_dialog_base.h"

namespace z7::ui::runtime_support
{
    namespace
    {

        int result_message_row_height(QTableWidget const* view)
        {
            if (view == nullptr)
            {
                return 0;
            }

            return view->fontMetrics().lineSpacing() + 8;
        }

    } // namespace

    void TaskProgressDialogBase::append_log(QString const& line)
    {
        if (log_view_ != nullptr && (behavior_.append_blank_log_lines || !line.trimmed().isEmpty()))
        {
            log_view_->appendPlainText(line);
        }
        parse_progress_log_line(line);
        refresh_metrics();
    }

    void TaskProgressDialogBase::append_failure_result_message(QString const& message)
    {
        if (result_messages_view_ == nullptr)
        {
            return;
        }

        QStringList lines = message.split(QLatin1Char('\n'));
        while (!lines.isEmpty() && lines.front().trimmed().isEmpty())
        {
            lines.removeFirst();
        }
        while (!lines.isEmpty() && lines.back().trimmed().isEmpty())
        {
            lines.removeLast();
        }
        if (lines.isEmpty())
        {
            return;
        }

        ++failure_result_message_count_;
        bool first_line = true;
        for (QString const& raw_line : lines)
        {
            QString const line = raw_line.trimmed();
            if (line.isEmpty())
            {
                continue;
            }

            int const row = result_messages_view_->rowCount();
            result_messages_view_->insertRow(row);

            auto* number_item =
                new QTableWidgetItem(first_line
                                         ? QString::number(failure_result_message_count_)
                                         : QString());
            number_item->setTextAlignment(Qt::AlignRight | Qt::AlignTop);
            result_messages_view_->setItem(row, 0, number_item);

            auto* message_item = new QTableWidgetItem(line);
            message_item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            result_messages_view_->setItem(row, 1, message_item);
            result_messages_view_->setRowHeight(row, result_message_row_height(result_messages_view_));
            first_line = false;
        }

        result_messages_view_->setVisible(result_messages_view_->rowCount() > 0);
        result_messages_view_->scrollToBottom();
    }

    void TaskProgressDialogBase::set_failure_result_messages(QStringList const& messages)
    {
        if (result_messages_view_ == nullptr)
        {
            return;
        }

        result_messages_view_->clearContents();
        result_messages_view_->setRowCount(0);
        failure_result_message_count_ = 0;

        for (QString const& message : messages)
        {
            append_failure_result_message(message);
        }
    }

    void TaskProgressDialogBase::set_percent(int value)
    {
        if (value < 0)
        {
            percent_ = -1;
            if (!totals_known_ && progress_bar_ != nullptr)
            {
                progress_bar_->setRange(0, 0);
            }
            update_title();
            return;
        }

        percent_ = std::clamp(value, 0, 100);
        if (progress_bar_ != nullptr)
        {
            progress_bar_->setRange(0, 100);
            progress_bar_->setValue(percent_);
        }
        update_title();
    }

    void TaskProgressDialogBase::set_detailed_progress(bool totals_known,
                                                       quint64 total_bytes,
                                                       quint64 completed_bytes,
                                                       quint64 total_files,
                                                       quint64 completed_files,
                                                       quint64 error_count,
                                                       std::optional<TaskProgressRatioInfo> const& ratio_info,
                                                       QString const& current_path)
    {
        totals_known_ = totals_known;
        total_bytes_ = total_bytes;
        completed_bytes_ = completed_bytes;
        total_files_ = total_files;
        completed_files_ = completed_files;
        error_count_ = error_count;
        ratio_info_ = ratio_info;

        if (!current_path.trimmed().isEmpty())
        {
            current_path_ = current_path.trimmed();
        }
        update_current_path_labels();

        if (progress_bar_ != nullptr)
        {
            if (totals_known_)
            {
                quint64 const denom = total_bytes_ != 0 ? total_bytes_ : total_files_;
                quint64 const numer = total_bytes_ != 0 ? completed_bytes_ : completed_files_;
                if (denom == 0)
                {
                    progress_bar_->setRange(0, 1);
                    progress_bar_->setValue(1);
                }
                else
                {
                    progress_bar_->setRange(0, 1000);
                    progress_bar_->setValue(static_cast<int>(std::min<quint64>(1000, (numer * 1000) / denom)));
                }

                if (percent_ < 0 && denom != 0)
                {
                    percent_ = static_cast<int>(std::min<quint64>(100, (numer * 100) / denom));
                }
            }
            else if (percent_ < 0)
            {
                progress_bar_->setRange(0, 0);
            }
        }

        refresh_metrics();
        update_title();
    }

    void TaskProgressDialogBase::refresh_metrics()
    {
        if (started_ms_ >= 0)
        {
            qint64 const now = z7::ui::archive_support::now_msecs();
            qint64 paused_ms = paused_total_ms_;
            if (paused_ && paused_started_ms_ >= 0)
            {
                paused_ms += now - paused_started_ms_;
            }
            elapsed_ms_ = std::max<qint64>(0, now - started_ms_ - paused_ms);
        }

        if (elapsed_value_ != nullptr)
        {
            elapsed_value_->setText(z7::ui::archive_support::format_hhmmss(static_cast<quint64>(elapsed_ms_ / 1000)));
        }

        if (remaining_value_ != nullptr)
        {
            quint64 const denom = total_bytes_ != 0 ? total_bytes_ : total_files_;
            quint64 const numer = total_bytes_ != 0 ? completed_bytes_ : completed_files_;
            if (totals_known_ && denom != 0 && numer != 0 && numer < denom)
            {
                quint64 const remaining_ms = (static_cast<quint64>(elapsed_ms_) * (denom - numer)) / numer;
                remaining_value_->setText(z7::ui::archive_support::format_hhmmss(remaining_ms / 1000));
            }
            else
            {
                remaining_value_->clear();
            }
        }

        if (files_value_ != nullptr)
        {
            files_value_->setText(QString::number(completed_files_));
        }
        if (files_total_value_ != nullptr)
        {
            if (total_files_ != 0)
            {
                files_total_value_->setText(QStringLiteral("/ %1").arg(total_files_));
                files_total_value_->setVisible(true);
            }
            else
            {
                files_total_value_->clear();
                files_total_value_->setVisible(false);
            }
        }

        bool const has_errors = error_count_ != 0;
        if (errors_label_ != nullptr)
        {
            errors_label_->setVisible(has_errors);
        }
        if (errors_value_ != nullptr)
        {
            errors_value_->setVisible(has_errors);
            errors_value_->setText(QString::number(error_count_));
        }

        if (total_size_value_ != nullptr)
        {
            total_size_value_->setText(total_bytes_ == 0 ? QString()
                                                         : z7::ui::archive_support::format_size_short(total_bytes_));
        }
        std::optional<quint64> processed_size;
        std::optional<quint64> packed_size;
        if (ratio_info_.has_value() &&
            (ratio_info_->input_size_known || ratio_info_->output_size_known))
        {
            if (ratio_info_->compressing_mode)
            {
                if (ratio_info_->input_size_known)
                {
                    processed_size = ratio_info_->input_size;
                }
                if (ratio_info_->output_size_known)
                {
                    packed_size = ratio_info_->output_size;
                }
            }
            else
            {
                if (ratio_info_->output_size_known)
                {
                    processed_size = ratio_info_->output_size;
                }
                if (ratio_info_->input_size_known)
                {
                    packed_size = ratio_info_->input_size;
                }
            }
        }
        else
        {
            processed_size = completed_bytes_;
        }

        if (processed_value_ != nullptr)
        {
            processed_value_->setText(processed_size.has_value()
                                          ? z7::ui::archive_support::format_size_short(*processed_size)
                                          : QString());
        }
        if (packed_value_ != nullptr)
        {
            packed_value_->setText(packed_size.has_value()
                                       ? z7::ui::archive_support::format_size_short(*packed_size)
                                       : QString());
        }
        if (ratio_value_ != nullptr)
        {
            if (!ratio_info_.has_value() ||
                !ratio_info_->input_size_known ||
                !ratio_info_->output_size_known)
            {
                ratio_value_->clear();
            }
            else
            {
                quint64 const unpack_size = ratio_info_->compressing_mode
                                                ? ratio_info_->input_size
                                                : ratio_info_->output_size;
                quint64 const pack_size = ratio_info_->compressing_mode
                                              ? ratio_info_->output_size
                                              : ratio_info_->input_size;
                if (unpack_size == 0)
                {
                    ratio_value_->clear();
                }
                else
                {
                    quint64 const ratio =
                        pack_size <= (std::numeric_limits<quint64>::max() / 100ULL)
                            ? (pack_size * 100ULL) / unpack_size
                            : static_cast<quint64>((static_cast<long double>(pack_size) * 100.0L) /
                                                   static_cast<long double>(unpack_size));
                    ratio_value_->setText(QStringLiteral("%1%").arg(ratio));
                }
            }
        }
        if (speed_value_ != nullptr)
        {
            speed_value_->setText(z7::ui::archive_support::format_speed(completed_bytes_, elapsed_ms_));
        }

        update_title();
    }

    void TaskProgressDialogBase::update_current_path_labels()
    {
        if (current_path_.isEmpty())
        {
            if (current_path_label_ != nullptr)
            {
                current_path_label_->clear();
                current_path_label_->setVisible(false);
            }
            if (current_file_label_ != nullptr)
            {
                current_file_label_->clear();
                current_file_label_->setVisible(false);
            }
            return;
        }

        QString const native = QDir::toNativeSeparators(current_path_);
        int const slash_pos = std::max(native.lastIndexOf(QLatin1Char('/')), native.lastIndexOf(QLatin1Char('\\')));

        if (slash_pos >= 0)
        {
            QString const dir_part = native.left(slash_pos + 1);
            QString const file_part = native.mid(slash_pos + 1);
            if (current_path_label_ != nullptr)
            {
                current_path_label_->setText(dir_part);
                current_path_label_->setVisible(!dir_part.isEmpty());
            }
            if (current_file_label_ != nullptr)
            {
                current_file_label_->setText(file_part);
                current_file_label_->setVisible(!file_part.isEmpty());
            }
            return;
        }

        if (current_path_label_ != nullptr)
        {
            current_path_label_->clear();
            current_path_label_->setVisible(false);
        }
        if (current_file_label_ != nullptr)
        {
            current_file_label_->setText(native);
            current_file_label_->setVisible(!native.isEmpty());
        }
    }

    void TaskProgressDialogBase::parse_progress_log_line(QString const& line)
    {
        QString const trimmed = line.trimmed();
        if (trimmed.isEmpty())
        {
            return;
        }

        static QRegularExpression const kTestingRe(QStringLiteral("^\\s*Testing\\s+(.+?)\\s*$"),
                                                   QRegularExpression::CaseInsensitiveOption);
        static QRegularExpression const kPhysicalSizeRe(QStringLiteral("^\\s*Physical\\s+Size\\s*=\\s*(\\d+)\\s*$"),
                                                        QRegularExpression::CaseInsensitiveOption);
        static QRegularExpression const kFilesRe(QStringLiteral("^\\s*Files\\s*[:=]\\s*(\\d+)\\s*$"),
                                                 QRegularExpression::CaseInsensitiveOption);
        static QRegularExpression const kErrorsRe(QStringLiteral("^\\s*Errors\\s*[:=]\\s*(\\d+)\\s*$"),
                                                  QRegularExpression::CaseInsensitiveOption);

        QRegularExpressionMatch const testing_match = kTestingRe.match(trimmed);
        if (testing_match.hasMatch())
        {
            current_path_ = testing_match.captured(1).trimmed();
            update_current_path_labels();
        }

        if (!behavior_.parse_extended_progress_log)
        {
            return;
        }

        QRegularExpressionMatch const physical_match = kPhysicalSizeRe.match(trimmed);
        if (physical_match.hasMatch())
        {
            bool ok = false;
            quint64 const parsed = physical_match.captured(1).toULongLong(&ok);
            if (ok)
            {
                total_bytes_ = parsed;
                totals_known_ = true;
            }
        }

        QRegularExpressionMatch const files_match = kFilesRe.match(trimmed);
        if (files_match.hasMatch())
        {
            bool ok = false;
            quint64 const parsed = files_match.captured(1).toULongLong(&ok);
            if (ok)
            {
                total_files_ = parsed;
            }
        }

        QRegularExpressionMatch const errors_match = kErrorsRe.match(trimmed);
        if (errors_match.hasMatch())
        {
            bool ok = false;
            quint64 const parsed = errors_match.captured(1).toULongLong(&ok);
            if (ok)
            {
                error_count_ = parsed;
            }
        }
    }

} // namespace z7::ui::runtime_support
