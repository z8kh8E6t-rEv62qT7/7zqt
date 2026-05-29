#include "internal.h"

#include <QDir>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <thread>

#if !defined(Q_OS_MAC)

z7::macos_integration::native_drag::MacOSIntegrationNativeDragResult
z7::macos_integration::native_drag::run_macos_integration_native_drag(
    const z7::macos_integration::native_drag::MacOSIntegrationNativeDragRequest& request) {
  Q_UNUSED(request);
  return {};
}

#else

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

using z7::macos_integration::native_drag::detail::PromiseWriteConcurrencyPolicy;
using z7::macos_integration::native_drag::detail::ScopedLargeFileWriteSlotAcquire;
using z7::macos_integration::native_drag::detail::ScopedWriteSlotAcquire;
using z7::macos_integration::native_drag::detail::SharedState;
namespace z7nd_detail = z7::macos_integration::native_drag::detail;

static constexpr CGFloat kZ7NativeDragIconExtent = 64.0;
static constexpr CGFloat kZ7NativeDragPileOffset = 8.0;
static constexpr auto kZ7NativeDragNoPromiseGrace =
    std::chrono::milliseconds(150);

static NSString* Z7FallbackFilePromiseTypeIdentifier() {
  return @"public.data";
}

static NSOperationQueue* Z7MakeFilePromiseOperationQueue(NSInteger item_index) {
  NSOperationQueue* queue = [[NSOperationQueue alloc] init];
  queue.name = [NSString stringWithFormat:@"app.sevenzip.native-drag.file-promise.%ld",
                                          static_cast<long>(item_index)];
  queue.qualityOfService = NSQualityOfServiceUserInitiated;
  return queue;
}

static NSString* Z7ExplicitFileTypeIdentifier(
    const z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem& item) {
  const QString explicit_identifier = item.file_type_identifier.trimmed();
  if (explicit_identifier.isEmpty()) {
    return nil;
  }

  UTType* explicit_type =
      [UTType typeWithIdentifier:z7nd_detail::to_ns_string(explicit_identifier)];
  if (explicit_type == nil) {
    return nil;
  }
  return explicit_type.identifier;
}

static UTType* Z7ContentTypeForPromiseType(NSString* file_type_identifier) {
  if (file_type_identifier.length <= 0) {
    return UTTypeData;
  }

  UTType* content_type = [UTType typeWithIdentifier:file_type_identifier];
  return content_type == nil ? UTTypeData : content_type;
}

static NSString* Z7FilePromiseTypeIdentifier(
    const z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem& item) {
  if (item.is_dir) {
    return UTTypeFolder.identifier;
  }

  NSString* explicit_identifier = Z7ExplicitFileTypeIdentifier(item);
  if (explicit_identifier.length > 0) {
    return explicit_identifier;
  }

  const QString item_name = z7nd_detail::default_item_name(item);
  const QString extension = QFileInfo(item_name).suffix().trimmed();
  if (extension.isEmpty()) {
    return Z7FallbackFilePromiseTypeIdentifier();
  }

  UTType* content_type =
      [UTType typeWithFilenameExtension:z7nd_detail::to_ns_string(extension)];
  if (content_type == nil || content_type.identifier.length <= 0) {
    return Z7FallbackFilePromiseTypeIdentifier();
  }
  return content_type.identifier;
}

static NSImage* Z7DraggingIconForPromiseType(NSString* file_type_identifier) {
  UTType* content_type = Z7ContentTypeForPromiseType(file_type_identifier);
  return [[NSWorkspace sharedWorkspace] iconForContentType:content_type];
}

static bool Z7TryGetViewScreenRect(NSView* view, NSRect* out_rect) {
  if (view == nil || out_rect == nullptr || view.window == nil) {
    return false;
  }
  const NSRect view_bounds = [view bounds];
  const NSRect window_rect = [view convertRect:view_bounds toView:nil];
  *out_rect = [view.window convertRectToScreen:window_rect];
  return true;
}

