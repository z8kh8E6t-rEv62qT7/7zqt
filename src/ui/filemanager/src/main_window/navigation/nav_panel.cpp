// src/ui/filemanager/src/main_window/navigation/nav_panel.cpp
// Role: Action-state refresh and panel UI setup.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "main_window/drag_drop/drag_aware_views.h"
#include "official_lang_catalog.h"
#include "structured_list_config.h"
#include "structured_list_proxy.h"

namespace z7::ui::filemanager {

namespace {

bool is_filesystem_comment_target_editable(const QString& panel_directory,
                                           const QString& selected_path) {
  if (panel_directory.trimmed().isEmpty() || selected_path.trimmed().isEmpty()) {
    return false;
  }
  const QFileInfo selected_info(selected_path);
  if (!selected_info.exists()) {
    return false;
  }
  return QFileInfo(selected_info.absolutePath()).absoluteFilePath() ==
         QFileInfo(panel_directory).absoluteFilePath();
}

z7::ui::widgets::StructuredListConfig make_details_view_config() {
  using z7::ui::widgets::StructuredListColumn;
  using z7::ui::widgets::StructuredListConfig;
  using z7::ui::widgets::StructuredListStyle;

  StructuredListConfig cfg;
  cfg.primary_interactive_column = DirectoryListModel::kNameColumn;
  cfg.sorting_enabled = true;
  cfg.show_header = true;

  StructuredListStyle& s = cfg.style;
  // Fusion-light: very faint hover chip, clearer selection chip.
  s.primary_hover_bg = QColor(0, 0, 0, 20);           // ~8% black overlay.
  s.primary_selected_bg = QColor(60, 130, 210);       // Fusion-ish accent blue.
  s.primary_selected_text = QColor(Qt::white);
  s.row_hover_bg = QColor(0, 0, 0, 8);                // nearly imperceptible row tint.
  s.grid_line = QColor();                             // off.
  s.primary_text_padding_h = 6;
  s.primary_text_padding_v = 2;
  s.row_height_hint = 22;

  auto add_col = [&](const char* id,
                     uint32_t lang_id,
                     int default_width,
                     Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                     bool hidden_by_default = false) {
    StructuredListColumn c;
    c.id = QString::fromLatin1(id);
    c.header_text = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
    c.default_width = default_width;
    c.alignment = align;
    c.hidden_by_default = hidden_by_default;
    cfg.columns.push_back(c);
  };
  add_col("name", 1004, 320);
  add_col("size", 1007, 110, Qt::AlignRight | Qt::AlignVCenter);
  add_col("packed", 1008, 110, Qt::AlignRight | Qt::AlignVCenter);
  add_col("modified", 1012, 160);
  add_col("created", 1010, 160);
  add_col("accessed", 1011, 160);
  add_col("attributes", 1009, 90);
  add_col("encrypted", 1015, 80, Qt::AlignCenter);
  add_col("comment", 1028, 120);
  add_col("crc", 1019, 90, Qt::AlignRight | Qt::AlignVCenter);
  add_col("method", 1022, 110);
  add_col("characts", 1047, 90);
  add_col("host_os", 1023, 90);
  add_col("version", 1033, 80);
  add_col("volume_index", 1090, 80, Qt::AlignRight | Qt::AlignVCenter);
  add_col("offset", 1036, 90, Qt::AlignRight | Qt::AlignVCenter);
  add_col("folders", 1031, 70, Qt::AlignRight | Qt::AlignVCenter);
  add_col("files", 1032, 70, Qt::AlignRight | Qt::AlignVCenter);
  add_col("type_sort", 1020, 0, Qt::AlignLeft | Qt::AlignVCenter, true);
  return cfg;
}

}  // namespace

void MainWindow::refresh_action_states() {
  if (open_action_ == nullptr ||
      extract_action_ == nullptr) {
    return;
  }

  const QStringList selected_paths_including_parent_link =
      selected_filesystem_paths_including_parent_link();
  const bool has_selection = !selected_paths_including_parent_link.isEmpty();
  const PanelController& active_panel = active_panel_controller();
  const bool selected_rows_include_parent =
      active_panel.selected_rows_include_parent_link();
  const bool has_selected_real_items = has_selected_real_items_for_panel(
      active_panel_index_);
  const bool has_selected_real_items_without_parent_link =
      has_selected_real_items && !selected_rows_include_parent;
  const bool has_oper_smart_real_items = has_oper_smart_real_items_for_panel(active_panel_index_);
  const bool single_selection = selected_paths_including_parent_link.size() == 1;
  const bool archive_mode = active_panel.in_archive_view();
  const QModelIndexList selected_real_rows = active_panel.selected_real_item_rows();
  const bool has_selected_real_dirs =
      active_panel.source_rows_contain_dir(selected_real_rows);
  const bool has_extract_operands =
      !selected_real_rows.isEmpty() && !has_selected_real_dirs;
  bool all_files = has_selected_real_items_without_parent_link;
  for (const QString& path : selected_paths_including_parent_link) {
    if (!QFileInfo(path).isFile()) {
      all_files = false;
      break;
    }
  }
  const bool archive_preview_test_enabled =
      archive_mode &&
      build_archive_writeback_plan_for_panel(active_panel_index_).is_valid();
  const bool archive_preview_add_enabled =
      archive_mode &&
      can_add_external_files_to_archive_preview(active_panel_index_);
  const bool archive_preview_properties_enabled =
      archive_mode &&
      !active_panel.archive.source_archive.trimmed().isEmpty() &&
      (active_panel.archive.current_token.is_valid() ||
       QFileInfo(active_panel.archive.source_archive).exists());
  const bool has_oper_smart_archive_entries =
      archive_preview_test_enabled &&
      !active_panel.oper_smart_archive_entries().isEmpty();
  const bool focused_is_parent_link = active_panel.focused_item_is_parent_link();
  const QString focused_path = active_panel.focused_path();
  const bool has_focused_item = focused_is_parent_link || !focused_path.isEmpty();
  const int row_count = active_panel.model != nullptr ? active_panel.model->rowCount() : 0;
  const bool has_rows = row_count > 0;
  const bool has_focused_non_parent = has_focused_item && !focused_is_parent_link;
  const QString favorite_folder_prefix = active_panel.favorite_folder_prefix();
  const bool can_add_to_favorites = !favorite_folder_prefix.trimmed().isEmpty();
  const bool can_comment =
      has_focused_non_parent &&
      (archive_mode
           ? active_panel.archive.current_token.is_valid()
           : is_filesystem_comment_target_editable(active_panel.current_directory(),
                                                   focused_path));
  const bool can_link =
      single_selection && has_selected_real_items_without_parent_link &&
      !archive_mode;
#if defined(Q_OS_WIN)
  const bool supports_alternate_streams = true;
#else
  const bool supports_alternate_streams = false;
#endif
  const bool can_open_alternate_streams =
      supports_alternate_streams && single_selection &&
      has_selected_real_items_without_parent_link && !archive_mode;
  const bool archive_preview_rename_enabled =
      archive_mode && has_focused_non_parent &&
      active_panel.archive.current_token.is_valid();
  const bool can_open_parent = can_open_parent_from_current_dir();
  z7::platform::qt::PortableSettings settings;
  const QString diff_command =
      settings.value(QString::fromLatin1(kSettingsFmDiff), QString()).toString().trimmed();
  const bool has_diff_command = !diff_command.isEmpty();
  const QString version_control_command =
      settings.value(QString::fromLatin1(kSettingsFmVersionControl), QString()).toString().trimmed();
  const bool has_version_control_command = !version_control_command.isEmpty();
  bool show_version_edit = false;
  bool show_version_commit = false;
  bool show_version_revert = false;
  bool show_version_diff = false;
  if (has_diff_command && has_version_control_command && !archive_mode &&
      single_selection && has_selected_real_items_without_parent_link) {
    const QFileInfo version_control_file(selected_paths_including_parent_link.at(0));
    constexpr qint64 kOriginalVersionControlMaxSize = qint64{1} << 31;
    if (version_control_file.exists() &&
        version_control_file.isFile() &&
        version_control_file.size() < kOriginalVersionControlMaxSize) {
      if (version_control_file.isWritable()) {
        show_version_commit = true;
        show_version_revert = true;
        show_version_diff = true;
      } else {
        show_version_edit = true;
      }
    }
  }

  const bool can_edit_fs = !archive_mode;
  const bool writable_current_dir = QFileInfo(current_directory()).isWritable();
  const bool filesystem_rename_enabled =
      !archive_mode && has_focused_non_parent && writable_current_dir;

  for (int i = 0; i < 2; ++i) {
    PanelController& panel = panel_controller(i);
    if (panel.model == nullptr) {
      continue;
    }
    const bool panel_can_rename =
        panel.in_archive_view()
            ? panel.archive.current_token.is_valid()
            : QFileInfo(panel.current_directory()).isWritable();
    panel.model->set_rename_enabled(panel_can_rename);
  }

  open_action_->setEnabled(has_selection);
  open_inside_action_->setEnabled(has_focused_item);
  open_inside_one_action_->setEnabled(has_focused_item);
  open_inside_parser_action_->setEnabled(has_focused_item);
  open_outside_action_->setEnabled(has_selected_real_items_without_parent_link);
  view_action_->setEnabled(has_focused_non_parent);
  edit_action_->setEnabled(has_focused_non_parent);
  rename_action_->setEnabled(archive_mode
                                 ? archive_preview_rename_enabled
                                 : filesystem_rename_enabled);
  copy_to_action_->setEnabled(has_selected_real_items_without_parent_link);
  move_to_action_->setEnabled(has_selected_real_items_without_parent_link);
  delete_action_->setEnabled(has_selected_real_items_without_parent_link);
  split_action_->setEnabled(single_selection && all_files && !archive_mode &&
                            backend_capabilities_.supports_split);
  combine_action_->setEnabled(single_selection && all_files && !archive_mode &&
                              backend_capabilities_.supports_combine);
  properties_action_->setEnabled(archive_mode ? archive_preview_properties_enabled
                                             : has_selected_real_items_without_parent_link);
  const bool diff_with_two_selected =
      selected_paths_including_parent_link.size() == 2;
  const bool diff_with_dual_panel_single_selection =
      single_selection &&
      two_panels_visible_ &&
      !in_archive_view_for_panel(1 - active_panel_index_);
  diff_action_->setVisible(has_diff_command);
  diff_action_->setEnabled(
      has_diff_command && !archive_mode &&
      (diff_with_two_selected || diff_with_dual_panel_single_selection));
  version_edit_action_->setVisible(show_version_edit);
  version_commit_action_->setVisible(show_version_commit);
  version_revert_action_->setVisible(show_version_revert);
  version_diff_action_->setVisible(show_version_diff);
  version_edit_action_->setEnabled(false);
  version_commit_action_->setEnabled(false);
  version_revert_action_->setEnabled(false);
  version_diff_action_->setEnabled(false);
  if (benchmark2_action_ != nullptr) {
    benchmark2_action_->setVisible(has_diff_command);
    benchmark2_action_->setEnabled(has_diff_command);
  }
  create_folder_action_->setEnabled(can_edit_fs && writable_current_dir);
  create_file_action_->setEnabled(can_edit_fs && writable_current_dir);
  if (select_action_ != nullptr) {
    select_action_->setEnabled(has_rows);
  }
  if (deselect_action_ != nullptr) {
    deselect_action_->setEnabled(has_rows);
  }
  if (select_by_type_action_ != nullptr) {
    select_by_type_action_->setEnabled(has_focused_non_parent);
  }
  if (deselect_by_type_action_ != nullptr) {
    deselect_by_type_action_->setEnabled(has_focused_non_parent);
  }
  if (add_to_favorites_menu_ != nullptr) {
    add_to_favorites_menu_->menuAction()->setEnabled(can_add_to_favorites);
  }
  if (comment_action_ != nullptr) {
    comment_action_->setEnabled(can_comment);
  }
  if (link_action_ != nullptr) {
    link_action_->setEnabled(can_link);
  }
  if (alternate_streams_action_ != nullptr) {
    alternate_streams_action_->setEnabled(can_open_alternate_streams);
  }
  if (temp_files_action_ != nullptr) {
    temp_files_action_->setEnabled(true);
  }

  compress_action_->setEnabled(archive_mode ? archive_preview_add_enabled
                                            : has_selected_real_items_without_parent_link);
  extract_action_->setEnabled(archive_mode ? has_oper_smart_archive_entries
                                           : has_extract_operands);
  test_action_->setEnabled(archive_mode ? archive_preview_test_enabled
                                        : has_oper_smart_real_items);
  if (crc_menu_ != nullptr) {
    crc_menu_->menuAction()->setEnabled(has_oper_smart_real_items);
  }
  if (open_parent_action_ != nullptr) {
    open_parent_action_->setEnabled(can_open_parent);
  }
  if (up_dir_button_ != nullptr) {
    up_dir_button_->setEnabled(can_open_parent);
  }
  if (folders_history_action_ != nullptr) {
    folders_history_action_->setEnabled(has_folder_history());
  }
  update_view_menu_checks();
}

void MainWindow::setup_ui() {
  setAcceptDrops(true);
  installEventFilter(this);

  resize(kDefaultMainWindowWidth, kDefaultMainWindowHeight);

  auto* central = new QWidget(this);
  central->setAcceptDrops(true);
  central->installEventFilter(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(4);

  auto* path_row = new QWidget(central);
  auto* path_row_layout = new QHBoxLayout(path_row);
  path_row_layout->setContentsMargins(0, 0, 0, 0);
  path_row_layout->setSpacing(4);

  up_dir_button_ = new QToolButton(path_row);
  up_dir_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogToParent));
  up_dir_button_->setAutoRaise(false);
  up_dir_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  path_row_layout->addWidget(up_dir_button_);

