// src/ui/filemanager/src/temp_files_dialog.cpp
// Role: Temp-folder cleanup dialog UI and interactions.

#include "temp_files_dialog.h"

#include "temp_files_dialog_service.h"
#include "official_lang_catalog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace z7::ui::filemanager {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIsDirRole = Qt::UserRole + 2;
constexpr int kIsSymlinkRole = Qt::UserRole + 3;

QString trimmed_without_ellipsis(QString value) {
  value = z7::ui::runtime_support::strip_mnemonic(std::move(value));
  value.replace(QStringLiteral("..."), QString());
  return value.trimmed();
}

QString localized_text(const uint32_t id) {
  return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(id)).trimmed();
}

}  // namespace

TempFilesDialog::TempFilesDialog(const QString& temp_root_path, QWidget* parent)
    : QDialog(parent),
      model_(temp_root_path) {
#ifdef Z7_TESTING
  setObjectName(QStringLiteral("tempFilesDialog"));
#endif
  setModal(true);
  resize(720, 430);

  QString title = trimmed_without_ellipsis(z7::ui::runtime_support::L(910));
  setWindowTitle(title);

  auto* root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(8, 8, 8, 8);
  root_layout->setSpacing(6);

  auto* top_row = new QHBoxLayout();
  delete_button_ = new QPushButton(localized_text(7205), this);
#ifdef Z7_TESTING
  delete_button_->setObjectName(QStringLiteral("tempFilesDeleteButton"));
#endif
  refresh_button_ = new QPushButton(localized_text(737), this);
#ifdef Z7_TESTING
  refresh_button_->setObjectName(QStringLiteral("tempFilesRefreshButton"));
#endif
  top_row->addWidget(delete_button_);
  top_row->addWidget(refresh_button_);
  top_row->addStretch(1);
  root_layout->addLayout(top_row);

  auto* path_row = new QHBoxLayout();
  path_row->setSpacing(4);
  parent_button_ = new QPushButton(QStringLiteral("<--"), this);
#ifdef Z7_TESTING
  parent_button_->setObjectName(QStringLiteral("tempFilesParentButton"));
#endif
  parent_button_->setFixedWidth(34);
  path_edit_ = new QLineEdit(this);
#ifdef Z7_TESTING
  path_edit_->setObjectName(QStringLiteral("tempFilesPathEdit"));
#endif
  path_edit_->setReadOnly(true);
  path_row->addWidget(parent_button_);
  path_row->addWidget(path_edit_, 1);
  root_layout->addLayout(path_row);

  table_ = new QTableWidget(this);
#ifdef Z7_TESTING
  table_->setObjectName(QStringLiteral("tempFilesTable"));
#endif
  table_->setColumnCount(6);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setAlternatingRowColors(true);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->verticalHeader()->setVisible(false);
  table_header_ = table_->horizontalHeader();
  table_header_->setStretchLastSection(false);
  table_header_->setSectionResizeMode(0, QHeaderView::Stretch);
  table_header_->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table_header_->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table_header_->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  table_header_->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  table_header_->setSectionResizeMode(5, QHeaderView::ResizeToContents);
  table_->setHorizontalHeaderLabels({
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1012)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1007)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1032)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1031)),
      QStringLiteral("%1-2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004)))});
  table_->installEventFilter(this);
  root_layout->addWidget(table_, 1);

  context_menu_ = new QMenu(this);
#ifdef Z7_TESTING
  context_menu_->setObjectName(QStringLiteral("tempFilesContextMenu"));
#endif
  context_delete_action_ = context_menu_->addAction(
      QStringLiteral("%1\tDelete").arg(localized_text(7205)));
#ifdef Z7_TESTING
  context_delete_action_->setObjectName(QStringLiteral("tempFilesContextDeleteAction"));
#endif
  context_separator_after_delete_ = context_menu_->addSeparator();
  context_open_outside_action_ = context_menu_->addAction(
      QStringLiteral("%1\tShift+Enter")
          .arg(localized_text(542)));
#ifdef Z7_TESTING
  context_open_outside_action_->setObjectName(QStringLiteral("tempFilesContextOpenOutsideAction"));
#endif
  context_open_outside_7zip_action_ = context_menu_->addAction(
      QStringLiteral("%1 : 7-Zip")
          .arg(localized_text(542)));
#ifdef Z7_TESTING
  context_open_outside_7zip_action_->setObjectName(
      QStringLiteral("tempFilesContextOpenOutside7ZipAction"));
#endif
  context_separator_before_properties_ = context_menu_->addSeparator();
  context_properties_action_ = context_menu_->addAction(
      QStringLiteral("%1\tAlt+Enter")
          .arg(localized_text(551)));
#ifdef Z7_TESTING
  context_properties_action_->setObjectName(QStringLiteral("tempFilesContextPropertiesAction"));
#endif

  connect(context_open_outside_action_, &QAction::triggered, this, [this]() {
    on_open_outside_requested();
  });
  connect(context_open_outside_7zip_action_, &QAction::triggered, this, [this]() {
    on_open_outside_7zip_requested();
  });
  connect(context_delete_action_, &QAction::triggered, this, [this]() {
    on_delete_requested();
  });
  connect(context_properties_action_, &QAction::triggered, this, [this]() {
    on_properties_requested();
  });

  auto* bottom_row = new QHBoxLayout();
  filter_combo_ = new QComboBox(this);
#ifdef Z7_TESTING
  filter_combo_->setObjectName(QStringLiteral("tempFilesFilterCombo"));
#endif
  filter_combo_->addItem(QStringLiteral("7-Zip temp files (7z*)"));
  filter_combo_->setEnabled(false);
  bottom_row->addWidget(filter_combo_, 1);

  close_button_ = new QPushButton(localized_text(408), this);
#ifdef Z7_TESTING
  close_button_->setObjectName(QStringLiteral("tempFilesCloseButton"));
#endif
  close_button_->setDefault(true);
  bottom_row->addWidget(close_button_);

  help_button_ = new QPushButton(localized_text(409), this);
#ifdef Z7_TESTING
  help_button_->setObjectName(QStringLiteral("tempFilesHelpButton"));
#endif
  help_button_->setEnabled(false);
  bottom_row->addWidget(help_button_);
  root_layout->addLayout(bottom_row);

  connect(delete_button_, &QPushButton::clicked, this, [this]() { on_delete_requested(); });
  connect(refresh_button_, &QPushButton::clicked, this, [this]() { reload(); });
  connect(parent_button_, &QPushButton::clicked, this, [this]() { open_parent(); });
  connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
  connect(table_, &QTableWidget::itemDoubleClicked, this, [this]() {
    activate_current_row(QApplication::keyboardModifiers());
  });
  connect(table_header_, &QHeaderView::sectionClicked, this, [this](int column_index) {
    on_column_clicked(column_index);
  });
  connect(table_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    on_context_menu(pos);
  });
  connect(table_->selectionModel(),
          &QItemSelectionModel::selectionChanged,
          this,
          [this]() {
            update_controls();
            update_context_menu_actions();
          });

  reload();
  update_context_menu_actions();
}

