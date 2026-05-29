// src/ui/filemanager/src/main_window/drag_drop/drag_source_marker.cpp
// Role: Internal drag-source marker encode/decode and trust checks.

#include "drag_source_marker.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSet>
#include <QStringList>
#include <QUuid>

namespace z7::ui::filemanager {

namespace {

QString ensure_drag_source_instance_id() {
  QCoreApplication* app = QCoreApplication::instance();
  if (app == nullptr) {
    return QString();
  }

  QString instance_id =
      app->property(kDragSourceInstanceIdProperty).toString().trimmed();
  if (instance_id.isEmpty()) {
    instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    app->setProperty(kDragSourceInstanceIdProperty, instance_id);
  }
  return instance_id;
}

bool parse_marker_v1(const QByteArray& payload, qint64* pid, QString* sid) {
  if (pid == nullptr || sid == nullptr) {
    return false;
  }

  const QString text = QString::fromUtf8(payload).trimmed();
  if (!text.startsWith(QStringLiteral("v1;"))) {
    return false;
  }

  const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
  if (parts.size() < 3 || parts[0] != QStringLiteral("v1")) {
    return false;
  }

  QString pid_text;
  QString sid_text;
  for (int i = 1; i < parts.size(); ++i) {
    const QString part = parts[i];
    const int eq = part.indexOf(QLatin1Char('='));
    if (eq <= 0 || eq >= part.size() - 1) {
      continue;
    }
    const QString key = part.left(eq).trimmed().toLower();
    const QString value = part.mid(eq + 1).trimmed();
    if (key == QStringLiteral("pid")) {
      pid_text = value;
      continue;
    }
    if (key == QStringLiteral("sid")) {
      sid_text = value;
      continue;
    }
  }

  if (pid_text.isEmpty() || sid_text.isEmpty()) {
    return false;
  }

  bool ok = false;
  const qint64 parsed_pid = pid_text.toLongLong(&ok);
  if (!ok) {
    return false;
  }

  *pid = parsed_pid;
  *sid = sid_text;
  return true;
}

bool parse_archive_marker(const QByteArray& payload,
                          qint64* pid,
                          QString* sid,
                          InternalArchiveSourcePayload* archive_payload) {
  if (pid == nullptr || sid == nullptr || archive_payload == nullptr) {
    return false;
  }

  const QJsonDocument document = QJsonDocument::fromJson(payload);
  if (!document.isObject()) {
    return false;
  }

  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("v")).toInt() != 1) {
    return false;
  }

  const QJsonValue pid_value = object.value(QStringLiteral("pid"));
  if (pid_value.isDouble()) {
    *pid = static_cast<qint64>(pid_value.toInteger());
  } else if (pid_value.isString()) {
    bool ok = false;
    *pid = pid_value.toString().trimmed().toLongLong(&ok);
    if (!ok) {
      return false;
    }
  } else {
    return false;
  }

  *sid = object.value(QStringLiteral("sid")).toString().trimmed();
  if (sid->isEmpty()) {
    return false;
  }

  archive_payload->archive_path =
      object.value(QStringLiteral("archive_path")).toString().trimmed();
  archive_payload->archive_type_hint =
      object.value(QStringLiteral("archive_type_hint")).toString().trimmed();
  archive_payload->session_token_value = 0;

  const QJsonValue session_token_value = object.value(QStringLiteral("session_token"));
  if (session_token_value.isDouble()) {
    archive_payload->session_token_value =
        static_cast<quint64>(session_token_value.toInteger());
  } else if (session_token_value.isString()) {
    bool ok = false;
    archive_payload->session_token_value =
        session_token_value.toString().trimmed().toULongLong(&ok);
    if (!ok) {
      archive_payload->session_token_value = 0;
    }
  }

  const QJsonArray entries_array = object.value(QStringLiteral("entries")).toArray();
  QSet<QString> dedup;
  archive_payload->entries.clear();
  archive_payload->entries.reserve(entries_array.size());
  for (const QJsonValue& value : entries_array) {
    const QString entry = value.toString().trimmed();
    if (entry.isEmpty() || dedup.contains(entry)) {
      continue;
    }
    dedup.insert(entry);
    archive_payload->entries.push_back(entry);
  }

