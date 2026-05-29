// src/ui/filemanager/src/temp_files_dialog.h
// Role: Temp-folder cleanup dialog with original 7-Zip style root restrictions.

#pragma once

#include <Qt>
#include <QDialog>
#include <QStringList>
#include <QVector>

#include "temp_files_dialog_model.h"
#include "temp_files_listing.h"

class QComboBox;
class QLineEdit;
class QMenu;
class QPushButton;
class QTableWidget;
class QAction;
class QHeaderView;

namespace z7::ui::filemanager {

class TempFilesDialog final : public QDialog {
 public:
  explicit TempFilesDialog(const QString& temp_root_path,
                           QWidget* parent = nullptr);

 protected:
 bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void reload();
  void repopulate_table();
  void restore_selection_and_focus(const QStringList& selected_names,
                                   const QString& focused_name);
  void update_controls();
  void open_parent();
  void activate_current_row(Qt::KeyboardModifiers modifiers = Qt::NoModifier);
  void on_column_clicked(int column_index);
  void on_delete_requested();
  void on_context_menu(const QPoint& pos);
  void on_open_outside_requested();
  void on_open_outside_7zip_requested();
  void on_properties_requested();
  void update_context_menu_actions();
  void show_selected_entry_properties();
  void show_blocked_operation_warning();
  bool open_path_externally_with_warning(const QString& path);
  int row_for_entry_name(const QString& name) const;

  QVector<TempFilesListEntry> selected_entries() const;
  QStringList selected_entry_names() const;
  QString focused_entry_name() const;

  TempFilesDialogModel model_;

  QPushButton* delete_button_ = nullptr;
  QPushButton* refresh_button_ = nullptr;
  QPushButton* parent_button_ = nullptr;
  QLineEdit* path_edit_ = nullptr;
  QTableWidget* table_ = nullptr;
  QHeaderView* table_header_ = nullptr;
  QMenu* context_menu_ = nullptr;
  QAction* context_delete_action_ = nullptr;
  QAction* context_open_outside_action_ = nullptr;
  QAction* context_open_outside_7zip_action_ = nullptr;
  QAction* context_properties_action_ = nullptr;
  QAction* context_separator_after_delete_ = nullptr;
  QAction* context_separator_before_properties_ = nullptr;
  QComboBox* filter_combo_ = nullptr;
  QPushButton* close_button_ = nullptr;
  QPushButton* help_button_ = nullptr;
};

}  // namespace z7::ui::filemanager
