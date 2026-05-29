#pragma once

#include <QDialog>
#include <QPair>
#include <QVector>

class QCloseEvent;
class QTableWidget;

namespace z7::ui::filemanager {

class ChecksumResultDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ChecksumResultDialog(QWidget* parent = nullptr);
  void set_rows(const QVector<QPair<QString, QString>>& rows);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void load_table_column_widths();
  void save_table_column_widths() const;

  QTableWidget* table_ = nullptr;
  bool has_persisted_column_widths_ = false;
  bool auto_sized_once_ = false;
};

}  // namespace z7::ui::filemanager
