#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QVector>
#include <chrono>
#include <utility>
#include <vector>

#include "internal.h"
#include "password_relay_delegate.h"
#include "archive_suffix_catalog.h"
#include "common/archive_type_normalization.h"

namespace z7::macos_integration::capi_internal
{
    QString normalize_virtual_entry_path(QString const& entry)
    {
        QString out = QDir::fromNativeSeparators(entry.trimmed());
        while (out.startsWith(QLatin1Char('/')))
        {
            out.remove(0, 1);
        }
        while (out.endsWith(QLatin1Char('/')))
        {
            out.chop(1);
        }
        while (out.contains(QStringLiteral("//")))
        {
            out.replace(QStringLiteral("//"), QStringLiteral("/"));
        }
        return out;
    }

    QString join_virtual_dir_with_name(QString const& virtual_dir, QString const& name)
    {
        QString const left = normalize_virtual_entry_path(virtual_dir);
        QString const right = normalize_virtual_entry_path(name);
        if (left.isEmpty())
        {
            return right;
        }
        if (right.isEmpty())
        {
            return left;
        }
        return left + QLatin1Char('/') + right;
    }

    QString infer_archive_format(QString const& archive_path, QString const& type_hint)
    {
        QString const trimmed_hint = type_hint.trimmed();
        if (!trimmed_hint.isEmpty())
        {
            return QString::fromStdString(
                z7::common::normalize_archive_type_token_copy(
                    trimmed_hint.toStdString()));
        }
        std::string const suffix =
            QFileInfo(archive_path).suffix().toLower().toStdString();
        std::string const format =
            z7::common::canonical_archive_type_from_filename_suffix_copy(suffix);
        if (!format.empty())
        {
            return QString::fromStdString(format);
        }
        return QStringLiteral("7z");
    }

    QString effective_archive_type_key(QString const& archive_path,
                                       QString const& type_hint)
    {
        return infer_archive_format(archive_path, type_hint).trimmed().toLower();
    }

    QStringList normalize_nested_archive_entries(char const* const* values, size_t count, QString* error_message)
    {
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        QStringList out;
        out.reserve(static_cast<int>(count));
        if (count == 0)
        {
            return out;
        }
        if (values == nullptr)
        {
            if (error_message != nullptr)
            {
                *error_message = QStringLiteral("nested archive entries pointer is null.");
            }
            return {};
        }
        for (size_t i = 0; i < count; ++i)
        {
            QString const original = to_qstring(values[i]);
            if (original.isEmpty())
            {
                if (error_message != nullptr)
                {
                    *error_message = QStringLiteral("nested archive entry %1 is empty.").arg(i);
                }
                return {};
            }
            QString const native = QDir::fromNativeSeparators(original);
            if (QDir::isAbsolutePath(native))
            {
                if (error_message != nullptr)
                {
                    *error_message = QStringLiteral("nested archive entry %1 must be relative.").arg(i);
                }
                return {};
            }
            QString const normalized = normalize_virtual_entry_path(native);
            if (normalized.isEmpty())
            {
                if (error_message != nullptr)
                {
                    *error_message = QStringLiteral("nested archive entry %1 is invalid.").arg(i);
                }
                return {};
            }
            out.push_back(normalized);
        }
        return out;
    }

    QString nested_session_cache_key(QString const& archive_path,
                                     QString const& effective_archive_type,
                                     QStringList const& nested_archive_entries)
    {
        return QStringLiteral("%1|%2|%3")
            .arg(QFileInfo(archive_path).absoluteFilePath(),
                 effective_archive_type,
                 nested_archive_entries.join(QStringLiteral("::")));
    }

    bool is_archive_like_by_name(QString const& name)
    {
        return z7::ui::common::archive_name_has_known_suffix(name);
    }

    QString format_nested_error_message(size_t level, QString const& reason)
    {
        QString const detail =
            reason.trimmed().isEmpty() ? QStringLiteral("Unknown nested archive error.") : reason.trimmed();
        return QStringLiteral("nested level %1: %2").arg(level).arg(detail);
    }

