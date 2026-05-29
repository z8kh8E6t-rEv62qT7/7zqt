#pragma once

#include <QDialog>
#include <QPair>
#include <QVector>

class QTableWidget;

namespace z7::ui::gui {

class HashResultDialog : public QDialog {
  Q_OBJECT

 public:
  explicit HashResultDialog(QWidget* parent = nullptr);
  void set_rows(const QVector<QPair<QString, QString>>& rows);

 private:
  QTableWidget* table_view_ = nullptr;
};

}  // namespace z7::ui::gui
