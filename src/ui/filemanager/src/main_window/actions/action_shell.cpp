// src/ui/filemanager/src/main_window/actions/action_shell.cpp
// Role: Comment / link / alternate-stream actions aligned with FileManager menu semantics.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <filesystem>
#include <system_error>

namespace z7::ui::filemanager {
namespace {

enum class LinkDialogType {
  kHard,
  kFileSymlink,
  kDirectorySymlink,
  kJunction,
  kWsl
};

struct LinkDialogResult {
  QString link_from;
  QString link_to;
  LinkDialogType type = LinkDialogType::kFileSymlink;
};

QString resolve_link_target_path(const QString& input, const QString& base_dir) {
  QString path = QDir::cleanPath(input.trimmed());
  if (path.isEmpty()) {
    return {};
  }
  if (QDir::isRelativePath(path)) {
    path = QDir(base_dir).absoluteFilePath(path);
  }
  return QFileInfo(path).absoluteFilePath();
}

QString link_dialog_text(uint32_t lang_id, QStringView fallback) {
  QString text = z7::ui::runtime_support::strip_mnemonic(
      z7::ui::runtime_support::L(lang_id));
  if (text == QStringLiteral("#%1").arg(lang_id)) {
    text = fallback.toString();
  }
  return text;
}

QString default_link_from_path(const QFileInfo& source_info,
                               const QString& source_panel_base_dir,
                               const QString& opposite_panel_dir) {
  QString base_dir = opposite_panel_dir.trimmed();
  if (base_dir.isEmpty()) {
    base_dir = source_panel_base_dir;
  }
  if (base_dir.isEmpty()) {
    base_dir = source_info.absolutePath();
  }
  return QDir(base_dir).absoluteFilePath(source_info.fileName() +
                                         QStringLiteral(".link"));
}

class LinkDialog final : public QDialog {
 public:
  LinkDialog(const QString& initial_from,
             const QString& initial_to,
             LinkDialogType initial_type,
             QWidget* parent)
      : QDialog(parent),
        from_combo_(new QComboBox(this)),
        to_combo_(new QComboBox(this)),
        type_group_(new QButtonGroup(this)) {
#ifdef Z7_TESTING
    setObjectName(QStringLiteral("linkDialog"));
#endif
    setWindowTitle(z7::ui::runtime_support::strip_mnemonic(
        z7::ui::runtime_support::L(558)));
    resize(520, 300);

#ifdef Z7_TESTING
    from_combo_->setObjectName(QStringLiteral("linkFromCombo"));
#endif
    from_combo_->setEditable(true);
    from_combo_->setEditText(QDir::toNativeSeparators(initial_from));
#ifdef Z7_TESTING
    to_combo_->setObjectName(QStringLiteral("linkToCombo"));
#endif
    to_combo_->setEditable(true);
    to_combo_->setEditText(QDir::toNativeSeparators(initial_to));

    auto* from_browse = new QPushButton(QStringLiteral("..."), this);
#ifdef Z7_TESTING
    from_browse->setObjectName(QStringLiteral("linkFromBrowseButton"));
#endif
    auto* to_browse = new QPushButton(QStringLiteral("..."), this);
#ifdef Z7_TESTING
    to_browse->setObjectName(QStringLiteral("linkToBrowseButton"));
#endif

    auto* from_row = new QHBoxLayout;
    from_row->addWidget(from_combo_, 1);
    from_row->addWidget(from_browse);
    auto* to_row = new QHBoxLayout;
    to_row->addWidget(to_combo_, 1);
    to_row->addWidget(to_browse);

    auto* current_target_label = new QLabel(this);
#ifdef Z7_TESTING
    current_target_label->setObjectName(QStringLiteral("linkCurrentTargetLabel"));
#endif
    current_target_label->setVisible(false);

    auto* type_box =
        new QGroupBox(link_dialog_text(7710, QStringLiteral("Link Type")), this);
#ifdef Z7_TESTING
    type_box->setObjectName(QStringLiteral("linkTypeGroup"));
#endif
    auto* type_layout = new QVBoxLayout(type_box);
    add_type_radio(type_layout,
                   0,
                   link_dialog_text(7711, QStringLiteral("Hard Link")),
                   QStringLiteral("linkTypeHardRadio"),
                   false);
    file_symlink_radio_ =
        add_type_radio(type_layout,
                       1,
                       link_dialog_text(7712, QStringLiteral("File Symbolic Link")),
                       QStringLiteral("linkTypeFileSymlinkRadio"),
                       true);
    directory_symlink_radio_ =
        add_type_radio(type_layout,
                       2,
                       link_dialog_text(7713, QStringLiteral("Directory Symbolic Link")),
                       QStringLiteral("linkTypeDirectorySymlinkRadio"),
                       true);
    add_type_radio(type_layout,
                   3,
                   link_dialog_text(7714, QStringLiteral("Directory Junction")),
                   QStringLiteral("linkTypeJunctionRadio"),
                   false);
    add_type_radio(type_layout,
                   4,
                   link_dialog_text(7715, QStringLiteral("WSL")),
                   QStringLiteral("linkTypeWslRadio"),
                   false);

    if (initial_type == LinkDialogType::kDirectorySymlink) {
      directory_symlink_radio_->setChecked(true);
    } else {
      file_symlink_radio_->setChecked(true);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
#ifdef Z7_TESTING
    buttons->setObjectName(QStringLiteral("linkDialogButtons"));
#endif
    link_button_ = buttons->addButton(
        link_dialog_text(7701, QStringLiteral("Link")),
        QDialogButtonBox::AcceptRole);
#ifdef Z7_TESTING
    link_button_->setObjectName(QStringLiteral("linkCreateButton"));
#endif
    link_button_->setDefault(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(link_dialog_text(7702, QStringLiteral("Link from:")), this));
    layout->addLayout(from_row);
    layout->addWidget(new QLabel(link_dialog_text(7703, QStringLiteral("Link to:")), this));
    layout->addLayout(to_row);
    layout->addWidget(current_target_label);
    layout->addWidget(type_box);
    layout->addWidget(buttons);

    connect(from_browse,
            &QPushButton::clicked,
            this,
            [this]() { browse_for_combo(from_combo_); });
    connect(to_browse,
            &QPushButton::clicked,
            this,
            [this]() { browse_for_combo(to_combo_); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(from_combo_,
            &QComboBox::editTextChanged,
            this,
            [this]() { update_link_button(); });
    connect(to_combo_,
            &QComboBox::editTextChanged,
            this,
            [this]() { update_link_button(); });
    update_link_button();
  }

  LinkDialogResult result() const {
    LinkDialogResult value;
    value.link_from = from_combo_->currentText();
    value.link_to = to_combo_->currentText();
    if (directory_symlink_radio_->isChecked()) {
      value.type = LinkDialogType::kDirectorySymlink;
    } else {
      value.type = LinkDialogType::kFileSymlink;
    }
    return value;
  }

 private:
  QRadioButton* add_type_radio(QVBoxLayout* layout,
                               int id,
                               const QString& text,
                               const QString& object_name,
                               bool enabled) {
    auto* radio = new QRadioButton(text, this);
#ifdef Z7_TESTING
    radio->setObjectName(object_name);
#else
    Q_UNUSED(object_name);
#endif
    radio->setEnabled(enabled);
    if (!enabled) {
      radio->setToolTip(QStringLiteral("Not supported in this Qt file manager."));
    }
    type_group_->addButton(radio, id);
    layout->addWidget(radio);
    return radio;
  }

  void browse_for_combo(QComboBox* combo) {
    if (combo == nullptr) {
      return;
    }
    const QString current = combo->currentText().trimmed();
    QString start_dir = current;
    if (!start_dir.isEmpty() && !QFileInfo(start_dir).isDir()) {
      start_dir = QFileInfo(start_dir).absolutePath();
    }
    const QString selected = QFileDialog::getExistingDirectory(
        this,
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1003)),
        start_dir);
    if (!selected.isEmpty()) {
      combo->setEditText(QDir::toNativeSeparators(selected));
    }
  }

  void update_link_button() {
    link_button_->setEnabled(!from_combo_->currentText().trimmed().isEmpty() &&
                             !to_combo_->currentText().trimmed().isEmpty());
  }

  QComboBox* from_combo_ = nullptr;
  QComboBox* to_combo_ = nullptr;
  QButtonGroup* type_group_ = nullptr;
  QRadioButton* file_symlink_radio_ = nullptr;
  QRadioButton* directory_symlink_radio_ = nullptr;
  QPushButton* link_button_ = nullptr;
};

}  // namespace

void MainWindow::on_comment_requested() {
  const PanelController& panel = active_panel_controller();
  if (panel.focused_item_is_parent_link()) {
    QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(552)), z7::ui::runtime_support::L(3015));
    return;
  }

