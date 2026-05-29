#import "BrokerServiceRecords.h"

#if defined(Z7_TESTING)
#include <atomic>

static std::atomic<NSUInteger> Z7BrokerTaskCancelAndReleaseCount{0};

void Z7BrokerTestingResetTaskRecordCounters(void) {
    Z7BrokerTaskCancelAndReleaseCount.store(0);
}

NSUInteger Z7BrokerTestingTaskCancelAndReleaseCount(void) {
    return Z7BrokerTaskCancelAndReleaseCount.load();
}
#endif

@implementation BrokerTaskRecord {
    z7_mi_task_t *_task;
}

- (instancetype)initWithTask:(z7_mi_task_t *)task {
    self = [super init];
    if (self) {
        _task = task;
    }
    return self;
}

- (void)cancelAndRelease {
    if (_task == nullptr) {
        return;
    }
#if defined(Z7_TESTING)
    Z7BrokerTaskCancelAndReleaseCount.fetch_add(1);
#endif
    z7_mi_task_cancel(_task);
    z7_mi_task_release(_task);
    _task = nullptr;
}

- (void)releaseTask {
    if (_task == nullptr) {
        return;
    }
    z7_mi_task_release(_task);
    _task = nullptr;
}

- (void)dealloc {
    [self releaseTask];
}

@end

@implementation BrokerPasswordPromptRecord {
    z7_mi_password_prompt_t *_prompt;
}

- (instancetype)initWithPrompt:(z7_mi_password_prompt_t *)prompt {
    self = [super init];
    if (self) {
        _prompt = prompt;
    }
    return self;
}

- (void)providePassword:(NSString *)password {
    if (_prompt == nullptr) {
        return;
    }
    z7_mi_password_prompt_provide(_prompt, [password ?: @"" UTF8String]);
    _prompt = nullptr;
}

- (void)cancel {
    if (_prompt == nullptr) {
        return;
    }
    z7_mi_password_prompt_cancel(_prompt);
    _prompt = nullptr;
}

- (void)dealloc {
    [self cancel];
}

@end

@implementation BrokerRequestContext

- (instancetype)initWithService:(BrokerService *)service requestID:(NSString *)requestID {
    self = [super init];
    if (self) {
        _service = service;
        _requestID = [requestID copy];
    }
    return self;
}

@end
