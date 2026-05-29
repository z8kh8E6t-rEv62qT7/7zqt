#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Z7BrokerMenuAction : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *actionID;
@property (nonatomic, copy, readonly) NSString *title;

- (instancetype)initWithActionID:(NSString *)actionID
                           title:(NSString *)title NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerMenuPlan : NSObject <NSSecureCoding>

@property (nonatomic, assign, readonly) BOOL ok;
@property (nonatomic, assign, readonly) NSInteger status;
@property (nonatomic, copy, readonly, nullable) NSString *errorMessage;
@property (nonatomic, assign, readonly) BOOL menuVisible;
@property (nonatomic, copy, readonly) NSArray<Z7BrokerMenuAction *> *actions;

- (instancetype)initWithOK:(BOOL)ok
                    status:(NSInteger)status
              errorMessage:(nullable NSString *)errorMessage
               menuVisible:(BOOL)menuVisible
                   actions:(NSArray<Z7BrokerMenuAction *> *)actions NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerActionResult : NSObject <NSSecureCoding>

@property (nonatomic, assign, readonly) BOOL ok;
@property (nonatomic, assign, readonly) NSInteger status;
@property (nonatomic, copy, readonly, nullable) NSString *errorMessage;
@property (nonatomic, copy, readonly) NSString *actionID;

- (instancetype)initWithOK:(BOOL)ok
                    status:(NSInteger)status
              errorMessage:(nullable NSString *)errorMessage
                  actionID:(NSString *)actionID NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
