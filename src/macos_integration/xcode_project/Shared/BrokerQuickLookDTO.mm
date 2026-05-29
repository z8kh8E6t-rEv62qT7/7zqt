#import "BrokerQuickLookDTO.h"

static NSSet<Class> *Z7BrokerStringArrayClasses(void) {
    return [NSSet setWithObjects:[NSArray class], [NSString class], nil];
}

@implementation Z7BrokerQuickLookListRequest

+ (BOOL)supportsSecureCoding { return YES; }

- (instancetype)initWithRequestID:(NSString *)requestID
                      archivePath:(NSString *)archivePath
                       virtualDir:(NSString *)virtualDir
                  archiveTypeHint:(NSString *)archiveTypeHint
             nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries {
    self = [super init];
    if (self) {
        _requestID = [requestID copy];
        _archivePath = [archivePath copy];
        _virtualDir = [virtualDir copy];
        _archiveTypeHint = [archiveTypeHint copy];
        _nestedArchiveEntries = [nestedArchiveEntries copy] ?: @[];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    return [self initWithRequestID:[coder decodeObjectOfClass:[NSString class] forKey:@"requestID"] ?: @""
                       archivePath:[coder decodeObjectOfClass:[NSString class] forKey:@"archivePath"] ?: @""
                        virtualDir:[coder decodeObjectOfClass:[NSString class] forKey:@"virtualDir"] ?: @""
                   archiveTypeHint:[coder decodeObjectOfClass:[NSString class] forKey:@"archiveTypeHint"] ?: @""
              nestedArchiveEntries:[coder decodeObjectOfClasses:Z7BrokerStringArrayClasses()
                                                         forKey:@"nestedArchiveEntries"] ?: @[]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.requestID forKey:@"requestID"];
    [coder encodeObject:self.archivePath forKey:@"archivePath"];
    [coder encodeObject:self.virtualDir forKey:@"virtualDir"];
    [coder encodeObject:self.archiveTypeHint forKey:@"archiveTypeHint"];
    [coder encodeObject:self.nestedArchiveEntries forKey:@"nestedArchiveEntries"];
}

@end

@implementation Z7BrokerQuickLookItem

+ (BOOL)supportsSecureCoding { return YES; }

- (instancetype)initWithPath:(NSString *)path
                        name:(NSString *)name
                   directory:(BOOL)directory
                        size:(uint64_t)size
                  mtimeMsUtc:(int64_t)mtimeMsUtc
                 archiveLike:(BOOL)archiveLike {
    self = [super init];
    if (self) {
        _path = [path copy];
        _name = [name copy];
        _directory = directory;
        _size = size;
        _mtimeMsUtc = mtimeMsUtc;
        _archiveLike = archiveLike;
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSNumber *size = [coder decodeObjectOfClass:[NSNumber class] forKey:@"size"];
    return [self initWithPath:[coder decodeObjectOfClass:[NSString class] forKey:@"path"] ?: @""
                         name:[coder decodeObjectOfClass:[NSString class] forKey:@"name"] ?: @""
                    directory:[coder decodeBoolForKey:@"directory"]
                         size:size == nil ? 0 : [size unsignedLongLongValue]
                   mtimeMsUtc:[coder decodeInt64ForKey:@"mtimeMsUtc"]
                  archiveLike:[coder decodeBoolForKey:@"archiveLike"]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.path forKey:@"path"];
    [coder encodeObject:self.name forKey:@"name"];
    [coder encodeBool:self.directory forKey:@"directory"];
    [coder encodeObject:@(self.size) forKey:@"size"];
    [coder encodeInt64:self.mtimeMsUtc forKey:@"mtimeMsUtc"];
    [coder encodeBool:self.archiveLike forKey:@"archiveLike"];
}

@end

@implementation Z7BrokerQuickLookListResult

+ (BOOL)supportsSecureCoding { return YES; }

- (instancetype)initWithRequestID:(NSString *)requestID
                               ok:(BOOL)ok
                           status:(NSInteger)status
                     errorMessage:(NSString *)errorMessage
                      archivePath:(NSString *)archivePath
                       virtualDir:(NSString *)virtualDir
                            items:(NSArray<Z7BrokerQuickLookItem *> *)items {
    self = [super init];
    if (self) {
        _requestID = [requestID copy];
        _ok = ok;
        _status = status;
        _errorMessage = [errorMessage copy];
        _archivePath = [archivePath copy];
        _virtualDir = [virtualDir copy];
        _items = [items copy] ?: @[];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSSet<Class> *itemClasses = [NSSet setWithObjects:[NSArray class], [Z7BrokerQuickLookItem class], nil];
    return [self initWithRequestID:[coder decodeObjectOfClass:[NSString class] forKey:@"requestID"] ?: @""
                                ok:[coder decodeBoolForKey:@"ok"]
                            status:[coder decodeIntegerForKey:@"status"]
                      errorMessage:[coder decodeObjectOfClass:[NSString class] forKey:@"errorMessage"]
                       archivePath:[coder decodeObjectOfClass:[NSString class] forKey:@"archivePath"] ?: @""
                        virtualDir:[coder decodeObjectOfClass:[NSString class] forKey:@"virtualDir"] ?: @""
                             items:[coder decodeObjectOfClasses:itemClasses forKey:@"items"] ?: @[]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.requestID forKey:@"requestID"];
    [coder encodeBool:self.ok forKey:@"ok"];
    [coder encodeInteger:self.status forKey:@"status"];
    [coder encodeObject:self.errorMessage forKey:@"errorMessage"];
    [coder encodeObject:self.archivePath forKey:@"archivePath"];
    [coder encodeObject:self.virtualDir forKey:@"virtualDir"];
    [coder encodeObject:self.items forKey:@"items"];
}

@end

@implementation Z7BrokerQuickLookBatchExportItem

+ (BOOL)supportsSecureCoding { return YES; }

- (instancetype)initWithEntryPath:(NSString *)entryPath
                  destinationPath:(NSString *)destinationPath
                       listedSize:(uint64_t)listedSize
                        recursive:(BOOL)recursive
                 entryIsDirectory:(BOOL)entryIsDirectory {
    self = [super init];
    if (self) {
        _entryPath = [entryPath copy];
        _destinationPath = [destinationPath copy];
        _listedSize = listedSize;
        _recursive = recursive;
        _entryIsDirectory = entryIsDirectory;
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSNumber *listedSize = [coder decodeObjectOfClass:[NSNumber class] forKey:@"listedSize"];
    return [self initWithEntryPath:[coder decodeObjectOfClass:[NSString class] forKey:@"entryPath"] ?: @""
                   destinationPath:[coder decodeObjectOfClass:[NSString class] forKey:@"destinationPath"] ?: @""
                       listedSize:listedSize == nil ? 0 : [listedSize unsignedLongLongValue]
                         recursive:[coder decodeBoolForKey:@"recursive"]
                  entryIsDirectory:[coder decodeBoolForKey:@"entryIsDirectory"]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.entryPath forKey:@"entryPath"];
    [coder encodeObject:self.destinationPath forKey:@"destinationPath"];
    [coder encodeObject:@(self.listedSize) forKey:@"listedSize"];
    [coder encodeBool:self.recursive forKey:@"recursive"];
    [coder encodeBool:self.entryIsDirectory forKey:@"entryIsDirectory"];
}

@end

@implementation Z7BrokerQuickLookBatchExportRequest

+ (BOOL)supportsSecureCoding { return YES; }

- (instancetype)initWithRequestID:(NSString *)requestID
                      archivePath:(NSString *)archivePath
                  archiveTypeHint:(NSString *)archiveTypeHint
             nestedArchiveEntries:(NSArray<NSString *> *)nestedArchiveEntries
                            items:(NSArray<Z7BrokerQuickLookBatchExportItem *> *)items {
    self = [super init];
    if (self) {
        _requestID = [requestID copy];
        _archivePath = [archivePath copy];
        _archiveTypeHint = [archiveTypeHint copy];
        _nestedArchiveEntries = [nestedArchiveEntries copy] ?: @[];
        _items = [items copy] ?: @[];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSSet<Class> *itemClasses =
        [NSSet setWithObjects:[NSArray class], [Z7BrokerQuickLookBatchExportItem class], nil];
    return [self initWithRequestID:[coder decodeObjectOfClass:[NSString class] forKey:@"requestID"] ?: @""
                       archivePath:[coder decodeObjectOfClass:[NSString class] forKey:@"archivePath"] ?: @""
                   archiveTypeHint:[coder decodeObjectOfClass:[NSString class] forKey:@"archiveTypeHint"] ?: @""
              nestedArchiveEntries:[coder decodeObjectOfClasses:Z7BrokerStringArrayClasses()
                                                         forKey:@"nestedArchiveEntries"] ?: @[]
                             items:[coder decodeObjectOfClasses:itemClasses forKey:@"items"] ?: @[]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.requestID forKey:@"requestID"];
    [coder encodeObject:self.archivePath forKey:@"archivePath"];
    [coder encodeObject:self.archiveTypeHint forKey:@"archiveTypeHint"];
    [coder encodeObject:self.nestedArchiveEntries forKey:@"nestedArchiveEntries"];
    [coder encodeObject:self.items forKey:@"items"];
}

@end

@implementation Z7BrokerQuickLookBatchExportProgress

+ (BOOL)supportsSecureCoding { return YES; }

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
                      currentPath:(NSString *)currentPath
                          message:(NSString *)message {
    self = [super init];
    if (self) {
        _requestID = [requestID copy];
        _completedItemCount = completedItemCount;
        _totalItemCount = totalItemCount;
        _currentItemIndex = currentItemIndex;
        _currentEntryPath = [currentEntryPath copy];
        _currentDestinationPath = [currentDestinationPath copy];
        _currentPercent = currentPercent;
        _totalsKnown = totalsKnown;
        _totalBytes = totalBytes;
        _completedBytes = completedBytes;
        _currentPath = [currentPath copy];
        _message = [message copy];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    NSNumber *totalBytes = [coder decodeObjectOfClass:[NSNumber class] forKey:@"totalBytes"];
    NSNumber *completedBytes = [coder decodeObjectOfClass:[NSNumber class] forKey:@"completedBytes"];
    return [self initWithRequestID:[coder decodeObjectOfClass:[NSString class] forKey:@"requestID"] ?: @""
                completedItemCount:[coder decodeIntegerForKey:@"completedItemCount"]
                    totalItemCount:[coder decodeIntegerForKey:@"totalItemCount"]
                  currentItemIndex:[coder decodeIntegerForKey:@"currentItemIndex"]
                  currentEntryPath:[coder decodeObjectOfClass:[NSString class] forKey:@"currentEntryPath"] ?: @""
            currentDestinationPath:[coder decodeObjectOfClass:[NSString class] forKey:@"currentDestinationPath"] ?: @""
                    currentPercent:[coder decodeIntegerForKey:@"currentPercent"]
                       totalsKnown:[coder decodeBoolForKey:@"totalsKnown"]
                        totalBytes:totalBytes == nil ? 0 : [totalBytes unsignedLongLongValue]
                    completedBytes:completedBytes == nil ? 0 : [completedBytes unsignedLongLongValue]
                       currentPath:[coder decodeObjectOfClass:[NSString class] forKey:@"currentPath"]
                           message:[coder decodeObjectOfClass:[NSString class] forKey:@"message"]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.requestID forKey:@"requestID"];
    [coder encodeInteger:self.completedItemCount forKey:@"completedItemCount"];
    [coder encodeInteger:self.totalItemCount forKey:@"totalItemCount"];
    [coder encodeInteger:self.currentItemIndex forKey:@"currentItemIndex"];
    [coder encodeObject:self.currentEntryPath forKey:@"currentEntryPath"];
    [coder encodeObject:self.currentDestinationPath forKey:@"currentDestinationPath"];
    [coder encodeInteger:self.currentPercent forKey:@"currentPercent"];
    [coder encodeBool:self.totalsKnown forKey:@"totalsKnown"];
    [coder encodeObject:@(self.totalBytes) forKey:@"totalBytes"];
    [coder encodeObject:@(self.completedBytes) forKey:@"completedBytes"];
    [coder encodeObject:self.currentPath forKey:@"currentPath"];
    [coder encodeObject:self.message forKey:@"message"];
}

@end

@implementation Z7BrokerQuickLookBatchExportResult

+ (BOOL)supportsSecureCoding { return YES; }

- (instancetype)initWithRequestID:(NSString *)requestID
                               ok:(BOOL)ok
                           status:(NSInteger)status
                     errorMessage:(NSString *)errorMessage
               completedItemCount:(NSInteger)completedItemCount
                   totalItemCount:(NSInteger)totalItemCount
                  failedItemIndex:(NSInteger)failedItemIndex
                  failedEntryPath:(NSString *)failedEntryPath
            failedDestinationPath:(NSString *)failedDestinationPath {
    self = [super init];
    if (self) {
        _requestID = [requestID copy];
        _ok = ok;
        _status = status;
        _errorMessage = [errorMessage copy];
        _completedItemCount = completedItemCount;
        _totalItemCount = totalItemCount;
        _failedItemIndex = failedItemIndex;
        _failedEntryPath = [failedEntryPath copy];
        _failedDestinationPath = [failedDestinationPath copy];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    return [self initWithRequestID:[coder decodeObjectOfClass:[NSString class] forKey:@"requestID"] ?: @""
                                ok:[coder decodeBoolForKey:@"ok"]
                            status:[coder decodeIntegerForKey:@"status"]
                      errorMessage:[coder decodeObjectOfClass:[NSString class] forKey:@"errorMessage"]
                completedItemCount:[coder decodeIntegerForKey:@"completedItemCount"]
                    totalItemCount:[coder decodeIntegerForKey:@"totalItemCount"]
                   failedItemIndex:[coder decodeIntegerForKey:@"failedItemIndex"]
                   failedEntryPath:[coder decodeObjectOfClass:[NSString class] forKey:@"failedEntryPath"]
             failedDestinationPath:[coder decodeObjectOfClass:[NSString class] forKey:@"failedDestinationPath"]];
}

- (void)encodeWithCoder:(NSCoder *)coder {
    [coder encodeObject:self.requestID forKey:@"requestID"];
    [coder encodeBool:self.ok forKey:@"ok"];
    [coder encodeInteger:self.status forKey:@"status"];
    [coder encodeObject:self.errorMessage forKey:@"errorMessage"];
    [coder encodeInteger:self.completedItemCount forKey:@"completedItemCount"];
    [coder encodeInteger:self.totalItemCount forKey:@"totalItemCount"];
    [coder encodeInteger:self.failedItemIndex forKey:@"failedItemIndex"];
    [coder encodeObject:self.failedEntryPath forKey:@"failedEntryPath"];
    [coder encodeObject:self.failedDestinationPath forKey:@"failedDestinationPath"];
}

@end
