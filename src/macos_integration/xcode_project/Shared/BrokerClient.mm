#import "BrokerClient.h"

static NSString *const Z7BrokerServiceName = @"app.sevenzip.broker";
static NSInteger const Z7BrokerStatusInternalError = 4;

@class BrokerClient;

@interface BrokerClientCallbackReceiver : NSObject <BrokerClientCallbackProtocol>
- (instancetype)initWithClient:(BrokerClient *)client;
@end

@interface BrokerClient ()
- (nullable NSXPCConnection *)currentConnection;
- (void)handleListResult:(Z7BrokerQuickLookListResult *)result;
- (void)handleBatchExportProgress:(Z7BrokerQuickLookBatchExportProgress *)progress;
- (void)handleBatchExportResult:(Z7BrokerQuickLookBatchExportResult *)result;
- (void)handlePasswordPromptEvent:(Z7BrokerPasswordPromptEvent *)event;
- (void)completeListRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookListResult *)result;
- (void)completeBatchExportRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookBatchExportResult *)result;
- (void)handleConnectionFailure:(NSXPCConnection *)connection message:(NSString *)message;
@end

@implementation BrokerClientCallbackReceiver {
    __weak BrokerClient *_client;
}

- (instancetype)initWithClient:(BrokerClient *)client {
    self = [super init];
    if (self) {
        _client = client;
    }
    return self;
}

- (void)quickLookListDidFinishWithResult:(Z7BrokerQuickLookListResult *)result {
    [_client handleListResult:result];
}

- (void)quickLookBatchExportDidUpdateProgress:(Z7BrokerQuickLookBatchExportProgress *)progress {
    [_client handleBatchExportProgress:progress];
}

- (void)quickLookBatchExportDidFinishWithResult:(Z7BrokerQuickLookBatchExportResult *)result {
    [_client handleBatchExportResult:result];
}

- (void)passwordPromptDidRequestInput:(Z7BrokerPasswordPromptEvent *)event {
    [_client handlePasswordPromptEvent:event];
}

@end

@implementation BrokerClient {
    NSXPCConnection *_connection;
    BrokerClientCallbackReceiver *_callbackReceiver;
    NSMutableDictionary<NSString *, BrokerClientListCompletion> *_listCompletions;
    NSMutableDictionary<NSString *, BrokerClientBatchExportCompletion> *_batchExportCompletions;
    NSMutableDictionary<NSString *, BrokerClientBatchExportProgressHandler> *_batchExportProgressHandlers;
    BrokerClientPasswordPromptHandler _passwordPromptHandler;
}

+ (BrokerClient *)shared {
    static BrokerClient *client;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        client = [[BrokerClient alloc] init];
    });
    return client;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _listCompletions = [NSMutableDictionary dictionary];
        _batchExportCompletions = [NSMutableDictionary dictionary];
        _batchExportProgressHandlers = [NSMutableDictionary dictionary];
    }
    return self;
}

- (void)dealloc {
    [self invalidate];
}

- (NSXPCConnection *)connection {
    @synchronized (self) {
        if (_connection != nil) {
            return _connection;
        }

        NSXPCConnection *connection = [[NSXPCConnection alloc] initWithServiceName:Z7BrokerServiceName];
        _callbackReceiver = [[BrokerClientCallbackReceiver alloc] initWithClient:self];
        connection.remoteObjectInterface = Z7BrokerCreateBrokerInterface();
        connection.exportedInterface = Z7BrokerCreateClientCallbackInterface();
        connection.exportedObject = _callbackReceiver;

        __weak BrokerClient *weakSelf = self;
        __weak NSXPCConnection *weakConnection = connection;
        connection.interruptionHandler = ^{
            [weakSelf handleConnectionFailure:weakConnection message:@"XPC connection interrupted"];
        };
        connection.invalidationHandler = ^{
            [weakSelf handleConnectionFailure:weakConnection message:@"XPC connection invalidated"];
        };

        [connection resume];
        _connection = connection;
        return connection;
    }
}

- (nullable NSXPCConnection *)currentConnection {
    @synchronized (self) {
        return _connection;
    }
}

- (nullable Z7BrokerMenuPlan *)fetchMenuPlanWithPaths:(NSArray<NSString *> *)paths
                                               locale:(NSString *)locale {
    __block Z7BrokerMenuPlan *plan = nil;
    NSXPCConnection *connection = [self connection];
    id<BrokerXPCProtocol> broker = [connection synchronousRemoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
    }];
    [broker fetchMenuPlanWithPaths:paths ?: @[] locale:locale reply:^(Z7BrokerMenuPlan *replyPlan) {
        plan = replyPlan;
    }];
    return plan;
}

