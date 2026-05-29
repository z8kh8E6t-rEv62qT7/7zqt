// src/ui/gui/src/gui_app_controller/test_result.cpp
// Role: Test command result parsing and compact result dialog rendering.

#include "helpers.h"

#include <optional>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "archive_format.h"
#include "official_lang_catalog.h"

namespace z7::ui::gui::gui_app_controller_helpers {
namespace {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

QString localized(uint32_t id) {
  return strip_mnemonic(L(id));
}

struct ParsedTestSummary {
  std::optional<uint64_t> archives;
  std::optional<uint64_t> physical_size;
  std::optional<uint64_t> folders;
  std::optional<uint64_t> files;
  std::optional<uint64_t> size;
};

void merge_hash_summary(const z7::app::OperationResult& result,
                        ParsedTestSummary* summary) {
  if (summary == nullptr || !result.hash_summary.has_value()) {
    return;
  }
  const z7::app::HashSummary& hash = *result.hash_summary;
  if (hash.num_archives != 0) {
    summary->archives = hash.num_archives;
  }
  summary->folders = hash.num_dirs;
  summary->files = hash.num_files;
  summary->size = hash.files_size;
  if (hash.physical_size_defined) {
    summary->physical_size = hash.physical_size;
  }
}

ParsedTestSummary build_test_summary(const z7::app::OperationResult& result,
                                     uint64_t archive_count_hint) {
  ParsedTestSummary out;
  out.archives = archive_count_hint == 0 ? 1 : archive_count_hint;
  merge_hash_summary(result, &out);
  return out;
}

QString build_test_result_message_from_summary(const ParsedTestSummary& summary) {
  QStringList lines;
  lines << QStringLiteral("%1: %2")
               .arg(localized(3907))
               .arg(summary.archives.value_or(1));
  if (summary.physical_size.has_value()) {
    lines << QStringLiteral("%1: %2")
                 .arg(localized(1044))
                 .arg(z7::ui::archive_support::format_dual_size(
                     *summary.physical_size));
  }
  if (summary.folders.has_value()) {
    lines << QStringLiteral("%1: %2")
                 .arg(localized(1031))
                 .arg(*summary.folders);
  }
  if (summary.files.has_value()) {
    lines << QStringLiteral("%1: %2")
                 .arg(localized(1032))
                 .arg(*summary.files);
  }
  if (summary.size.has_value()) {
    lines << QStringLiteral("%1: %2")
                 .arg(localized(1007))
                 .arg(z7::ui::archive_support::format_dual_size(*summary.size));
  }
  lines << QString();
  lines << localized(3001);
  return lines.join(QLatin1Char('\n'));
}

void show_compact_result_dialog(QWidget* parent,
                                const QString& title,
                                const QString& text) {
  if (parent == nullptr) {
    parent = QApplication::activeModalWidget();
  }
  QDialog dialog(parent);
#ifdef Z7_TESTING
  dialog.setObjectName(QStringLiteral("testResultDialog"));
#endif
  dialog.setWindowTitle(title);
  dialog.resize(420, 280);

  auto* layout = new QVBoxLayout(&dialog);
  auto* content = new QLabel(text, &dialog);
#ifdef Z7_TESTING
  content->setObjectName(QStringLiteral("testResultTextLabel"));
#endif
  content->setWordWrap(true);
  content->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(content, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, &dialog);
  if (QPushButton* ok_button = buttons->button(QDialogButtonBox::Ok)) {
    ok_button->setText(L(401));
  }
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  layout->addWidget(buttons);

  dialog.exec();
}

}  // namespace

QString build_test_result_message(const z7::app::OperationResult& result,
                                  uint64_t archive_count_hint) {
  const ParsedTestSummary summary = build_test_summary(result, archive_count_hint);
  return build_test_result_message_from_summary(summary);
}

void show_test_result_dialog(QWidget* parent,
                             const QString& title,
                             const QString& text) {
#ifdef Z7_TESTING
  if (suppress_result_dialogs_for_tests()) {
    return;
  }
#endif
  show_compact_result_dialog(parent, title, text);
}

}  // namespace z7::ui::gui::gui_app_controller_helpers
