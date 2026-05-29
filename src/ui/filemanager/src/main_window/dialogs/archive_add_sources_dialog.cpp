// src/ui/filemanager/src/main_window/dialogs/archive_add_sources_dialog.cpp
// Role: Polished filesystem-source picker for archive-view Add.

#include "main_window/dialogs/archive_add_sources_dialog.h"

#include "archive_virtual_path.h"
#include "custom_localization.h"
#include "main_window/drag_drop/drag_aware_views.h"
#include "main_window/internal.h"
#include "main_window/model/model.h"
#include "official_lang_catalog.h"
#include "structured_list_config.h"
#include "structured_list_proxy.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace z7::ui::filemanager {
namespace {

QString normalize_existing_directory_for_add_sources_dialog(
    const QString& value) {
  const QFileInfo info(QDir::fromNativeSeparators(value.trimmed()));
  if (info.exists() && info.isDir()) {
    return info.absoluteFilePath();
  }
  return QString();
}

QString archive_virtual_dir_display_for_add_sources_dialog(
    const QString& value) {
  const QString normalized =
      z7::ui::archive_support::normalize_virtual_dir(value);
  if (normalized.isEmpty()) {
    return QStringLiteral("/");
  }
  return QStringLiteral("/%1").arg(normalized);
}

z7::ui::widgets::StructuredListConfig make_add_sources_view_config() {
  using z7::ui::widgets::StructuredListColumn;
  using z7::ui::widgets::StructuredListConfig;
  using z7::ui::widgets::StructuredListStyle;

  StructuredListConfig cfg;
  cfg.primary_interactive_column = DirectoryListModel::kNameColumn;
  cfg.sorting_enabled = true;
  cfg.show_header = true;

  StructuredListStyle& s = cfg.style;
  s.primary_hover_bg = QColor(0, 0, 0, 20);
  s.primary_selected_bg = QColor(60, 130, 210);
  s.primary_selected_text = QColor(Qt::white);
  s.row_hover_bg = QColor(0, 0, 0, 8);
  s.grid_line = QColor();
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
    c.header_text = z7::ui::runtime_support::strip_mnemonic(
        z7::ui::runtime_support::L(lang_id));
    c.default_width = default_width;
    c.alignment = align;
    c.hidden_by_default = hidden_by_default;
    cfg.columns.push_back(c);
  };

  add_col("name", 1004, 360);
  add_col("size", 1007, 110, Qt::AlignRight | Qt::AlignVCenter);
  add_col("packed", 1008, 0, Qt::AlignRight | Qt::AlignVCenter, true);
  add_col("modified", 1012, 170);
  return cfg;
}

class ArchiveAddSourcesDialog final : public QDialog {
 public:
  ArchiveAddSourcesDialog(QWidget* parent,
                          const QString& title,
                          const QString& initial_directory,
                          const QString& target_virtual_dir_display)
      : QDialog(parent),
        current_directory_(
            normalize_existing_directory_for_add_sources_dialog(
                initial_directory)) {
#ifdef Z7_TESTING
    setObjectName(QStringLiteral("archiveAddSourcesDialog"));
#endif
    setWindowTitle(title);
    resize(760, 520);
    setMinimumSize(560, 360);

    if (current_directory_.isEmpty()) {
      current_directory_ = QDir::homePath();
    }
    if (current_directory_.isEmpty()) {
      current_directory_ = QDir::rootPath();
    }

    build_ui(target_virtual_dir_display);
    wire_ui();
    set_current_directory(current_directory_);
    update_ok_enabled();
  }

  ArchiveAddSourcesDialogResult result() const { return result_; }

 private:
  void build_ui(const QString& target_virtual_dir_display) {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(6);

    auto* prompt_label = new QLabel(
        z7::ui::runtime_support::J(
            QStringLiteral("ui.archive.add_sources.prompt")),
        this);
#ifdef Z7_TESTING
    prompt_label->setObjectName(
        QStringLiteral("archiveAddSourcesPromptLabel"));
#endif
    root_layout->addWidget(prompt_label);

    auto* path_row = new QWidget(this);
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->setSpacing(6);

    up_button_ = new QToolButton(path_row);
#ifdef Z7_TESTING
    up_button_->setObjectName(QStringLiteral("archiveAddSourcesUpButton"));
#endif
    up_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogToParent));
    up_button_->setToolTip(z7::ui::runtime_support::strip_mnemonic(
        z7::ui::runtime_support::L(735)));
    up_button_->setAccessibleName(up_button_->toolTip());
    up_button_->setAutoRaise(true);
    up_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    up_button_->setFixedSize(28, 28);

    path_combo_ = new PathComboBox(path_row);
#ifdef Z7_TESTING
    path_combo_->setObjectName(QStringLiteral("archiveAddSourcesPathCombo"));
#endif
    path_combo_->setEditable(true);
    path_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    browse_button_ = new QToolButton(path_row);
#ifdef Z7_TESTING
    browse_button_->setObjectName(
        QStringLiteral("archiveAddSourcesBrowseButton"));
