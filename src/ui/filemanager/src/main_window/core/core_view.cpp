// src/ui/filemanager/src/main_window/core/core_view.cpp
// Role: Panel activation, view/sort/time menu, and basic open actions.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

namespace {

QString parent_archive_virtual_dir(const QString& virtual_dir) {
  QString parent = z7::ui::archive_support::normalize_virtual_dir(virtual_dir);
  const int slash = parent.lastIndexOf(QLatin1Char('/'));
  if (slash < 0) {
    parent.clear();
  } else {
    parent = parent.left(slash);
  }
  return parent;
}

QString external_open_suspicious_name_message(QString name) {
  if (name.isEmpty()) {
    return {};
  }

  QString displayed_name = name;
  bool suspicious = false;
  bool long_space = false;

  const QChar rlo(0x202E);
  if (displayed_name.contains(rlo)) {
    displayed_name.replace(rlo, QStringLiteral("[RLO]"));
    suspicious = true;
  }

  const QString long_spaces = QStringLiteral("     ");
  while (displayed_name.contains(long_spaces)) {
    displayed_name.replace(long_spaces, QStringLiteral(" "));
    suspicious = true;
    long_space = true;
  }

#if defined(Q_OS_WIN)
  int suffix_start = displayed_name.size();
  while (suffix_start > 0) {
    const QChar c = displayed_name.at(suffix_start - 1);
    if (c != QLatin1Char('.') && c != QLatin1Char(' ')) {
      break;
    }
    --suffix_start;
  }
  if (suffix_start != displayed_name.size()) {
    const QString trimmed_name = displayed_name.left(suffix_start);
    const QString suffix = QFileInfo(trimmed_name).suffix().toLower();
    if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("bat") ||
        suffix == QStringLiteral("ps1") || suffix == QStringLiteral("com") ||
        suffix == QStringLiteral("lnk")) {
      suspicious = true;
    }
  }
#endif

  if (!suspicious) {
    return {};
  }

  QString message = z7::ui::runtime_support::L(3012);
  if (!long_space) {
    const qsizetype left = message.indexOf(QLatin1Char('('));
    const qsizetype right =
        left >= 0 ? message.indexOf(QLatin1Char(')'), left + 1) : -1;
    if (left >= 0 && right >= left) {
      message.remove(left, right + 1 - left);
      message = message.simplified();
    }
  }

  displayed_name.replace(QLatin1Char('\n'), QLatin1Char('_'));
  name.replace(QLatin1Char('\n'), QLatin1Char('_'));
  return QStringLiteral("%1\n%2\n%3").arg(message, displayed_name, name);
}

struct FoldersHistoryDialogResult {
  QString selected_path;
  QStringList history;
  bool history_changed = false;
};

class FoldersHistoryDialog final : public QDialog {
 public:
  explicit FoldersHistoryDialog(const QStringList& history, QWidget* parent)
      : QDialog(parent), list_(new QListWidget(this)) {
#ifdef Z7_TESTING
    setObjectName(QStringLiteral("foldersHistoryDialog"));
#endif
    setWindowTitle(z7::ui::runtime_support::strip_mnemonic(
        z7::ui::runtime_support::L(736)));
    resize(520, 360);

#ifdef Z7_TESTING
    list_->setObjectName(QStringLiteral("foldersHistoryList"));
#endif
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setAlternatingRowColors(true);
    list_->installEventFilter(this);
    for (const QString& entry : history) {
      list_->addItem(entry);
    }
    if (list_->count() > 0) {
      list_->setCurrentRow(0);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
#ifdef Z7_TESTING
    buttons->setObjectName(QStringLiteral("foldersHistoryButtons"));
#endif
    delete_button_ = buttons->addButton(
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7205)),
        QDialogButtonBox::ActionRole);
#ifdef Z7_TESTING
    delete_button_->setObjectName(QStringLiteral("foldersHistoryDeleteButton"));
#endif

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(list_);
    layout->addWidget(buttons);

