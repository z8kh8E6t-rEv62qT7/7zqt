#import <Foundation/Foundation.h>

#import "ServiceDelegate.h"

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    ServiceDelegate *delegate = [[ServiceDelegate alloc] init];
    NSXPCListener *listener = [NSXPCListener serviceListener];
    listener.delegate = delegate;
    [listener resume];
    return 0;
}
