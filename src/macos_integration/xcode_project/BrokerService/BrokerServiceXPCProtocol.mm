#import "BrokerService.h"

@interface BrokerService (Menu)
- (Z7BrokerMenuPlan *)buildMenuPlanOnQueueWithPaths:(NSArray<NSString *> *)paths;
- (Z7BrokerActionResult *)runMenuActionOnQueueWithActionID:(NSString *)actionID
                                                     paths:(NSArray<NSString *> *)paths;
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
                         reply:(void (^)(Z7BrokerMenuPlan *plan))reply {
    dispatch_async(_queue, ^{
        reply([self buildMenuPlanOnQueueWithPaths:paths ?: @[]]);
    });
}

- (void)runMenuActionWithActionID:(NSString *)actionID
                            paths:(NSArray<NSString *> *)paths
                            reply:(void (^)(Z7BrokerActionResult *result))reply {
    dispatch_async(_queue, ^{
        reply([self runMenuActionOnQueueWithActionID:actionID ?: @"" paths:paths ?: @[]]);
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