    connect(list_,
            &QListWidget::itemSelectionChanged,
            this,
            [this]() { update_delete_button(); });
    connect(list_,
            &QListWidget::itemDoubleClicked,
            this,
            [this](QListWidgetItem*) { accept(); });
    connect(delete_button_,
            &QPushButton::clicked,
            this,
            [this]() { delete_selected_items(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    update_delete_button();
  }

  FoldersHistoryDialogResult result() const {
    FoldersHistoryDialogResult value;
    value.history_changed = history_changed_;
    value.selected_path = current_path();
    for (int row = 0; row < list_->count(); ++row) {
      const QListWidgetItem* item = list_->item(row);
      if (item != nullptr) {
        value.history.push_back(item->text());
      }
    }
    return value;
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == list_ && event != nullptr &&
        event->type() == QEvent::KeyPress) {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (key_event->key() == Qt::Key_Delete) {
        delete_selected_items();
        return true;
      }
      if ((key_event->key() == Qt::Key_Return ||
           key_event->key() == Qt::Key_Enter) &&
          key_event->modifiers() == Qt::NoModifier) {
        accept();
        return true;
      }
    }
    return QDialog::eventFilter(watched, event);
  }

 private:
  QString current_path() const {
    QListWidgetItem* item = list_->currentItem();
    return item == nullptr ? QString() : item->text();
  }

  void delete_selected_items() {
    if (list_->selectedItems().isEmpty()) {
      return;
    }

    int next_row = list_->currentRow();
    for (;;) {
      int row_to_delete = -1;
      for (QListWidgetItem* item : list_->selectedItems()) {
        const int row = list_->row(item);
        if (row > row_to_delete) {
          row_to_delete = row;
        }
      }
      if (row_to_delete < 0) {
        break;
      }
      if (next_row < 0 || row_to_delete < next_row) {
        next_row = row_to_delete;
      }
      delete list_->takeItem(row_to_delete);
      history_changed_ = true;
    }

    if (list_->count() > 0) {
      if (next_row < 0) {
        next_row = 0;
      }
      if (next_row >= list_->count()) {
        next_row = list_->count() - 1;
      }
      list_->setCurrentRow(next_row);
    }
    update_delete_button();
  }

  void update_delete_button() {
    delete_button_->setEnabled(!list_->selectedItems().isEmpty());
  }

  QListWidget* list_ = nullptr;
  QPushButton* delete_button_ = nullptr;
  bool history_changed_ = false;
};

}  // namespace

void MainWindow::apply_archive_preview_columns_visibility_for_panel(int panel_index) {
  const PanelController& panel = panel_controller(panel_index);
  if (panel.ui.details_view == nullptr) {
    return;
  }

  const bool archive_mode = panel.in_archive_view();
  const std::array<int, 11> archive_only_columns = {
      DirectoryListModel::kPackedSizeColumn,
      DirectoryListModel::kAccessedColumn,
      DirectoryListModel::kAttributesColumn,
      DirectoryListModel::kEncryptedColumn,
      DirectoryListModel::kCrcColumn,
      DirectoryListModel::kMethodColumn,
      DirectoryListModel::kCharactsColumn,
      DirectoryListModel::kHostOsColumn,
      DirectoryListModel::kVersionColumn,
      DirectoryListModel::kVolumeIndexColumn,
      DirectoryListModel::kOffsetColumn};

  for (const int column : archive_only_columns) {
    panel.ui.details_view->setColumnHidden(column, !archive_mode);
  }
  panel.ui.details_view->setColumnHidden(DirectoryListModel::kTypeSortColumn, true);
}

void MainWindow::refresh_all_details_column_visibility() {
  for (int i = 0; i < 2; ++i) {
    apply_archive_preview_columns_visibility_for_panel(i);
  }
}

void MainWindow::set_active_panel(int panel_index) {
  if (panel_index < 0 || panel_index > 1) {
    return;
  }
  if (!two_panels_visible_ && panel_index == 1) {
    panel_index = 0;
  }
  if (active_panel_index_ == panel_index) {
    return;
  }
  active_panel_index_ = panel_index;
  refresh_active_panel_chrome();
}

MainWindow::CrossPanelArchiveBindTarget
MainWindow::archive_bind_target_from_panel(int panel_index,
                                           const QString& virtual_dir) const {
  const PanelController& panel = panel_controller(panel_index);
  CrossPanelArchiveBindTarget target;
  target.source_archive = panel.archive.source_archive;
  target.virtual_dir = z7::ui::archive_support::normalize_virtual_dir(
      virtual_dir);
  target.origin_dir = panel.archive.origin_dir;
  target.type_hint = panel.archive.type_hint;
  target.virtual_display_source = panel.archive_display_source();
  target.archive_entry_from_parent = panel.archive.archive_entry_from_parent;
  target.parent_stack = panel.archive.parent_stack;
  target.temp_session = panel.archive.temp_session;
  target.session_token = panel.archive.current_token;
  return target;
}

