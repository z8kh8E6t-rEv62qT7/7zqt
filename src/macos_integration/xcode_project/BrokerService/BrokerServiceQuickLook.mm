#import "BrokerService.h"

#import "BrokerServiceUtilities.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>

static Z7BrokerQuickLookListResult *Z7MakeListResult(NSString *requestID,
                                                     const z7_mi_quicklook_list_result_t *result) {
    if (result == nullptr) {
        return [[Z7BrokerQuickLookListResult alloc] initWithRequestID:requestID
                                                                   ok:NO
                                                               status:Z7_MI_STATUS_INTERNAL_ERROR
                                                         errorMessage:@"quicklook_list returned no result."
                                                          archivePath:@""
                                                           virtualDir:@""
                                                                items:@[]];
    }

    NSMutableArray<Z7BrokerQuickLookItem *> *items = [NSMutableArray arrayWithCapacity:result->item_count];
    if (result->items != nullptr) {
        for (size_t index = 0; index < result->item_count; ++index) {
            const z7_mi_quicklook_item_t &item = result->items[index];
            if (item.path == nullptr || item.name == nullptr) {
                continue;
            }
            [items addObject:[[Z7BrokerQuickLookItem alloc] initWithPath:Z7ToNSString(item.path)
                                                                    name:Z7ToNSString(item.name)
                                                               directory:item.is_dir
                                                                    size:item.size
                                                              mtimeMsUtc:item.mtime_msecs_utc
                                                             archiveLike:item.is_archive_like]];
        }
    }

    return [[Z7BrokerQuickLookListResult alloc] initWithRequestID:requestID
                                                               ok:result->ok
                                                           status:result->status
                                                     errorMessage:Z7NullableNSString(result->error_message)
                                                      archivePath:Z7ToNSString(result->archive_path)
                                                       virtualDir:Z7ToNSString(result->virtual_dir)
                                                            items:items];
}

static Z7BrokerQuickLookBatchExportProgress *Z7MakeBatchExportProgress(
    NSString *requestID,
    const z7_mi_quicklook_batch_export_progress_t *progress) {
    if (progress == nullptr) {
        return [[Z7BrokerQuickLookBatchExportProgress alloc] initWithRequestID:requestID
                                                             completedItemCount:0
                                                                 totalItemCount:0
                                                               currentItemIndex:-1
                                                               currentEntryPath:@""
                                                         currentDestinationPath:@""
                                                                 currentPercent:-1
                                                                    totalsKnown:NO
                                                                     totalBytes:0
                                                                 completedBytes:0
                                                                    currentPath:nil
                                                                        message:nil];
    }

    return [[Z7BrokerQuickLookBatchExportProgress alloc] initWithRequestID:requestID
                                                         completedItemCount:(NSInteger)progress->completed_item_count
                                                             totalItemCount:(NSInteger)progress->total_item_count
                                                           currentItemIndex:(NSInteger)progress->current_item_index
                                                           currentEntryPath:Z7NullableNSString(progress->current_entry_path) ?: @""
                                                     currentDestinationPath:Z7NullableNSString(progress->current_destination_path) ?: @""
                                                             currentPercent:progress->current_percent
                                                                totalsKnown:progress->totals_known
                                                                 totalBytes:progress->total_bytes
                                                             completedBytes:progress->completed_bytes
                                                                currentPath:Z7NullableNSString(progress->current_path)
                                                                    message:Z7NullableNSString(progress->message)];
}

