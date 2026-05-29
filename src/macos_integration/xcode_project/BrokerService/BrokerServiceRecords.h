#import <Foundation/Foundation.h>

#include "macos_integration_c_api.h"

@class BrokerService;

@interface BrokerTaskRecord : NSObject
- (instancetype)initWithTask:(z7_mi_task_t *)task;
- (void)cancelAndRelease;
- (void)releaseTask;
@end

@interface BrokerPasswordPromptRecord : NSObject
- (instancetype)initWithPrompt:(z7_mi_password_prompt_t *)prompt;
- (void)providePassword:(NSString *)password;
- (void)cancel;
@end

@interface BrokerRequestContext : NSObject
- (instancetype)initWithService:(BrokerService *)service requestID:(NSString *)requestID;
@property (nonatomic, strong, readonly) BrokerService *service;
@property (nonatomic, copy, readonly) NSString *requestID;
@end

#if defined(Z7_TESTING)
FOUNDATION_EXPORT void Z7BrokerTestingResetTaskRecordCounters(void);
FOUNDATION_EXPORT NSUInteger Z7BrokerTestingTaskCancelAndReleaseCount(void);
#endif
