#import <Foundation/Foundation.h>

#import "../Shared/BrokerXPCProtocol.h"
#import "BrokerServiceRecords.h"

@interface BrokerService : NSObject {
@private
    id<BrokerClientCallbackProtocol> _callback;
    dispatch_queue_t _queue;
    z7_mi_session_t *_session;
    NSMutableDictionary<NSString *, BrokerTaskRecord *> *_tasks;
    NSMutableDictionary<NSString *, BrokerPasswordPromptRecord *> *_prompts;
    BOOL _invalidated;
}
- (instancetype)initWithCallback:(id<BrokerClientCallbackProtocol>)callback;
- (void)invalidate;
- (void)finishListRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookListResult *)result;
- (void)sendBatchExportProgressOnQueue:(Z7BrokerQuickLookBatchExportProgress *)progress;
- (void)finishBatchExportRequestWithID:(NSString *)requestID result:(Z7BrokerQuickLookBatchExportResult *)result;
- (void)handlePasswordPrompt:(z7_mi_password_prompt_t *)prompt
                 archivePath:(NSString *)archivePath
                 nestedChain:(NSArray<NSString *> *)nestedChain
                   reasonKey:(NSString *)reasonKey;
- (void)sendListResultOnQueue:(Z7BrokerQuickLookListResult *)result;
- (void)sendBatchExportResultOnQueue:(Z7BrokerQuickLookBatchExportResult *)result;
@end

@interface BrokerService (BrokerXPCProtocol) <BrokerXPCProtocol>
@end