std::optional<MainWindow::CrossPanelBindTarget>
MainWindow::focused_archive_bind_target_for_panel(int panel_index) const {
  const PanelController& panel = panel_controller(panel_index);
  if (!panel.in_archive_view() || panel.model == nullptr) {
    return std::nullopt;
  }

  const QModelIndex focused = panel.focused_source_index();
  if (!focused.isValid()) {
    return std::nullopt;
  }

  if (panel.model->is_parent_link_for_row(focused.row())) {
    const QString current_virtual =
        z7::ui::archive_support::normalize_virtual_dir(
            panel.archive.virtual_dir);
    if (!current_virtual.isEmpty()) {
      CrossPanelBindTarget out;
      out.archive = true;
      out.archive_target =
          archive_bind_target_from_panel(panel_index,
                                         parent_archive_virtual_dir(
                                             current_virtual));
      return out;
    }

    if (!panel.archive.parent_stack.isEmpty()) {
      QVector<PanelController::ArchiveState::ParentContext> parent_stack =
          panel.archive.parent_stack;
      const PanelController::ArchiveState::ParentContext parent =
          parent_stack.back();
      parent_stack.removeLast();

      CrossPanelBindTarget out;
      out.archive = true;
      out.archive_target.source_archive = parent.archive_path.isEmpty()
                                              ? panel.archive.source_archive
                                              : parent.archive_path;
      out.archive_target.virtual_dir =
          z7::ui::archive_support::normalize_virtual_dir(parent.virtual_dir);
      out.archive_target.origin_dir = parent.origin_dir;
      out.archive_target.type_hint = parent.type_hint;
      out.archive_target.virtual_display_source =
          parent.virtual_display_source.isEmpty()
              ? out.archive_target.source_archive
              : parent.virtual_display_source;
      out.archive_target.archive_entry_from_parent =
          parent.archive_entry_from_parent;
      out.archive_target.parent_stack = std::move(parent_stack);
      out.archive_target.temp_session = parent.temp_session;
      out.archive_target.session_token = parent.session_token;
      return out;
    }

    CrossPanelBindTarget out;
    out.filesystem_dir = panel.archive.origin_dir.isEmpty()
                             ? QFileInfo(panel.archive.source_archive)
                                   .absolutePath()
                             : panel.archive.origin_dir;
    return out;
  }

  if (!panel.model->is_dir_for_row(focused.row())) {
    return std::nullopt;
  }

  const QString focused_virtual_dir =
      z7::ui::archive_support::normalize_virtual_dir(
          panel.model->path_for_row(focused.row()));
  if (focused_virtual_dir.isEmpty()) {
    return std::nullopt;
  }

  CrossPanelBindTarget out;
  out.archive = true;
  out.archive_target =
      archive_bind_target_from_panel(panel_index, focused_virtual_dir);
  return out;
}

bool MainWindow::bind_panel_to_filesystem_directory(int panel_index,
                                                   const QString& directory) {
  if (panel_index < 0 || panel_index > 1) {
    return false;
  }

  const QFileInfo info(directory);
  if (!info.exists() || !info.isDir()) {
    return false;
  }
  const QString target_dir = info.absoluteFilePath();

  if (in_archive_view_for_panel(panel_index)) {
    close_archive_view_for_panel(
        panel_index,
        [this, panel_index, target_dir](bool ok) {
          if (ok) {
            set_current_directory_for_panel(panel_index, target_dir);
          }
        });
    return true;
  }

  set_current_directory_for_panel(panel_index, target_dir);
  return true;
}

