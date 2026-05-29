#import <Foundation/Foundation.h>

#import "BrokerXPCProtocol.h"

NS_ASSUME_NONNULL_BEGIN

typedef void (^BrokerClientListCompletion)(Z7BrokerQuickLookListResult *result);
typedef void (^BrokerClientBatchExportProgressHandler)(Z7BrokerQuickLookBatchExportProgress *progress);
typedef void (^BrokerClientBatchExportCompletion)(Z7BrokerQuickLookBatchExportResult *result);
typedef void (^BrokerClientPasswordPromptHandler)(Z7BrokerPasswordPromptEvent *event);

@interface BrokerClient : NSObject

@property (class, nonatomic, readonly, strong) BrokerClient *shared;

- (nullable Z7BrokerMenuPlan *)fetchMenuPlanWithPaths:(NSArray<NSString *> *)paths
                                               locale:(nullable NSString *)locale
    NS_SWIFT_NAME(fetchPlan(paths:locale:));

- (nullable Z7BrokerActionResult *)runMenuActionWithActionID:(NSString *)actionID
                                                       paths:(NSArray<NSString *> *)paths
                                                      locale:(nullable NSString *)locale
    NS_SWIFT_NAME(run(actionID:paths:locale:));

- (void)listArchivePath:(NSString *)archivePath
             virtualDir:(NSString *)virtualDir
        archiveTypeHint:(NSString *)archiveTypeHint
   nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries
              requestID:(NSString *)requestID
             completion:(BrokerClientListCompletion)completion
    NS_SWIFT_NAME(list(archivePath:virtualDir:archiveTypeHint:nestedArchiveEntries:requestID:completion:));

- (void)batchExportArchivePath:(NSString *)archivePath
               archiveTypeHint:(NSString *)archiveTypeHint
          nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries
                         items:(NSArray<Z7BrokerQuickLookBatchExportItem *> *)items
                     requestID:(NSString *)requestID
                      progress:(nullable BrokerClientBatchExportProgressHandler)progress
                    completion:(BrokerClientBatchExportCompletion)completion
    NS_SWIFT_NAME(batchExport(archivePath:archiveTypeHint:nestedArchiveEntries:items:requestID:progress:completion:));

- (void)cancelRequestWithID:(NSString *)requestID NS_SWIFT_NAME(cancelRequest(withID:));
- (void)providePasswordForPromptID:(NSString *)promptID
                           password:(NSString *)password
    NS_SWIFT_NAME(providePassword(promptID:password:));
- (void)cancelPasswordPromptWithID:(NSString *)promptID NS_SWIFT_NAME(cancelPasswordPrompt(promptID:));
- (void)setPasswordPromptHandler:(nullable BrokerClientPasswordPromptHandler)handler
    NS_SWIFT_NAME(setPasswordPromptHandler(_:));
- (void)invalidate;

@end

NS_ASSUME_NONNULL_END
