#include <QCloseEvent>
#include <QMessageBox>

#include "official_lang_catalog.h"
#include "task_progress_dialog_base.h"

namespace z7::ui::runtime_support
{

    void TaskProgressDialogBase::on_cancel_clicked()
    {
        if (!running_)
        {
            emit cancel_requested();
            return;
        }

        if (behavior_.confirm_cancel_only_for_test_mode && !test_mode_)
        {
            emit cancel_requested();
            return;
        }

        bool const was_paused = paused_;
        bool const apply_temp_pause = !was_paused && pause_available_;
        if (apply_temp_pause)
        {
            set_paused(true);
            emit pause_requested();
        }

        QMessageBox::StandardButton answer = QMessageBox::Cancel;
        if (cancel_confirmation_handler_)
        {
            answer = static_cast<QMessageBox::StandardButton>(cancel_confirmation_handler_());
        }
        else
        {
            QMessageBox cancel_box(this);
            QString const cancel_title = display_stage_text();
            cancel_box.setWindowTitle(cancel_title.isEmpty() ? strip_mnemonic(L(3304)) : cancel_title);
            cancel_box.setText(strip_mnemonic(L(448)));
            cancel_box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            cancel_box.setDefaultButton(QMessageBox::Yes);

            cancel_box.exec();
            answer = cancel_box.standardButton(cancel_box.clickedButton());
            if (answer == QMessageBox::NoButton)
            {
                answer = static_cast<QMessageBox::StandardButton>(cancel_box.result());
            }
        }

        if (apply_temp_pause)
        {
            set_paused(false);
            emit resume_requested();
        }

        if (answer == QMessageBox::Yes)
        {
            emit cancel_requested();
        }
    }

    void TaskProgressDialogBase::closeEvent(QCloseEvent* event)
    {
        if (behavior_.running_close_requests_cancel && running_)
        {
            on_cancel_clicked();
            if (event != nullptr)
            {
                event->ignore();
            }
            return;
        }
        QDialog::closeEvent(event);
    }

    void TaskProgressDialogBase::reject()
    {
        if (behavior_.running_close_requests_cancel && running_)
        {
            on_cancel_clicked();
            return;
        }
        QDialog::reject();
    }

} // namespace z7::ui::runtime_support
