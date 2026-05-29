// src/ui/filemanager/src/main_window/core/core_path.cpp
// Role: Path-bar navigation and selection helpers.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

bool MainWindow::can_open_parent_from_current_dir() const {
  const PanelController& panel = active_panel_controller();
  if (panel.in_archive_view()) {
    return !panel.current_directory().isEmpty();
  }

  const QString current_dir = panel.current_directory();
  if (current_dir.isEmpty()) {
    return false;
  }
  QDir dir(current_dir);
  return dir.cdUp();
}

void MainWindow::remember_path_history(const QString& abs_path) {
  const QString normalized = QDir(abs_path).absolutePath();
  if (normalized.isEmpty()) {
    return;
  }

  path_history_.removeAll(normalized);
  path_history_.prepend(normalized);
  constexpr int kMaxHistory = 48;
  while (path_history_.size() > kMaxHistory) {
    path_history_.removeLast();
  }
}

void MainWindow::sync_path_bar_from_current_dir() {
  if (path_combo_ == nullptr) {
    return;
  }

  if (in_archive_view()) {
    const QString display =
        archive_virtual_display_path_for_panel(active_panel_index_);
    const QSignalBlocker blocker(path_combo_);
    const QSignalBlocker edit_blocker(path_combo_->lineEdit());
    path_combo_->setEditText(display);
    return;
  }

  const QString absolute = QDir(current_directory()).absolutePath();
  const QString native = QDir::toNativeSeparators(absolute);
  remember_path_history(absolute);

  const QSignalBlocker blocker(path_combo_);
  const QSignalBlocker edit_blocker(path_combo_->lineEdit());
  path_combo_->setEditText(native);
}

void MainWindow::rebuild_path_bar_popup_items() {
  if (path_combo_ == nullptr) {
    return;
  }

  if (in_archive_view()) {
    const PanelController& panel = active_panel_controller();
    const QString virtual_dir =
        z7::ui::archive_support::normalize_virtual_dir(panel.current_directory());
    const QString current_data =
        QString::fromLatin1(kArchivePathBarDataPrefix) + virtual_dir;
    const QString display_source = panel.archive_display_source();

    const QSignalBlocker blocker(path_combo_);
    const QSignalBlocker edit_blocker(path_combo_->lineEdit());
    path_combo_->clear();

    QStringList dirs;
    dirs << QString();
    QString acc;
    const QStringList parts = virtual_dir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
      acc = acc.isEmpty() ? part : acc + QLatin1Char('/') + part;
      dirs << acc;
    }

    const QIcon dir_icon = style()->standardIcon(QStyle::SP_DirIcon);
    for (const QString& dir : dirs) {
      const QString display =
          z7::ui::archive_support::virtual_display_path(display_source, dir);
      const QString data = QString::fromLatin1(kArchivePathBarDataPrefix) + dir;
      path_combo_->addItem(dir_icon, display, data);
    }

    const int current_index = path_combo_->findData(current_data, Qt::UserRole);
    if (current_index >= 0) {
      path_combo_->setCurrentIndex(current_index);
    }
    path_combo_->setEditText(archive_virtual_display_path_for_panel(active_panel_index_));
    return;
  }

  const QString current = QDir(current_directory()).absolutePath();
  const QString current_native = QDir::toNativeSeparators(current);

  QSet<QString> seen;
  QFileIconProvider icon_provider;

  const QSignalBlocker blocker(path_combo_);
  const QSignalBlocker edit_blocker(path_combo_->lineEdit());
  path_combo_->clear();

  auto add_path = [&](const QString& path) {
    const QString absolute = QDir(path).absolutePath();
    if (absolute.isEmpty() || seen.contains(absolute)) {
      return;
    }
    seen.insert(absolute);

    const QFileInfo info(absolute);
    const QIcon icon = info.exists()
                           ? icon_provider.icon(info)
                           : style()->standardIcon(QStyle::SP_DirIcon);
    path_combo_->addItem(icon, QDir::toNativeSeparators(absolute), absolute);
  };

  for (const QString& path : ancestor_paths(current)) {
    add_path(path);
  }
  for (const QString& path : path_history_) {
    add_path(path);
  }

  const int current_index = path_combo_->findData(current, Qt::UserRole);
  if (current_index >= 0) {
    path_combo_->setCurrentIndex(current_index);
  }
  path_combo_->setEditText(current_native);
}

bool MainWindow::navigate_to_path_from_bar(const QString& path_text) {
  if (in_archive_view()) {
    PanelController& panel = active_panel_controller();
    QString normalized =
        QDir::fromNativeSeparators(path_text.trimmed());
    const QString prefix = QString::fromLatin1(kArchivePathBarDataPrefix);
    if (normalized.startsWith(prefix)) {
      normalized = normalized.mid(prefix.size());
    } else {
      const QString display_source = panel.archive_display_source();
      const QString source =
          QDir::fromNativeSeparators(display_source.trimmed());
      QString source_prefix = source;
      if (!source_prefix.endsWith(QLatin1Char('/'))) {
        source_prefix += QLatin1Char('/');
      }
#ifdef Q_OS_WIN
      constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
      constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif
      if (normalized.compare(source, kPathCase) == 0 ||
          normalized.compare(source_prefix, kPathCase) == 0) {
        normalized.clear();
      } else if (normalized.startsWith(source_prefix, kPathCase)) {
        normalized = normalized.mid(source_prefix.size());
      } else if (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
      }
    }

    normalized = z7::ui::archive_support::normalize_virtual_dir(normalized);
    return load_archive_virtual_directory_for_panel(
        active_panel_index_,
        panel.archive.source_archive,
        normalized,
        panel.archive.origin_dir,
        panel.archive.type_hint,
        false,
        {},
        false,
        {},
        panel.archive.current_token,
        panel.archive_display_source());
  }

  const QString normalized =
      QDir::fromNativeSeparators(path_text.trimmed());
  if (normalized.isEmpty()) {
    return false;
  }

  const QFileInfo info(normalized);
  if (!info.exists() || !info.isDir()) {
    return false;
  }

  set_current_directory(info.absoluteFilePath());
  return true;
}

QStringList MainWindow::selected_file_paths() const {
  QStringList files;
  for (const QString& path :
       active_panel_controller().selected_real_item_paths()) {
    if (QFileInfo(path).isFile()) {
      files << path;
    }
  }
  return files;
}

QString MainWindow::create_archive_name(bool is_hash, QString* base_name) const {
  return z7::shell_integration::shell_integration_create_archive_name_from_paths(
      active_panel_controller().selected_real_item_paths(), is_hash, base_name);
}

bool MainWindow::hash_recursive_dirs_for_active_panel() const {
  const PanelController& panel = active_panel_controller();
  if (panel.model == nullptr) {
    return true;
  }
  return !panel.model->flat_view();
}

QString MainWindow::suggested_extract_subdir_for_menu() const {
  const QStringList paths = active_panel_controller().selected_real_item_paths();
  if (paths.isEmpty()) {
    return QStringLiteral("*") + QDir::separator();
  }
  if (paths.size() == 1) {
    return z7::shell_integration::shell_integration_extract_subfolder_name(
               QFileInfo(paths.front()).fileName()) +
           QDir::separator();
  }
  return QStringLiteral("*") + QDir::separator();
}

}  // namespace z7::ui::filemanager