- (nullable Z7BrokerActionResult *)runMenuActionWithActionID:(NSString *)actionID
                                                       paths:(NSArray<NSString *> *)paths
                                                      locale:(NSString *)locale {
    __block Z7BrokerActionResult *result = nil;
    NSXPCConnection *connection = [self connection];
    id<BrokerXPCProtocol> broker = [connection synchronousRemoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
    }];
    [broker runMenuActionWithActionID:actionID ?: @""
                                paths:paths ?: @[]
                               locale:locale
                                reply:^(Z7BrokerActionResult *replyResult) {
        result = replyResult;
    }];
    return result;
}

- (void)listArchivePath:(NSString *)archivePath
             virtualDir:(NSString *)virtualDir
        archiveTypeHint:(NSString *)archiveTypeHint
   nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries
              requestID:(NSString *)requestID
             completion:(BrokerClientListCompletion)completion {
    if (requestID.length == 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
            completion([[Z7BrokerQuickLookListResult alloc] initWithRequestID:@""
                                                                           ok:NO
                                                                       status:Z7BrokerStatusInternalError
                                                                 errorMessage:@"Missing Quick Look list request id"
                                                                  archivePath:archivePath ?: @""
                                                                   virtualDir:virtualDir ?: @""
                                                                        items:@[]]);
        });
        return;
    }
    Z7BrokerQuickLookListRequest *request =
        [[Z7BrokerQuickLookListRequest alloc] initWithRequestID:requestID
                                                    archivePath:archivePath ?: @""
                                                     virtualDir:virtualDir ?: @""
                                                archiveTypeHint:archiveTypeHint ?: @""
                                           nestedArchiveEntries:nestedArchiveEntries ?: @[]];
    @synchronized (self) {
        _listCompletions[requestID] = [completion copy];
    }

    __weak BrokerClient *weakSelf = self;
    NSXPCConnection *connection = [self connection];
    id<BrokerXPCProtocol> broker = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        [weakSelf completeListRequestWithID:requestID
                                     result:[[Z7BrokerQuickLookListResult alloc] initWithRequestID:requestID
                                                                                                ok:NO
                                                                                            status:Z7BrokerStatusInternalError
                                                                                      errorMessage:error.localizedDescription ?: @"XPC list request failed"
                                                                                       archivePath:archivePath ?: @""
                                                                                        virtualDir:virtualDir ?: @""
                                                                                             items:@[]]];
    }];
    [broker listWithRequest:request];
}

- (void)batchExportArchivePath:(NSString *)archivePath
               archiveTypeHint:(NSString *)archiveTypeHint
          nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries
                         items:(NSArray<Z7BrokerQuickLookBatchExportItem *> *)items
                     requestID:(NSString *)requestID
                      progress:(BrokerClientBatchExportProgressHandler)progress
                    completion:(BrokerClientBatchExportCompletion)completion {
    NSInteger totalItemCount = items.count;
    if (requestID.length == 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
            completion([[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:@""
                                                                                  ok:NO
                                                                              status:Z7BrokerStatusInternalError
                                                                        errorMessage:@"Missing Quick Look batch export request id"
                                                                  completedItemCount:0
                                                                      totalItemCount:totalItemCount
                                                                     failedItemIndex:-1
                                                                     failedEntryPath:nil
                                                               failedDestinationPath:nil]);
        });
        return;
    }
    Z7BrokerQuickLookBatchExportRequest *request =
        [[Z7BrokerQuickLookBatchExportRequest alloc] initWithRequestID:requestID
                                                           archivePath:archivePath ?: @""
                                                       archiveTypeHint:archiveTypeHint ?: @""
                                                  nestedArchiveEntries:nestedArchiveEntries ?: @[]
                                                                 items:items ?: @[]];
    @synchronized (self) {
        _batchExportCompletions[requestID] = [completion copy];
        if (progress != nil) {
            _batchExportProgressHandlers[requestID] = [progress copy];
        }
    }

    __weak BrokerClient *weakSelf = self;
    NSXPCConnection *connection = [self connection];
    id<BrokerXPCProtocol> broker = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        [weakSelf completeBatchExportRequestWithID:requestID
                                            result:[[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:requestID
                                                                                                                ok:NO
                                                                                                            status:Z7BrokerStatusInternalError
                                                                                                      errorMessage:error.localizedDescription ?: @"XPC batch export request failed"
                                                                                                completedItemCount:0
                                                                                                    totalItemCount:totalItemCount
                                                                                                   failedItemIndex:-1
                                                                                                   failedEntryPath:nil
                                                                                             failedDestinationPath:nil]];
    }];
    [broker batchExportWithRequest:request];
}

- (void)cancelRequestWithID:(NSString *)requestID {
    if (requestID.length == 0) {
        return;
    }

    @synchronized (self) {
        [_listCompletions removeObjectForKey:requestID];
        [_batchExportCompletions removeObjectForKey:requestID];
        [_batchExportProgressHandlers removeObjectForKey:requestID];
    }

    NSXPCConnection *connection = [self currentConnection];
    if (connection == nil) {
        return;
    }
    id<BrokerXPCProtocol> broker = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
    }];
    [broker cancelRequestWithID:requestID];
}

