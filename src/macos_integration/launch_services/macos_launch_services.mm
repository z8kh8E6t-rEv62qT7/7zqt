#include "macos_launch_services.h"

#include <AppKit/AppKit.h>
#include <dispatch/dispatch.h>

#include <QDir>
#include <QFileInfo>

namespace z7::macos_integration::launch_services {
namespace {

QString app_bundle_path_for_executable(const QString& executable_path) {
  QFileInfo info(executable_path);
  if (!info.exists() || !info.isFile()) {
    return QString();
  }

  QDir parent_dir = info.absoluteDir();
  for (;;) {
    const QString dir_name = parent_dir.dirName();
    if (dir_name.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
      return parent_dir.absolutePath();
    }
    if (!parent_dir.cdUp()) {
      break;
    }
  }
  return QString();
}

NSString* qstring_to_nsstring(const QString& value) {
  const QByteArray utf8 = value.toUtf8();
  if (utf8.isEmpty()) {
    return @"";
  }
  return [NSString stringWithUTF8String:utf8.constData()];
}

NSArray<NSString*>* qstringlist_to_nsarray(const QStringList& values) {
  NSMutableArray<NSString*>* array =
      [NSMutableArray arrayWithCapacity:values.size()];
  for (const QString& value : values) {
    [array addObject:qstring_to_nsstring(value)];
  }
  return array;
}

QString launch_error_message_from_nsstring(NSString* value) {
  if (value == nil || value.length == 0) {
    return QStringLiteral("Failed to launch 7zFM via NSWorkspace.");
  }
  return QString::fromUtf8(value.UTF8String);
}

}  // namespace

FileManagerInstanceLaunchResult launch_new_filemanager_instance(
    const FileManagerInstanceLaunchRequest& request,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

  const QString bundle_path = app_bundle_path_for_executable(request.program_path);
  if (bundle_path.isEmpty()) {
    return FileManagerInstanceLaunchResult::kFallbackToDetached;
  }

  @autoreleasepool {
    NSURL* const application_url =
        [NSURL fileURLWithPath:qstring_to_nsstring(bundle_path) isDirectory:YES];
    if (application_url == nil) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Failed to resolve 7zFM app bundle URL.");
      }
      return FileManagerInstanceLaunchResult::kFailed;
    }

    NSWorkspaceOpenConfiguration* const configuration =
        [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;
    configuration.createsNewApplicationInstance = YES;
    configuration.allowsRunningApplicationSubstitution = NO;
    configuration.arguments = qstringlist_to_nsarray(request.arguments);

    __block bool launched = false;
    __block NSString* failure_message = nil;
    dispatch_semaphore_t const semaphore = dispatch_semaphore_create(0);
    [[NSWorkspace sharedWorkspace]
        openApplicationAtURL:application_url
                configuration:configuration
            completionHandler:^(NSRunningApplication* app, NSError* error) {
              launched = (app != nil && error == nil);
              if (!launched && error.localizedDescription.length > 0) {
                failure_message = [error.localizedDescription copy];
              }
              dispatch_semaphore_signal(semaphore);
            }];
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    if (launched) {
      return FileManagerInstanceLaunchResult::kLaunched;
    }
    if (error_message != nullptr) {
      *error_message = launch_error_message_from_nsstring(failure_message);
    }
#if !__has_feature(objc_arc)
    [failure_message release];
#endif
  }

  return FileManagerInstanceLaunchResult::kFailed;
}

}  // namespace z7::macos_integration::launch_services
