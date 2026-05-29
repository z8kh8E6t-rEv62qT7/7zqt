#pragma once
  struct SevenZipMenuState;
  void setup_ui();
  void setup_actions();
  void setup_connections();
  void retranslate_ui();
  void apply_toolbar_action_texts();
  void show_context_menu(const QPoint& pos);
  void populate_context_menu(QMenu* menu, const SevenZipMenuState& seven_zip_state);
  void sync_path_bar_from_current_dir();
  void rebuild_path_bar_popup_items();
  bool navigate_to_path_from_bar(const QString& path_text);
  bool can_open_parent_from_current_dir() const;
  void rebuild_file_menu_seven_zip_section();
  void remember_path_history(const QString& abs_path);
  void rebuild_file_crc_menu();
  void populate_crc_hash_menu(
      QMenu* menu,
      bool enabled,
      const std::function<void(const QString&)>& on_trigger);
  void refresh_action_states();
  void refresh_active_panel_chrome();
  void update_status();
  void update_status_for_panel(int panel_index);
  void show_transient_status_message(const QString& message,
                                     int timeout_ms,
                                     int panel_index = -1);
  void clear_transient_status_messages();
  void update_window_title();
  void refresh_directory();
  bool refresh_directory_for_panel(int panel_index);
  void ensure_auto_refresh_watcher_for_panel(int panel_index);
  void rebind_auto_refresh_watcher_for_panel(int panel_index);
  void mark_panel_auto_refresh_dirty(int panel_index);
  void on_auto_refresh_timer_tick();
  void set_current_directory(const QString& path);
  void set_current_directory_for_panel(int panel_index, const QString& path);
  static QVector<int> default_details_column_widths();
  void restore_main_window_geometry();
  void save_main_window_geometry() const;
  void load_details_column_state();
  void save_details_column_state() const;
  void restore_panel_ui_state_from_settings();
  void save_panel_ui_state() const;
  void load_folder_history();
  void save_folder_history() const;
  void set_folder_history(const QStringList& history);
  void restore_panel_paths_from_settings();
  void save_panel_paths() const;
  void remember_folder_history(const QString& path);
  bool has_folder_history() const;
  bool open_folder_prefix_for_panel(int panel_index, const QString& path);
  QVector<z7::app::ArchiveSessionToken> run_shutdown_cleanup_once();
  static void dispatch_detached_archive_session_close(
      QVector<z7::app::ArchiveSessionToken> tokens);
  void load_runtime_settings();
  void apply_runtime_settings();
  void apply_model_display_settings_to_panel(int panel_index);
  void apply_model_display_settings_to_all_panels();
  bool selected_rows_include_parent_link_for_panel(int panel_index) const;
  bool active_selected_rows_include_parent_link() const;
  bool has_selected_real_items_for_panel(int panel_index) const;
  bool has_selected_real_items() const;
  bool has_oper_smart_real_items_for_panel(int panel_index) const;
  bool has_oper_smart_real_items() const;
  void set_operable_rows_selected_for_panel(int panel_index, bool selected);
  void invert_operable_selection_for_panel(int panel_index);
  QString current_directory() const;
  QString current_directory_for_panel(int panel_index) const;
  QString focused_comment_for_panel(int panel_index) const;
  QStringList selected_filesystem_paths_including_parent_link() const;
  QStringList selected_filesystem_paths_including_parent_link_for_panel(int panel_index) const;
  QStringList selected_real_archive_file_paths_for_panel(int panel_index) const;
  QString selected_archive_path() const;
  QStringList selected_archive_entries() const;
  QStringList archive_test_entries_for_panel(int panel_index) const;
  void select_archive_entry_for_panel(int panel_index, const QString& archive_entry);
  bool in_archive_view() const;
  bool in_archive_view_for_panel(int panel_index) const;
  void clear_archive_view();
  QString archive_virtual_display_path_for_panel(int panel_index) const;
  bool load_archive_virtual_directory_for_panel(
      int panel_index,
      const QString& archive_path,
      const QString& virtual_dir,
      const QString& origin_dir,
      const QString& archive_type_hint,
      bool update_origin_dir,
      const std::function<void(bool)>& finished_cb = {},
      bool suppress_unsupported_warning = false,
      const std::function<void(int, const QString&)>& failed_cb = {},
      z7::app::ArchiveSessionToken session_token = {},
      const QString& virtual_display_source = {});
  bool apply_archive_list_result_for_panel(
      int panel_index,
      const QString& archive_path,
      const QString& virtual_dir,
      const QString& origin_dir,
      const QString& archive_type_hint,
      const z7::app::ListResult& list_result,
      bool update_origin_dir,
      z7::app::ArchiveSessionToken session_token = {},
      const QString& virtual_display_source = {});
  bool reload_archive_virtual_directory_for_panel(int panel_index);
  bool reload_archive_virtual_directory_for_panel(
      int panel_index,
      const std::function<void(bool)>& finished_cb);
  void reload_archive_virtual_directories_serially(
      QVector<int> panel_indexes,
      const std::function<void()>& finished_cb = {});
  void close_archive_view_for_panel(
      int panel_index,
      const std::function<void(bool)>& finished_cb = {});
  void close_archive_sessions_async(
      QVector<z7::app::ArchiveSessionToken> tokens,
      const std::function<void(bool)>& finished_cb = {});
  QSharedPointer<QTemporaryDir> create_temporary_directory_with_prefix(
      const QString& prefix,
      const QString& failure_caption);
  QSharedPointer<QTemporaryDir> create_archive_open_temporary_directory(
      const QString& failure_caption);
  QSharedPointer<QTemporaryDir> create_archive_drag_temporary_directory(
      const QString& failure_caption);
  void materialize_archive_drag_entries_for_panel(
      int panel_index,
      const QStringList& entries,
      const std::function<void(const QStringList&, const QString&)>& finished_cb);
  bool export_archive_drag_entry_to_destination_for_panel(
      int panel_index,
      const QString& archive_entry,
      bool entry_is_dir,
      const QString& destination_path,
      QString* error);
  void open_archive_entries_outside_for_panel(
      int panel_index,
      const QStringList& entries);
  void open_archive_file_inside_for_panel(
      int panel_index,
      const QString& entry_path,
      const QString& archive_type_hint);
  void open_archive_inside(const QString& archive_path,
                           const QString& archive_type_hint = QString());
  void open_archive_inside_for_panel(int panel_index,
                                     const QString& archive_path,
                                     const QString& archive_type_hint = QString(),
                                     std::function<void()> open_failure_fallback = {});
  struct ArchiveOpenSelectionTarget {
    QStringList entries;
    QString single_entry_path;
    bool single_entry_is_dir = false;
  };
  bool activate_archive_parent_link_for_panel(
      int panel_index,
      const QModelIndex& view_index = QModelIndex());
  void activate_panel_selection(Qt::KeyboardModifiers modifiers);
  void open_selected_archive_entries(bool try_internal);
  void open_selected_filesystem_paths_including_parent_link(bool try_internal);
  ArchiveOpenSelectionTarget resolve_archive_open_selection_target(
      int panel_index) const;
  void open_focused_item_as_internal(
      const QString& archive_type_hint = QString());
  void open_item(const QString& path,
                 bool try_internal,
                 bool try_external,
                 const QString& archive_type_hint = QString());
  QString focused_path_for_panel(int panel_index) const;
  bool open_path_externally_untracked(const QString& path);
  bool confirm_external_open_targets_safe(const QStringList& paths,
                                          const QString& caption);
  struct OpenOutsideLaunchResult {
    QStringList launched_paths;
    QStringList failed_paths;
    QString message;
    QVector<QPointer<QProcess>> tracked_processes;
#if defined(Q_OS_WIN)
    QVector<void*> tracked_process_handles;
#endif

    bool has_success() const { return !launched_paths.isEmpty(); }
    bool has_failures() const { return !failed_paths.isEmpty(); }
    bool has_tracked_lifecycle() const {
      for (const QPointer<QProcess>& process : tracked_processes) {
        if (process != nullptr) {
          return true;
        }
      }
#if defined(Q_OS_WIN)
      for (void* handle : tracked_process_handles) {
        if (handle != nullptr) {
          return true;
        }
      }
#endif
      return false;
    }
  };
  OpenOutsideLaunchResult launch_open_outside(const QStringList& paths,
                                              const QString& working_dir);
  struct ArchiveTempFileSnapshot {
    QString archive_entry;
    QString extracted_path;
    bool existed = false;
    qint64 size = -1;
    qint64 mtime_msecs_utc = -1;
  };
  enum class ArchiveTempSessionPurpose {
    kDragOut,
    kViewEdit,
    kOpenInside,
    kOpenOutside
  };
  enum class OpenOutsideCleanupPolicy {
    kRetainUntilClose,
    kReleaseWhenTrackersDrain
  };
  struct ArchiveWritebackFrame {
    // Empty for the top-level archive frame. Non-empty for every nested frame,
    // and names the archive entry selected in the parent archive to enter this
    // frame.
    QString archive_entry_from_parent;
    QString virtual_display_source;
    QString virtual_dir;
    QString type_hint;
  };
  struct ArchiveWritebackPlan {
    QString source_archive;
    QString origin_dir;
    // Ordered archive frames from the top-level archive view to the currently
    // visible nested frame. The last frame is the one that must be reloaded
    // after a writeback-style command succeeds.
    QVector<ArchiveWritebackFrame> reopen_frames;
    // Nested archive entry path from one frame to the next, suitable for
    // feeding backend add/delete/update/open-outside requests.
    QStringList nested_archive_entries;

    bool is_valid() const;
    QString root_display_source() const;
    QString root_type_hint() const;
    QString current_display_source() const;
    QString current_virtual_dir() const;
    QString current_type_hint() const;
  };
 public:
  void open_startup_target(const QString& path,
                           const QString& archive_type_hint = QString());
  struct NativeProcessSnapshotEntry {
    quint32 process_id = 0;
    quint32 parent_process_id = 0;
    quint64 start_time_token = 0;
  };
 private:
  struct ArchiveTempSession {
    ArchiveTempSessionPurpose purpose = ArchiveTempSessionPurpose::kViewEdit;
    QSharedPointer<QTemporaryDir> temp_dir;
    QString archive_path;
    QString archive_display_source;
    QString archive_type_hint;
    z7::app::ArchiveSessionToken session_token;
    QString command_caption;
    QVector<ArchiveTempFileSnapshot> file_snapshots;
    QStringList extracted_paths;
    QVector<QPointer<QProcess>> tracked_processes;
    QPointer<QProcess> process;
#if defined(Q_OS_WIN)
    QVector<void*> tracked_process_handles;
#endif
    int pending_open_outside_trackers = 0;
    OpenOutsideCleanupPolicy open_outside_cleanup_policy =
        OpenOutsideCleanupPolicy::kRetainUntilClose;
    bool process_finished_handled = false;
  };
  ArchiveTempFileSnapshot capture_archive_temp_file_snapshot(
      const QString& archive_entry,
      const QString& extracted_path) const;
  bool archive_temp_file_snapshot_changed(
      const ArchiveTempFileSnapshot& snapshot) const;
  QString archive_update_format_for_session(
      const ArchiveTempSession& session) const;
  void update_archive_entries_from_snapshots(
      const ArchiveTempSession& session,
      const QVector<ArchiveTempFileSnapshot>& changed_snapshots,
      const std::function<void(bool, const QString&)>& finished_cb) const;
  ArchiveWritebackPlan build_archive_writeback_plan_for_panel(
      int panel_index) const;
  bool can_add_external_files_to_archive_preview(int panel_index) const;
  bool select_model_path_for_panel(int panel_index, const QString& model_path);
  bool edit_focused_item_label_for_panel(int panel_index);
  bool start_rename_item_for_panel(int panel_index,
                                   const QString& item_path,
                                   const QString& new_name,
                                   bool item_is_dir);
  bool start_rename_filesystem_item_for_panel(int panel_index,
                                              const QString& source_path,
                                              const QString& new_name);
  bool start_add_external_files_to_archive_preview(
      int panel_index,
      const QStringList& input_paths,
      const QString& archive_destination_virtual_dir,
      const QString& caption);
  bool start_add_mapped_files_to_archive_preview(
      const ArchiveWritebackPlan& plan,
      z7::app::ArchiveSessionToken session_token,
      const QVector<ArchiveAddInputItem>& input_items,
      const QString& caption,
      bool reload_on_success = true,
      const std::function<void(bool,
                               int,
                               int,
                               const QString&,
                               const z7::app::OperationOutcome&)>&
          finished_cb = {});
  bool start_rename_archive_entry_in_preview(
      int panel_index,
      const QString& archive_entry,
      const QString& new_name,
      bool entry_is_dir);
  void restore_archive_writeback_plan_for_panel(
      int panel_index,
      const ArchiveWritebackPlan& plan,
      const std::function<void(bool)>& finished_cb = {});
  void retain_archive_temp_session(const QSharedPointer<ArchiveTempSession>& session);
  void release_archive_temp_session(const QSharedPointer<ArchiveTempSession>& session);
  void reload_matching_archive_writeback_panels(
      const QString& archive_path,
      const QString& archive_display_source,
      z7::app::ArchiveSessionToken session_token);
  QString locate_7zg_program() const;
  void ensure_task_ipc_completion_notifier();
  void poll_task_ipc_completions();
  bool payload_represents_archive_scoped_refresh(
      const z7::task_ipc_runtime::TaskIpcPayload& payload) const;
  bool archive_refresh_target_matches_panel(
      const z7::task_ipc_runtime::TaskIpcPayload& payload,
      int panel_index) const;
  bool refresh_archive_panels_for_task_ipc_payload(
      const z7::task_ipc_runtime::TaskIpcPayload& payload);
  void finalize_archive_temp_session(const QSharedPointer<ArchiveTempSession>& session);
  void on_open_outside_temp_session_tracking_finished(
      const QSharedPointer<ArchiveTempSession>& session);
  void on_archive_temp_session_process_finished(
      const QSharedPointer<ArchiveTempSession>& session);
  void on_open_outside_temp_session_finished(
      const QSharedPointer<ArchiveTempSession>& session);
  struct PanelController {
    enum ViewMode {
      kViewModeLargeIcons = 0,
      kViewModeSmallIcons = 1,
      kViewModeList = 2,
      kViewModeDetails = 3
    };
    struct UiRefs {
      QWidget* container = nullptr;
      QStackedWidget* view_stack = nullptr;
      DragAwareStructuredListView* details_view = nullptr;
      QListView* icon_list_view = nullptr;
      QStatusBar* status_bar = nullptr;
      QLabel* status_selected_count = nullptr;
      QLabel* status_selected_size = nullptr;
      QLabel* status_focused_size = nullptr;
      QLabel* status_focused_modified = nullptr;
      QLabel* status_transient_message = nullptr;
    } ui;
    DirectoryListModel* model = nullptr;
    z7::ui::widgets::StructuredListSortFilterProxy* proxy = nullptr;
    ViewMode view_mode = kViewModeDetails;
    struct SelectionSnapshot {
      QStringList selected_filesystem_paths_including_parent_link;
      QString current_path;
    };
    struct RuntimeState {
      QFileSystemWatcher* auto_refresh_watcher = nullptr;
      QString auto_refresh_watched_dir;
      bool auto_refresh_dirty = false;
      SelectionSnapshot pending_layout_selection;
      bool has_pending_layout_selection = false;
      int active_sort_action = 0;  // kSortActionName; enum is internal-only.
    } runtime;
    struct ViewScrollPosition {
      bool valid = false;
      int horizontal_value = 0;
      int vertical_value = 0;
    };
    struct ScrollPositionSnapshot {
      ViewScrollPosition details;
      ViewScrollPosition icon;
    };
    struct ArchiveState {
      struct ParentContext {
        QString archive_path;
        QString archive_entry_from_parent;
        QString virtual_display_source;
        QString virtual_dir;
        QString origin_dir;
        QString type_hint;
        QSharedPointer<ArchiveTempSession> temp_session;
        // Registry token for the archive session this frame represents. Used
        // by on_open_parent_requested to close the current token and restore
        // the parent's token when navigating up out of a nested archive.
        z7::app::ArchiveSessionToken session_token;
      };
      bool view_enabled = false;
      QString virtual_dir;
      QString source_archive;
      // Empty for the top-level archive. Non-empty when the currently visible
      // archive view was entered from a parent archive entry.
      QString archive_entry_from_parent;
      QString virtual_display_source;
      QString origin_dir;
      QString type_hint;
      QVector<ParentContext> parent_stack;
      QSharedPointer<ArchiveTempSession> temp_session;
      // Registry token for the currently active archive session (top of the
      // nested stack). Invalid when not viewing an archive.
      z7::app::ArchiveSessionToken current_token;
    } archive;
    struct ParentArchiveReturnTransition {
      ArchiveState::ParentContext parent;
      QString leaving_archive_entry_from_parent;
      QSharedPointer<ArchiveTempSession> leaving_temp_session;
      z7::app::ArchiveSessionToken leaving_token;
    };
    struct ArchiveFilesystemExitTransition {
      QString target_directory;
      QVector<z7::app::ArchiveSessionToken> tokens_to_close;
      bool refresh_directory_after_exit = false;
    };
    void set_view_mode(ViewMode mode);
    bool in_archive_view() const;
    QString current_directory() const;
    QString archive_display_source() const;
    QString favorite_folder_prefix() const;
    QString focused_path() const;
    bool focused_item_is_dir() const;
    bool focused_item_is_parent_link() const;
    QStringList selected_filesystem_paths_including_parent_link() const;
    QStringList selected_real_archive_file_paths() const;
    bool selected_rows_include_parent_link() const;
    QModelIndexList selected_real_item_rows() const;
    QModelIndexList oper_smart_real_item_rows() const;
    bool source_rows_contain_dir(const QModelIndexList& rows) const;
    QStringList real_item_paths_for_rows(const QModelIndexList& rows) const;
    QStringList selected_real_item_paths() const;
    QStringList oper_smart_real_item_paths() const;
    void clear_auto_refresh_binding();
    void mark_auto_refresh_dirty();
    bool auto_refresh_needs_rebind(const QString& current_dir) const;
    void enter_archive_view(const QString& source_archive,
                            const QString& normalized_virtual_dir,
                            const QString& origin_dir,
                            const QString& trimmed_type_hint,
                            bool update_origin_dir);
    ArchiveState::ParentContext current_archive_parent_context() const;
    void push_current_archive_to_parent_stack();
    bool discard_last_parent_archive_frame();
    std::optional<ParentArchiveReturnTransition> begin_return_to_parent_archive();
    void commit_return_to_parent_archive(
        const ParentArchiveReturnTransition& transition);
    void rollback_return_to_parent_archive(
        const ParentArchiveReturnTransition& transition);
    ArchiveFilesystemExitTransition begin_exit_archive_view_to_filesystem(
        const QString& target_directory,
        const std::function<void(const QSharedPointer<ArchiveTempSession>&)>&
            release_temp_session);
    void clear_archive_view_state(
        const std::function<void(const QSharedPointer<ArchiveTempSession>&)>&
            release_temp_session,
        const std::function<void(z7::app::ArchiveSessionToken)>&
            close_session = {});
    // Archive-mode selection query. Returns normalized virtual entries for the
    // current selection, suitable for feeding extract/test/open-outside command
    // arguments. Parent-link rows and empty entries are filtered out.
    QStringList selected_archive_entries() const;
    QStringList archive_entries_for_source_rows(const QModelIndexList& rows) const;
    QStringList operated_archive_entries() const;
    QStringList oper_smart_archive_entries() const;
    // Native display path "/archive.ext/relative/virtual/dir" for this panel.
    // Falls back to the current directory when the panel is not in archive view.
    QString archive_virtual_display_path() const;
    // True when this panel currently points at the same archive target that a
    // temp-session writeback just updated.
    bool matches_archive_writeback_target(const QString& source_archive,
                                          const QString& archive_display_source) const;
    // Rebuild just the parent-stack/session-token portion of the panel's
    // archive state after a writeback reload has already updated the visible
    // frame via apply_archive_list_result_for_panel().
    void apply_restored_writeback_plan(
        const ArchiveWritebackPlan& plan,
        const QVector<z7::app::ArchiveSessionToken>& opened_tokens);
    // Rebase the archive state to a brand-new top-level archive: releases the
    // current temp session and every parent-stack temp session through
    // release_temp_session, clears the parent stack, and resets the virtual
    // display source to source_archive.
    void reset_archive_state_for_rebase(
        const QString& source_archive,
        const std::function<void(const QSharedPointer<ArchiveTempSession>&)>&
            release_temp_session,
        const std::function<void(z7::app::ArchiveSessionToken)>&
            close_session = {});
    QAbstractItemView* current_item_view() const;
    ScrollPositionSnapshot capture_scroll_position() const;
    void restore_scroll_position(const ScrollPositionSnapshot& snapshot) const;
    SelectionSnapshot capture_selection_snapshot() const;
    void restore_selection_snapshot(const SelectionSnapshot& snapshot) const;

    // Selection bridging. The details view's selection model lives on the
    // proxy, but every business call site wants source-model rows so it can
    // talk to DirectoryListModel::path_for_row / is_dir_for_row / etc.
    QItemSelectionModel* selection_model() const;
    QModelIndex map_proxy_to_source(const QModelIndex& proxy_index) const;
    QModelIndex map_source_to_proxy(const QModelIndex& source_index) const;
    QModelIndex focused_source_index() const;
    QModelIndexList selected_rows_including_parent_link() const;
  };
  struct CrossPanelArchiveBindTarget {
    QString source_archive;
    QString virtual_dir;
    QString origin_dir;
    QString type_hint;
    QString virtual_display_source;
    QString archive_entry_from_parent;
    QVector<PanelController::ArchiveState::ParentContext> parent_stack;
    QSharedPointer<ArchiveTempSession> temp_session;
    z7::app::ArchiveSessionToken session_token;
  };
  struct CrossPanelBindTarget {
    bool archive = false;
    QString filesystem_dir;
    CrossPanelArchiveBindTarget archive_target;
  };
  PanelController& panel_controller(int panel_index);
  const PanelController& panel_controller(int panel_index) const;
  PanelController& active_panel_controller();
  const PanelController& active_panel_controller() const;
  QAbstractItemView* active_item_view() const;
  QModelIndex hovered_source_index_for_panel(int panel_index,
                                             QAbstractItemView* view,
                                             const QObject* watched,
                                             const QPoint& watched_pos) const;
  QAbstractItemView* drop_target_view_for_panel(const PanelController& panel,
                                                const QObject* watched) const;
  DropTargetInfo resolve_drop_target_info_for_panel(
      int panel_index,
      QAbstractItemView* view,
      const QObject* watched,
      const QDropEvent* event,
      const QString& fallback_directory) const;
  int panel_index_for_view(const QObject* view_object) const;
  void set_active_panel(int panel_index);
  void bind_opposite_panel_to_same_folder();
  void bind_opposite_panel_to_focused_folder();
  bool bind_panel_to_filesystem_directory(int panel_index,
                                          const QString& directory);
  bool bind_panel_to_archive_target(
      int panel_index,
      const CrossPanelArchiveBindTarget& target);
  CrossPanelArchiveBindTarget archive_bind_target_from_panel(
      int panel_index,
      const QString& virtual_dir) const;
  std::optional<CrossPanelBindTarget> focused_archive_bind_target_for_panel(
      int panel_index) const;
  bool archive_session_token_referenced_outside_panel(
      int panel_index,
      z7::app::ArchiveSessionToken token) const;
  QVector<z7::app::ArchiveSessionToken>
  filter_archive_session_tokens_for_panel_close(
      int panel_index,
      QVector<z7::app::ArchiveSessionToken> tokens) const;
  bool archive_temp_session_referenced_outside_panel(
      int panel_index,
      const QSharedPointer<ArchiveTempSession>& session) const;
  void release_archive_temp_session_for_panel_close(
      int panel_index,
      const QSharedPointer<ArchiveTempSession>& session);
  void apply_archive_preview_columns_visibility_for_panel(int panel_index);
  void refresh_all_details_column_visibility();
  void apply_view_mode_to_panel(int panel_index, int view_mode);
  void apply_sort_mode_to_panel(int panel_index, int sort_mode, bool toggle_if_same);
  void update_view_menu_checks();
  void update_time_menu();
  void rebuild_favorites_menu();
  void on_view_mode_action_triggered(int view_mode);
  void on_sort_mode_action_triggered(int sort_mode);
  void on_flat_view_action_triggered();
  void on_two_panels_action_triggered();
  void on_folders_history_requested();
  void on_toggle_time_utc();
  void on_time_precision_requested(int timestamp_level);
  QString format_sample_time_for_level(int timestamp_level) const;
  void on_open_requested();
  void on_open_inside_requested();
  void on_open_inside_one_requested();
  void on_open_inside_parser_requested();
  void on_open_outside_requested();
  void on_view_requested();
  void on_edit_requested();
  void on_diff_requested();
  void on_compress_requested();
  void on_extract_requested();
  void on_test_requested();
  void on_hash_with_method_requested(const QString& method);
  void on_refresh_requested();
  void on_open_parent_requested();
  void on_open_root_requested();
  void on_rename_requested();
  void on_copy_to_requested();
  void on_move_to_requested();
  void on_delete_requested();
  void on_split_requested();
  void on_combine_requested();
  void on_create_folder_requested();
  void on_create_file_requested();
  void on_options_requested();
  void on_benchmark_requested();
  void on_benchmark2_requested();
  bool handle_favorite_slot_shortcut(const QKeyEvent& event,
                                     Qt::KeyboardModifiers modifiers);
  void set_favorite_slot(int slot);
  void open_favorite_slot(int slot);
  void open_favorite_path(const QString& path);
  void on_add_to_favorites_requested();
  void on_open_favorite_requested();
  void on_select_requested();
  void on_deselect_requested();
  void on_select_by_type_requested();
  void on_deselect_by_type_requested();
  void on_temp_files_requested();
  void on_contents_requested();
  void on_comment_requested();
  void on_link_requested();
  void on_alternate_streams_requested();
  struct DropSourceState {
    int source_panel_index = -1;
    bool same_panel_source = false;
    bool internal_fs_source = false;
    bool trusted_internal_fs_source = false;
    bool internal_archive_source = false;
    bool trusted_internal_archive_source = false;
    bool source_target_same_volume = false;
    QString archive_source_path;
    QString archive_source_type_hint;
    QStringList archive_source_entries;
    z7::app::ArchiveSessionToken archive_source_session_token;
  };
  DropSourceState resolve_drop_source_state(
      int panel_index,
      bool window_drop_target,
      const QDropEvent* drop_event,
      const QStringList& dropped_paths,
      const QString& drop_target_directory) const;
  bool handle_panel_drag_enter_or_move(QObject* watched,
                                       int panel_index,
                                       bool window_drop_target,
                                       QDropEvent* drop_like_event);
  bool handle_panel_drop(QObject* watched,
                         int panel_index,
                         bool window_drop_target,
                         QDropEvent* drop_event);
  void on_panel_drag_finished(const z7::ui::filemanager::DragExecutionReport& report);
  void run_copy_or_move(bool move, bool copy_to_same = false);
  QString copy_move_info_text_for_source_rows(
      int panel_index,
      const QModelIndexList& rows) const;
  bool run_filesystem_transfer_to_archive_panel(int target_panel_index,
                                                const QStringList& sources,
                                                bool move,
                                                const QString& history_path,
                                                const QString& caption);
  void run_copy_or_move_archive_same_folder(bool move);
  void run_copy_or_move_archive_context(bool move);
  QString default_target_directory_for_transfer() const;
  void show_properties_dialog();
  struct PropertiesDialogRow {
    enum class Kind {
      kPair,
      kSeparator,
      kSeparatorSmall
    };
    Kind kind = Kind::kPair;
    QString key;
    QString value;
  };
  void show_properties_table(const QString& title,
                             const QVector<PropertiesDialogRow>& rows);
  void run_view_or_edit_requested(bool use_editor_command);
  void run_archive_view_or_edit_with_command(const QString& command_line,
                                             const QString& caption);
  bool run_external_command_with_targets(const QString& command_line,
                                         const QStringList& targets,
                                         const QString& working_dir,
                                         QString* error_message = nullptr,
                                         bool controlled_process = false,
                                         QProcess** started_process = nullptr);
  bool resolve_diff_targets(QString* path1, QString* path2) const;
  struct SevenZipMenuState {
    bool visible = false;
    bool show_open = false;
    bool show_open_as = false;
    bool show_extract_group = false;
    bool show_test = false;
    bool show_compress_group = false;
    bool show_crc_group = false;
    QStringList selected_real_item_paths;
    QStringList selected_files;
    QString base_folder;
    QString extract_subdir;
    QString archive_name;
    QString archive_name_7z;
    QString archive_name_zip;
  };
  SevenZipMenuState compute_seven_zip_menu_state(bool shift_pressed) const;
  QMenu* append_seven_zip_submenu(QMenu* menu,
                                  const SevenZipMenuState& state,
                                  QAction* insert_before = nullptr);
  QStringList selected_file_paths() const;
  QString create_archive_name(bool is_hash, QString* base_name = nullptr) const;
  bool build_archive_scoped_open_payload_for_panel(
      int panel_index,
      z7::task_ipc_runtime::TaskIpcOpenPayload* out_payload) const;
  QString archive_export_info_text_for_panel(
      int panel_index,
      const QStringList& archive_entries) const;
  bool run_archive_export_from_active_panel();
  QString suggested_extract_subdir_for_menu() const;
  bool hash_recursive_dirs_for_active_panel() const;
  void run_sevenzip_open_archive();
  void run_sevenzip_open_archive_as(const QString& type);
  void run_sevenzip_extract_files_dialog();
  void run_sevenzip_extract_here();
  void run_sevenzip_extract_to();
  void run_sevenzip_test_archive();
  void run_sevenzip_add_to_archive();
  void run_sevenzip_add_to_type(const QString& type);
  void run_sevenzip_hash(const QString& method);
  void run_sevenzip_generate_sha256();
  void run_sevenzip_checksum_test();
  bool launch_7zfm_open_request(const QString& caption,
                                const QString& target_path,
                                const QString& archive_type_hint);
  bool launch_gui_subprocess_task(const QString& caption,
                                  const z7::task_ipc_runtime::TaskIpcPayload& payload);
  void start_split_task(const QString& source_file_path,
                        const QString& output_dir,
                        const QString& volume_size_spec);
  void start_combine_task(const QString& source_part_path,
                          const QString& output_dir);
  void start_hash_task(const QStringList& inputs,
                       const QString& hash_method,
                       bool recursive_dirs = true);
  QVector<QPair<QString, QString>> build_hash_result_rows(
      const z7::app::HashSummary& summary) const;
  enum class RunnerTaskUiMode {
    kDefault,
    kSilent
  };
  bool start_archive_source_extract_task(
      const QString& task_header,
      const QString& failure_caption,
      const QString& archive_path,
      const QString& archive_type_hint,
      z7::app::ArchiveSessionToken session_token,
      const QString& output_dir,
      OverwriteMode overwrite_mode,
      const QStringList& archive_entries,
      const std::function<void(bool,
                               int,
                               int,
                               const QString&,
                               const z7::app::OperationOutcome&)>&
          finished_cb = {},
      RunnerTaskUiMode task_ui_mode = RunnerTaskUiMode::kDefault,
      const std::function<bool(int, const QString&)>& should_show_failure = {});
  QStringList extracted_archive_entry_paths(
      const QString& temp_dir_path,
      const QStringList& archive_entries) const;
  QVector<ArchiveTempFileSnapshot> extracted_archive_entry_snapshots(
      const QString& temp_dir_path,
      const QStringList& archive_entries) const;
  bool start_task_with_runner(const QString& header,
                              const QString& failure_caption,
                              const std::function<bool(ArchiveProcessRunner*)>& start_fn,
                              const std::function<void(bool,
                                                       int,
                                                       int,
                                                       const QString&,
                                                       const z7::app::OperationOutcome&)>&
                                  finished_cb = {},
                              RunnerTaskUiMode task_ui_mode = RunnerTaskUiMode::kDefault,
                              const std::function<bool(int, const QString&)>&
                                  should_show_failure = {});
  struct RunningTaskContext {
    QPointer<ArchiveProcessRunner> runner;
    QPointer<TaskProgressDialog> dialog;
  };
  QComboBox* path_combo_ = nullptr;
  QToolButton* up_dir_button_ = nullptr;
  QSplitter* panels_splitter_ = nullptr;
  std::array<PanelController, 2> panels_{};
  int active_panel_index_ = -1;
  DisplaySettings display_settings_{};
  bool two_panels_visible_ = false;
  bool shutdown_cleanup_started_ = false;
  z7::app::BackendCapabilities backend_capabilities_{};
  QMenu *file_menu_ = nullptr, *edit_menu_ = nullptr, *view_menu_ = nullptr,
        *favorites_menu_ = nullptr, *tools_menu_ = nullptr, *help_menu_ = nullptr,
        *crc_menu_ = nullptr, *toolbars_submenu_ = nullptr, *time_submenu_ = nullptr,
        *add_to_favorites_menu_ = nullptr, *file_menu_seven_zip_menu_ = nullptr;
  QAction* file_menu_seven_zip_separator_action_ = nullptr;
  QToolBar* archive_toolbar_ = nullptr;
  QToolBar* standard_toolbar_ = nullptr;
  QAction *open_action_ = nullptr, *open_inside_action_ = nullptr,
          *open_inside_one_action_ = nullptr, *open_inside_parser_action_ = nullptr,
          *open_outside_action_ = nullptr, *view_action_ = nullptr, *edit_action_ = nullptr,
          *rename_action_ = nullptr, *copy_to_action_ = nullptr, *move_to_action_ = nullptr,
          *delete_action_ = nullptr, *split_action_ = nullptr, *combine_action_ = nullptr,
          *properties_action_ = nullptr, *comment_action_ = nullptr, *diff_action_ = nullptr,
          *version_edit_action_ = nullptr, *version_commit_action_ = nullptr,
          *version_revert_action_ = nullptr, *version_diff_action_ = nullptr,
          *create_folder_action_ = nullptr, *create_file_action_ = nullptr,
          *exit_action_ = nullptr, *link_action_ = nullptr,
          *alternate_streams_action_ = nullptr;
  QAction *compress_action_ = nullptr, *extract_action_ = nullptr, *test_action_ = nullptr;
  QAction *select_all_action_ = nullptr, *deselect_all_action_ = nullptr,
          *invert_selection_action_ = nullptr, *select_action_ = nullptr,
          *deselect_action_ = nullptr, *select_by_type_action_ = nullptr,
          *deselect_by_type_action_ = nullptr;
  QAction *large_icons_action_ = nullptr, *small_icons_action_ = nullptr,
          *list_mode_action_ = nullptr, *details_mode_action_ = nullptr,
          *sort_name_action_ = nullptr, *sort_type_action_ = nullptr,
          *sort_date_action_ = nullptr, *sort_size_action_ = nullptr,
          *unsorted_action_ = nullptr, *flat_view_action_ = nullptr,
          *two_panels_action_ = nullptr, *open_root_action_ = nullptr,
          *open_parent_action_ = nullptr, *folders_history_action_ = nullptr,
          *refresh_action_ = nullptr, *auto_refresh_action_ = nullptr,
          *archive_toolbar_action_ = nullptr, *standard_toolbar_action_ = nullptr,
          *large_buttons_action_ = nullptr, *show_buttons_text_action_ = nullptr,
          *time_day_action_ = nullptr, *time_min_action_ = nullptr,
          *time_sec_action_ = nullptr, *time_ntfs_action_ = nullptr, *time_ns_action_ = nullptr,
          *time_utc_action_ = nullptr;
  QActionGroup *view_mode_action_group_ = nullptr, *sort_action_group_ = nullptr,
               *time_action_group_ = nullptr;
  QAction *options_action_ = nullptr, *benchmark_action_ = nullptr,
          *benchmark2_action_ = nullptr, *temp_files_action_ = nullptr,
          *contents_action_ = nullptr, *about_action_ = nullptr;
  QVector<std::shared_ptr<RunningTaskContext>> active_runner_tasks_;
  QTimer* auto_refresh_timer_ = nullptr;
  bool confirm_delete_ = true;
  QStringList path_history_;
  QStringList folder_history_;
  QVector<QSharedPointer<ArchiveTempSession>> archive_temp_sessions_;
  QString task_ipc_owner_instance_id_;
  bool task_ipc_completion_notifier_registered_ = false;
  QHash<quint64, int> task_ipc_session_panels_;
  quint64 transient_status_generation_ = 0;
  std::function<bool(const QString&)> external_opener_;
  std::function<bool(const QString&, const QStringList&, const QString&, qint64*)>
      external_command_launcher_;
  std::function<QMessageBox::StandardButton(
      const QString&, const QString&, QMessageBox::StandardButtons, QMessageBox::StandardButton)>
      question_box_;