static Z7BrokerQuickLookBatchExportResult *Z7MakeBatchExportResult(
    NSString *requestID,
    const z7_mi_quicklook_batch_export_result_t *result) {
    if (result == nullptr) {
        return [[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:requestID
                                                                          ok:NO
                                                                      status:Z7_MI_STATUS_INTERNAL_ERROR
                                                                errorMessage:@"quicklook_batch_export returned no result."
                                                          completedItemCount:0
                                                              totalItemCount:0
                                                             failedItemIndex:-1
                                                             failedEntryPath:nil
                                                       failedDestinationPath:nil];
    }

    return [[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:requestID
                                                                      ok:result->ok
                                                                  status:result->status
                                                            errorMessage:Z7NullableNSString(result->error_message)
                                                      completedItemCount:(NSInteger)result->completed_item_count
                                                          totalItemCount:(NSInteger)result->total_item_count
                                                         failedItemIndex:(NSInteger)result->failed_item_index
                                                         failedEntryPath:Z7NullableNSString(result->failed_entry_path)
                                                   failedDestinationPath:Z7NullableNSString(result->failed_destination_path)];
}

static void Z7ListCallback(z7_mi_quicklook_list_result_t *result, void *userData) {
    if (userData == nullptr) {
        if (result != nullptr) {
            z7_mi_destroy_quicklook_list_result(result);
        }
        return;
    }

    BrokerRequestContext *context = CFBridgingRelease(userData);
    Z7BrokerQuickLookListResult *dto = Z7MakeListResult(context.requestID, result);
    if (result != nullptr) {
        z7_mi_destroy_quicklook_list_result(result);
    }
    [context.service finishListRequestWithID:context.requestID result:dto];
}

@interface Z7BatchExportCallbackContext : NSObject
@property (nonatomic, unsafe_unretained) BrokerService *service;
@property (nonatomic, copy) NSString *requestID;
- (instancetype)initWithService:(BrokerService *)service requestID:(NSString *)requestID;
@end

@implementation Z7BatchExportCallbackContext

- (instancetype)initWithService:(BrokerService *)service requestID:(NSString *)requestID {
    self = [super init];
    if (self) {
        _service = service;
        _requestID = [requestID copy];
    }
    return self;
}

@end

@interface BrokerService (QuickLookInternal)
- (void)enqueueBatchExportProgress:(Z7BrokerQuickLookBatchExportProgress *)progress;
@end

static void Z7BatchExportProgressCallback(
    z7_mi_quicklook_batch_export_progress_t *progress,
    void *userData) {
    if (userData == nullptr) {
        if (progress != nullptr) {
            z7_mi_destroy_quicklook_batch_export_progress(progress);
        }
        return;
    }

    Z7BatchExportCallbackContext *context =
        (__bridge Z7BatchExportCallbackContext *)userData;
    BrokerService *service = context.service;
    if (service != nil) {
        [service enqueueBatchExportProgress:Z7MakeBatchExportProgress(context.requestID, progress)];
    }
    if (progress != nullptr) {
        z7_mi_destroy_quicklook_batch_export_progress(progress);
    }
}

static void Z7BatchExportCompletionCallback(
    z7_mi_quicklook_batch_export_result_t *result,
    void *userData) {
    if (userData == nullptr) {
        if (result != nullptr) {
            z7_mi_destroy_quicklook_batch_export_result(result);
        }
        return;
    }

    Z7BatchExportCallbackContext *context =
        CFBridgingRelease(userData);
    BrokerService *service = context.service;
    if (service != nil) {
        [service finishBatchExportRequestWithID:context.requestID
                                         result:Z7MakeBatchExportResult(context.requestID, result)];
    }
    if (result != nullptr) {
        z7_mi_destroy_quicklook_batch_export_result(result);
    }
}

@implementation BrokerService (QuickLook)

- (void)enqueueBatchExportProgress:(Z7BrokerQuickLookBatchExportProgress *)progress {
    dispatch_async(_queue, ^{
        [self sendBatchExportProgressOnQueue:progress];
    });
}

- (void)startListRequestOnQueue:(Z7BrokerQuickLookListRequest *)request {
    if (_invalidated || _session == nullptr) {
        [self sendListResultOnQueue:[[Z7BrokerQuickLookListResult alloc] initWithRequestID:request.requestID
                                                                                         ok:NO
                                                                                     status:Z7_MI_STATUS_INTERNAL_ERROR
                                                                               errorMessage:@"Broker service is not available."
                                                                                archivePath:request.archivePath
                                                                                 virtualDir:request.virtualDir
                                                                                      items:@[]]];
        return;
    }

    std::string archivePath = Z7ToStdString(request.archivePath);
    std::string virtualDir = Z7ToStdString(request.virtualDir);
    std::string typeHint = Z7ToStdString(request.archiveTypeHint);
    Z7CStringArray nestedEntries(request.nestedArchiveEntries);
    z7_mi_task_t *task = nullptr;
    z7_mi_quicklook_list_request_t cRequest = {
        archivePath.c_str(),
        virtualDir.c_str(),
        typeHint.c_str(),
        nestedEntries.data(),
        nestedEntries.size()
    };
    BrokerRequestContext *context = [[BrokerRequestContext alloc] initWithService:self requestID:request.requestID];
    z7_mi_status_t status =
        z7_mi_quicklook_list(_session, &cRequest, Z7ListCallback, (void *)CFBridgingRetain(context), &task);
    if (status != Z7_MI_STATUS_OK) {
        if (task != nullptr) {
            z7_mi_task_release(task);
        }
        [self sendListResultOnQueue:[[Z7BrokerQuickLookListResult alloc] initWithRequestID:request.requestID
                                                                                         ok:NO
                                                                                     status:status
                                                                               errorMessage:@"Cannot start quicklook list request."
                                                                                archivePath:request.archivePath
                                                                                 virtualDir:request.virtualDir
                                                                                      items:@[]]];
        CFRelease((__bridge CFTypeRef)context);
        return;
    }

    _tasks[request.requestID] = [[BrokerTaskRecord alloc] initWithTask:task];
}

- (void)startBatchExportRequestOnQueue:(Z7BrokerQuickLookBatchExportRequest *)request {
    if (_invalidated || _session == nullptr) {
        [self sendBatchExportResultOnQueue:[[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:request.requestID
                                                                                                       ok:NO
                                                                                                   status:Z7_MI_STATUS_INTERNAL_ERROR
                                                                                             errorMessage:@"Broker service is not available."
                                                                                       completedItemCount:0
                                                                                           totalItemCount:request.items.count
                                                                                          failedItemIndex:-1
                                                                                          failedEntryPath:nil
                                                                                    failedDestinationPath:nil]];
        return;
    }

    std::string archivePath = Z7ToStdString(request.archivePath);
    std::string typeHint = Z7ToStdString(request.archiveTypeHint);
    Z7CStringArray nestedEntries(request.nestedArchiveEntries);

    std::vector<std::string> entryPaths;
    std::vector<std::string> destinationPaths;
    std::vector<z7_mi_quicklook_batch_export_item_t> nativeItems;
    entryPaths.reserve(request.items.count);
    destinationPaths.reserve(request.items.count);
    nativeItems.reserve(request.items.count);
    for (Z7BrokerQuickLookBatchExportItem *item in request.items) {
        entryPaths.push_back(Z7ToStdString(item.entryPath));
        destinationPaths.push_back(Z7ToStdString(item.destinationPath));
    }
    for (NSUInteger index = 0; index < request.items.count; ++index) {
        Z7BrokerQuickLookBatchExportItem *item = request.items[index];
        z7_mi_quicklook_batch_export_item_t nativeItem = {
            entryPaths[index].c_str(),
            destinationPaths[index].c_str(),
            item.listedSize,
            item.recursive,
            item.entryIsDirectory
        };
        nativeItems.push_back(nativeItem);
    }

    z7_mi_quicklook_batch_export_request_t cRequest = {
        archivePath.c_str(),
        typeHint.c_str(),
        nestedEntries.data(),
        nestedEntries.size(),
        nativeItems.data(),
        nativeItems.size()
    };
    z7_mi_task_t *task = nullptr;
    Z7BatchExportCallbackContext *context =
        [[Z7BatchExportCallbackContext alloc] initWithService:self requestID:request.requestID];
    const z7_mi_status_t status = z7_mi_quicklook_batch_export(
        _session,
        &cRequest,
        Z7BatchExportProgressCallback,
        Z7BatchExportCompletionCallback,
        (void *)CFBridgingRetain(context),
        &task);
    if (status != Z7_MI_STATUS_OK) {
        if (task != nullptr) {
            z7_mi_task_release(task);
        }
        CFRelease((__bridge CFTypeRef)context);
        [self sendBatchExportResultOnQueue:[[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:request.requestID
                                                                                                       ok:NO
                                                                                                   status:status
                                                                                             errorMessage:@"Cannot start quicklook batch export request."
                                                                                       completedItemCount:0
                                                                                           totalItemCount:request.items.count
                                                                                          failedItemIndex:-1
                                                                                          failedEntryPath:nil
                                                                                    failedDestinationPath:nil]];
        return;
    }

    _tasks[request.requestID] = [[BrokerTaskRecord alloc] initWithTask:task];
}

- (void)cancelRequestOnQueueWithID:(NSString *)requestID {
    BrokerTaskRecord *task = _tasks[requestID];
    if (task == nil) {
        return;
    }
    [_tasks removeObjectForKey:requestID];
    [task cancelAndRelease];
}

- (void)providePasswordOnQueueForPromptID:(NSString *)promptID password:(NSString *)password {
    BrokerPasswordPromptRecord *prompt = _prompts[promptID];
    if (prompt == nil) {
        return;
    }
    [_prompts removeObjectForKey:promptID];
    [prompt providePassword:password ?: @""];
}

- (void)cancelPasswordPromptOnQueueWithID:(NSString *)promptID {
    BrokerPasswordPromptRecord *prompt = _prompts[promptID];
    if (prompt == nil) {
        return;
    }
    [_prompts removeObjectForKey:promptID];
    [prompt cancel];
}

@end