bool MainWindow::bind_panel_to_archive_target(
    int panel_index,
    const CrossPanelArchiveBindTarget& target) {
  if (panel_index < 0 || panel_index > 1 ||
      target.source_archive.trimmed().isEmpty() ||
      !target.session_token.is_valid()) {
    return false;
  }

  const auto start_load =
      [this, panel_index, target]() -> bool {
        return load_archive_virtual_directory_for_panel(
            panel_index,
            target.source_archive,
            target.virtual_dir,
            target.origin_dir,
            target.type_hint,
            false,
            [this, panel_index, target](bool loaded) {
              if (!loaded) {
                return;
              }
              PanelController& panel = panel_controller(panel_index);
              panel.archive.parent_stack = target.parent_stack;
              panel.archive.archive_entry_from_parent =
                  target.archive_entry_from_parent;
              panel.archive.temp_session = target.temp_session;
              if (panel_index == active_panel_index_) {
                refresh_active_panel_chrome();
              } else {
                update_status_for_panel(panel_index);
              }
            },
            false,
            {},
            target.session_token,
            target.virtual_display_source);
      };

  if (in_archive_view_for_panel(panel_index)) {
    close_archive_view_for_panel(
        panel_index,
        [start_load](bool ok) {
          if (ok) {
            (void)start_load();
          }
        });
    return true;
  }

  return start_load();
}

void MainWindow::bind_opposite_panel_to_same_folder() {
  if (!two_panels_visible_) {
    return;
  }

  const int source_panel = active_panel_index_;
  const int target_panel = 1 - source_panel;
  const PanelController& source = panel_controller(source_panel);
  if (source.in_archive_view()) {
    (void)bind_panel_to_archive_target(
        target_panel,
        archive_bind_target_from_panel(source_panel,
                                       source.archive.virtual_dir));
    return;
  }

  (void)bind_panel_to_filesystem_directory(target_panel,
                                           source.current_directory());
}

void MainWindow::bind_opposite_panel_to_focused_folder() {
  if (!two_panels_visible_) {
    return;
  }

  const int source_panel = active_panel_index_;
  const int target_panel = 1 - source_panel;
  const PanelController& source = panel_controller(source_panel);
  if (source.model == nullptr) {
    return;
  }

  if (source.in_archive_view()) {
    const std::optional<CrossPanelBindTarget> target =
        focused_archive_bind_target_for_panel(source_panel);
    if (!target.has_value()) {
      return;
    }
    if (target->archive) {
      (void)bind_panel_to_archive_target(target_panel,
                                         target->archive_target);
    } else {
      (void)bind_panel_to_filesystem_directory(target_panel,
                                               target->filesystem_dir);
    }
    return;
  }

  const QModelIndex focused = source.focused_source_index();
  if (!focused.isValid() || !source.model->is_dir_for_row(focused.row())) {
    return;
  }

  (void)bind_panel_to_filesystem_directory(
      target_panel,
      source.model->path_for_row(focused.row()));
}

void MainWindow::apply_view_mode_to_panel(int panel_index, int view_mode) {
  PanelController& panel = panel_controller(panel_index);
  int normalized_mode = view_mode;
  if (normalized_mode < PanelController::kViewModeLargeIcons ||
      normalized_mode > PanelController::kViewModeDetails) {
    normalized_mode = PanelController::kViewModeDetails;
  }
  panel.set_view_mode(static_cast<PanelController::ViewMode>(normalized_mode));
}

namespace {

struct SortColumnAndOrder {
  int column;
  Qt::SortOrder order;
};

// Translates a SortAction (menu semantic) to the concrete (column, order) pair
// driven into the proxy. `kSortActionUnsorted` is signalled by column == -1.
SortColumnAndOrder resolve_sort_action(int sort_action) {
  switch (sort_action) {
    case kSortActionName:
      return {DirectoryListModel::kNameColumn, Qt::AscendingOrder};
    case kSortActionType:
      return {DirectoryListModel::kTypeSortColumn, Qt::AscendingOrder};
    case kSortActionDate:
      return {DirectoryListModel::kModifiedColumn, Qt::DescendingOrder};
    case kSortActionSize:
      return {DirectoryListModel::kSizeColumn, Qt::DescendingOrder};
    case kSortActionUnsorted:
    default:
      return {-1, Qt::AscendingOrder};
  }
}

// Reverse map: given the proxy's current sort column, what is the semantic
// SortAction to check in the view menu? Sort direction is a separate header
// state, matching original 7-Zip's SortID + Ascending split.
int detect_active_sort_action(int sort_column) {
  if (sort_column < 0) return kSortActionUnsorted;
  switch (sort_column) {
    case DirectoryListModel::kNameColumn:
      return kSortActionName;
    case DirectoryListModel::kTypeSortColumn:
      return kSortActionType;
    case DirectoryListModel::kModifiedColumn:
      return kSortActionDate;
    case DirectoryListModel::kSizeColumn:
      return kSortActionSize;
    default:
      return -1;
  }
}

}  // namespace

