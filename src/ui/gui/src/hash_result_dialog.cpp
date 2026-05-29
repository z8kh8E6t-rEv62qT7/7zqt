#include "hash_result_dialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "official_lang_catalog.h"

namespace z7::ui::gui {

HashResultDialog::HashResultDialog(QWidget* parent)
    : QDialog(parent) {
#ifdef Z7_TESTING
  setObjectName(QStringLiteral("hashResultDialog"));
#endif
  setWindowTitle(z7::ui::runtime_support::L(7501));
  resize(720, 480);

  auto* layout = new QVBoxLayout(this);
  table_view_ = new QTableWidget(this);
#ifdef Z7_TESTING
  table_view_->setObjectName(QStringLiteral("hashResultTableView"));
#endif
  table_view_->setColumnCount(2);
  table_view_->setAlternatingRowColors(false);
  table_view_->setSelectionMode(QAbstractItemView::NoSelection);
  table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_view_->setShowGrid(false);
  table_view_->setWordWrap(true);
  table_view_->horizontalHeader()->setVisible(false);
  table_view_->horizontalHeader()->setStretchLastSection(true);
  table_view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table_view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_view_->verticalHeader()->setVisible(false);
  table_view_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  layout->addWidget(table_view_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
  if (QPushButton* close_button = buttons->button(QDialogButtonBox::Close)) {
    close_button->setText(z7::ui::runtime_support::L(408));
  }
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  layout->addWidget(buttons);
}

void HashResultDialog::set_rows(const QVector<QPair<QString, QString>>& rows) {
  table_view_->setRowCount(rows.size());
  for (int i = 0; i < rows.size(); ++i) {
    auto* key_item = new QTableWidgetItem(rows[i].first);
    auto* value_item = new QTableWidgetItem(rows[i].second);

    key_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    value_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    key_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    value_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    table_view_->setItem(i, 0, key_item);
    table_view_->setItem(i, 1, value_item);
  }
  table_view_->resizeRowsToContents();
}

}  // namespace z7::ui::gui
