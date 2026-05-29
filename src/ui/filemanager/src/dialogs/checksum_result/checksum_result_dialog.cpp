// src/ui/filemanager/src/dialogs/checksum_result/checksum_result_dialog.cpp
// Role: Checksum result table dialog and persisted column widths.

#include "checksum_result_dialog.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "portable_settings.h"
#include "official_lang_catalog.h"
#include "shared/column_width_persistence.h"

namespace z7::ui::filemanager {

namespace {

constexpr const char* kSettingsFmChecksumResultColumns = "FM/View/ChecksumResultColumns";

}  // namespace

ChecksumResultDialog::ChecksumResultDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(z7::ui::runtime_support::L(7501));
  resize(960, 640);
  setModal(true);

  auto* layout = new QVBoxLayout(this);

  table_ = new QTableWidget(this);
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels(
      {z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004)), QStringLiteral("Value")});
  auto* header = table_->horizontalHeader();
  header->setVisible(true);
  header->setStretchLastSection(false);
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setMinimumSectionSize(column_width_persistence::kMinColumnWidth);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionMode(QAbstractItemView::NoSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table_, 1);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
  if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) {
    ok->setText(z7::ui::runtime_support::L(401));
  }
  if (QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel)) {
    cancel->setText(z7::ui::runtime_support::L(402));
  }
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  load_table_column_widths();
}

void ChecksumResultDialog::set_rows(const QVector<QPair<QString, QString>>& rows) {
  table_->setRowCount(rows.size());
  for (int row = 0; row < rows.size(); ++row) {
    const QPair<QString, QString>& entry = rows.at(row);
    auto* name_item = new QTableWidgetItem(entry.first);
    name_item->setFlags(Qt::ItemIsEnabled);
    table_->setItem(row, 0, name_item);

    auto* value_item = new QTableWidgetItem(entry.second);
    value_item->setFlags(Qt::ItemIsEnabled);
    table_->setItem(row, 1, value_item);
  }
  if (!has_persisted_column_widths_ && !auto_sized_once_) {
    table_->resizeColumnToContents(0);
    auto_sized_once_ = true;
  }
}

void ChecksumResultDialog::closeEvent(QCloseEvent* event) {
  save_table_column_widths();
  QDialog::closeEvent(event);
}

void ChecksumResultDialog::load_table_column_widths() {
  if (table_ == nullptr || table_->horizontalHeader() == nullptr) {
    return;
  }

  constexpr int kExpectedColumns = 2;
  const QVector<int> default_widths = {280, 620};

  z7::platform::qt::PortableSettings settings;
  const QString encoded = settings
                              .value(QString::fromLatin1(kSettingsFmChecksumResultColumns),
                                     QString())
                              .toString()
                              .trimmed();
  QVector<int> widths;
  has_persisted_column_widths_ = column_width_persistence::decode_widths(
      encoded, kExpectedColumns, &widths);
  if (!has_persisted_column_widths_) {
    widths = default_widths;
  }
  column_width_persistence::apply_widths(table_->horizontalHeader(), widths);
}

void ChecksumResultDialog::save_table_column_widths() const {
  if (table_ == nullptr || table_->horizontalHeader() == nullptr) {
    return;
  }

  constexpr int kExpectedColumns = 2;
  const QVector<int> widths = column_width_persistence::capture_widths(
      table_->horizontalHeader(), kExpectedColumns);
  if (widths.size() != kExpectedColumns) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  settings.setValue(
      QString::fromLatin1(kSettingsFmChecksumResultColumns),
      column_width_persistence::encode_widths(widths));
}

}  // namespace z7::ui::filemanager