void MainWindow::apply_sort_mode_to_panel(int panel_index, int sort_action, bool toggle_if_same) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.ui.details_view == nullptr) {
    return;
  }

  const SortColumnAndOrder target = resolve_sort_action(sort_action);
  auto* header = panel.ui.details_view->horizontalHeader();
  panel.runtime.active_sort_action = sort_action;
  const PanelController::SelectionSnapshot selection_snapshot =
      panel.capture_selection_snapshot();
  panel.runtime.pending_layout_selection = selection_snapshot;
  panel.runtime.has_pending_layout_selection = true;

  if (target.column < 0) {
    if (header != nullptr) {
      header->setSortIndicator(-1, Qt::AscendingOrder);
    }
    panel.ui.details_view->setSortingEnabled(false);
    panel.ui.details_view->setSortingEnabled(true);
    panel.restore_selection_snapshot(selection_snapshot);
    refresh_action_states();
    return;
  }

  // Toggle order when clicking the same sort action twice.
  Qt::SortOrder order = target.order;
  if (toggle_if_same && header != nullptr &&
      header->sortIndicatorSection() == target.column) {
    order = header->sortIndicatorOrder() == Qt::AscendingOrder
                ? Qt::DescendingOrder
                : Qt::AscendingOrder;
  }

  panel.ui.details_view->sortByColumn(target.column, order);
  panel.restore_selection_snapshot(selection_snapshot);
  refresh_action_states();
}

void MainWindow::update_view_menu_checks() {
  const PanelController& panel = active_panel_controller();
  const int view_mode = panel.view_mode;
  if (large_icons_action_ != nullptr) {
    large_icons_action_->setChecked(view_mode == PanelController::kViewModeLargeIcons);
  }
  if (small_icons_action_ != nullptr) {
    small_icons_action_->setChecked(view_mode == PanelController::kViewModeSmallIcons);
  }
  if (list_mode_action_ != nullptr) {
    list_mode_action_->setChecked(view_mode == PanelController::kViewModeList);
  }
  if (details_mode_action_ != nullptr) {
    details_mode_action_->setChecked(view_mode == PanelController::kViewModeDetails);
  }

  int sort_column = -1;
  if (panel.ui.details_view != nullptr) {
    auto* header = panel.ui.details_view->horizontalHeader();
    if (header != nullptr) {
      sort_column = header->sortIndicatorSection();
      if (!panel.ui.details_view->isSortingEnabled()) {
        sort_column = -1;
      }
    }
  }
  int active_sort_action = panel.runtime.active_sort_action;
  if (active_sort_action < kSortActionName ||
      active_sort_action > kSortActionUnsorted) {
    active_sort_action = detect_active_sort_action(sort_column);
  }
  if (sort_name_action_ != nullptr) {
    sort_name_action_->setChecked(active_sort_action == kSortActionName);
  }
  if (sort_type_action_ != nullptr) {
    sort_type_action_->setChecked(active_sort_action == kSortActionType);
  }
  if (sort_date_action_ != nullptr) {
    sort_date_action_->setChecked(active_sort_action == kSortActionDate);
  }
  if (sort_size_action_ != nullptr) {
    sort_size_action_->setChecked(active_sort_action == kSortActionSize);
  }
  if (unsorted_action_ != nullptr) {
    unsorted_action_->setChecked(active_sort_action == kSortActionUnsorted);
  }
  if (flat_view_action_ != nullptr && panel.model != nullptr) {
    flat_view_action_->setChecked(panel.model->flat_view());
  }
  if (two_panels_action_ != nullptr) {
    two_panels_action_->setChecked(two_panels_visible_);
  }
  if (time_day_action_ != nullptr) {
    time_day_action_->setChecked(
        display_settings_.timestamp_level == DirectoryListModel::kTimestampPrintLevelDay);
  }
  if (time_min_action_ != nullptr) {
    time_min_action_->setChecked(
        display_settings_.timestamp_level == DirectoryListModel::kTimestampPrintLevelMin);
  }
  if (time_sec_action_ != nullptr) {
    time_sec_action_->setChecked(
        display_settings_.timestamp_level == DirectoryListModel::kTimestampPrintLevelSec);
  }
  if (time_ntfs_action_ != nullptr) {
    time_ntfs_action_->setChecked(
        display_settings_.timestamp_level == DirectoryListModel::kTimestampPrintLevelNtfs);
  }
  if (time_ns_action_ != nullptr) {
    time_ns_action_->setChecked(
        display_settings_.timestamp_level == DirectoryListModel::kTimestampPrintLevelNs);
  }
  if (time_utc_action_ != nullptr) {
    time_utc_action_->setChecked(display_settings_.timestamp_show_utc);
  }
}