#endif
    browse_button_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    browse_button_->setToolTip(z7::ui::runtime_support::J(
        QStringLiteral("ui.archive.add_sources.browse_folder")));
    browse_button_->setAccessibleName(browse_button_->toolTip());
    browse_button_->setAutoRaise(true);
    browse_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    browse_button_->setFixedSize(28, 28);

    path_layout->addWidget(up_button_);
    path_layout->addWidget(path_combo_, 1);
    path_layout->addWidget(browse_button_);
    root_layout->addWidget(path_row);

    model_ = new DirectoryListModel(this);
    model_->set_icon_style_context(this);

    proxy_ = new z7::ui::widgets::StructuredListSortFilterProxy(this);
    proxy_->setSourceModel(model_);

    view_ = new DragAwareStructuredListView(this);
#ifdef Z7_TESTING
    view_->setObjectName(QStringLiteral("archiveAddSourcesView"));
#endif
    view_->setModel(proxy_);
    view_->set_config(make_add_sources_view_config());
    view_->setDragEnabled(false);
    view_->setAcceptDrops(false);
    view_->setIconSize(QSize(16, 16));
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* header = view_->horizontalHeader();
    header->setMinimumSectionSize(40);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(DirectoryListModel::kNameColumn,
                                 QHeaderView::Stretch);
    header->setSectionResizeMode(DirectoryListModel::kSizeColumn,
                                 QHeaderView::Interactive);
    header->setSectionResizeMode(DirectoryListModel::kModifiedColumn,
                                 QHeaderView::Interactive);
    hide_non_picker_columns();
    view_->sortByColumn(DirectoryListModel::kNameColumn, Qt::AscendingOrder);
    root_layout->addWidget(view_, 1);

    auto* target_label = new QLabel(
        z7::ui::runtime_support::JF(
            QStringLiteral("ui.archive.add_sources.target_folder"),
            {archive_virtual_dir_display_for_add_sources_dialog(
                target_virtual_dir_display)}),
        this);
#ifdef Z7_TESTING
    target_label->setObjectName(
        QStringLiteral("archiveAddSourcesTargetLabel"));
#endif
    target_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root_layout->addWidget(target_label);

    auto* buttons_row = new QWidget(this);
    auto* buttons_layout = new QHBoxLayout(buttons_row);
    buttons_layout->setContentsMargins(0, 0, 0, 0);
    buttons_layout->addStretch(1);

    ok_button_ = new QPushButton(z7::ui::runtime_support::L(401), buttons_row);
#ifdef Z7_TESTING
    ok_button_->setObjectName(QStringLiteral("archiveAddSourcesOkButton"));
#endif
    ok_button_->setMinimumWidth(92);
    ok_button_->setDefault(true);
    ok_button_->setAutoDefault(true);

    cancel_button_ =
        new QPushButton(z7::ui::runtime_support::L(402), buttons_row);
#ifdef Z7_TESTING
    cancel_button_->setObjectName(
        QStringLiteral("archiveAddSourcesCancelButton"));
