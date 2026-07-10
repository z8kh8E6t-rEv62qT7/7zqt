#include "delayed_progress_dialog_presenter.h"

#include <QTimer>
#include <utility>

#include "task_progress_dialog_base.h"

namespace z7::ui::runtime_support {

    struct DelayedProgressDialogPresenter::State {
        QPointer<TaskProgressDialogBase> dialog;
        ShowCallback show_callback;
        quint64 generation = 0;
        bool pending = false;
        bool shown = false;
    };

    DelayedProgressDialogPresenter::DelayedProgressDialogPresenter(TaskProgressDialogBase* dialog) :
        state_(std::make_shared<State>()) {
        state_->dialog = dialog;
    }

    DelayedProgressDialogPresenter::~DelayedProgressDialogPresenter() {
        cancel_pending();
    }

    void DelayedProgressDialogPresenter::set_dialog(TaskProgressDialogBase* dialog) {
        state_->dialog = dialog;
        if (dialog == nullptr) {
            cancel_pending();
        }
    }

    void DelayedProgressDialogPresenter::set_show_callback(ShowCallback callback) {
        state_->show_callback = std::move(callback);
    }

    void DelayedProgressDialogPresenter::schedule(QObject* context, int delay_msecs) {
        if (context == nullptr || state_->dialog.isNull()) {
            return;
        }

        state_->pending = true;
        quint64 const generation = ++state_->generation;
        std::shared_ptr<State> const state = state_;
        QTimer::singleShot(delay_msecs, context, [state, generation]() {
            if (!state->pending || state->generation != generation) {
                return;
            }
            state->pending = false;
            show_state(state);
        });
    }

    void DelayedProgressDialogPresenter::show_now() {
        state_->pending = false;
        ++state_->generation;
        show_state(state_);
    }

    void DelayedProgressDialogPresenter::cancel_pending() {
        state_->pending = false;
        ++state_->generation;
    }

    bool DelayedProgressDialogPresenter::was_shown() const {
        return state_->shown;
    }

    bool DelayedProgressDialogPresenter::is_pending() const {
        return state_->pending;
    }

    void DelayedProgressDialogPresenter::show_state(std::shared_ptr<State> const& state) {
        if (state == nullptr || state->dialog.isNull()) {
            return;
        }

        state->shown = true;
        if (state->show_callback) {
            state->show_callback(state->dialog.data());
            return;
        }
        state->dialog->show();
    }

} // namespace z7::ui::runtime_support
