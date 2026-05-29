#import "BrokerService.h"

@interface BrokerService (Menu)
- (Z7BrokerMenuPlan *)buildMenuPlanOnQueueWithPaths:(NSArray<NSString *> *)paths locale:(NSString *)locale;
- (Z7BrokerActionResult *)runMenuActionOnQueueWithActionID:(NSString *)actionID
                                                     paths:(NSArray<NSString *> *)paths
                                                    locale:(NSString *)locale;
@end

@interface BrokerService (QuickLook)
- (void)startListRequestOnQueue:(Z7BrokerQuickLookListRequest *)request;
- (void)startBatchExportRequestOnQueue:(Z7BrokerQuickLookBatchExportRequest *)request;
- (void)cancelRequestOnQueueWithID:(NSString *)requestID;
- (void)providePasswordOnQueueForPromptID:(NSString *)promptID password:(NSString *)password;
- (void)cancelPasswordPromptOnQueueWithID:(NSString *)promptID;
@end

@implementation BrokerService (BrokerXPCProtocol)

- (void)fetchMenuPlanWithPaths:(NSArray<NSString *> *)paths
                        locale:(NSString *)locale
                         reply:(void (^)(Z7BrokerMenuPlan *plan))reply {
    dispatch_async(_queue, ^{
        reply([self buildMenuPlanOnQueueWithPaths:paths ?: @[] locale:locale]);
    });
}

- (void)runMenuActionWithActionID:(NSString *)actionID
                            paths:(NSArray<NSString *> *)paths
                           locale:(NSString *)locale
                            reply:(void (^)(Z7BrokerActionResult *result))reply {
    dispatch_async(_queue, ^{
        reply([self runMenuActionOnQueueWithActionID:actionID ?: @"" paths:paths ?: @[] locale:locale]);
    });
}

- (void)listWithRequest:(Z7BrokerQuickLookListRequest *)request {
    dispatch_async(_queue, ^{
        [self startListRequestOnQueue:request];
    });
}

- (void)batchExportWithRequest:(Z7BrokerQuickLookBatchExportRequest *)request {
    dispatch_async(_queue, ^{
        [self startBatchExportRequestOnQueue:request];
    });
}

- (void)cancelRequestWithID:(NSString *)requestID {
    dispatch_async(_queue, ^{
        [self cancelRequestOnQueueWithID:requestID];
    });
}

- (void)providePasswordForPromptID:(NSString *)promptID password:(NSString *)password {
    dispatch_async(_queue, ^{
        [self providePasswordOnQueueForPromptID:promptID password:password];
    });
}

- (void)cancelPasswordPromptWithID:(NSString *)promptID {
    dispatch_async(_queue, ^{
        [self cancelPasswordPromptOnQueueWithID:promptID];
    });
}

@end
