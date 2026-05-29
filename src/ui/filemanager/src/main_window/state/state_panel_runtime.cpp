// src/ui/filemanager/src/main_window/state/state_panel_runtime.cpp
// Role: Original-style folder history, panel path persistence, and auto-refresh runtime state.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager
{

    namespace
    {

        constexpr int kFolderHistoryMax = 100;

        QString normalize_folder_prefix(QString const& path)
        {
            QString const trimmed = QDir::fromNativeSeparators(path.trimmed());
            if (trimmed.isEmpty())
            {
                return {};
            }
            return QDir(trimmed).absolutePath();
        }

        void add_unique_history_path(QStringList* history, QString const& path)
        {
            if (history == nullptr)
            {
                return;
            }
            QString const normalized = normalize_folder_prefix(path);
            if (normalized.isEmpty())
            {
                return;
            }
            for (int i = 0; i < history->size();)
            {
                if (QString::compare(history->at(i), normalized, Qt::CaseInsensitive) == 0)
                {
                    history->removeAt(i);
                }
                else
                {
                    ++i;
                }
            }
            history->prepend(normalized);
            while (history->size() > kFolderHistoryMax)
            {
                history->removeLast();
            }
        }

        QStringList normalize_folder_history(QStringList const& history)
        {
            QStringList normalized;
            for (QString const& path : history)
            {
                QString const entry = normalize_folder_prefix(path);
                if (entry.isEmpty() || normalized.contains(entry, Qt::CaseInsensitive))
                {
                    continue;
                }
                normalized.push_back(entry);
                if (normalized.size() >= kFolderHistoryMax)
                {
                    break;
                }
            }
            return normalized;
        }

        QString nearest_existing_directory(QString const& path)
        {
            QString current = normalize_folder_prefix(path);
            while (!current.isEmpty())
            {
                QFileInfo const info(current);
                if (info.exists() && info.isDir())
                {
                    return info.absoluteFilePath();
                }

                QString const parent = info.absolutePath();
                if (parent.isEmpty() || parent == current)
                {
                    break;
                }
                current = parent;
            }

            QString root = QDir(path).rootPath();
            if (root.isEmpty())
            {
                root = QDir::rootPath();
            }
            QFileInfo const root_info(root);
            return root_info.exists() && root_info.isDir() ? root_info.absoluteFilePath() : QString();
        }

        struct ArchivePrefixTarget
        {
            QString archive_path;
            QString virtual_dir;
        };

        std::optional<ArchivePrefixTarget> resolve_archive_prefix_target(QString const& path)
        {
            QString const absolute = normalize_folder_prefix(path);
            if (absolute.isEmpty())
            {
                return std::nullopt;
            }

            QString candidate = absolute;
            while (!candidate.isEmpty())
            {
                QFileInfo const info(candidate);
                if (info.exists() && info.isFile() && is_archive_file(info.absoluteFilePath()))
                {
                    QString virtual_dir = absolute.mid(candidate.size());
                    while (virtual_dir.startsWith(QLatin1Char('/')))
                    {
                        virtual_dir.remove(0, 1);
                    }
                    return ArchivePrefixTarget{
                        info.absoluteFilePath(),
                        z7::ui::archive_support::normalize_virtual_dir(virtual_dir)};
                }

                QString const parent = info.absolutePath();
                if (parent.isEmpty() || parent == candidate)
                {
                    break;
                }
                candidate = parent;
            }
            return std::nullopt;
        }

    } // namespace

    void MainWindow::load_folder_history()
    {
        z7::platform::qt::PortableSettings settings;
        folder_history_ = normalize_folder_history(
            settings.value(QString::fromLatin1(kSettingsFmFolderHistory)).toStringList());
    }

    void MainWindow::save_folder_history() const
    {
        z7::platform::qt::PortableSettings settings;
        settings.setValue(QString::fromLatin1(kSettingsFmFolderHistory),
                          normalize_folder_history(folder_history_));
        settings.sync();
    }

    void MainWindow::set_folder_history(QStringList const& history)
    {
        folder_history_ = normalize_folder_history(history);
        save_folder_history();
    }

    void MainWindow::restore_panel_paths_from_settings()
    {
        z7::platform::qt::PortableSettings settings;
        std::array<char const*, 2> const keys = {kSettingsFmPanelPath0, kSettingsFmPanelPath1};
        bool const two_panels_configured =
            read_fm_panels_state(settings).two_panels;

        for (int i = 0; i < 2; ++i)
        {
            QString const stored =
                settings.value(QString::fromLatin1(keys[static_cast<size_t>(i)]), QString()).toString().trimmed();
            if (!stored.isEmpty())
            {
                QString const restored = nearest_existing_directory(stored);
                if (!restored.isEmpty())
                {
                    set_current_directory_for_panel(i, restored);
                    continue;
                }
            }

            if (i == 0 || two_panels_configured)
            {
                remember_folder_history(current_directory_for_panel(i));
            }
        }
    }

    void MainWindow::save_panel_paths() const
    {
        z7::platform::qt::PortableSettings settings;
        std::array<char const*, 2> const keys = {kSettingsFmPanelPath0, kSettingsFmPanelPath1};
        for (int i = 0; i < 2; ++i)
        {
            PanelController const& panel = panels_[i];
            QString path;
            if (panel.in_archive_view())
            {
                QString const origin_dir = panel.archive.origin_dir.trimmed();
                if (!origin_dir.isEmpty())
                {
                    path = QDir(origin_dir).absolutePath();
                }
                else
                {
                    QString const archive_path = panel.archive.source_archive.trimmed();
                    if (!archive_path.isEmpty())
                    {
                        path = QFileInfo(archive_path).absolutePath();
                    }
                }
            }
            else
            {
                QString const current_dir = panel.current_directory().trimmed();
                if (!current_dir.isEmpty())
                {
                    path = QDir(current_dir).absolutePath();
                }
            }
            if (path.isEmpty())
            {
                continue;
            }
            settings.setValue(QString::fromLatin1(keys[static_cast<size_t>(i)]), path);
        }
        settings.sync();
    }

    void MainWindow::remember_folder_history(QString const& path)
    {
        add_unique_history_path(&folder_history_, path);
    }

    bool MainWindow::has_folder_history() const
    {
        return !folder_history_.isEmpty();
    }

    bool MainWindow::open_folder_prefix_for_panel(int panel_index, QString const& path)
    {
        if (auto const archive_target = resolve_archive_prefix_target(path))
        {
            auto open_archive_target = [this, panel_index, archive_target]() {
                return load_archive_virtual_directory_for_panel(
                    panel_index,
                    archive_target->archive_path,
                    archive_target->virtual_dir,
                    QFileInfo(archive_target->archive_path).absolutePath(),
                    QString(),
                    true);
            };
            if (in_archive_view_for_panel(panel_index))
            {
                close_archive_view_for_panel(
                    panel_index,
                    [open_archive_target](bool ok)
                    {
                        if (ok)
                        {
                            open_archive_target();
                        }
                    });
                return true;
            }
            return open_archive_target();
        }

        QString const selected_dir = nearest_existing_directory(path);
        if (selected_dir.isEmpty())
        {
            return false;
        }

        if (in_archive_view_for_panel(panel_index))
        {
            close_archive_view_for_panel(
                panel_index,
                [this, panel_index, selected_dir](bool ok)
                {
                    if (ok)
                    {
                        set_current_directory_for_panel(panel_index, selected_dir);
                    }
                });
            return true;
        }

        set_current_directory_for_panel(panel_index, selected_dir);
        return true;
    }

    void MainWindow::PanelController::clear_auto_refresh_binding()
    {
        if (runtime.auto_refresh_watcher != nullptr)
        {
            QStringList const watched_dirs = runtime.auto_refresh_watcher->directories();
            if (!watched_dirs.isEmpty())
            {
                runtime.auto_refresh_watcher->removePaths(watched_dirs);
            }
            QStringList const watched_files = runtime.auto_refresh_watcher->files();
            if (!watched_files.isEmpty())
            {
                runtime.auto_refresh_watcher->removePaths(watched_files);
            }
        }

        runtime.auto_refresh_watched_dir.clear();
        runtime.auto_refresh_dirty = false;
    }

    void MainWindow::PanelController::mark_auto_refresh_dirty()
    {
        runtime.auto_refresh_dirty = true;
    }

    bool MainWindow::PanelController::auto_refresh_needs_rebind(QString const& current_dir) const
    {
        bool const watcher_missing = runtime.auto_refresh_watcher == nullptr;
        bool const directory_mismatch = runtime.auto_refresh_watched_dir != current_dir;
        bool const watch_detached =
            runtime.auto_refresh_watcher != nullptr
            && !runtime.auto_refresh_watched_dir.isEmpty()
            && !runtime.auto_refresh_watcher->directories().contains(runtime.auto_refresh_watched_dir);
        return watcher_missing || directory_mismatch || watch_detached;
    }

} // namespace z7::ui::filemanager