- (void)providePasswordForPromptID:(NSString *)promptID password:(NSString *)password {
    if (promptID.length == 0) {
        return;
    }
    NSXPCConnection *connection = [self currentConnection];
    if (connection == nil) {
        return;
    }
    id<BrokerXPCProtocol> broker = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
    }];
    [broker providePasswordForPromptID:promptID password:password ?: @""];
}

- (void)cancelPasswordPromptWithID:(NSString *)promptID {
    if (promptID.length == 0) {
        return;
    }
    NSXPCConnection *connection = [self currentConnection];
    if (connection == nil) {
        return;
    }
    id<BrokerXPCProtocol> broker = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
    }];
    [broker cancelPasswordPromptWithID:promptID];
}

- (void)setPasswordPromptHandler:(BrokerClientPasswordPromptHandler)handler {
    @synchronized (self) {
        _passwordPromptHandler = [handler copy];
    }
}

- (void)invalidate {
    NSXPCConnection *connection = nil;
    @synchronized (self) {
        connection = _connection;
        _connection = nil;
        _callbackReceiver = nil;
        [_listCompletions removeAllObjects];
        [_batchExportCompletions removeAllObjects];
        [_batchExportProgressHandlers removeAllObjects];
        _passwordPromptHandler = nil;
    }
    [connection invalidate];
}

- (void)handleListResult:(Z7BrokerQuickLookListResult *)result {
    [self completeListRequestWithID:result.requestID result:result];
}

- (void)handleBatchExportProgress:(Z7BrokerQuickLookBatchExportProgress *)progress {
    BrokerClientBatchExportProgressHandler handler = nil;
    @synchronized (self) {
        handler = [_batchExportProgressHandlers[progress.requestID] copy];
    }
    if (handler == nil) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        handler(progress);
    });
}

- (void)handleBatchExportResult:(Z7BrokerQuickLookBatchExportResult *)result {
    [self completeBatchExportRequestWithID:result.requestID result:result];
}

- (void)handlePasswordPromptEvent:(Z7BrokerPasswordPromptEvent *)event {
    BrokerClientPasswordPromptHandler handler = nil;
    @synchronized (self) {
        handler = [_passwordPromptHandler copy];
    }
    if (handler == nil) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        handler(event);
    });
}

- (void)completeListRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookListResult *)result {
    BrokerClientListCompletion completion = nil;
    @synchronized (self) {
        completion = [_listCompletions[requestID] copy];
        [_listCompletions removeObjectForKey:requestID];
    }
    if (completion == nil) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        completion(result);
    });
}

- (void)completeBatchExportRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookBatchExportResult *)result {
    BrokerClientBatchExportCompletion completion = nil;
    @synchronized (self) {
        completion = [_batchExportCompletions[requestID] copy];
        [_batchExportCompletions removeObjectForKey:requestID];
        [_batchExportProgressHandlers removeObjectForKey:requestID];
    }
    if (completion == nil) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        completion(result);
    });
}

- (void)handleConnectionFailure:(NSXPCConnection *)connection message:(NSString *)message {
    NSDictionary<NSString *, BrokerClientListCompletion> *listCompletions = nil;
    NSDictionary<NSString *, BrokerClientBatchExportCompletion> *batchExportCompletions = nil;

    @synchronized (self) {
        if (connection != _connection) {
            return;
        }
        _connection = nil;
        _callbackReceiver = nil;
        listCompletions = [_listCompletions copy];
        batchExportCompletions = [_batchExportCompletions copy];
        [_listCompletions removeAllObjects];
        [_batchExportCompletions removeAllObjects];
        [_batchExportProgressHandlers removeAllObjects];
    }

    [listCompletions enumerateKeysAndObjectsUsingBlock:^(NSString *requestID,
                                                         BrokerClientListCompletion completion,
                                                         BOOL *stop) {
        (void)stop;
        Z7BrokerQuickLookListResult *result =
            [[Z7BrokerQuickLookListResult alloc] initWithRequestID:requestID
                                                                ok:NO
                                                            status:Z7BrokerStatusInternalError
                                                      errorMessage:message
                                                       archivePath:@""
                                                        virtualDir:@""
                                                             items:@[]];
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(result);
        });
    }];

    [batchExportCompletions enumerateKeysAndObjectsUsingBlock:^(NSString *requestID,
                                                                BrokerClientBatchExportCompletion completion,
                                                                BOOL *stop) {
        (void)stop;
        Z7BrokerQuickLookBatchExportResult *result =
            [[Z7BrokerQuickLookBatchExportResult alloc] initWithRequestID:requestID
                                                                       ok:NO
                                                                   status:Z7BrokerStatusInternalError
                                                             errorMessage:message
                                                       completedItemCount:0
                                                           totalItemCount:0
                                                          failedItemIndex:-1
                                                          failedEntryPath:nil
                                                    failedDestinationPath:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(result);
        });
    }];
}

@end