  const QString source_path = panel.focused_path();
  if (source_path.trimmed().isEmpty()) {
    QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(552)), z7::ui::runtime_support::L(3015));
    return;
  }

  const QString current_comment = focused_comment_for_panel(active_panel_index_);
  bool ok = false;
  const QString comment = QInputDialog::getMultiLineText(this,
                                                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(552)),
                                                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6401)),
                                                         current_comment,
                                                         &ok);
  if (!ok) {
    return;
  }

  start_task_with_runner(
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(552)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(552)),
      [source_path,
       comment,
       archive_mode = panel.in_archive_view(),
       archive_path = panel.archive.source_archive,
       archive_token = panel.archive.current_token](ArchiveProcessRunner* runner) {
        if (runner == nullptr) {
          return false;
        }
        if (archive_mode) {
          return runner->start_archive_comment(archive_path,
                                               source_path,
                                               archive_token,
                                               comment);
        }
        const QFileInfo source_info(source_path);
        return runner->start_filesystem_comment(source_info.absolutePath(),
                                                source_info.fileName(),
                                                comment);
      },
      [this](bool ok_result,
             int,
             int,
             const QString&,
             const z7::app::OperationOutcome&) {
        if (ok_result) {
          refresh_directory();
        }
      });
}

void MainWindow::on_link_requested() {
  const int source_panel_index = active_panel_index_;
  if (source_panel_index < 0 || in_archive_view_for_panel(source_panel_index)) {
    return;
  }

  const QStringList paths =
      panel_controller(source_panel_index).selected_real_item_paths();
  if (paths.size() != 1) {
    QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(558)), z7::ui::runtime_support::L(3015));
    return;
  }

  // Match original 7-Zip semantics: resolve relative link targets against
  // the source panel's filesystem directory captured when the action starts.
  const QString source_panel_base_dir =
      current_directory_for_panel(source_panel_index).trimmed();
  if (source_panel_base_dir.isEmpty()) {
    return;
  }

  const QFileInfo source_info(paths.front());
  if (!source_info.exists()) {
    QMessageBox::warning(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(558)), z7::ui::runtime_support::L(3014));
    return;
  }

  QString opposite_panel_dir;
  const int opposite_panel_index = source_panel_index == 0 ? 1 : 0;
  if (two_panels_visible_ && !in_archive_view_for_panel(opposite_panel_index)) {
    opposite_panel_dir = current_directory_for_panel(opposite_panel_index);
  }

  const QString initial_link_from =
      default_link_from_path(source_info, source_panel_base_dir, opposite_panel_dir);
  const QString initial_link_to = source_info.absoluteFilePath();
  const LinkDialogType initial_type = source_info.isDir()
                                          ? LinkDialogType::kDirectorySymlink
                                          : LinkDialogType::kFileSymlink;
  LinkDialog dialog(initial_link_from, initial_link_to, initial_type, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const LinkDialogResult result = dialog.result();
  const QString link_from_path =
      resolve_link_target_path(result.link_from, source_panel_base_dir);
  const QString link_to_path =
      resolve_link_target_path(result.link_to, source_panel_base_dir);
  if (link_from_path.isEmpty() || link_to_path.isEmpty()) {
    return;
  }

  const QFileInfo link_from_info(link_from_path);
  if (link_from_info.exists()) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(558)),
                         QStringLiteral("Target already exists:\n%1")
                             .arg(QDir::toNativeSeparators(link_from_path)));
    return;
  }

  const QFileInfo link_to_info(link_to_path);
  const bool directory_symlink = result.type == LinkDialogType::kDirectorySymlink;
  if (link_to_info.exists() && link_to_info.isDir() != directory_symlink) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(558)),
                         QStringLiteral("Incorrect link type"));
    return;
  }

  const QString parent_dir = link_from_info.absolutePath();
  if (!QDir().mkpath(parent_dir)) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(558)),
                         QStringLiteral("Failed to prepare target directory:\n%1")
                             .arg(QDir::toNativeSeparators(parent_dir)));
    return;
  }

  std::error_code ec;
  const std::filesystem::path link_to_native(
      z7::ui::archive_support::to_native_string(link_to_path));
  const std::filesystem::path link_from_native(
      z7::ui::archive_support::to_native_string(link_from_path));
  if (directory_symlink) {
    std::filesystem::create_directory_symlink(link_to_native, link_from_native, ec);
  } else {
    std::filesystem::create_symlink(link_to_native, link_from_native, ec);
  }
  if (ec) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(558)),
                         QStringLiteral("Failed to create link:\n%1")
                             .arg(QString::fromLocal8Bit(ec.message())));
    return;
  }

  refresh_directory();
}

void MainWindow::on_alternate_streams_requested() {
  if (in_archive_view()) {
    return;
  }

  const QStringList paths = active_panel_controller().selected_real_item_paths();
  if (paths.size() != 1) {
    QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(559)), z7::ui::runtime_support::L(3015));
    return;
  }

  const QFileInfo source_info(paths.front());
  if (!source_info.exists()) {
    QMessageBox::warning(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(559)), z7::ui::runtime_support::L(3014));
    return;
  }

#if defined(Q_OS_WIN)
  const QString stream_view_path = source_info.absoluteFilePath() + QStringLiteral(":");
  if (!open_path_externally_untracked(stream_view_path)) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(559)),
                         QStringLiteral("Failed to open alternate streams view:\n%1")
                             .arg(QDir::toNativeSeparators(stream_view_path)));
  }
#else
  QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(559)), z7::ui::runtime_support::L(3014));
#endif
}

}  // namespace z7::ui::filemanager