QString MainWindow::format_sample_time_for_level(int timestamp_level) const {
  const QDateTime now = display_settings_.timestamp_show_utc
                            ? QDateTime::currentDateTimeUtc()
                            : QDateTime::currentDateTime();
  const QString suffix =
      display_settings_.timestamp_show_utc ? QStringLiteral("Z") : QString();
  switch (timestamp_level) {
    case DirectoryListModel::kTimestampPrintLevelDay:
      return now.toString(QStringLiteral("yyyy-MM-dd")) + suffix;
    case DirectoryListModel::kTimestampPrintLevelMin:
      return now.toString(QStringLiteral("yyyy-MM-dd HH:mm")) + suffix;
    case DirectoryListModel::kTimestampPrintLevelSec:
      return now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + suffix;
    case DirectoryListModel::kTimestampPrintLevelNtfs:
    case DirectoryListModel::kTimestampPrintLevelNs: {
      const int digits =
          (timestamp_level == DirectoryListModel::kTimestampPrintLevelNtfs) ? 7 : 9;
      QString frac = QString::number(now.time().msec()).rightJustified(
          3, QLatin1Char('0'));
      while (frac.size() < digits) {
        frac += QLatin1Char('0');
      }
      if (frac.size() > digits) {
        frac = frac.left(digits);
      }
      return now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
             QStringLiteral(".") + frac + suffix;
    }
    default:
      return now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + suffix;
  }
}

void MainWindow::update_time_menu() {
  if (time_submenu_ == nullptr) {
    return;
  }

  time_submenu_->setTitle(format_sample_time_for_level(DirectoryListModel::kTimestampPrintLevelDay));
  if (time_day_action_ != nullptr) {
    time_day_action_->setText(format_sample_time_for_level(DirectoryListModel::kTimestampPrintLevelDay));
  }
  if (time_min_action_ != nullptr) {
    time_min_action_->setText(format_sample_time_for_level(DirectoryListModel::kTimestampPrintLevelMin));
  }
  if (time_sec_action_ != nullptr) {
    time_sec_action_->setText(format_sample_time_for_level(DirectoryListModel::kTimestampPrintLevelSec));
  }
  if (time_ntfs_action_ != nullptr) {
    time_ntfs_action_->setText(format_sample_time_for_level(DirectoryListModel::kTimestampPrintLevelNtfs));
  }
  if (time_ns_action_ != nullptr) {
    time_ns_action_->setText(format_sample_time_for_level(DirectoryListModel::kTimestampPrintLevelNs));
  }
  update_view_menu_checks();
}

void MainWindow::on_view_mode_action_triggered(int view_mode) {
  apply_view_mode_to_panel(active_panel_index_, view_mode);
  update_view_menu_checks();
}

void MainWindow::on_sort_mode_action_triggered(int sort_mode) {
  apply_sort_mode_to_panel(active_panel_index_, sort_mode, true);
  update_view_menu_checks();
}

void MainWindow::on_flat_view_action_triggered() {
  PanelController& panel = active_panel_controller();
  if (panel.model == nullptr) {
    return;
  }
  const bool enabled = !panel.model->flat_view();
  panel.model->set_flat_view(enabled);
  if (in_archive_view_for_panel(active_panel_index_)) {
    reload_archive_virtual_directory_for_panel(active_panel_index_);
  }
  update_view_menu_checks();
}

void MainWindow::on_two_panels_action_triggered() {
  two_panels_visible_ = !two_panels_visible_;
  if (panels_[1].ui.container != nullptr) {
    panels_[1].ui.container->setVisible(two_panels_visible_);
  }
  if (panels_splitter_ != nullptr) {
    if (two_panels_visible_) {
      panels_splitter_->setSizes({1, 1});
    } else {
      panels_splitter_->setSizes({1, 0});
      if (active_panel_index_ == 1) {
        set_active_panel(0);
      }
    }
  }
  z7::platform::qt::PortableSettings settings;
  write_fm_panels_state(&settings,
                        two_panels_visible_,
                        active_panel_index_,
                        current_fm_splitter_pos(panels_splitter_));
  update_view_menu_checks();
}