    z7_mi_status_t open_nested_session_chain(std::shared_ptr<z7_mi_session_state> const& state,
                                             QString const& archive_path,
                                             QString const& archive_type_hint,
                                             QStringList const& nested_archive_entries,
                                             std::shared_ptr<AsyncTaskState> const& task_state,
                                             std::shared_ptr<NestedSessionChain>* out_chain,
                                             QString* error_message)
    {
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        if (out_chain == nullptr)
        {
            if (error_message != nullptr)
            {
                *error_message = QStringLiteral("Nested session output is null.");
            }
            return Z7_MI_STATUS_INTERNAL_ERROR;
        }
        out_chain->reset();

        auto prune_cache = [state]()
        {
            if (!state)
            {
                return;
            }
            static constexpr auto kTtl = std::chrono::seconds(30);
            static constexpr int kCapacity = 4;
            auto const now = std::chrono::steady_clock::now();
            QVector<QString> keys_to_remove;
            std::vector<std::shared_ptr<NestedSessionChain>> removed;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                for (auto it = state->nested_session_cache.begin(); it != state->nested_session_cache.end(); ++it)
                {
                    auto const& chain = it.value();
                    if (!chain)
                    {
                        keys_to_remove.push_back(it.key());
                        continue;
                    }
                    if (chain->active_users.load() == 0 && now - chain->last_used > kTtl)
                    {
                        chain->cached.store(false);
                        keys_to_remove.push_back(it.key());
                        removed.push_back(chain);
                    }
                }
                for (QString const& key : keys_to_remove)
                {
                    state->nested_session_cache.remove(key);
                }
                while (state->nested_session_cache.size() > kCapacity)
                {
                    auto oldest = state->nested_session_cache.end();
                    for (auto it = state->nested_session_cache.begin(); it != state->nested_session_cache.end(); ++it)
                    {
                        auto const& chain = it.value();
                        if (!chain || chain->active_users.load() != 0)
                        {
                            continue;
                        }
                        if (oldest == state->nested_session_cache.end() || chain->last_used < oldest.value()->last_used)
                        {
                            oldest = it;
                        }
                    }
                    if (oldest == state->nested_session_cache.end())
                    {
                        break;
                    }
                    oldest.value()->cached.store(false);
                    removed.push_back(oldest.value());
                    state->nested_session_cache.erase(oldest);
                }
            }
            for (auto const& chain : removed)
            {
                close_nested_session_chain(chain);
            }
        };

        QString const effective_root_archive_type =
            effective_archive_type_key(archive_path, archive_type_hint);

