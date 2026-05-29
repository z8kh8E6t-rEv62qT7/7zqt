#import <Foundation/Foundation.h>

#import "BrokerMenuDTO.h"
#import "BrokerPasswordDTO.h"
#import "BrokerQuickLookDTO.h"

NS_ASSUME_NONNULL_BEGIN

@protocol BrokerClientCallbackProtocol

- (void)quickLookListDidFinishWithResult:(Z7BrokerQuickLookListResult *)result;
- (void)quickLookBatchExportDidUpdateProgress:(Z7BrokerQuickLookBatchExportProgress *)progress;
- (void)quickLookBatchExportDidFinishWithResult:(Z7BrokerQuickLookBatchExportResult *)result;
- (void)passwordPromptDidRequestInput:(Z7BrokerPasswordPromptEvent *)event;

@end

@protocol BrokerXPCProtocol

- (void)fetchMenuPlanWithPaths:(NSArray<NSString *> *)paths
                        locale:(nullable NSString *)locale
                         reply:(void (^)(Z7BrokerMenuPlan *plan))reply;

- (void)runMenuActionWithActionID:(NSString *)actionID
                            paths:(NSArray<NSString *> *)paths
                           locale:(nullable NSString *)locale
                            reply:(void (^)(Z7BrokerActionResult *result))reply;

- (void)listWithRequest:(Z7BrokerQuickLookListRequest *)request;
- (void)batchExportWithRequest:(Z7BrokerQuickLookBatchExportRequest *)request;
- (void)cancelRequestWithID:(NSString *)requestID;
- (void)providePasswordForPromptID:(NSString *)promptID password:(NSString *)password;
- (void)cancelPasswordPromptWithID:(NSString *)promptID;

@end

NSXPCInterface *Z7BrokerCreateBrokerInterface(void);
NSXPCInterface *Z7BrokerCreateClientCallbackInterface(void);

NS_ASSUME_NONNULL_END