void MainWindow::on_folders_history_requested() {
  const int panel_index = active_panel_index_;
  const QStringList history = folder_history_;
  if (history.isEmpty()) {
    return;
  }

  FoldersHistoryDialog dialog(history, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const FoldersHistoryDialogResult result = dialog.result();
  if (result.history_changed) {
    set_folder_history(result.history);
  }
  if (!result.selected_path.isEmpty()) {
    open_folder_prefix_for_panel(panel_index, result.selected_path);
  }
}

void MainWindow::on_toggle_time_utc() {
  display_settings_.timestamp_show_utc = !display_settings_.timestamp_show_utc;
  apply_model_display_settings_to_all_panels();
  update_time_menu();
  z7::platform::qt::PortableSettings settings;
  save_display_settings(settings, display_settings_);
}

void MainWindow::on_time_precision_requested(int timestamp_level) {
  display_settings_.timestamp_level = timestamp_level;
  apply_model_display_settings_to_all_panels();
  update_time_menu();
  z7::platform::qt::PortableSettings settings;
  save_display_settings(settings, display_settings_);
}

bool MainWindow::activate_archive_parent_link_for_panel(
    int panel_index,
    const QModelIndex& view_index) {
  PanelController& panel = panel_controller(panel_index);
  if (!panel.in_archive_view() || panel.model == nullptr) {
    return false;
  }

  auto open_parent = [this, panel_index]() {
    if (panel_index != active_panel_index_) {
      set_active_panel(panel_index);
    }
    on_open_parent_requested();
    return true;
  };

  auto source_index_for_view_index = [&panel](const QModelIndex& index) {
    if (!index.isValid()) {
      return QModelIndex{};
    }
    if (panel.proxy != nullptr && index.model() == panel.proxy) {
      return panel.proxy->mapToSource(index);
    }
    return index;
  };

  const QModelIndex source_index = source_index_for_view_index(view_index);
  if (source_index.isValid() &&
      panel.model->is_parent_link_for_row(source_index.row())) {
    return open_parent();
  }

  if (panel.focused_item_is_parent_link()) {
    return open_parent();
  }

  const QModelIndexList selected_rows = panel.selected_rows_including_parent_link();
  if (selected_rows.size() == 1 &&
      panel.model->is_parent_link_for_row(selected_rows.front().row())) {
    return open_parent();
  }
  return false;
}

void MainWindow::activate_panel_selection(Qt::KeyboardModifiers modifiers) {
  const bool archive_mode = in_archive_view();
  if (archive_mode &&
      activate_archive_parent_link_for_panel(active_panel_index_)) {
    return;
  }

  const bool shift = (modifiers & Qt::ShiftModifier) != 0;
  const bool alt = (modifiers & Qt::AltModifier) != 0;
  const bool ctrl = (modifiers & Qt::ControlModifier) != 0;
  if (!shift && alt && !ctrl) {
    show_properties_dialog();
    return;
  }
  const bool try_internal = !shift || alt || ctrl;
  if (archive_mode) {
    open_selected_archive_entries(try_internal);
    return;
  }
  open_selected_filesystem_paths_including_parent_link(try_internal);
}

bool MainWindow::open_path_externally_untracked(const QString& path) {
  if (path.isEmpty()) {
    return false;
  }
  if (!confirm_external_open_targets_safe(
          QStringList{path},
          z7::ui::runtime_support::strip_mnemonic(
              z7::ui::runtime_support::L(540)))) {
    return false;
  }
  return external_opener_(path);
}

bool MainWindow::confirm_external_open_targets_safe(
    const QStringList& paths,
    const QString& caption) {
  for (const QString& path : paths) {
    const QString name = QFileInfo(path).fileName();
    const QString message = external_open_suspicious_name_message(name);
    if (message.isEmpty()) {
      continue;
    }
    QMessageBox::warning(this,
                         caption.trimmed().isEmpty()
                             ? QStringLiteral("7-Zip")
                             : caption,
                         message);
    return false;
  }
  return true;
}


}  // namespace z7::ui::filemanager