static QVector<NSRect> Z7OwnAppWindowScreenRects() {
  QVector<NSRect> rects;
  const QWidgetList top_level_widgets = QApplication::topLevelWidgets();
  rects.reserve(top_level_widgets.size());
  for (QWidget* widget : top_level_widgets) {
    if (widget == nullptr || !widget->isWindow() || !widget->isVisible()) {
      continue;
    }
    widget->winId();
    NSView* view =
        (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
    NSRect rect = NSZeroRect;
    if (Z7TryGetViewScreenRect(view, &rect)) {
      rects.push_back(rect);
    }
  }
  return rects;
}

static NSDragOperation Z7NsDragOperationMaskForRequest(
    const SharedState& shared) {
  if (shared.request.kind ==
      z7::macos_integration::native_drag::MacOSIntegrationNativeDragKind::kArchive) {
    return NSDragOperationCopy;
  }

  NSDragOperation operations = 0;
  if (shared.request.supported_actions.testFlag(Qt::CopyAction)) {
    operations |= NSDragOperationCopy;
  }
  if (shared.request.supported_actions.testFlag(Qt::MoveAction)) {
    operations |= NSDragOperationMove;
  }
  return operations == 0 ? NSDragOperationCopy : operations;
}

static bool Z7RemoveMovedSourcePath(const QString& source_path,
                                    bool is_dir,
                                    QString* error_message) {
  const QString normalized_source = source_path.trimmed();
  if (normalized_source.isEmpty()) {
    return true;
  }

  const QFileInfo source_info(normalized_source);
  if (!source_info.exists()) {
    return true;
  }

  if (is_dir && !source_info.isSymLink()) {
    if (QDir(normalized_source).removeRecursively()) {
      return true;
    }
  } else if (QFile::remove(normalized_source)) {
    return true;
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral("Failed to remove moved source path: %1")
                         .arg(QDir::toNativeSeparators(normalized_source));
  }
  return false;
}

static qint64 Z7EstimatedPayloadSizeBytes(
    const z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem& item,
    const PromiseWriteConcurrencyPolicy& policy) {
  qint64 estimated_bytes = 0;
  if (item.estimate_payload_size_bytes) {
    estimated_bytes = qMax<qint64>(0, item.estimate_payload_size_bytes());
  }

  if (estimated_bytes > 0) {
    return estimated_bytes;
  }

  if (item.is_dir) {
    return qMax<qint64>(0, policy.large_file_threshold_bytes);
  }

  return 0;
}

@interface Z7MacDragSource : NSObject <NSDraggingSource> {
 @private
  std::shared_ptr<SharedState> _shared;
  NSArray* _retainedDelegates;
}

- (instancetype)initWithShared:(std::shared_ptr<SharedState>)shared
              retainedDelegates:(NSArray*)retainedDelegates;

@end

@interface Z7MacFilePromiseDelegate : NSObject <NSFilePromiseProviderDelegate> {
 @private
  std::shared_ptr<SharedState> _shared;
  NSInteger _itemIndex;
  NSOperationQueue* _operationQueue;
}

- (instancetype)initWithShared:(std::shared_ptr<SharedState>)shared
                     itemIndex:(NSInteger)itemIndex;

@end

@implementation Z7MacDragSource

- (instancetype)initWithShared:(std::shared_ptr<SharedState>)shared
              retainedDelegates:(NSArray*)retainedDelegates {
  self = [super init];
  if (self != nil) {
    _shared = std::move(shared);
    _retainedDelegates = retainedDelegates;
  }
  return self;
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  Q_UNUSED(session);
  Q_UNUSED(context);
  if (!_shared) {
    return NSDragOperationCopy;
  }
  return Z7NsDragOperationMaskForRequest(*_shared);
}

- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session {
  Q_UNUSED(session);
  return NO;
}

- (void)draggingSession:(NSDraggingSession*)session
             endedAtPoint:(NSPoint)screenPoint
                operation:(NSDragOperation)operation {
  Q_UNUSED(session);

  if (!_shared) {
    return;
  }

  bool ended_in_source_view = false;
  if (_shared->source_view_screen_rect_valid) {
    ended_in_source_view =
        NSMouseInRect(screenPoint, _shared->source_view_screen_rect, NO);
  }
  bool ended_in_own_app_window = false;
  for (const NSRect& rect : _shared->own_app_window_screen_rects) {
    if (NSMouseInRect(screenPoint, rect, NO)) {
      ended_in_own_app_window = true;
      break;
    }
  }
  _shared->mark_drag_session_ended(
      z7nd_detail::qt_action_from_ns_operation(operation),
      ended_in_source_view,
      ended_in_own_app_window);
}

@end

@implementation Z7MacFilePromiseDelegate

- (instancetype)initWithShared:(std::shared_ptr<SharedState>)shared
                     itemIndex:(NSInteger)itemIndex {
  self = [super init];
  if (self != nil) {
    _shared = std::move(shared);
    _itemIndex = itemIndex;
    _operationQueue = Z7MakeFilePromiseOperationQueue(itemIndex);
  }
  return self;
}

- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
                  fileNameForType:(NSString*)fileType {
  Q_UNUSED(filePromiseProvider);
  Q_UNUSED(fileType);
  const qsizetype item_index = static_cast<qsizetype>(_itemIndex);
  if (!_shared || item_index < 0 || item_index >= _shared->request.items.size()) {
    return @"7zFM-item";
  }

  const auto& item = _shared->request.items[static_cast<int>(item_index)];
  return z7nd_detail::to_ns_string(z7nd_detail::default_item_name(item));
}

- (NSOperationQueue*)operationQueueForFilePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider {
  Q_UNUSED(filePromiseProvider);
  return _operationQueue;
}

- (void)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
          writePromiseToURL:(NSURL*)url
           completionHandler:(void (^)(NSError* _Nullable))completionHandler {
  Q_UNUSED(filePromiseProvider);

  const qsizetype item_index = static_cast<qsizetype>(_itemIndex);
  if (!_shared || item_index < 0 || item_index >= _shared->request.items.size()) {
    if (completionHandler != nullptr) {
      completionHandler(
          z7nd_detail::make_copy_error(QStringLiteral("Invalid drag promise state.")));
    }
    return;
  }

  _shared->on_promise_request_started(static_cast<int>(item_index));
  ScopedWriteSlotAcquire write_gate(_shared.get());
  [[maybe_unused]] const int active_write_count = _shared->on_write_start();

  auto& item = _shared->request.items[static_cast<int>(item_index)];
  const QString item_name = z7nd_detail::default_item_name(item);
  QElapsedTimer total_timer;
  total_timer.start();

  qint64 direct_export_ms = 0;
  qint64 payload_size_bytes = 0;
  bool large_file_throttle_applied = false;
  QString destination_path;
  QString write_error;
  bool write_success = false;
  bool direct_export_attempted = false;
  bool direct_export_succeeded = false;

  auto finish_and_reply = [&](bool success, const QString& error_message) {
    _shared->on_write_finish(direct_export_ms,
                             total_timer.elapsed(),
                             success,
                             direct_export_attempted,
                             direct_export_succeeded,
                             payload_size_bytes,
                             large_file_throttle_applied,
                             static_cast<int>(item_index));

    if (!success) {
      const QString error = error_message.trimmed().isEmpty()
                                ? ( _shared->request.kind ==
                                            z7::macos_integration::native_drag::MacOSIntegrationNativeDragKind::kFilesystem
                                        ? QStringLiteral("Filesystem drag promise write failed.")
                                        : QStringLiteral("Archive drag promise write failed.") )
                                : error_message.trimmed();
      _shared->set_error_message_if_empty(error);
      if (completionHandler != nullptr) {
        completionHandler(z7nd_detail::make_copy_error(error));
      }
      return;
    }

    if (completionHandler != nullptr) {
      completionHandler(nil);
    }
  };

  destination_path = z7nd_detail::to_q_string(url);
  if (destination_path.trimmed().isEmpty()) {
    write_error = QStringLiteral("Promise destination path is empty.");
    finish_and_reply(false, write_error);
    return;
  }

  const PromiseWriteConcurrencyPolicy concurrency_policy =
      PromiseWriteConcurrencyPolicy::from_defaults();
  payload_size_bytes = Z7EstimatedPayloadSizeBytes(item, concurrency_policy);
  large_file_throttle_applied =
      _shared->should_use_large_file_throttle(payload_size_bytes);

  QElapsedTimer direct_export_timer;
  ScopedLargeFileWriteSlotAcquire large_write_gate(_shared.get(), large_file_throttle_applied);
  direct_export_timer.start();
  direct_export_attempted = true;
  if (!z7nd_detail::write_to_destination_if_needed(&item,
                                                   destination_path,
                                                   &write_error)) {
    direct_export_ms = direct_export_timer.elapsed();
    finish_and_reply(false, write_error);
    return;
  }
  direct_export_ms = direct_export_timer.elapsed();
  direct_export_succeeded = true;

  write_success = true;
  finish_and_reply(write_success, write_error);
}

@end

z7::macos_integration::native_drag::MacOSIntegrationNativeDragResult
z7::macos_integration::native_drag::run_macos_integration_native_drag(
    const z7::macos_integration::native_drag::MacOSIntegrationNativeDragRequest& request) {
  z7::macos_integration::native_drag::MacOSIntegrationNativeDragResult result;

  if (request.source_widget == nullptr || request.items.isEmpty()) {
    result.error_message = QStringLiteral("Invalid macOS native drag request.");
    return result;
  }

  QWidget* source_widget = request.source_widget->window();
  if (source_widget == nullptr) {
    source_widget = request.source_widget;
  }
  if (source_widget == nullptr) {
    result.error_message = QStringLiteral("Missing source widget.");
    return result;
  }
  result.source_widget_resolved = true;

  source_widget->winId();
  NSView* native_view =
      (__bridge NSView*)reinterpret_cast<void*>(source_widget->winId());
  if (native_view == nil) {
    result.error_message = QStringLiteral("Native NSView is unavailable.");
    return result;
  }
  result.native_view_available = true;

  NSEvent* current_event = [NSApp currentEvent];
  if (current_event == nil) {
    result.error_message = QStringLiteral("No active Cocoa event for drag start.");
    return result;
  }
  result.current_event_available = true;
  result.drag_item_count = request.items.size();

  NSRect source_view_screen_rect = NSZeroRect;
  bool source_view_screen_rect_valid = false;
  request.source_widget->winId();
  NSView* source_boundary_view =
      (__bridge NSView*)reinterpret_cast<void*>(request.source_widget->winId());
  if (source_boundary_view == nil) {
    source_boundary_view = native_view;
  }
  source_view_screen_rect_valid =
      Z7TryGetViewScreenRect(source_boundary_view, &source_view_screen_rect);

  const PromiseWriteConcurrencyPolicy concurrency_policy =
      PromiseWriteConcurrencyPolicy::from_defaults();
  std::shared_ptr<SharedState> shared =
      std::make_shared<SharedState>(request, concurrency_policy);
  shared->set_source_view_screen_rect(source_view_screen_rect,
                                      source_view_screen_rect_valid);
  shared->set_own_app_window_screen_rects(Z7OwnAppWindowScreenRects());

  NSMutableArray* dragging_items = [NSMutableArray array];
  NSMutableArray* delegates = [NSMutableArray array];

  for (int i = 0; i < request.items.size(); ++i) {
    Z7MacFilePromiseDelegate* delegate =
        [[Z7MacFilePromiseDelegate alloc] initWithShared:shared itemIndex:i];
    [delegates addObject:delegate];

    const auto& item = request.items[i];
    NSString* file_type_identifier = Z7FilePromiseTypeIdentifier(item);
    NSFilePromiseProvider* provider =
        [[NSFilePromiseProvider alloc] initWithFileType:file_type_identifier
                                               delegate:delegate];
    NSDraggingItem* dragging_item =
        [[NSDraggingItem alloc] initWithPasteboardWriter:provider];

    NSImage* icon = Z7DraggingIconForPromiseType(file_type_identifier);
    NSRect frame = NSMakeRect(kZ7NativeDragPileOffset * i,
                              -kZ7NativeDragPileOffset * i,
                              kZ7NativeDragIconExtent,
                              kZ7NativeDragIconExtent);
    [dragging_item setDraggingFrame:frame contents:icon];
    [dragging_items addObject:dragging_item];
  }

  Z7MacDragSource* drag_source =
      [[Z7MacDragSource alloc] initWithShared:shared retainedDelegates:delegates];
  NSDraggingSession* session =
      [native_view beginDraggingSessionWithItems:dragging_items event:current_event source:drag_source];
  if (session == nil) {
    result.error_message = QStringLiteral("Failed to begin native drag session.");
    return result;
  }
  result.drag_session_started = true;
  [session setDraggingFormation:NSDraggingFormationPile];

  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  timeout.start(120000);
  std::atomic_bool stop_waiter{false};
  std::thread settle_waiter([shared, &loop, &stop_waiter]() {
    quint64 observed_epoch = 0;
    bool grace_active = false;
    auto grace_deadline = std::chrono::steady_clock::time_point{};

    while (!stop_waiter.load(std::memory_order_acquire)) {
      const SharedState::MetricsSnapshot metrics = shared->metrics_snapshot();
      const auto now = std::chrono::steady_clock::now();
      if (metrics.drag_session_ended) {
        if (metrics.promise_requests_started == 0) {
          if (!grace_active) {
            grace_active = true;
            grace_deadline = now + kZ7NativeDragNoPromiseGrace;
          } else if (now >= grace_deadline) {
            break;
          }
        } else {
          grace_active = false;
          if (metrics.promise_requests_finished >=
              metrics.promise_requests_started) {
            break;
          }
        }
      } else {
        grace_active = false;
      }

      std::unique_lock<std::mutex> lock(shared->completion_wait_mutex);
      if (grace_active) {
        shared->completion_wait_condition.wait_until(
            lock,
            grace_deadline,
            [shared, &observed_epoch, &stop_waiter]() {
              return stop_waiter.load(std::memory_order_acquire) ||
                     shared->completion_wait_epoch != observed_epoch;
            });
      } else {
        shared->completion_wait_condition.wait(
            lock,
            [shared, &observed_epoch, &stop_waiter]() {
              return stop_waiter.load(std::memory_order_acquire) ||
                     shared->completion_wait_epoch != observed_epoch;
            });
      }
      observed_epoch = shared->completion_wait_epoch;
    }

    if (!stop_waiter.load(std::memory_order_acquire)) {
      QMetaObject::invokeMethod(
          QCoreApplication::instance(),
          [&loop]() { loop.quit(); },
          Qt::QueuedConnection);
    }
  });
  loop.exec();
  stop_waiter.store(true, std::memory_order_release);
  shared->notify_completion_waiters();
  if (settle_waiter.joinable()) {
    settle_waiter.join();
  }

  result.handled = true;
  const SharedState::MetricsSnapshot metrics = shared->metrics_snapshot();
  result.drag_completed = metrics.drag_session_ended;
  result.ended_in_source_view = metrics.ended_in_source_view;
  result.ended_in_own_app_window = metrics.ended_in_own_app_window;
  result.promise_writes_settled =
      metrics.drag_session_ended &&
      metrics.promise_requests_started == request.items.size() &&
      metrics.promise_requests_finished == request.items.size();
  result.transfer_requested = metrics.transfer_requested;
  result.promise_write_requests = metrics.promise_requests_started;
  result.promise_write_finishes = metrics.promise_requests_finished;
  result.promise_write_successes = metrics.write_successes;
  result.direct_export_attempts = metrics.direct_export_attempts;
  result.direct_export_successes = metrics.direct_export_successes;
  result.result_action = metrics.result_action;
  QString result_error_message = metrics.error_message;
  if (request.kind ==
          z7::macos_integration::native_drag::MacOSIntegrationNativeDragKind::kFilesystem &&
      result.result_action == Qt::MoveAction &&
      !result.ended_in_own_app_window) {
    for (int i = 0; i < request.items.size(); ++i) {
      const auto& item = request.items[i];
      if (!metrics.promise_write_succeeded_by_item.value(i, false)) {
        continue;
      }
      QString remove_error;
      if (!Z7RemoveMovedSourcePath(item.source_path, item.is_dir,
                                   &remove_error) &&
          result_error_message.trimmed().isEmpty()) {
        result_error_message = remove_error;
      }
    }
  }
  result.error_message = result_error_message;
  if (timeout.isActive()) {
    timeout.stop();
  } else {
    result.timed_out = true;
    if (result.error_message.isEmpty()) {
      result.error_message =
          QStringLiteral("Native drag timed out waiting for promised file writes.");
    }
  }
  return result;
}

#endif