  auto* combo = new PathComboBox(path_row);
  combo->setEditable(true);
  combo->setInsertPolicy(QComboBox::NoInsert);
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  const int small_icon_extent = z7::platform::qt::small_icon_extent(this);
  combo->setIconSize(QSize(small_icon_extent, small_icon_extent));
  combo->before_show_popup = [this]() {
    rebuild_path_bar_popup_items();
  };
  path_combo_ = combo;
  path_combo_->lineEdit()->installEventFilter(this);
  path_row_layout->addWidget(path_combo_, 1);

  layout->addWidget(path_row);

  panels_splitter_ = new QSplitter(Qt::Horizontal, central);
  layout->addWidget(panels_splitter_, 1);

  for (int i = 0; i < 2; ++i) {
    PanelController& panel = panels_[i];
    panel.ui.container = new QWidget(panels_splitter_);
    auto* panel_layout = new QVBoxLayout(panel.ui.container);
    panel_layout->setContentsMargins(0, 0, 0, 0);
    panel_layout->setSpacing(0);

    panel.ui.view_stack = new QStackedWidget(panel.ui.container);
    panel.model = new DirectoryListModel(panel.ui.view_stack);
    panel.model->set_icon_style_context(this);
    panel.model->set_archive_drag_materializer(
        [this, i](const QStringList& entries,
                  DirectoryListModel::ArchiveDragMaterializedCallback finished_cb) {
          materialize_archive_drag_entries_for_panel(i, entries, std::move(finished_cb));
        });
    panel.model->set_rename_handler(
        [this, i](const QString& item_path,
                  const QString& new_name,
                  bool item_is_dir) {
          return start_rename_item_for_panel(
              i, item_path, new_name, item_is_dir);
        });

    panel.proxy = new z7::ui::widgets::StructuredListSortFilterProxy(panel.ui.view_stack);
    panel.proxy->setSourceModel(panel.model);

    auto* details_view = new DragAwareStructuredListView(panel.ui.view_stack);
    panel.ui.details_view = details_view;
    panel.ui.details_view->setModel(panel.proxy);
    panel.ui.details_view->set_config(make_details_view_config());
    panel.ui.details_view->setDragEnabled(true);
    panel.ui.details_view->setAcceptDrops(true);
    panel.ui.details_view->viewport()->setAcceptDrops(true);
    panel.ui.details_view->setIconSize(QSize(small_icon_extent, small_icon_extent));
    panel.ui.details_view->installEventFilter(this);
    panel.ui.details_view->viewport()->installEventFilter(this);

    auto* header = panel.ui.details_view->horizontalHeader();
    header->setMinimumSectionSize(column_width_persistence::kMinColumnWidth);
    header->setSectionsMovable(true);
    connect(header,
            &QHeaderView::sectionPressed,
            this,
            [this, i](int section) {
              PanelController& panel = panel_controller(i);
              panel.runtime.pending_layout_selection =
                  panel.capture_selection_snapshot();
              panel.runtime.has_pending_layout_selection = true;
              Q_UNUSED(section);
            });
    connect(header,
            &QHeaderView::sortIndicatorChanged,
            this,
            [this, i](int section, Qt::SortOrder order) {
              PanelController& panel = panel_controller(i);
              const int detected_sort_action = [&panel, section]() -> int {
                if (panel.ui.details_view == nullptr ||
                    !panel.ui.details_view->isSortingEnabled() ||
                    section < 0) {
                  return kSortActionUnsorted;
                }
                switch (section) {
                  case DirectoryListModel::kNameColumn:
                    return kSortActionName;
                  case DirectoryListModel::kTypeSortColumn:
                    return kSortActionType;
                  case DirectoryListModel::kModifiedColumn:
                    return kSortActionDate;
                  case DirectoryListModel::kSizeColumn:
                    return kSortActionSize;
                  default:
                    return panel.runtime.active_sort_action;
                }
              }();
              panel.runtime.active_sort_action = detected_sort_action;
              const PanelController::SelectionSnapshot selection_snapshot =
                  panel.runtime.has_pending_layout_selection
                      ? panel.runtime.pending_layout_selection
                      : panel.capture_selection_snapshot();
              panel.runtime.has_pending_layout_selection = false;
              Q_UNUSED(order);
              QTimer::singleShot(0, this, [this, i, selection_snapshot]() {
                PanelController& panel = panel_controller(i);
                panel.restore_selection_snapshot(selection_snapshot);
                QTimer::singleShot(0, this, [this, i, selection_snapshot]() {
                  PanelController& panel = panel_controller(i);
                  panel.restore_selection_snapshot(selection_snapshot);
                  if (i == active_panel_index_) {
                    refresh_action_states();
                  }
                });
              });
            });

    auto* icon_list_view = new DragAwareListView(panel.ui.view_stack);
    panel.ui.icon_list_view = icon_list_view;
    panel.ui.icon_list_view->setModel(panel.proxy);
    panel.ui.icon_list_view->setModelColumn(DirectoryListModel::kNameColumn);
    panel.ui.icon_list_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    panel.ui.icon_list_view->setDragEnabled(true);
    panel.ui.icon_list_view->setAcceptDrops(true);
    panel.ui.icon_list_view->viewport()->setAcceptDrops(true);
    panel.ui.icon_list_view->setContextMenuPolicy(Qt::CustomContextMenu);
    panel.ui.icon_list_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    panel.ui.icon_list_view->installEventFilter(this);
    panel.ui.icon_list_view->viewport()->installEventFilter(this);

    // Both views share a single selection model that operates on proxy indices.
    panel.ui.icon_list_view->setSelectionModel(panel.ui.details_view->selectionModel());

    details_view->set_drag_finished_callback(
        [this](const DragExecutionReport& report) {
          on_panel_drag_finished(report);
        });
    details_view->set_archive_drag_materializer(
        [this, i](const QStringList& entries,
                  DragAwareStructuredListView::ArchiveDragMaterializedCallback finished_cb) {
          materialize_archive_drag_entries_for_panel(i, entries, std::move(finished_cb));
        });
    details_view->set_archive_drag_direct_exporter(
        [this, i](const QString& archive_entry,
                  bool is_dir,
                  const QString& destination_path,
                  QString* error) {
          return export_archive_drag_entry_to_destination_for_panel(
              i, archive_entry, is_dir, destination_path, error);
        });
    icon_list_view->set_drag_finished_callback(
        [this](const DragExecutionReport& report) {
          on_panel_drag_finished(report);
        });
    icon_list_view->set_archive_drag_materializer(
        [this, i](const QStringList& entries,
                  DragAwareListView::ArchiveDragMaterializedCallback finished_cb) {
          materialize_archive_drag_entries_for_panel(i, entries, std::move(finished_cb));
        });
    icon_list_view->set_archive_drag_direct_exporter(
        [this, i](const QString& archive_entry,
                  bool is_dir,
                  const QString& destination_path,
                  QString* error) {
          return export_archive_drag_entry_to_destination_for_panel(
              i, archive_entry, is_dir, destination_path, error);
        });

    panel.ui.view_stack->addWidget(panel.ui.details_view);
    panel.ui.view_stack->addWidget(panel.ui.icon_list_view);
    panel_layout->addWidget(panel.ui.view_stack);

    panel.ui.status_bar = new QStatusBar(panel.ui.container);
    panel.ui.status_bar->setSizeGripEnabled(false);
    panel.ui.status_bar->setContentsMargins(0, 0, 0, 0);
    auto* selected_count = new QLabel(panel.ui.status_bar);
    auto* selected_size = new QLabel(panel.ui.status_bar);
    auto* focused_size = new QLabel(panel.ui.status_bar);
    auto* focused_modified = new QLabel(panel.ui.status_bar);
    auto* transient_message = new QLabel(panel.ui.status_bar);
    const QList<QLabel*> status_labels = {
        selected_count,
        selected_size,
        focused_size,
        focused_modified,
        transient_message,
    };
    for (QLabel* label : status_labels) {
      label->setFrameStyle(QFrame::NoFrame);
      label->setMargin(2);
      label->setTextInteractionFlags(Qt::NoTextInteraction);
      label->setTextFormat(Qt::PlainText);
      label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    selected_size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    focused_size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    focused_modified->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    transient_message->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    transient_message->setMinimumWidth(0);
    transient_message->setSizePolicy(QSizePolicy::Ignored,
                                     QSizePolicy::Preferred);
    selected_count->setMinimumWidth(220);
    selected_size->setMinimumWidth(100);
    focused_size->setMinimumWidth(100);
    focused_modified->setMinimumWidth(160);
    panel.ui.status_selected_count = selected_count;
    panel.ui.status_selected_size = selected_size;
    panel.ui.status_focused_size = focused_size;
    panel.ui.status_focused_modified = focused_modified;
    panel.ui.status_transient_message = transient_message;
    panel.ui.status_bar->addWidget(selected_count, 2);
    panel.ui.status_bar->addWidget(selected_size, 1);
    panel.ui.status_bar->addWidget(focused_size, 1);
    panel.ui.status_bar->addWidget(focused_modified, 2);
    panel.ui.status_bar->addWidget(transient_message, 4);
    panel_layout->addWidget(panel.ui.status_bar);
    panels_splitter_->addWidget(panel.ui.container);

    // Details view routes all interaction through explicit signals. No
    // manual sortByColumn on header click wiring: QTableView handles that
    // itself because setSortingEnabled(true) is set by set_config.
    connect(details_view,
            &z7::ui::widgets::StructuredListView::primary_clicked,
            this,
            [this, i](const QModelIndex& index) {
              set_active_panel(i);
              if (!display_settings_.single_click_open || !index.isValid()) {
                return;
              }
              auto* sm = panel_controller(i).ui.details_view->selectionModel();
              sm->setCurrentIndex(index,
                                  QItemSelectionModel::ClearAndSelect |
                                      QItemSelectionModel::Rows);
              if (activate_archive_parent_link_for_panel(i, index)) {
                return;
              }
              activate_panel_selection(QApplication::keyboardModifiers());
            });
    connect(details_view,
            &z7::ui::widgets::StructuredListView::primary_double_clicked,
            this,
            [this, i](const QModelIndex& index) {
              set_active_panel(i);
              if (!index.isValid()) return;
              if (activate_archive_parent_link_for_panel(i, index)) {
                return;
              }
              activate_panel_selection(QApplication::keyboardModifiers());
            });
    connect(details_view,
            &z7::ui::widgets::StructuredListView::primary_enter_pressed,
            this,
            [this, i](const QModelIndex&) {
              set_active_panel(i);
              activate_panel_selection(QApplication::keyboardModifiers());
            });
    connect(details_view,
            &z7::ui::widgets::StructuredListView::backspace_pressed,
            this,
            [this, i]() {
              set_active_panel(i);
              on_open_parent_requested();
            });
    connect(details_view,
            &z7::ui::widgets::StructuredListView::delete_pressed,
            this,
            [this, i]() {
              set_active_panel(i);
              on_delete_requested();
            });
    connect(details_view,
            &z7::ui::widgets::StructuredListView::context_menu_requested,
            this,
            [this, i](const QModelIndex& /*index*/,
                      const QPoint& viewport_pos,
                      const QPoint& /*global_pos*/) {
              set_active_panel(i);
              show_context_menu(viewport_pos);
            });

    // Icon view keeps Qt's default click/double-click/context-menu signals.
    auto on_item_activated = [this, i](const QModelIndex& index) {
      set_active_panel(i);
      if (!index.isValid()) return;
      if (activate_archive_parent_link_for_panel(i, index)) {
        return;
      }
      activate_panel_selection(QApplication::keyboardModifiers());
    };
    auto on_item_clicked = [this, i](const QModelIndex& index) {
      set_active_panel(i);
      if (!display_settings_.single_click_open || !index.isValid()) return;
      auto* sm = panel_controller(i).ui.details_view->selectionModel();
      sm->setCurrentIndex(index,
                          QItemSelectionModel::ClearAndSelect |
                              QItemSelectionModel::Rows);
      if (activate_archive_parent_link_for_panel(i, index)) {
        return;
      }
      activate_panel_selection(QApplication::keyboardModifiers());
    };
    connect(panel.ui.icon_list_view, &QListView::doubleClicked, this, on_item_activated);
    connect(panel.ui.icon_list_view, &QListView::clicked, this, on_item_clicked);
    connect(panel.ui.icon_list_view, &QWidget::customContextMenuRequested,
            this, &MainWindow::show_context_menu);

    connect(panel.ui.details_view->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this, i]() {
              set_active_panel(i);
              update_status();
              refresh_action_states();
            });
    connect(panel.ui.details_view->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this, i]() {
              set_active_panel(i);
              update_status();
            });
    connect(panel.model,
            &QAbstractItemModel::modelReset,
            this,
            [this, i]() {
              update_status_for_panel(i);
            });
    connect(panel.model,
            &QAbstractItemModel::dataChanged,
            this,
            [this, i](const QModelIndex&, const QModelIndex&,
                      const QList<int>&) {
              update_status_for_panel(i);
            });

    apply_view_mode_to_panel(i, PanelController::kViewModeDetails);
    panel.ui.details_view->sortByColumn(DirectoryListModel::kNameColumn,
                                        Qt::AscendingOrder);
    apply_model_display_settings_to_panel(i);
    panel.model->set_directory(QDir::homePath());
    apply_archive_preview_columns_visibility_for_panel(i);
    update_status_for_panel(i);
  }

  two_panels_visible_ = false;
  panels_[1].ui.container->setVisible(false);
  panels_splitter_->setStretchFactor(0, 1);
  panels_splitter_->setStretchFactor(1, 1);
  panels_splitter_->setSizes({1, 0});
  load_details_column_state();

  set_active_panel(0);
  setCentralWidget(central);

  auto_refresh_timer_ = new QTimer(this);
  auto_refresh_timer_->setInterval(1000);
}

}  // namespace z7::ui::filemanager
