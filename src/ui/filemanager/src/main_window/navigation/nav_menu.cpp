// src/ui/filemanager/src/main_window/navigation/nav_menu.cpp
// Role: SevenZip submenu composition.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

QMenu* MainWindow::append_seven_zip_submenu(QMenu* menu,
                                             const SevenZipMenuState& state,
                                             QAction* insert_before) {
  if (menu == nullptr || !state.visible) {
    return nullptr;
  }

  auto* seven = new QMenu(z7::ui::runtime_support::L(0), menu);
  if (insert_before != nullptr) {
    menu->insertMenu(insert_before, seven);
  } else {
    menu->addMenu(seven);
  }

  if (state.show_open) {
    QAction* open_action = seven->addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(2322)));
    connect(open_action, &QAction::triggered, this, &MainWindow::run_sevenzip_open_archive);

    if (state.show_open_as) {
      QMenu* open_as = seven->addMenu(z7::ui::runtime_support::strip_mnemonic(lang_or(2322)));
      static const QStringList kOpenTypes = {
          QStringLiteral("*"),
          QStringLiteral("#"),
          QStringLiteral("#:e"),
          QStringLiteral("7z"),
          QStringLiteral("zip"),
          QStringLiteral("cab"),
          QStringLiteral("rar")};
      for (const QString& type : kOpenTypes) {
        QAction* open_as_action = open_as->addAction(type);
        open_as_action->setProperty(kActionCapabilityKeyProperty,
                                    action_capability_key(ActionCapabilityKey::kSevenZipOpenAs));
        open_as_action->setProperty(kActionCapabilityReasonProperty, QVariant());
        connect(open_as_action, &QAction::triggered, this, [this, type]() {
          run_sevenzip_open_archive_as(type);
        });
      }
    }
  }

  if (state.show_extract_group) {
    QAction* extract_files_action = seven->addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(2323)));
    connect(extract_files_action,
            &QAction::triggered,
            this,
            &MainWindow::run_sevenzip_extract_files_dialog);

    QAction* extract_here_action = seven->addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(2326)));
    connect(extract_here_action,
            &QAction::triggered,
            this,
            &MainWindow::run_sevenzip_extract_here);

    const QString extract_to_label = format_menu_name(2327, state.extract_subdir);
    QAction* extract_to_action = seven->addAction(extract_to_label);
    connect(extract_to_action,
            &QAction::triggered,
            this,
            &MainWindow::run_sevenzip_extract_to);

    if (state.show_test) {
      QAction* test_action = seven->addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(2325)));
      connect(test_action,
              &QAction::triggered,
              this,
              &MainWindow::run_sevenzip_test_archive);
    }

    seven->addSeparator();
  }

  if (state.show_compress_group) {
    QAction* add_dialog_action = seven->addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(2324)));
    connect(add_dialog_action,
            &QAction::triggered,
            this,
            &MainWindow::run_sevenzip_add_to_archive);

    const QString add_7z_label = format_menu_name(2328, state.archive_name_7z);
    QAction* add_7z_action = seven->addAction(add_7z_label);
    connect(add_7z_action, &QAction::triggered, this, [this]() {
      run_sevenzip_add_to_type(QStringLiteral("7z"));
    });

    const QString add_zip_label = format_menu_name(2328, state.archive_name_zip);
    QAction* add_zip_action = seven->addAction(add_zip_label);
    connect(add_zip_action, &QAction::triggered, this, [this]() {
      run_sevenzip_add_to_type(QStringLiteral("zip"));
    });
  }

  if (state.show_crc_group) {
    QMenu* crc_menu = seven->addMenu(z7::ui::runtime_support::J(
        QStringLiteral("shell.actions.crc_sha_menu")));
    for (const z7::shell_integration::ShellIntegrationHashMethodDef& item :
         z7::shell_integration::shell_integration_hash_method_defs()) {
      QAction* action = crc_menu->addAction(QString::fromLatin1(item.label));
      const QString method = QString::fromLatin1(item.method);
      connect(action, &QAction::triggered, this, [this, method]() {
        run_sevenzip_hash(method);
      });
    }

    crc_menu->addSeparator();

    const QStringList crc_paths = active_panel_controller().oper_smart_real_item_paths();
    QString dynamic_sha_name =
        z7::shell_integration::shell_integration_create_archive_name_from_paths(
            crc_paths.isEmpty() ? state.selected_real_item_paths : crc_paths,
            true,
            nullptr) +
        QStringLiteral(".sha256");
    reduce_menu_string(&dynamic_sha_name);
    QAction* generate_sha256 =
        crc_menu->addAction(z7::ui::runtime_support::JF(
            QStringLiteral("shell.actions.generate_sha256"),
            {dynamic_sha_name}));
    connect(generate_sha256,
            &QAction::triggered,
            this,
            &MainWindow::run_sevenzip_generate_sha256);

    QAction* checksum_test =
        crc_menu->addAction(z7::ui::runtime_support::J(
            QStringLiteral("shell.actions.checksum_test")));
    connect(checksum_test,
            &QAction::triggered,
            this,
            &MainWindow::run_sevenzip_checksum_test);
  }

  return seven;
}

void MainWindow::rebuild_file_menu_seven_zip_section() {
  if (file_menu_ == nullptr) {
    return;
  }

  if (file_menu_seven_zip_menu_ != nullptr) {
    file_menu_->removeAction(file_menu_seven_zip_menu_->menuAction());
    file_menu_seven_zip_menu_->deleteLater();
    file_menu_seven_zip_menu_ = nullptr;
  }
  if (file_menu_seven_zip_separator_action_ != nullptr) {
    file_menu_->removeAction(file_menu_seven_zip_separator_action_);
    file_menu_seven_zip_separator_action_->deleteLater();
    file_menu_seven_zip_separator_action_ = nullptr;
  }

  const bool shift_pressed =
      (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
  const SevenZipMenuState seven_zip_state =
      compute_seven_zip_menu_state(shift_pressed);
  if (!seven_zip_state.visible) {
    return;
  }

  QAction* first_action =
      file_menu_->actions().isEmpty() ? nullptr : file_menu_->actions().front();
  file_menu_seven_zip_menu_ =
      append_seven_zip_submenu(file_menu_, seven_zip_state, first_action);
  if (file_menu_seven_zip_menu_ == nullptr) {
    return;
  }

  if (first_action != nullptr) {
    file_menu_seven_zip_separator_action_ =
        file_menu_->insertSeparator(first_action);
  } else {
    file_menu_seven_zip_separator_action_ = file_menu_->addSeparator();
  }
}

}  // namespace z7::ui::filemanager