        if (!nested_archive_entries.isEmpty() && state)
        {
            prune_cache();
            QString const cache_key = nested_session_cache_key(
                archive_path, effective_root_archive_type, nested_archive_entries);
            std::shared_ptr<NestedSessionChain> cached_chain;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                auto const it = state->nested_session_cache.constFind(cache_key);
                if (it != state->nested_session_cache.cend())
                {
                    cached_chain = it.value();
                    if (cached_chain)
                    {
                        cached_chain->active_users.fetch_add(1);
                        cached_chain->last_used = std::chrono::steady_clock::now();
                    }
                }
            }
            if (cached_chain)
            {
                *out_chain = std::move(cached_chain);
                return Z7_MI_STATUS_OK;
            }
        }

        if (task_state && task_state->cancel_requested.load())
        {
            if (error_message != nullptr)
            {
                *error_message = QStringLiteral("Operation canceled.");
            }
            return Z7_MI_STATUS_CANCELED;
        }

        z7::app::OpenArchiveFromPathRequest open_root_request;
        open_root_request.archive_path = z7::ui::archive_support::to_native_string(archive_path);
        open_root_request.archive_type_hint = z7::ui::archive_support::to_native_string(archive_type_hint);
        auto root_delegate = make_quicklook_password_relay_delegate(
            state,
            archive_path,
            effective_root_archive_type,
            QStringList{},
            task_state);
        z7::app::OperationOutcome open_root_outcome;
        int root_attempt = 0;
        do
        {
            open_root_outcome =
                run_archive_request_sync(z7::app::ArchiveRequest{open_root_request}, task_state, root_delegate);
            ++root_attempt;
        }
        while (open_root_outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword
               && task_state
               && !task_state->password_prompt_canceled.load()
               && !task_state->password_prompt_missing_callback.load()
               && root_attempt < 3);
        if (task_state && should_report_canceled(open_root_outcome, task_state->cancel_requested.load()))
        {
            if (error_message != nullptr)
            {
                *error_message = canceled_message(open_root_outcome);
            }
            return Z7_MI_STATUS_CANCELED;
        }
        auto const root_payload = z7::app::outcome_payload_as<z7::app::OpenArchiveSessionResult>(open_root_outcome);
        if (!root_payload.has_value() || !root_payload->ok || !root_payload->token.is_valid())
        {
            if (error_message != nullptr)
            {
                *error_message = format_nested_error_message(
                    0,
                    fallback_archive_error_summary(open_root_outcome));
            }
            if (open_root_outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword
                && task_state
                && task_state->password_prompt_canceled.load())
            {
                return Z7_MI_STATUS_PASSWORD_REQUIRED;
            }
            return Z7_MI_STATUS_BACKEND_ERROR;
        }
        auto chain = std::make_shared<NestedSessionChain>();
        chain->active_users.store(1);
        chain->last_used = std::chrono::steady_clock::now();
        chain->tokens.push_back(root_payload->token);

        for (int i = 0; i < nested_archive_entries.size(); ++i)
        {
            if (task_state && task_state->cancel_requested.load())
            {
                if (error_message != nullptr)
                {
                    *error_message = QStringLiteral("Operation canceled.");
                }
                close_nested_session_chain(chain);
                return Z7_MI_STATUS_CANCELED;
            }

            QString const& nested_entry = nested_archive_entries.at(i);
            z7::app::OpenArchiveFromParentRequest open_child_request;
            open_child_request.parent = chain->tokens.back();
            open_child_request.entry_path = z7::ui::archive_support::to_utf8_string(nested_entry);
            open_child_request.archive_type_hint =
                z7::ui::archive_support::to_native_string(infer_archive_format(nested_entry, QString()));
            open_child_request.display_path_hint = z7::ui::archive_support::to_utf8_string(nested_entry);
            QStringList const child_chain = nested_archive_entries.mid(0, i + 1);
            auto child_delegate = make_quicklook_password_relay_delegate(
                state,
                archive_path,
                effective_root_archive_type,
                child_chain,
                task_state);
            z7::app::OperationOutcome open_child_outcome;
            int child_attempt = 0;
            do
            {
                open_child_outcome =
                    run_archive_request_sync(z7::app::ArchiveRequest{open_child_request}, task_state, child_delegate);
                ++child_attempt;
            }
            while (open_child_outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword
                   && task_state
                   && !task_state->password_prompt_canceled.load()
                   && !task_state->password_prompt_missing_callback.load()
                   && child_attempt < 3);
            if (task_state && should_report_canceled(open_child_outcome, task_state->cancel_requested.load()))
            {
                if (error_message != nullptr)
                {
                    *error_message = canceled_message(open_child_outcome);
                }
                close_nested_session_chain(chain);
                return Z7_MI_STATUS_CANCELED;
            }
            auto const child_payload =
                z7::app::outcome_payload_as<z7::app::OpenArchiveSessionResult>(open_child_outcome);
            if (!child_payload.has_value() || !child_payload->ok || !child_payload->token.is_valid())
            {
                if (error_message != nullptr)
                {
                    *error_message = format_nested_error_message(
                        static_cast<size_t>(i + 1),
                        fallback_archive_error_summary(open_child_outcome));
                }
                if (open_child_outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword
                    && task_state
                    && task_state->password_prompt_canceled.load())
                {
                    close_nested_session_chain(chain);
                    return Z7_MI_STATUS_PASSWORD_REQUIRED;
                }
                close_nested_session_chain(chain);
                return Z7_MI_STATUS_BACKEND_ERROR;
            }
            chain->tokens.push_back(child_payload->token);
        }

        if (!nested_archive_entries.isEmpty() && state)
        {
            QString const cache_key = nested_session_cache_key(
                archive_path, effective_root_archive_type, nested_archive_entries);
            chain->cached.store(true);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->nested_session_cache.insert(cache_key, chain);
            }
            prune_cache();
        }

        *out_chain = std::move(chain);
        return Z7_MI_STATUS_OK;
    }

    void release_nested_session_chain(std::shared_ptr<z7_mi_session_state> const& state,
                                      std::shared_ptr<NestedSessionChain> const& chain)
    {
        if (!chain)
        {
            return;
        }
        chain->last_used = std::chrono::steady_clock::now();
        uint32_t const previous = chain->active_users.fetch_sub(1);
        if (previous <= 1 && !chain->cached.load())
        {
            close_nested_session_chain(chain);
            return;
        }
        if (previous <= 1 && state)
        {
            std::vector<std::shared_ptr<NestedSessionChain>> removed;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                for (auto it = state->nested_session_cache.begin(); it != state->nested_session_cache.end();)
                {
                    auto const& cached_chain = it.value();
                    if (cached_chain && cached_chain->cached.load())
                    {
                        ++it;
                        continue;
                    }
                    if (cached_chain)
                    {
                        removed.push_back(cached_chain);
                    }
                    it = state->nested_session_cache.erase(it);
                }
            }
            for (auto const& removed_chain : removed)
            {
                close_nested_session_chain(removed_chain);
            }
        }
    }

    void close_nested_session_chain(std::shared_ptr<NestedSessionChain> const& chain)
    {
        if (!chain)
        {
            return;
        }
        if (chain->closed.exchange(true))
        {
            return;
        }
        chain->cached.store(false);
        std::vector<z7::app::ArchiveSessionToken> tokens = std::move(chain->tokens);
        chain->tokens.clear();
        for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
        {
            if (!it->is_valid())
            {
                continue;
            }
            z7::app::CloseArchiveSessionRequest close_request;
            close_request.token = *it;
            (void)run_archive_request_sync(z7::app::ArchiveRequest{close_request}, nullptr);
        }
    }

    std::vector<std::shared_ptr<NestedSessionChain>>
    take_nested_session_cache_for_destroy(std::shared_ptr<z7_mi_session_state> const& state)
    {
        std::vector<std::shared_ptr<NestedSessionChain>> chains;
        if (!state)
        {
            return chains;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        chains.reserve(static_cast<size_t>(state->nested_session_cache.size()));
        for (auto it = state->nested_session_cache.begin(); it != state->nested_session_cache.end(); ++it)
        {
            if (it.value())
            {
                it.value()->cached.store(false);
                chains.push_back(it.value());
            }
        }
        state->nested_session_cache.clear();
        return chains;
    }

} // namespace z7::macos_integration::capi_internal