#endif
    cancel_button_->setMinimumWidth(92);

    buttons_layout->addWidget(ok_button_);
    buttons_layout->addWidget(cancel_button_);
    root_layout->addWidget(buttons_row);
  }

  void wire_ui() {
    connect(ok_button_, &QPushButton::clicked, this, [this]() {
      accept_selected_paths();
    });
    connect(cancel_button_, &QPushButton::clicked, this, &QDialog::reject);

    path_combo_->before_show_popup = [this]() { rebuild_path_combo(); };
    connect(path_combo_,
            qOverload<int>(&QComboBox::activated),
            this,
            [this](int index) {
              const QString data =
                  path_combo_->itemData(index, Qt::UserRole).toString();
              const QString next =
                  data.trimmed().isEmpty() ? path_combo_->itemText(index) : data;
              set_current_directory(next);
            });
    if (path_combo_->lineEdit() != nullptr) {
      connect(path_combo_->lineEdit(),
              &QLineEdit::returnPressed,
              this,
              [this]() { set_current_directory(path_combo_->currentText()); });
    }

    connect(up_button_, &QToolButton::clicked, this, [this]() {
      QDir dir(current_directory_);
      if (dir.cdUp()) {
        set_current_directory(dir.absolutePath());
      }
    });
    connect(browse_button_, &QToolButton::clicked, this, [this]() {
      const QString selected = QFileDialog::getExistingDirectory(
          this,
          z7::ui::runtime_support::L(4070),
          current_directory_,
          QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
      if (!selected.isEmpty()) {
        set_current_directory(selected);
      }
    });
    connect(view_,
            &DragAwareStructuredListView::primary_double_clicked,
            this,
            [this](const QModelIndex& index) { activate_index(index); });
    connect(view_,
            &DragAwareStructuredListView::primary_enter_pressed,
            this,
            [this](const QModelIndex& index) { activate_index(index); });

    if (view_->selectionModel() != nullptr) {
      connect(view_->selectionModel(),
              &QItemSelectionModel::selectionChanged,
              this,
              [this]() { update_ok_enabled(); });
      connect(view_->selectionModel(),
              &QItemSelectionModel::currentChanged,
              this,
              [this]() { update_ok_enabled(); });
    }
  }

  void hide_non_picker_columns() {
    if (view_ == nullptr) {
      return;
    }
    for (int column = 0; column < DirectoryListModel::kColumnCount; ++column) {
      const bool visible =
          column == DirectoryListModel::kNameColumn ||
          column == DirectoryListModel::kSizeColumn ||
          column == DirectoryListModel::kModifiedColumn;
      view_->setColumnHidden(column, !visible);
    }
  }

  void rebuild_path_combo() {
    const QString current_absolute = QDir(current_directory_).absolutePath();
    const QStringList items = ancestor_paths(current_absolute);
    const QString current_native = QDir::toNativeSeparators(current_absolute);

    const QSignalBlocker blocker(path_combo_);
    path_combo_->clear();
    for (const QString& item : items) {
      path_combo_->addItem(QDir::toNativeSeparators(item), item);
    }
    const int current_index =
        path_combo_->findData(current_absolute, Qt::UserRole);
    if (current_index >= 0) {
      path_combo_->setCurrentIndex(current_index);
    }
    path_combo_->setEditText(current_native);
  }

  bool set_current_directory(const QString& raw_path) {
    const QString normalized =
        normalize_existing_directory_for_add_sources_dialog(raw_path);
    if (normalized.isEmpty()) {
      rebuild_path_combo();
      return false;
    }

    current_directory_ = normalized;
    model_->set_directory(current_directory_);
    view_->clearSelection();
    rebuild_path_combo();

    QDir dir(current_directory_);
    up_button_->setEnabled(dir.cdUp());
    update_ok_enabled();
    return true;
  }

  QModelIndex source_index_for_proxy_index(const QModelIndex& proxy_index) const {
    if (!proxy_index.isValid()) {
      return {};
    }
    return proxy_->mapToSource(proxy_index);
  }

  QModelIndexList selected_source_rows() const {
    if (view_ == nullptr || view_->selectionModel() == nullptr ||
        model_ == nullptr) {
      return {};
    }

    QModelIndexList out;
    QSet<int> seen_rows;
    auto add_proxy_index = [&](const QModelIndex& proxy_index) {
      const QModelIndex source = source_index_for_proxy_index(proxy_index);
      if (!source.isValid() || seen_rows.contains(source.row())) {
        return;
      }
      seen_rows.insert(source.row());
      out << model_->index(source.row(), DirectoryListModel::kNameColumn);
    };

    const int primary = view_->primary_column();
    for (const QModelIndex& index :
         view_->selectionModel()->selectedRows(primary)) {
      add_proxy_index(index);
    }
    for (const QModelIndex& index :
         view_->selectionModel()->selectedIndexes()) {
      add_proxy_index(index);
    }
    return out;
  }

  QStringList selected_real_filesystem_paths() const {
    QStringList paths;
    const QModelIndexList rows = selected_source_rows();
    paths.reserve(rows.size());
    for (const QModelIndex& row : rows) {
      if (!row.isValid() || model_->is_parent_link_for_row(row.row())) {
        continue;
      }
      const QString path = model_->path_for_row(row.row());
      const QFileInfo info(path);
      if (info.exists() && (info.isFile() || info.isDir())) {
        paths << info.absoluteFilePath();
      }
    }
    return paths;
  }

  void update_ok_enabled() {
    if (ok_button_ != nullptr) {
      ok_button_->setEnabled(!selected_real_filesystem_paths().isEmpty());
    }
  }

  void activate_index(const QModelIndex& proxy_index) {
    const QModelIndex source = source_index_for_proxy_index(proxy_index);
    if (!source.isValid() || !model_->is_dir_for_row(source.row())) {
      return;
    }
    set_current_directory(model_->path_for_row(source.row()));
  }

  void accept_selected_paths() {
    const QStringList selected_paths = selected_real_filesystem_paths();
    if (selected_paths.isEmpty()) {
      update_ok_enabled();
      return;
    }
    result_.accepted = true;
    result_.selected_paths = selected_paths;
    accept();
  }

  ArchiveAddSourcesDialogResult result_;
  QString current_directory_;
  PathComboBox* path_combo_ = nullptr;
  QToolButton* up_button_ = nullptr;
  QToolButton* browse_button_ = nullptr;
  DirectoryListModel* model_ = nullptr;
  z7::ui::widgets::StructuredListSortFilterProxy* proxy_ = nullptr;
  DragAwareStructuredListView* view_ = nullptr;
  QPushButton* ok_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
};

}  // namespace

ArchiveAddSourcesDialogResult show_archive_add_sources_dialog(
    QWidget* parent,
    const QString& title,
    const QString& initial_directory,
    const QString& target_virtual_dir_display) {
  ArchiveAddSourcesDialog dialog(parent,
                                 title,
                                 initial_directory,
                                 target_virtual_dir_display);
  if (dialog.exec() != QDialog::Accepted) {
    return {};
  }
  return dialog.result();
}

}  // namespace z7::ui::filemanager
