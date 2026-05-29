#import "BrokerPasswordDTO.h"

@implementation Z7BrokerPasswordPromptEvent

+ (BOOL)supportsSecureCoding {
    return YES;
}

- (instancetype)initWithPromptID:(NSString *)promptID
                     archivePath:(NSString *)archivePath
                     nestedChain:(NSArray<NSString *> *)nestedChain
                       reasonKey:(NSString *)reasonKey {
    self = [super init];
    if (self) {
        _promptID = [promptID copy];
        _archivePath = [archivePath copy];
        _nestedChain = [nestedChain copy] ?: @[];
        _reasonKey = [reasonKey copy];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSSet<Class> *chainClasses = [NSSet setWithObjects:[NSArray class], [NSString class], nil];
    return [self initWithPromptID:[coder decodeObjectOfClass:[NSString class] forKey:@"promptID"] ?: @""
                      archivePath:[coder decodeObjectOfClass:[NSString class] forKey:@"archivePath"] ?: @""
                      nestedChain:[coder decodeObjectOfClasses:chainClasses forKey:@"nestedChain"] ?: @[]
                        reasonKey:[coder decodeObjectOfClass:[NSString class] forKey:@"reasonKey"] ?: @"password_required"];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.promptID forKey:@"promptID"];
    [coder encodeObject:self.archivePath forKey:@"archivePath"];
    [coder encodeObject:self.nestedChain forKey:@"nestedChain"];
    [coder encodeObject:self.reasonKey forKey:@"reasonKey"];
}

@end