  if (archive_payload->archive_path.isEmpty() || archive_payload->entries.isEmpty()) {
    return false;
  }
  return true;
}

}  // namespace

QString drag_source_instance_id() {
  return ensure_drag_source_instance_id();
}

QByteArray make_internal_fs_source_marker() {
  const QString instance_id = ensure_drag_source_instance_id();
  if (instance_id.trimmed().isEmpty()) {
    return QByteArrayLiteral("1");
  }

  return QStringLiteral("v1;pid=%1;sid=%2")
      .arg(QCoreApplication::applicationPid())
      .arg(instance_id)
      .toUtf8();
}

QByteArray make_internal_archive_source_marker(const QString& archive_path,
                                               const QString& archive_type_hint,
                                               const QStringList& entries,
                                               quint64 session_token_value) {
  const QString instance_id = ensure_drag_source_instance_id();
  if (instance_id.trimmed().isEmpty()) {
    return {};
  }

  QSet<QString> dedup;
  QJsonArray normalized_entries;
  for (const QString& entry : entries) {
    const QString trimmed = entry.trimmed();
    if (trimmed.isEmpty() || dedup.contains(trimmed)) {
      continue;
    }
    dedup.insert(trimmed);
    normalized_entries.append(trimmed);
  }

  if (archive_path.trimmed().isEmpty() || normalized_entries.isEmpty()) {
    return {};
  }

  QJsonObject object;
  object.insert(QStringLiteral("v"), 1);
  object.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
  object.insert(QStringLiteral("sid"), instance_id);
  object.insert(QStringLiteral("archive_path"), archive_path.trimmed());
  object.insert(QStringLiteral("archive_type_hint"), archive_type_hint.trimmed());
  if (session_token_value != 0) {
    object.insert(QStringLiteral("session_token"), QString::number(session_token_value));
  }
  object.insert(QStringLiteral("entries"), normalized_entries);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool has_internal_fs_source_marker(const QMimeData* mime_data) {
  if (mime_data == nullptr) {
    return false;
  }
  return mime_data->hasFormat(QString::fromLatin1(kMimeTypeZ7FmFsSource));
}

bool is_trusted_internal_fs_source_marker(const QMimeData* mime_data) {
  if (!has_internal_fs_source_marker(mime_data)) {
    return false;
  }

  qint64 marker_pid = -1;
  QString marker_instance_id;
  if (!parse_marker_v1(
          mime_data->data(QString::fromLatin1(kMimeTypeZ7FmFsSource)),
          &marker_pid,
          &marker_instance_id)) {
    return false;
  }

  const QString current_instance_id = ensure_drag_source_instance_id();
  if (current_instance_id.isEmpty()) {
    return false;
  }
  if (marker_instance_id != current_instance_id) {
    return false;
  }
  if (marker_pid != QCoreApplication::applicationPid()) {
    return false;
  }
  return true;
}

bool has_internal_archive_source_marker(const QMimeData* mime_data) {
  if (mime_data == nullptr) {
    return false;
  }
  return mime_data->hasFormat(QString::fromLatin1(kMimeTypeZ7FmArchiveSource));
}

bool read_internal_archive_source_marker(const QMimeData* mime_data,
                                         InternalArchiveSourcePayload* payload,
                                         bool* trusted) {
  if (trusted != nullptr) {
    *trusted = false;
  }
  if (!has_internal_archive_source_marker(mime_data)) {
    return false;
  }

  InternalArchiveSourcePayload parsed_payload;
  qint64 marker_pid = -1;
  QString marker_instance_id;
  if (!parse_archive_marker(
          mime_data->data(QString::fromLatin1(kMimeTypeZ7FmArchiveSource)),
          &marker_pid,
          &marker_instance_id,
          &parsed_payload)) {
    return false;
  }

  if (payload != nullptr) {
    *payload = parsed_payload;
  }

  const QString current_instance_id = ensure_drag_source_instance_id();
  const bool marker_is_trusted =
      !current_instance_id.isEmpty() &&
      marker_instance_id == current_instance_id &&
      marker_pid == QCoreApplication::applicationPid();
  if (trusted != nullptr) {
    *trusted = marker_is_trusted;
  }
  return true;
}

}  // namespace z7::ui::filemanager
