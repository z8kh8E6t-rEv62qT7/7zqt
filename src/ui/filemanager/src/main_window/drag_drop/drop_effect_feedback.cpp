// src/ui/filemanager/src/main_window/drag_drop/drop_effect_feedback.cpp
// Role: Apply drag/drop performed-effect feedback compatible with Windows OLE semantics.

#include "drop_effect_feedback.h"

#include <QDropEvent>
#include <QMimeData>

namespace z7::ui::filemanager {

namespace {

constexpr quint32 kDropEffectNone = 0;
constexpr quint32 kDropEffectCopy = 1;
constexpr quint32 kDropEffectMove = 2;

constexpr const char* kClipboardNamePerformedDropEffect = "Performed DropEffect";
constexpr const char* kClipboardNameLogicalPerformedDropEffect =
    "Logical Performed DropEffect";

quint32 performed_drop_effect_value(bool operation_succeeded) {
  if (!operation_succeeded) {
    return kDropEffectNone;
  }
  // Keep copy as performed effect for safety, matching original 7-Zip behavior
  // that avoids source-side duplicate delete for external sources.
  return kDropEffectCopy;
}

quint32 logical_drop_effect_value(bool operation_succeeded,
                                  Qt::DropAction reported_action,
                                  bool trusted_internal_source) {
  if (!operation_succeeded || reported_action == Qt::IgnoreAction) {
    return kDropEffectNone;
  }
  if (reported_action == Qt::MoveAction && trusted_internal_source) {
    return kDropEffectMove;
  }
  return kDropEffectCopy;
}

void set_windows_clipboard_format_payload(QMimeData* mime_data,
                                          const char* clipboard_name,
                                          const QByteArray& payload) {
  if (mime_data == nullptr || clipboard_name == nullptr) {
    return;
  }

  // Keep both forms for Qt/Windows compatibility across parser differences:
  // quoted is the canonical form from Qt docs, unquoted mirrors legacy usage.
  const QString quoted_key = QStringLiteral(
      "application/x-qt-windows-mime;value=\"%1\"")
                                 .arg(QString::fromLatin1(clipboard_name));
  const QString unquoted_key = QStringLiteral(
      "application/x-qt-windows-mime;value=%1")
                                   .arg(QString::fromLatin1(clipboard_name));
  mime_data->setData(quoted_key, payload);
  mime_data->setData(unquoted_key, payload);
}

}  // namespace

QByteArray encode_windows_drop_effect_dword(quint32 value) {
  QByteArray payload(4, Qt::Uninitialized);
  payload[0] = static_cast<char>(value & 0xFF);
  payload[1] = static_cast<char>((value >> 8) & 0xFF);
  payload[2] = static_cast<char>((value >> 16) & 0xFF);
  payload[3] = static_cast<char>((value >> 24) & 0xFF);
  return payload;
}

void apply_windows_drop_effect_feedback(const QDropEvent* event,
                                        bool operation_succeeded,
                                        Qt::DropAction reported_action,
                                        bool trusted_internal_source) {
  if (event == nullptr) {
    return;
  }

  QMimeData* mime_data = const_cast<QMimeData*>(event->mimeData());
  if (mime_data == nullptr) {
    return;
  }

  const quint32 performed =
      performed_drop_effect_value(operation_succeeded);
  const quint32 logical =
      logical_drop_effect_value(operation_succeeded,
                                reported_action,
                                trusted_internal_source);

  set_windows_clipboard_format_payload(
      mime_data,
      kClipboardNamePerformedDropEffect,
      encode_windows_drop_effect_dword(performed));
  set_windows_clipboard_format_payload(
      mime_data,
      kClipboardNameLogicalPerformedDropEffect,
      encode_windows_drop_effect_dword(logical));
}

}  // namespace z7::ui::filemanager
