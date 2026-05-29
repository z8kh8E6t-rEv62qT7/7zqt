#include "task_progress_dialog_base.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractItemView>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QHeaderView>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "official_lang_catalog.h"

namespace z7::ui::runtime_support
{

    namespace
    {

        void set_value_alignment(QLabel* label)
        {
            if (label == nullptr)
            {
                return;
            }
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }

        void set_widget_object_name(QWidget* widget, char const* object_name)
        {
#ifdef Z7_TESTING
            if (widget == nullptr || object_name == nullptr)
            {
                return;
            }
            widget->setObjectName(QString::fromLatin1(object_name));
#else
            Q_UNUSED(widget);
            Q_UNUSED(object_name);
#endif
        }

    } // namespace

    TaskProgressDialogBase::TaskProgressDialogBase(TaskProgressDialogBehavior const& behavior, QWidget* parent) :
        QDialog(parent), behavior_(behavior)
    {
        set_widget_object_name(this, behavior_.dialog_object_name);
        setWindowTitle(strip_mnemonic(L(3304)));
        resize(behavior_.initial_width, behavior_.initial_height);
        setModal(behavior_.modal);
        if (behavior_.delete_on_close)
        {
            setAttribute(Qt::WA_DeleteOnClose, true);
        }

        auto* layout = new QVBoxLayout(this);

        metrics_widget_ = new QWidget(this);
        auto* metrics_layout = new QGridLayout(metrics_widget_);
        metrics_layout->setHorizontalSpacing(18);
        metrics_layout->setVerticalSpacing(6);

        int row = 0;
        metrics_layout->addWidget(new QLabel(strip_mnemonic(L(3900)), metrics_widget_), row, 0);
        elapsed_value_ = new QLabel(QStringLiteral("00:00:00"), metrics_widget_);
        set_value_alignment(elapsed_value_);
        metrics_layout->addWidget(elapsed_value_, row, 1);
        metrics_layout->addWidget(new QLabel(strip_mnemonic(L(3902)), metrics_widget_), row, 2);
        total_size_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(total_size_value_);
        metrics_layout->addWidget(total_size_value_, row, 3);

        ++row;
        metrics_layout->addWidget(new QLabel(strip_mnemonic(L(3901)), metrics_widget_), row, 0);
        remaining_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(remaining_value_);
        metrics_layout->addWidget(remaining_value_, row, 1);
        metrics_layout->addWidget(new QLabel(strip_mnemonic(L(3903)), metrics_widget_), row, 2);
        speed_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(speed_value_);
        metrics_layout->addWidget(speed_value_, row, 3);

        ++row;
        metrics_layout->addWidget(new QLabel(label_text_with_optional_colon(L(1032)), metrics_widget_), row, 0);
        files_value_ = new QLabel(QStringLiteral("0"), metrics_widget_);
        set_value_alignment(files_value_);
        metrics_layout->addWidget(files_value_, row, 1);
        metrics_layout->addWidget(new QLabel(strip_mnemonic(L(3904)), metrics_widget_), row, 2);
        processed_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(processed_value_);
        metrics_layout->addWidget(processed_value_, row, 3);

        ++row;
        files_total_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(files_total_value_);
        metrics_layout->addWidget(files_total_value_, row, 1);
        metrics_layout->addWidget(new QLabel(label_text_with_optional_colon(L(1008)), metrics_widget_), row, 2);
        packed_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(packed_value_);
        metrics_layout->addWidget(packed_value_, row, 3);

        ++row;
        errors_label_ = new QLabel(strip_mnemonic(L(3906)), metrics_widget_);
        metrics_layout->addWidget(errors_label_, row, 0);
        errors_value_ = new QLabel(QStringLiteral("0"), metrics_widget_);
        set_value_alignment(errors_value_);
        metrics_layout->addWidget(errors_value_, row, 1);
        metrics_layout->addWidget(new QLabel(strip_mnemonic(L(3905)), metrics_widget_), row, 2);
        ratio_value_ = new QLabel(QString(), metrics_widget_);
        set_value_alignment(ratio_value_);
        metrics_layout->addWidget(ratio_value_, row, 3);

        layout->addWidget(metrics_widget_);

        errors_label_->setVisible(false);
        errors_value_->setVisible(false);

        status_label_ = new QLabel(QString(), this);
        status_label_->setWordWrap(true);
        status_label_->setVisible(false);
        layout->addWidget(status_label_);

        current_path_label_ = new QLabel(QString(), this);
        current_path_label_->setWordWrap(true);
        current_path_label_->setVisible(false);
        layout->addWidget(current_path_label_);

        current_file_label_ = new QLabel(QString(), this);
        current_file_label_->setWordWrap(true);
        current_file_label_->setVisible(false);
        layout->addWidget(current_file_label_);

        progress_bar_ = new QProgressBar(this);
        progress_bar_->setRange(0, 0);
        progress_bar_->setTextVisible(true);
        layout->addWidget(progress_bar_);

        log_view_ = new QPlainTextEdit(this);
        log_view_->setReadOnly(true);
        log_view_->setVisible(false);
        layout->addWidget(log_view_, 1);

        result_messages_view_ = new QTableWidget(this);
        set_widget_object_name(result_messages_view_, behavior_.result_messages_view_object_name);
        result_messages_view_->setColumnCount(2);
        result_messages_view_->setHorizontalHeaderLabels({QString(), QString()});
        result_messages_view_->horizontalHeader()->setVisible(false);
        result_messages_view_->verticalHeader()->setVisible(false);
        result_messages_view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        result_messages_view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        result_messages_view_->setSelectionMode(QAbstractItemView::NoSelection);
        result_messages_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        result_messages_view_->setShowGrid(false);
        result_messages_view_->setTextElideMode(Qt::ElideNone);
        result_messages_view_->setWordWrap(false);
        result_messages_view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        result_messages_view_->verticalHeader()->setDefaultSectionSize(
            result_messages_view_->fontMetrics().lineSpacing() + 8);
        result_messages_view_->setVisible(false);
        layout->addWidget(result_messages_view_, 1);

        auto* buttons_layout = new QHBoxLayout();
        buttons_layout->addStretch(1);

        background_button_ = new QPushButton(L(444), this);
        set_widget_object_name(background_button_, behavior_.background_button_object_name);
        buttons_layout->addWidget(background_button_);

        pause_button_ = new QPushButton(L(446), this);
        set_widget_object_name(pause_button_, behavior_.pause_button_object_name);
        buttons_layout->addWidget(pause_button_);

        cancel_button_ = new QPushButton(L(402), this);
        set_widget_object_name(cancel_button_, behavior_.cancel_button_object_name);
        buttons_layout->addWidget(cancel_button_);

        close_button_ = new QPushButton(L(401), this);
        set_widget_object_name(close_button_, behavior_.close_button_object_name);
        close_button_->setVisible(false);
        close_button_->setEnabled(false);
        buttons_layout->addWidget(close_button_);

        connect(background_button_,
                &QPushButton::clicked,
                this,
                [this]()
                {
                    set_backgrounded(!backgrounded_);
                    emit background_requested(backgrounded_);
                });
        connect(pause_button_,
                &QPushButton::clicked,
                this,
                [this]()
                {
                    if (!pause_available_)
                    {
                        return;
                    }
                    set_paused(!paused_);
                    if (paused_)
                    {
                        emit pause_requested();
                    }
                    else
                    {
                        emit resume_requested();
                    }
                });
        connect(cancel_button_, &QPushButton::clicked, this, &TaskProgressDialogBase::on_cancel_clicked);
        connect(close_button_, &QPushButton::clicked, this, &TaskProgressDialogBase::accept);

        layout->addLayout(buttons_layout);

        refresh_timer_ = new QTimer(this);
        refresh_timer_->setInterval(250);
        connect(refresh_timer_, &QTimer::timeout, this, &TaskProgressDialogBase::refresh_metrics);
    }

    QString TaskProgressDialogBase::label_text_with_optional_colon(QString const& text) const
    {
        QString const stripped = strip_mnemonic(text);
        if (!behavior_.normalize_metric_label_colons)
        {
            return stripped + QLatin1Char(':');
        }
        return stripped.endsWith(QLatin1Char(':')) ? stripped : stripped + QLatin1Char(':');
    }

} // namespace z7::ui::runtime_support
