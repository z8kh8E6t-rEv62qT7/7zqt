#import "BrokerXPCProtocol.h"

static NSSet<Class> *Z7BrokerStringArrayClasses(void) {
    return [NSSet setWithObjects:[NSArray class], [NSString class], nil];
}

static NSSet<Class> *Z7BrokerMenuPlanClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerMenuPlan class],
            [Z7BrokerMenuAction class],
            [NSArray class],
            [NSString class],
            nil];
}

static NSSet<Class> *Z7BrokerActionResultClasses(void) {
    return [NSSet setWithObjects:[Z7BrokerActionResult class], [NSString class], nil];
}

static NSSet<Class> *Z7BrokerListRequestClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerQuickLookListRequest class],
            [NSArray class],
            [NSString class],
            nil];
}

static NSSet<Class> *Z7BrokerListResultClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerQuickLookListResult class],
            [Z7BrokerQuickLookItem class],
            [NSArray class],
            [NSString class],
            [NSNumber class],
            nil];
}

static NSSet<Class> *Z7BrokerBatchExportRequestClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerQuickLookBatchExportRequest class],
            [Z7BrokerQuickLookBatchExportItem class],
            [NSArray class],
            [NSString class],
            nil];
}

static NSSet<Class> *Z7BrokerBatchExportProgressClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerQuickLookBatchExportProgress class],
            [NSString class],
            [NSNumber class],
            nil];
}

static NSSet<Class> *Z7BrokerBatchExportResultClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerQuickLookBatchExportResult class],
            [NSString class],
            [NSNumber class],
            nil];
}

static NSSet<Class> *Z7BrokerPasswordPromptClasses(void) {
    return [NSSet setWithObjects:
            [Z7BrokerPasswordPromptEvent class],
            [NSArray class],
            [NSString class],
            nil];
}

NSXPCInterface *Z7BrokerCreateBrokerInterface(void) {
    NSXPCInterface *interface = [NSXPCInterface interfaceWithProtocol:@protocol(BrokerXPCProtocol)];

    [interface setClasses:Z7BrokerStringArrayClasses()
              forSelector:@selector(fetchMenuPlanWithPaths:locale:reply:)
            argumentIndex:0
                  ofReply:NO];
    [interface setClasses:Z7BrokerMenuPlanClasses()
              forSelector:@selector(fetchMenuPlanWithPaths:locale:reply:)
            argumentIndex:0
                  ofReply:YES];

    [interface setClasses:Z7BrokerStringArrayClasses()
              forSelector:@selector(runMenuActionWithActionID:paths:locale:reply:)
            argumentIndex:1
                  ofReply:NO];
    [interface setClasses:Z7BrokerActionResultClasses()
              forSelector:@selector(runMenuActionWithActionID:paths:locale:reply:)
            argumentIndex:0
                  ofReply:YES];

    [interface setClasses:Z7BrokerListRequestClasses()
              forSelector:@selector(listWithRequest:)
            argumentIndex:0
                  ofReply:NO];
    [interface setClasses:Z7BrokerBatchExportRequestClasses()
              forSelector:@selector(batchExportWithRequest:)
            argumentIndex:0
                  ofReply:NO];

    return interface;
}

NSXPCInterface *Z7BrokerCreateClientCallbackInterface(void) {
    NSXPCInterface *interface = [NSXPCInterface interfaceWithProtocol:@protocol(BrokerClientCallbackProtocol)];

    [interface setClasses:Z7BrokerListResultClasses()
              forSelector:@selector(quickLookListDidFinishWithResult:)
            argumentIndex:0
                  ofReply:NO];
    [interface setClasses:Z7BrokerBatchExportProgressClasses()
              forSelector:@selector(quickLookBatchExportDidUpdateProgress:)
            argumentIndex:0
                  ofReply:NO];
    [interface setClasses:Z7BrokerBatchExportResultClasses()
              forSelector:@selector(quickLookBatchExportDidFinishWithResult:)
            argumentIndex:0
                  ofReply:NO];
    [interface setClasses:Z7BrokerPasswordPromptClasses()
              forSelector:@selector(passwordPromptDidRequestInput:)
            argumentIndex:0
                  ofReply:NO];

    return interface;
}
