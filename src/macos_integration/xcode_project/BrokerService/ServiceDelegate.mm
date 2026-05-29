#import "ServiceDelegate.h"

#import "BrokerService.h"

@implementation ServiceDelegate

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    (void)listener;
    newConnection.exportedInterface = Z7BrokerCreateBrokerInterface();
    newConnection.remoteObjectInterface = Z7BrokerCreateClientCallbackInterface();

    id<BrokerClientCallbackProtocol> callback =
        [newConnection remoteObjectProxyWithErrorHandler:^(NSError *error) {
            (void)error;
        }];
    BrokerService *service = [[BrokerService alloc] initWithCallback:callback];
    __weak BrokerService *weakService = service;
    newConnection.exportedObject = service;
    newConnection.interruptionHandler = ^{
        [weakService invalidate];
    };
    newConnection.invalidationHandler = ^{
        [weakService invalidate];
    };
    [newConnection resume];
    return YES;
}

@end
