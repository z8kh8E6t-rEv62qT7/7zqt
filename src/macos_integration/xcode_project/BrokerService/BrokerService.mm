#import "BrokerService.h"

#import "BrokerServiceUtilities.h"

static void *Z7BrokerServiceQueueKey = &Z7BrokerServiceQueueKey;

static void Z7PasswordPromptCallback(z7_mi_password_prompt_t *handle,
                                     const char *archivePath,
                                     const char *const *nestedChain,
                                     size_t nestedChainCount,
                                     const char *reasonKey,
                                     void *userData) {
    if (handle == nullptr || userData == nullptr) {
        return;
    }

    BrokerService *service = (__bridge BrokerService *)userData;
    NSMutableArray<NSString *> *chain = [NSMutableArray arrayWithCapacity:nestedChainCount];
    if (nestedChain != nullptr) {
        for (size_t index = 0; index < nestedChainCount; ++index) {
            if (nestedChain[index] != nullptr) {
                [chain addObject:Z7ToNSString(nestedChain[index])];
            }
        }
    }
    [service handlePasswordPrompt:handle
                       archivePath:Z7ToNSString(archivePath)
                       nestedChain:chain
                         reasonKey:reasonKey == nullptr ? @"password_required" : Z7ToNSString(reasonKey)];
}

@implementation BrokerService

- (instancetype)initWithCallback:(id<BrokerClientCallbackProtocol>)callback {
    self = [super init];
    if (self) {
        _callback = callback;
        _queue = dispatch_queue_create("app.sevenzip.broker.service", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(_queue, Z7BrokerServiceQueueKey, Z7BrokerServiceQueueKey, nullptr);
        _session = z7_mi_session_create();
        _tasks = [NSMutableDictionary dictionary];
        _prompts = [NSMutableDictionary dictionary];
        if (_session != nullptr) {
            z7_mi_session_set_password_prompt_callback(_session, Z7PasswordPromptCallback, (__bridge void *)self);
        }
    }
    return self;
}

- (void)dealloc {
    [self invalidate];
    if (_session != nullptr) {
        z7_mi_session_destroy(_session);
        _session = nullptr;
    }
}

- (void)invalidate {
    void (^cleanup)(void) = ^{
        if (self->_invalidated) {
            return;
        }
        self->_invalidated = YES;
        self->_callback = nil;
        if (self->_session != nullptr) {
            z7_mi_session_set_password_prompt_callback(self->_session, nullptr, nullptr);
        }
        for (BrokerPasswordPromptRecord *prompt in self->_prompts.allValues) {
            [prompt cancel];
        }
        [self->_prompts removeAllObjects];
        for (BrokerTaskRecord *task in self->_tasks.allValues) {
            [task cancelAndRelease];
        }
        [self->_tasks removeAllObjects];
    };

    if (dispatch_get_specific(Z7BrokerServiceQueueKey) == Z7BrokerServiceQueueKey) {
        cleanup();
        return;
    }
    dispatch_sync(_queue, cleanup);
}

- (void)finishListRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookListResult *)result {
    dispatch_async(_queue, ^{
        BrokerTaskRecord *task = self->_tasks[requestID];
        if (task == nil) {
            return;
        }
        [self->_tasks removeObjectForKey:requestID];
        [task releaseTask];
        [self sendListResultOnQueue:result];
    });
}

- (void)sendBatchExportProgressOnQueue:(Z7BrokerQuickLookBatchExportProgress *)progress {
    if (_invalidated || _callback == nil) {
        return;
    }
    [_callback quickLookBatchExportDidUpdateProgress:progress];
}

- (void)finishBatchExportRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookBatchExportResult *)result {
    dispatch_async(_queue, ^{
        BrokerTaskRecord *task = self->_tasks[requestID];
        if (task == nil) {
            return;
        }
        [self->_tasks removeObjectForKey:requestID];
        [task releaseTask];
        [self sendBatchExportResultOnQueue:result];
    });
}

- (void)handlePasswordPrompt:(z7_mi_password_prompt_t *)prompt
                 archivePath:(NSString *)archivePath
                 nestedChain:(NSArray<NSString *> *)nestedChain
                   reasonKey:(NSString *)reasonKey {
    dispatch_async(_queue, ^{
        if (self->_invalidated || self->_callback == nil) {
            z7_mi_password_prompt_cancel(prompt);
            return;
        }
        NSString *promptID = [NSUUID UUID].UUIDString;
        self->_prompts[promptID] = [[BrokerPasswordPromptRecord alloc] initWithPrompt:prompt];
        Z7BrokerPasswordPromptEvent *event =
            [[Z7BrokerPasswordPromptEvent alloc] initWithPromptID:promptID
                                                      archivePath:archivePath ?: @""
                                                      nestedChain:nestedChain ?: @[]
                                                        reasonKey:reasonKey ?: @"password_required"];
        [self->_callback passwordPromptDidRequestInput:event];
    });
}

- (void)sendListResultOnQueue:(Z7BrokerQuickLookListResult *)result {
    if (_invalidated || _callback == nil) {
        return;
    }
    [_callback quickLookListDidFinishWithResult:result];
}

- (void)sendBatchExportResultOnQueue:(Z7BrokerQuickLookBatchExportResult *)result {
    if (_invalidated || _callback == nil) {
        return;
    }
    [_callback quickLookBatchExportDidFinishWithResult:result];
}

@end
