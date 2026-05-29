#include "internal.h"

#include <QDir>
#include <QFileInfo>

namespace z7::macos_integration::native_drag::detail {

Qt::DropAction qt_action_from_ns_operation(NSDragOperation op) {
  if ((op & NSDragOperationMove) == NSDragOperationMove) {
    return Qt::MoveAction;
  }
  if ((op & NSDragOperationCopy) == NSDragOperationCopy) {
    return Qt::CopyAction;
  }
  return Qt::IgnoreAction;
}

NSString* to_ns_string(const QString& value) {
  return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

QString to_q_string(NSString* value) {
  if (value == nil) {
    return {};
  }
  return QString::fromUtf8([value UTF8String]);
}

QString to_q_string(NSURL* url) {
  if (url == nil) {
    return {};
  }
  return to_q_string([url path]);
}

NSError* make_copy_error(const QString& message) {
  NSDictionary* user_info = @{
    NSLocalizedDescriptionKey: to_ns_string(message)
  };
  return [NSError errorWithDomain:NSCocoaErrorDomain
                             code:NSFileWriteUnknownError
                         userInfo:user_info];
}

QString default_item_name(const MacOSIntegrationNativeDragItem& item) {
  const QString suggested_name = item.suggested_file_name.trimmed();
  if (!suggested_name.isEmpty()) {
    return suggested_name;
  }

  const QString entry_name =
      QFileInfo(item.archive_entry_path.trimmed()).fileName();
  if (!entry_name.isEmpty()) {
    return entry_name;
  }

  return QStringLiteral("7zFM-item");
}

}  // namespace z7::macos_integration::native_drag::detail
