// src/ui/filemanager/src/main_window/state/state_panel_archive.cpp
// Role: PanelController archive-view state and nested archive transitions.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager
{

    bool MainWindow::PanelController::in_archive_view() const
    {
        return archive.view_enabled && !archive.source_archive.isEmpty();
    }

    QString MainWindow::PanelController::current_directory() const
    {
        if (in_archive_view())
        {
            if (archive.virtual_dir.isEmpty())
            {
                return QStringLiteral("/");
            }
            return QStringLiteral("/") + archive.virtual_dir;
        }
        return model != nullptr ? model->directory() : QString();
    }

    QString MainWindow::PanelController::archive_display_source() const
    {
        return archive.virtual_display_source.isEmpty() ? archive.source_archive : archive.virtual_display_source;
    }

    QString MainWindow::PanelController::favorite_folder_prefix() const
    {
        if (!in_archive_view())
        {
            return current_directory();
        }
        return archive_virtual_display_path();
    }

    void MainWindow::PanelController::enter_archive_view(QString const& source_archive,
                                                         QString const& normalized_virtual_dir,
                                                         QString const& origin_dir,
                                                         QString const& trimmed_type_hint,
                                                         bool update_origin_dir)
    {
        archive.view_enabled = true;
        archive.source_archive = source_archive;
        archive.virtual_dir = normalized_virtual_dir;
        if (archive.virtual_display_source.isEmpty())
        {
            archive.virtual_display_source = source_archive;
        }
        archive.type_hint = trimmed_type_hint;
        if (update_origin_dir || archive.origin_dir.isEmpty())
        {
            archive.origin_dir =
                origin_dir.isEmpty() ? QFileInfo(source_archive).absolutePath() : QDir(origin_dir).absolutePath();
        }
        if (model != nullptr)
        {
            quint64 const drag_session_token_value = archive.current_token.is_valid() ? archive.current_token.value : 0;
            model->set_archive_drag_source(archive.source_archive, archive.type_hint, drag_session_token_value);
        }
    }

    MainWindow::PanelController::ArchiveState::ParentContext
    MainWindow::PanelController::current_archive_parent_context() const
    {
        ArchiveState::ParentContext parent;
        parent.archive_path = archive.source_archive;
        parent.archive_entry_from_parent = archive.archive_entry_from_parent;
        parent.virtual_display_source = archive.virtual_display_source;
        parent.virtual_dir = archive.virtual_dir;
        parent.origin_dir = archive.origin_dir;
        parent.type_hint = archive.type_hint;
        parent.temp_session = archive.temp_session;
        parent.session_token = archive.current_token;
        return parent;
    }

    void MainWindow::PanelController::push_current_archive_to_parent_stack()
    {
        archive.parent_stack.push_back(current_archive_parent_context());
    }

    bool MainWindow::PanelController::discard_last_parent_archive_frame()
    {
        if (archive.parent_stack.isEmpty())
        {
            return false;
        }
        archive.parent_stack.removeLast();
        return true;
    }

    std::optional<MainWindow::PanelController::ParentArchiveReturnTransition>
    MainWindow::PanelController::begin_return_to_parent_archive()
    {
        if (archive.parent_stack.isEmpty())
        {
            return std::nullopt;
        }

        ParentArchiveReturnTransition transition;
        transition.parent = archive.parent_stack.back();
        transition.leaving_archive_entry_from_parent = archive.archive_entry_from_parent;
        transition.leaving_temp_session = archive.temp_session;
        transition.leaving_token = archive.current_token;
        archive.parent_stack.removeLast();
        return transition;
    }

    void MainWindow::PanelController::commit_return_to_parent_archive(ParentArchiveReturnTransition const& transition)
    {
        archive.archive_entry_from_parent = transition.parent.archive_entry_from_parent;
        archive.temp_session = transition.parent.temp_session;
    }

    void MainWindow::PanelController::rollback_return_to_parent_archive(ParentArchiveReturnTransition const& transition)
    {
        archive.parent_stack.push_back(transition.parent);
        archive.archive_entry_from_parent = transition.leaving_archive_entry_from_parent;
        archive.temp_session = transition.leaving_temp_session;
    }

    MainWindow::PanelController::ArchiveFilesystemExitTransition
    MainWindow::PanelController::begin_exit_archive_view_to_filesystem(
        QString const& target_directory,
        std::function<void(QSharedPointer<ArchiveTempSession> const&)> const& release_temp_session)
    {
        ArchiveFilesystemExitTransition transition;
        if (!target_directory.trimmed().isEmpty())
        {
            transition.target_directory = QDir(target_directory).absolutePath();
        }
        transition.refresh_directory_after_exit = transition.target_directory.isEmpty();

        clear_archive_view_state(
            release_temp_session,
            [&transition](z7::app::ArchiveSessionToken token)
            {
                if (token.is_valid())
                {
                    transition.tokens_to_close.push_back(token);
                }
            });
        return transition;
    }

    void MainWindow::PanelController::clear_archive_view_state(
        std::function<void(QSharedPointer<ArchiveTempSession> const&)> const& release_temp_session,
        std::function<void(z7::app::ArchiveSessionToken)> const& close_session)
    {
        if (release_temp_session)
        {
            if (archive.temp_session != nullptr)
            {
                release_temp_session(archive.temp_session);
            }
            for (ArchiveState::ParentContext const& parent_ctx : archive.parent_stack)
            {
                if (parent_ctx.temp_session != nullptr)
                {
                    release_temp_session(parent_ctx.temp_session);
                }
            }
        }
        if (close_session)
        {
            if (archive.current_token.is_valid())
            {
                close_session(archive.current_token);
            }
            for (ArchiveState::ParentContext const& parent_ctx : archive.parent_stack)
            {
                if (parent_ctx.session_token.is_valid())
                {
                    close_session(parent_ctx.session_token);
                }
            }
        }

        archive.view_enabled = false;
        archive.virtual_dir.clear();
        archive.source_archive.clear();
        archive.archive_entry_from_parent.clear();
        archive.virtual_display_source.clear();
        archive.origin_dir.clear();
        archive.type_hint.clear();
        archive.parent_stack.clear();
        archive.temp_session.clear();
        archive.current_token = {};
        if (model != nullptr)
        {
            model->clear_archive_drag_source();
        }
    }

    QString MainWindow::PanelController::archive_virtual_display_path() const
    {
        if (!in_archive_view())
        {
            return QDir::toNativeSeparators(current_directory());
        }
        return z7::ui::archive_support::virtual_display_path(
            archive_display_source(), z7::ui::archive_support::normalize_virtual_dir(current_directory()));
    }

    void MainWindow::PanelController::reset_archive_state_for_rebase(
        QString const& source_archive,
        std::function<void(QSharedPointer<ArchiveTempSession> const&)> const& release_temp_session,
        std::function<void(z7::app::ArchiveSessionToken)> const& close_session)
    {
        if (release_temp_session)
        {
            if (archive.temp_session != nullptr)
            {
                release_temp_session(archive.temp_session);
            }
            for (ArchiveState::ParentContext const& parent_ctx : archive.parent_stack)
            {
                if (parent_ctx.temp_session != nullptr)
                {
                    release_temp_session(parent_ctx.temp_session);
                }
            }
        }
        if (close_session)
        {
            if (archive.current_token.is_valid())
            {
                close_session(archive.current_token);
            }
            for (ArchiveState::ParentContext const& parent_ctx : archive.parent_stack)
            {
                if (parent_ctx.session_token.is_valid())
                {
                    close_session(parent_ctx.session_token);
                }
            }
        }
        archive.temp_session.clear();
        archive.parent_stack.clear();
        archive.current_token = {};
        archive.archive_entry_from_parent.clear();
        archive.virtual_display_source = source_archive;
    }

} // namespace z7::ui::filemanager
