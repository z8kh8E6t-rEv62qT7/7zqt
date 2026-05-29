#import "BrokerMenuDTO.h"

@implementation Z7BrokerMenuAction

+ (BOOL)supportsSecureCoding {
    return YES;
}

- (instancetype)initWithActionID:(NSString *)actionID title:(NSString *)title {
    self = [super init];
    if (self) {
        _actionID = [actionID copy];
        _title = [title copy];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSString *actionID = [coder decodeObjectOfClass:[NSString class] forKey:@"actionID"] ?: @"";
    NSString *title = [coder decodeObjectOfClass:[NSString class] forKey:@"title"] ?: @"";
    return [self initWithActionID:actionID title:title];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.actionID forKey:@"actionID"];
    [coder encodeObject:self.title forKey:@"title"];
}

@end

@implementation Z7BrokerMenuPlan

+ (BOOL)supportsSecureCoding {
    return YES;
}

- (instancetype)initWithOK:(BOOL)ok
                    status:(NSInteger)status
              errorMessage:(NSString *)errorMessage
               menuVisible:(BOOL)menuVisible
                   actions:(NSArray<Z7BrokerMenuAction *> *)actions {
    self = [super init];
    if (self) {
        _ok = ok;
        _status = status;
        _errorMessage = [errorMessage copy];
        _menuVisible = menuVisible;
        _actions = [actions copy] ?: @[];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSSet<Class> *actionClasses = [NSSet setWithObjects:[NSArray class], [Z7BrokerMenuAction class], nil];
    NSArray<Z7BrokerMenuAction *> *actions = [coder decodeObjectOfClasses:actionClasses forKey:@"actions"] ?: @[];
    return [self initWithOK:[coder decodeBoolForKey:@"ok"]
                     status:[coder decodeIntegerForKey:@"status"]
               errorMessage:[coder decodeObjectOfClass:[NSString class] forKey:@"errorMessage"]
                menuVisible:[coder decodeBoolForKey:@"menuVisible"]
                    actions:actions];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeBool:self.ok forKey:@"ok"];
    [coder encodeInteger:self.status forKey:@"status"];
    [coder encodeObject:self.errorMessage forKey:@"errorMessage"];
    [coder encodeBool:self.menuVisible forKey:@"menuVisible"];
    [coder encodeObject:self.actions forKey:@"actions"];
}

@end

@implementation Z7BrokerActionResult

+ (BOOL)supportsSecureCoding {
    return YES;
}

- (instancetype)initWithOK:(BOOL)ok
                    status:(NSInteger)status
              errorMessage:(NSString *)errorMessage
                  actionID:(NSString *)actionID {
    self = [super init];
    if (self) {
        _ok = ok;
        _status = status;
        _errorMessage = [errorMessage copy];
        _actionID = [actionID copy];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSString *actionID = [coder decodeObjectOfClass:[NSString class] forKey:@"actionID"] ?: @"";
    return [self initWithOK:[coder decodeBoolForKey:@"ok"]
                     status:[coder decodeIntegerForKey:@"status"]
               errorMessage:[coder decodeObjectOfClass:[NSString class] forKey:@"errorMessage"]
                   actionID:actionID];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeBool:self.ok forKey:@"ok"];
    [coder encodeInteger:self.status forKey:@"status"];
    [coder encodeObject:self.errorMessage forKey:@"errorMessage"];
    [coder encodeObject:self.actionID forKey:@"actionID"];
}

@end
