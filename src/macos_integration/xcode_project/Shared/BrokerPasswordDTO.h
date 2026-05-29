#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Z7BrokerPasswordPromptEvent : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *promptID;
@property (nonatomic, copy, readonly) NSString *archivePath;
@property (nonatomic, copy, readonly) NSArray<NSString *> *nestedChain;
@property (nonatomic, copy, readonly) NSString *reasonKey;

- (instancetype)initWithPromptID:(NSString *)promptID
                     archivePath:(NSString *)archivePath
                     nestedChain:(NSArray<NSString *> *)nestedChain
                       reasonKey:(NSString *)reasonKey NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
