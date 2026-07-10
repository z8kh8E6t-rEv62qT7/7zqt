#pragma once

#include <QPointer>
#include <functional>
#include <memory>

class QObject;

namespace z7::ui::runtime_support {

    class TaskProgressDialogBase;

    inline constexpr int kOriginal7ZipProgressDialogDelayMs = 500;

    class DelayedProgressDialogPresenter final {
    public:
        using ShowCallback = std::function<void(TaskProgressDialogBase*)>;

        explicit DelayedProgressDialogPresenter(TaskProgressDialogBase* dialog = nullptr);
        ~DelayedProgressDialogPresenter();

        DelayedProgressDialogPresenter(DelayedProgressDialogPresenter const&) = delete;
        DelayedProgressDialogPresenter& operator=(DelayedProgressDialogPresenter const&) = delete;

        void set_dialog(TaskProgressDialogBase* dialog);
        void set_show_callback(ShowCallback callback);

        void schedule(QObject* context, int delay_msecs = kOriginal7ZipProgressDialogDelayMs);
        void show_now();
        void cancel_pending();

        bool was_shown() const;
        bool is_pending() const;

    private:
        struct State;

        static void show_state(std::shared_ptr<State> const& state);

        std::shared_ptr<State> state_;
    };

} // namespace z7::ui::runtime_support
