#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <utility>

#include "internal.h"

namespace z7::macos_integration::capi_internal
{
    namespace
    {

        class CallbackDispatcher
        {
        public:
            static CallbackDispatcher& instance()
            {
                static CallbackDispatcher dispatcher;
                return dispatcher;
            }

            void post(std::function<void()> task)
            {
                if (!task)
                {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    queue_.push_back(std::move(task));
                }
                cv_.notify_one();
            }

        private:
            CallbackDispatcher() : worker_([this]() { run(); }) {}

            ~CallbackDispatcher()
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stopping_ = true;
                }
                cv_.notify_one();
                if (worker_.joinable())
                {
                    worker_.join();
                }
            }

            void run()
            {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
                        if (stopping_ && queue_.empty())
                        {
                            break;
                        }
                        task = std::move(queue_.front());
                        queue_.pop_front();
                    }
                    task();
                }
            }

            std::mutex mutex_;
            std::condition_variable cv_;
            std::deque<std::function<void()>> queue_;
            bool stopping_ = false;
            std::thread worker_;
        };

    } // namespace

    uint64_t register_in_flight_task(std::shared_ptr<z7_mi_session_state> const& state,
                                     std::shared_ptr<AsyncTaskState> task)
    {
        if (!state || !task)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        uint64_t const task_id = state->next_task_id++;
        state->in_flight_tasks.emplace(task_id, std::move(task));
        return task_id;
    }

    void unregister_in_flight_task(std::weak_ptr<z7_mi_session_state> const& weak_state, uint64_t task_id)
    {
        auto const state = weak_state.lock();
        if (!state || task_id == 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        state->in_flight_tasks.erase(task_id);
    }

    void unregister_in_flight_task(std::shared_ptr<z7_mi_session_state> const& state, uint64_t task_id)
    {
        unregister_in_flight_task(std::weak_ptr<z7_mi_session_state>(state), task_id);
    }

    std::shared_ptr<AsyncTaskState> lookup_in_flight_task(std::shared_ptr<z7_mi_session_state> const& state,
                                                          uint64_t task_id)
    {
        if (!state || task_id == 0)
        {
            return {};
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        auto const it = state->in_flight_tasks.find(task_id);
        if (it == state->in_flight_tasks.end())
        {
            return {};
        }
        return it->second;
    }

    void set_active_archive_session(std::shared_ptr<AsyncTaskState> const& task_state,
                                    z7::app::ArchiveSession const& session)
    {
        if (!task_state)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(task_state->archive_session_mutex);
        task_state->archive_session = session;
    }

    void clear_active_archive_session(std::shared_ptr<AsyncTaskState> const& task_state)
    {
        if (!task_state)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(task_state->archive_session_mutex);
        task_state->archive_session = z7::app::ArchiveSession{};
    }

    void cancel_active_archive_session(std::shared_ptr<AsyncTaskState> const& task_state)
    {
        if (!task_state)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(task_state->archive_session_mutex);
        if (task_state->archive_session.valid())
        {
            task_state->archive_session.cancel();
        }
    }

    void mark_task_completed(std::shared_ptr<AsyncTaskState> const& task_state)
    {
        if (!task_state)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(task_state->completion_mutex);
            task_state->completed.store(true);
        }
        task_state->completion_cv.notify_all();
    }

    void wait_for_task_completion(std::shared_ptr<AsyncTaskState> const& task_state)
    {
        if (!task_state || task_state->completed.load())
        {
            return;
        }
        std::unique_lock<std::mutex> lock(task_state->completion_mutex);
        task_state->completion_cv.wait(lock, [task_state]() { return task_state->completed.load(); });
    }

    bool report_once(std::shared_ptr<AsyncTaskState> task_state)
    {
        if (!task_state)
        {
            return true;
        }
        bool expected = false;
        return task_state->callback_dispatched.compare_exchange_strong(expected, true);
    }

    void dispatch_result_closure(std::function<void()> closure)
    {
        CallbackDispatcher::instance().post(std::move(closure));
    }

} // namespace z7::macos_integration::capi_internal
