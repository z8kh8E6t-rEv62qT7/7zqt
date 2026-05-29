#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Z7BrokerQuickLookListRequest : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *requestID;
@property (nonatomic, copy, readonly) NSString *archivePath;
@property (nonatomic, copy, readonly) NSString *virtualDir;
@property (nonatomic, copy, readonly) NSString *archiveTypeHint;
@property (nonatomic, copy, readonly) NSArray<NSString *> *nestedArchiveEntries;

- (instancetype)initWithRequestID:(NSString *)requestID
                      archivePath:(NSString *)archivePath
                       virtualDir:(NSString *)virtualDir
                  archiveTypeHint:(NSString *)archiveTypeHint
             nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerQuickLookItem : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *path;
@property (nonatomic, copy, readonly) NSString *name;
@property (nonatomic, assign, readonly) BOOL directory;
@property (nonatomic, assign, readonly) uint64_t size;
@property (nonatomic, assign, readonly) int64_t mtimeMsUtc;
@property (nonatomic, assign, readonly) BOOL archiveLike;

- (instancetype)initWithPath:(NSString *)path
                        name:(NSString *)name
                   directory:(BOOL)directory
                        size:(uint64_t)size
                 mtimeMsUtc:(int64_t)mtimeMsUtc
                 archiveLike:(BOOL)archiveLike NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerQuickLookListResult : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *requestID;
@property (nonatomic, assign, readonly) BOOL ok;
@property (nonatomic, assign, readonly) NSInteger status;
@property (nonatomic, copy, readonly, nullable) NSString *errorMessage;
@property (nonatomic, copy, readonly) NSString *archivePath;
@property (nonatomic, copy, readonly) NSString *virtualDir;
@property (nonatomic, copy, readonly) NSArray<Z7BrokerQuickLookItem *> *items;

- (instancetype)initWithRequestID:(NSString *)requestID
                               ok:(BOOL)ok
                           status:(NSInteger)status
                     errorMessage:(nullable NSString *)errorMessage
                      archivePath:(NSString *)archivePath
                       virtualDir:(NSString *)virtualDir
                            items:(NSArray<Z7BrokerQuickLookItem *> *)items NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerQuickLookBatchExportItem : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *entryPath;
@property (nonatomic, copy, readonly) NSString *destinationPath;
@property (nonatomic, assign, readonly) uint64_t listedSize;
@property (nonatomic, assign, readonly) BOOL recursive;
@property (nonatomic, assign, readonly) BOOL entryIsDirectory;

- (instancetype)initWithEntryPath:(NSString *)entryPath
                  destinationPath:(NSString *)destinationPath
                       listedSize:(uint64_t)listedSize
                        recursive:(BOOL)recursive
                 entryIsDirectory:(BOOL)entryIsDirectory NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerQuickLookBatchExportRequest : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *requestID;
@property (nonatomic, copy, readonly) NSString *archivePath;
@property (nonatomic, copy, readonly) NSString *archiveTypeHint;
@property (nonatomic, copy, readonly) NSArray<NSString *> *nestedArchiveEntries;
@property (nonatomic, copy, readonly) NSArray<Z7BrokerQuickLookBatchExportItem *> *items;

- (instancetype)initWithRequestID:(NSString *)requestID
                      archivePath:(NSString *)archivePath
                  archiveTypeHint:(NSString *)archiveTypeHint
             nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries
                            items:(NSArray<Z7BrokerQuickLookBatchExportItem *> *)items NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerQuickLookBatchExportProgress : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *requestID;
@property (nonatomic, assign, readonly) NSInteger completedItemCount;
@property (nonatomic, assign, readonly) NSInteger totalItemCount;
@property (nonatomic, assign, readonly) NSInteger currentItemIndex;
@property (nonatomic, copy, readonly) NSString *currentEntryPath;
@property (nonatomic, copy, readonly) NSString *currentDestinationPath;
@property (nonatomic, assign, readonly) NSInteger currentPercent;
@property (nonatomic, assign, readonly) BOOL totalsKnown;
@property (nonatomic, assign, readonly) uint64_t totalBytes;
@property (nonatomic, assign, readonly) uint64_t completedBytes;
@property (nonatomic, copy, readonly, nullable) NSString *currentPath;
@property (nonatomic, copy, readonly, nullable) NSString *message;

- (instancetype)initWithRequestID:(NSString *)requestID
               completedItemCount:(NSInteger)completedItemCount
                   totalItemCount:(NSInteger)totalItemCount
                 currentItemIndex:(NSInteger)currentItemIndex
                 currentEntryPath:(NSString *)currentEntryPath
           currentDestinationPath:(NSString *)currentDestinationPath
                   currentPercent:(NSInteger)currentPercent
                      totalsKnown:(BOOL)totalsKnown
                       totalBytes:(uint64_t)totalBytes
                   completedBytes:(uint64_t)completedBytes
                      currentPath:(nullable NSString *)currentPath
                          message:(nullable NSString *)message NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface Z7BrokerQuickLookBatchExportResult : NSObject <NSSecureCoding>

@property (nonatomic, copy, readonly) NSString *requestID;
@property (nonatomic, assign, readonly) BOOL ok;
@property (nonatomic, assign, readonly) NSInteger status;
@property (nonatomic, copy, readonly, nullable) NSString *errorMessage;
@property (nonatomic, assign, readonly) NSInteger completedItemCount;
@property (nonatomic, assign, readonly) NSInteger totalItemCount;
@property (nonatomic, assign, readonly) NSInteger failedItemIndex;
@property (nonatomic, copy, readonly, nullable) NSString *failedEntryPath;
@property (nonatomic, copy, readonly, nullable) NSString *failedDestinationPath;

- (instancetype)initWithRequestID:(NSString *)requestID
                               ok:(BOOL)ok
                           status:(NSInteger)status
                     errorMessage:(nullable NSString *)errorMessage
               completedItemCount:(NSInteger)completedItemCount
                   totalItemCount:(NSInteger)totalItemCount
                  failedItemIndex:(NSInteger)failedItemIndex
                  failedEntryPath:(nullable NSString *)failedEntryPath
            failedDestinationPath:(nullable NSString *)failedDestinationPath NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