bool TempFilesDialog::eventFilter(QObject* watched, QEvent* event) {
  if (watched == table_ && event != nullptr && event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() == Qt::Key_Delete) {
      on_delete_requested();
      return true;
    }
    if (key_event->key() == Qt::Key_Backspace) {
      open_parent();
      return true;
    }
    if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) {
      activate_current_row(key_event->modifiers());
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void TempFilesDialog::reload() {
  const QStringList selected_names = selected_entry_names();
  const QString focused_name = focused_entry_name();
  model_.reload();
  repopulate_table();
  path_edit_->setText(QDir::toNativeSeparators(model_.current_path()));
  restore_selection_and_focus(selected_names, focused_name);
  if (table_header_ != nullptr) {
    table_header_->setSortIndicator(
        model_.sort_column(),
        model_.ascending() ? Qt::AscendingOrder : Qt::DescendingOrder);
  }
  update_controls();
}

void TempFilesDialog::repopulate_table() {
  table_->setSortingEnabled(false);
  table_->setRowCount(model_.entries().size());

  for (int i = 0; i < model_.entries().size(); ++i) {
    const TempFilesListEntry& entry = model_.entries().at(i);

    const QString modified_text = format_datetime(entry.modified);
    const QString size_text = QString::number(entry.size);

    QString files_text;
    if (entry.is_dir && entry.is_symlink) {
      files_text = localized_text(1090);
    } else if (entry.is_dir && entry.num_files != 0) {
      files_text = QString::number(entry.num_files);
    }

    QString folders_text;
    if (entry.is_dir && entry.num_dirs != 0) {
      folders_text = QString::number(entry.num_dirs);
    }

    auto* name_item = new QTableWidgetItem(entry.name);
    name_item->setData(kPathRole, entry.absolute_path);
    name_item->setData(kIsDirRole, entry.is_dir);
    name_item->setData(kIsSymlinkRole, entry.is_symlink);
    name_item->setIcon(style()->standardIcon(entry.is_dir ? QStyle::SP_DirIcon
                                                           : QStyle::SP_FileIcon));

    auto* modified_item = new QTableWidgetItem(modified_text);
    auto* size_item = new QTableWidgetItem(size_text);
    auto* files_item = new QTableWidgetItem(files_text);
    auto* folders_item = new QTableWidgetItem(folders_text);
    auto* sub_name_item = new QTableWidgetItem(entry.sub_file_name);

    size_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    files_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    folders_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    table_->setItem(i, 0, name_item);
    table_->setItem(i, 1, modified_item);
    table_->setItem(i, 2, size_item);
    table_->setItem(i, 3, files_item);
    table_->setItem(i, 4, folders_item);
    table_->setItem(i, 5, sub_name_item);
  }
}

void TempFilesDialog::restore_selection_and_focus(const QStringList& selected_names,
                                                  const QString& focused_name) {
  if (table_ == nullptr || table_->selectionModel() == nullptr) {
    return;
  }

  QSignalBlocker selection_blocker(table_->selectionModel());
  table_->selectionModel()->clearSelection();
  table_->setCurrentCell(-1, -1);

  bool restored_selection = false;
  int focused_row = -1;
  for (const QString& name : selected_names) {
    const int row = row_for_entry_name(name);
    if (row < 0) {
      continue;
    }
    table_->selectionModel()->select(
        table_->model()->index(row, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    if (focused_row < 0) {
      focused_row = row;
    }
    restored_selection = true;
  }

  if (!focused_name.isEmpty()) {
    const int row = row_for_entry_name(focused_name);
    if (row >= 0) {
      focused_row = row;
    }
  }

  if (focused_row >= 0) {
    table_->setCurrentCell(focused_row, 0);
    return;
  }

  if (!model_.entries().isEmpty()) {
    table_->setCurrentCell(0, 0);
    if (!restored_selection) {
      table_->selectRow(0);
    }
  }
}

int TempFilesDialog::row_for_entry_name(const QString& name) const {
  if (table_ == nullptr) {
    return -1;
  }
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (QTableWidgetItem* item = table_->item(row, 0)) {
      if (item->text() == name) {
        return row;
      }
    }
  }
  return -1;
}

QStringList TempFilesDialog::selected_entry_names() const {
  QStringList names;
  const QVector<TempFilesListEntry> entries = selected_entries();
  names.reserve(entries.size());
  for (const TempFilesListEntry& entry : entries) {
    names.push_back(entry.name);
  }
  return names;
}

QString TempFilesDialog::focused_entry_name() const {
  if (table_ == nullptr) {
    return {};
  }
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  if (QTableWidgetItem* item = table_->item(row, 0)) {
    return item->text();
  }
  return {};
}

void TempFilesDialog::update_controls() {
  parent_button_->setEnabled(!model_.is_at_root());
  delete_button_->setEnabled(!selected_entries().isEmpty());
  update_context_menu_actions();
}

void TempFilesDialog::open_parent() {
  if (model_.is_at_root()) {
    return;
  }
  model_.open_parent();
  reload();
}

void TempFilesDialog::on_column_clicked(int column_index) {
  const QStringList selected_names = selected_entry_names();
  const QString focused_name = focused_entry_name();
  model_.apply_sort_column(column_index, table_->columnCount());
  repopulate_table();
  restore_selection_and_focus(selected_names, focused_name);
  if (table_header_ != nullptr) {
    table_header_->setSortIndicator(
        model_.sort_column(),
        model_.ascending() ? Qt::AscendingOrder : Qt::DescendingOrder);
  }
  update_controls();
}

void TempFilesDialog::activate_current_row(Qt::KeyboardModifiers modifiers) {
  const QVector<TempFilesListEntry> entries = selected_entries();
  if (entries.size() != 1) {
    return;
  }
  if (modifiers.testFlag(Qt::AltModifier)) {
    show_selected_entry_properties();
    return;
  }
  const TempFilesListEntry& entry = entries.front();
  if (entry.is_symlink) {
    show_blocked_operation_warning();
    return;
  }
  if (entry.is_dir && !modifiers.testFlag(Qt::ShiftModifier)) {
    model_.set_current_path(entry.absolute_path);
    reload();
    return;
  }
  open_path_externally_with_warning(entry.absolute_path);
}

}  // namespace z7::ui::filemanager
