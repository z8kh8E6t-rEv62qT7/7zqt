#import <Foundation/Foundation.h>

#import "macos_integration_c_api.h"

typedef struct {
  z7_mi_status_t status;
  z7_mi_quicklook_list_result_t *result;
} QuickLookListCallResult;

typedef struct {
  z7_mi_status_t status;
  z7_mi_quicklook_batch_export_result_t *result;
} QuickLookBatchExportCallResult;

typedef struct {
  dispatch_semaphore_t semaphore;
  QuickLookListCallResult result;
} QuickLookListCallbackContext;

typedef struct {
  dispatch_semaphore_t semaphore;
  QuickLookBatchExportCallResult result;
} QuickLookBatchExportCallbackContext;

@interface PasswordPromptScript : NSObject
@property(nonatomic, copy) NSArray<id> *responses;
@property(nonatomic, strong) NSMutableArray<NSString *> *reasonKeys;
@property(nonatomic, strong) NSMutableArray<NSArray<NSString *> *> *nestedChains;
@property(nonatomic, assign) NSInteger callCount;
@end

@implementation PasswordPromptScript

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _responses = @[];
    _reasonKeys = [NSMutableArray array];
    _nestedChains = [NSMutableArray array];
    _callCount = 0;
  }
  return self;
}

@end

static void fail_now(NSString *message) {
  fprintf(stderr, "FAIL: %s\n", message.UTF8String);
  exit(1);
}

static void expect_true(BOOL condition, NSString *message) {
  if (!condition) {
    fail_now(message);
  }
}

static NSString *sevenzz_path(void) {
  NSString *path = @"/opt/homebrew/bin/7zz";
  expect_true([[NSFileManager defaultManager] isExecutableFileAtPath:path],
              @"missing /opt/homebrew/bin/7zz");
  return path;
}

static NSString *temporary_root(NSString *prefix) {
  NSString *templateValue =
      [NSTemporaryDirectory() stringByAppendingPathComponent:
                                   [NSString stringWithFormat:@"%@.XXXXXX",
                                                              prefix]];
  NSMutableData *buffer =
      [NSMutableData dataWithBytes:templateValue.fileSystemRepresentation
                            length:strlen(templateValue.fileSystemRepresentation) +
                                   1];
  char *value = (char *)buffer.mutableBytes;
  expect_true(mkdtemp(value) != NULL,
              [NSString stringWithFormat:@"mkdtemp failed for %@", prefix]);
  return [[NSFileManager defaultManager]
             stringWithFileSystemRepresentation:value
                                         length:strlen(value)];
}

static void write_text_file(NSString *path, NSString *text) {
  NSError *error = nil;
  expect_true([text dataUsingEncoding:NSUTF8StringEncoding] != nil,
              @"failed to encode test text");
  BOOL ok = [[text dataUsingEncoding:NSUTF8StringEncoding]
      writeToURL:[NSURL fileURLWithPath:path]
          options:NSDataWritingAtomic
            error:&error];
  expect_true(ok,
              [NSString stringWithFormat:@"failed to write %@: %@",
                                         path,
                                         error.localizedDescription]);
}

static void ensure_directory(NSString *path) {
  NSError *error = nil;
  BOOL ok = [[NSFileManager defaultManager]
      createDirectoryAtPath:path
withIntermediateDirectories:YES
                  attributes:nil
                       error:&error];
  expect_true(ok,
              [NSString stringWithFormat:@"failed to create directory %@: %@",
                                         path,
                                         error.localizedDescription]);
}

static void run_task_or_fail(NSString *launchPath,
                             NSArray<NSString *> *arguments,
                             NSString *workingDirectory) {
  NSTask *task = [[NSTask alloc] init];
  task.launchPath = launchPath;
  task.arguments = arguments;
  task.currentDirectoryPath = workingDirectory;

  NSPipe *stderrPipe = [NSPipe pipe];
  task.standardError = stderrPipe;
  NSPipe *stdoutPipe = [NSPipe pipe];
  task.standardOutput = stdoutPipe;

  @try {
    [task launch];
  } @catch (NSException *exception) {
    fail_now([NSString stringWithFormat:@"failed to launch %@: %@",
                                         launchPath,
                                         exception.reason]);
  }

  [task waitUntilExit];
  if (task.terminationStatus != 0) {
    NSData *stderrData = [[stderrPipe fileHandleForReading] readDataToEndOfFile];
    NSString *stderrText =
        [[NSString alloc] initWithData:stderrData
                               encoding:NSUTF8StringEncoding] ?: @"";
    fail_now([NSString stringWithFormat:@"%@ %@ failed with status %d: %@",
                                         launchPath,
                                         [arguments componentsJoinedByString:@" "],
                                         task.terminationStatus,
                                         stderrText]);
  }
}

static void create_7z_archive(NSString *workingDirectory,
                              NSString *archivePath,
                              NSArray<NSString *> *inputs) {
  NSMutableArray<NSString *> *arguments = [NSMutableArray arrayWithObject:@"a"];
  [arguments addObject:archivePath.lastPathComponent];
  [arguments addObject:@"-t7z"];
  [arguments addObjectsFromArray:inputs];
  run_task_or_fail(sevenzz_path(), arguments, workingDirectory);
}

static void create_encrypted_7z_archive(NSString *workingDirectory,
                                        NSString *archivePath,
                                        NSArray<NSString *> *inputs,
                                        NSString *password) {
  NSMutableArray<NSString *> *arguments = [NSMutableArray arrayWithObject:@"a"];
  [arguments addObject:archivePath.lastPathComponent];
  [arguments addObject:@"-t7z"];
  [arguments addObject:[NSString stringWithFormat:@"-p%@", password]];
  [arguments addObject:@"-mhe=on"];
  [arguments addObjectsFromArray:inputs];
  run_task_or_fail(sevenzz_path(), arguments, workingDirectory);
}

static NSArray<NSString *> *collect_item_names(const z7_mi_quicklook_list_result_t *result) {
  NSMutableArray<NSString *> *names = [NSMutableArray array];
  if (result == NULL || result->items == NULL) {
    return names;
  }
  for (size_t index = 0; index < result->item_count; ++index) {
    const z7_mi_quicklook_item_t item = result->items[index];
    if (item.name != NULL) {
      [names addObject:[NSString stringWithUTF8String:item.name]];
    }
  }
  return names;
}

static const z7_mi_quicklook_item_t *find_item_named(
    const z7_mi_quicklook_list_result_t *result,
    NSString *name) {
  if (result == NULL || result->items == NULL) {
    return NULL;
  }
  for (size_t index = 0; index < result->item_count; ++index) {
    const z7_mi_quicklook_item_t *item = &result->items[index];
    if (item->name != NULL &&
        [name isEqualToString:[NSString stringWithUTF8String:item->name]]) {
      return item;
    }
  }
  return NULL;
}

static NSString *item_path(const z7_mi_quicklook_item_t *item) {
  return item != NULL && item->path != NULL
             ? [NSString stringWithUTF8String:item->path]
             : @"";
}

static void quicklook_list_callback(z7_mi_quicklook_list_result_t *result,
                                    void *userData) {
  QuickLookListCallbackContext *context =
      (QuickLookListCallbackContext *)userData;
  context->result.result = result;
  context->result.status =
      result != NULL ? result->status : Z7_MI_STATUS_INTERNAL_ERROR;
  dispatch_semaphore_signal(context->semaphore);
}

static void quicklook_batch_export_progress_callback(
    z7_mi_quicklook_batch_export_progress_t *progress,
    void *userData) {
  (void)userData;
  if (progress != NULL) {
    z7_mi_destroy_quicklook_batch_export_progress(progress);
  }
}

static void quicklook_batch_export_callback(
    z7_mi_quicklook_batch_export_result_t *result,
    void *userData) {
  QuickLookBatchExportCallbackContext *context =
      (QuickLookBatchExportCallbackContext *)userData;
  context->result.result = result;
  context->result.status =
      result != NULL ? result->status : Z7_MI_STATUS_INTERNAL_ERROR;
  dispatch_semaphore_signal(context->semaphore);
}

static QuickLookListCallResult quicklook_list(z7_mi_session_t *session,
                                              NSString *archivePath,
                                              NSString *virtualDir,
                                              NSString *archiveTypeHint,
                                              NSArray<NSString *> *nestedArchiveEntries) {
  QuickLookListCallbackContext context = {
      .semaphore = dispatch_semaphore_create(0),
      .result = {Z7_MI_STATUS_INTERNAL_ERROR, NULL},
  };

  NSMutableArray<NSData *> *nestedBuffers = [NSMutableArray array];
  NSMutableArray<NSValue *> *nestedPointers = [NSMutableArray array];
  for (NSString *entry in nestedArchiveEntries) {
    NSData *data = [entry dataUsingEncoding:NSUTF8StringEncoding];
    [nestedBuffers addObject:data];
    [nestedPointers addObject:[NSValue valueWithPointer:data.bytes]];
  }
  const char *nestedRaw[5] = {0};
  for (NSUInteger index = 0; index < nestedPointers.count; ++index) {
    nestedRaw[index] = (const char *)nestedPointers[index].pointerValue;
  }

  z7_mi_quicklook_list_request_t request = {
      .archive_path = archivePath.UTF8String,
      .virtual_dir = virtualDir.UTF8String,
      .archive_type_hint = archiveTypeHint.UTF8String,
      .nested_archive_entries = nestedPointers.count == 0 ? NULL : nestedRaw,
      .nested_archive_entry_count = nestedPointers.count,
  };

  z7_mi_task_t *task = NULL;
  z7_mi_status_t status = z7_mi_quicklook_list(
      session,
      &request,
      quicklook_list_callback,
      &context,
      &task);
  if (status != Z7_MI_STATUS_OK) {
    if (task != NULL) {
      z7_mi_task_release(task);
    }
    context.result.status = status;
    return context.result;
  }
  expect_true(dispatch_semaphore_wait(
                  context.semaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0,
              @"quicklook_list timed out");
  if (task != NULL) {
    z7_mi_task_release(task);
  }
  return context.result;
}

static QuickLookBatchExportCallResult quicklook_batch_export(
    z7_mi_session_t *session,
    NSString *archivePath,
    NSString *archiveTypeHint,
    NSArray<NSString *> *nestedArchiveEntries,
    NSString *entryPath,
    NSString *destinationPath,
    BOOL recursive,
    BOOL entryIsDirectory) {
  QuickLookBatchExportCallbackContext context = {
      .semaphore = dispatch_semaphore_create(0),
      .result = {Z7_MI_STATUS_INTERNAL_ERROR, NULL},
  };

  NSMutableArray<NSData *> *nestedBuffers = [NSMutableArray array];
  NSMutableArray<NSValue *> *nestedPointers = [NSMutableArray array];
  for (NSString *entry in nestedArchiveEntries) {
    NSData *data = [entry dataUsingEncoding:NSUTF8StringEncoding];
    [nestedBuffers addObject:data];
    [nestedPointers addObject:[NSValue valueWithPointer:data.bytes]];
  }
  const char *nestedRaw[5] = {0};
  for (NSUInteger index = 0; index < nestedPointers.count; ++index) {
    nestedRaw[index] = (const char *)nestedPointers[index].pointerValue;
  }

  z7_mi_quicklook_batch_export_item_t item = {
      .entry_path = entryPath.UTF8String,
      .destination_path = destinationPath.UTF8String,
      .listed_size = 0,
      .recursive = recursive,
      .entry_is_directory = entryIsDirectory,
  };

  z7_mi_quicklook_batch_export_request_t request = {
      .archive_path = archivePath.UTF8String,
      .archive_type_hint = archiveTypeHint.UTF8String,
      .nested_archive_entries = nestedPointers.count == 0 ? NULL : nestedRaw,
      .nested_archive_entry_count = nestedPointers.count,
      .items = &item,
      .item_count = 1,
  };

  z7_mi_task_t *task = NULL;
  z7_mi_status_t status = z7_mi_quicklook_batch_export(
      session,
      &request,
      quicklook_batch_export_progress_callback,
      quicklook_batch_export_callback,
      &context,
      &task);
  if (status != Z7_MI_STATUS_OK) {
    if (task != NULL) {
      z7_mi_task_release(task);
    }
    context.result.status = status;
    return context.result;
  }
  expect_true(dispatch_semaphore_wait(
                  context.semaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0,
              @"quicklook_batch_export timed out");
  if (task != NULL) {
    z7_mi_task_release(task);
  }
  return context.result;
}

static void password_prompt_callback(z7_mi_password_prompt_t *handle,
                                     const char *archivePath,
                                     const char *const *nestedChain,
                                     size_t nestedChainCount,
                                     const char *reasonKey,
                                     void *userData) {
  (void)archivePath;
  PasswordPromptScript *script = (__bridge PasswordPromptScript *)userData;
  script.callCount += 1;
  [script.reasonKeys addObject:reasonKey != NULL
                                   ? [NSString stringWithUTF8String:reasonKey]
                                   : @"password_required"];
  NSMutableArray<NSString *> *chain = [NSMutableArray array];
  if (nestedChain != NULL) {
    for (size_t index = 0; index < nestedChainCount; ++index) {
      if (nestedChain[index] != NULL) {
        [chain addObject:[NSString stringWithUTF8String:nestedChain[index]]];
      }
    }
  }
  [script.nestedChains addObject:chain];

  id response = script.callCount <= script.responses.count
                    ? script.responses[(NSUInteger)script.callCount - 1]
                    : nil;
  if (response == nil || response == [NSNull null]) {
    z7_mi_password_prompt_cancel(handle);
    return;
  }
  z7_mi_password_prompt_provide(handle, [(NSString *)response UTF8String]);
}

static NSString *create_plain_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-ql-plain");
  write_text_file([root stringByAppendingPathComponent:@"seed.txt"],
                  @"quicklook-seed");
  NSString *archivePath = [root stringByAppendingPathComponent:@"plain.7z"];
  create_7z_archive(root, archivePath, @[ @"seed.txt" ]);
  return archivePath;
}

static NSDictionary<NSString *, NSString *> *create_nested_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-ql-nested");
  write_text_file([root stringByAppendingPathComponent:@"leaf.txt"],
                  @"nested-leaf");
  NSString *childArchive = [root stringByAppendingPathComponent:@"child.7z"];
  create_7z_archive(root, childArchive, @[ @"leaf.txt" ]);
  write_text_file([root stringByAppendingPathComponent:@"note.txt"], @"note");
  NSString *outerArchive = [root stringByAppendingPathComponent:@"outer.7z"];
  create_7z_archive(root, outerArchive, @[ @"child.7z", @"note.txt" ]);
  return @{
    @"outer": outerArchive,
    @"childEntry": @"child.7z",
    @"leafName": @"leaf.txt"
  };
}

static NSDictionary<NSString *, NSString *> *create_encrypted_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-ql-encrypted");
  write_text_file([root stringByAppendingPathComponent:@"secret.txt"], @"secret");
  NSString *archivePath = [root stringByAppendingPathComponent:@"encrypted.7z"];
  create_encrypted_7z_archive(root, archivePath, @[ @"secret.txt" ], @"test-password");
  return @{@"archive": archivePath, @"entry": @"secret.txt"};
}

static NSDictionary<NSString *, NSString *> *create_nested_encrypted_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-ql-nested-encrypted");
  write_text_file([root stringByAppendingPathComponent:@"secret.txt"], @"nested-secret");
  NSString *childArchive = [root stringByAppendingPathComponent:@"child-encrypted.7z"];
  create_encrypted_7z_archive(root, childArchive, @[ @"secret.txt" ], @"test-password");
  NSString *outerArchive = [root stringByAppendingPathComponent:@"outer.7z"];
  create_7z_archive(root, outerArchive, @[ @"child-encrypted.7z" ]);
  return @{
    @"outer": outerArchive,
    @"childEntry": @"child-encrypted.7z",
    @"leafName": @"secret.txt"
  };
}

static NSDictionary<NSString *, NSString *> *create_directory_archive_fixture(NSUInteger fileCount) {
  NSString *root = temporary_root(@"z7-ql-docs");
  NSString *docsDir = [root stringByAppendingPathComponent:@"docs"];
  ensure_directory(docsDir);
  for (NSUInteger index = 0; index < fileCount; ++index) {
    NSString *name =
        [NSString stringWithFormat:@"entry-%04lu.txt", (unsigned long)index];
    write_text_file([docsDir stringByAppendingPathComponent:name], @"docs");
  }
  NSString *archivePath = [root stringByAppendingPathComponent:@"docs.7z"];
  create_7z_archive(root, archivePath, @[ @"docs" ]);
  return @{@"root": root, @"archive": archivePath, @"dir": @"docs"};
}

static NSDictionary<NSString *, NSString *> *create_tree_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-ql-tree");
  NSString *docsDir = [root stringByAppendingPathComponent:@"docs"];
  NSString *nestedDir = [docsDir stringByAppendingPathComponent:@"nested"];
  ensure_directory(nestedDir);
  write_text_file([docsDir stringByAppendingPathComponent:@"readme.md"],
                  @"tree-readme");
  write_text_file([nestedDir stringByAppendingPathComponent:@"leaf.txt"],
                  @"tree-leaf");
  write_text_file([root stringByAppendingPathComponent:@"report.txt"],
                  @"tree-report");

  NSString *archivePath = [root stringByAppendingPathComponent:@"tree.7z"];
  create_7z_archive(root, archivePath, @[ @"docs", @"report.txt" ]);
  return @{
    @"archive": archivePath,
    @"docs": @"docs",
    @"nested": @"docs/nested",
    @"readme": @"readme.md",
    @"leaf": @"leaf.txt",
    @"report": @"report.txt"
  };
}

static NSDictionary<NSString *, NSString *> *create_nested_encrypted_directory_fixture(void) {
  NSString *root = temporary_root(@"z7-ql-nested-encrypted-dir");
  NSString *docsDir = [root stringByAppendingPathComponent:@"docs"];
  ensure_directory(docsDir);
  write_text_file([docsDir stringByAppendingPathComponent:@"readme.md"],
                  @"nested-encrypted-dir");
  NSString *childArchive = [root stringByAppendingPathComponent:@"child-encrypted-dir.7z"];
  create_encrypted_7z_archive(root, childArchive, @[ @"docs" ], @"test-password");
  NSString *outerArchive = [root stringByAppendingPathComponent:@"outer.7z"];
  create_7z_archive(root, outerArchive, @[ @"child-encrypted-dir.7z" ]);
  return @{
    @"root": root,
    @"outer": outerArchive,
    @"childEntry": @"child-encrypted-dir.7z",
    @"dir": @"docs",
    @"leafName": @"readme.md"
  };
}

static void test_quicklook_list_returns_entries(void) {
  NSString *archivePath = create_plain_archive_fixture();
  z7_mi_session_t *session = z7_mi_session_create();
  expect_true(session != NULL, @"failed to create quicklook session");

  QuickLookListCallResult result =
      quicklook_list(session, archivePath, @"", @"7z", @[]);
  expect_true(result.status == Z7_MI_STATUS_OK, @"plain list should succeed");
  expect_true(result.result != NULL && result.result->ok,
              @"plain list result should be ok");
  expect_true([collect_item_names(result.result) containsObject:@"seed.txt"],
              @"plain list should contain seed.txt");

  z7_mi_destroy_quicklook_list_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_quicklook_list_uses_direct_child_display_names(void) {
  NSDictionary<NSString *, NSString *> *fixture = create_tree_archive_fixture();
  z7_mi_session_t *session = z7_mi_session_create();
  expect_true(session != NULL, @"failed to create quicklook tree session");

  QuickLookListCallResult root =
      quicklook_list(session, fixture[@"archive"], @"", @"7z", @[]);
  expect_true(root.status == Z7_MI_STATUS_OK && root.result->ok,
              @"root tree list should succeed");
  const z7_mi_quicklook_item_t *docs = find_item_named(root.result, fixture[@"docs"]);
  const z7_mi_quicklook_item_t *report =
      find_item_named(root.result, fixture[@"report"]);
  expect_true(docs != NULL && docs->is_dir,
              @"root list should expose docs as a direct directory child");
  expect_true([item_path(docs) isEqualToString:@"docs"],
              @"root docs item path should be docs");
  expect_true(report != NULL && !report->is_dir,
              @"root list should expose report.txt as a direct file child");
  expect_true([item_path(report) isEqualToString:@"report.txt"],
              @"root report item path should be report.txt");
  expect_true(![collect_item_names(root.result) containsObject:@"docs/readme.md"],
              @"root list item names should not contain slash-qualified descendants");
  z7_mi_destroy_quicklook_list_result(root.result);

  QuickLookListCallResult docsList =
      quicklook_list(session, fixture[@"archive"], fixture[@"docs"], @"7z", @[]);
  expect_true(docsList.status == Z7_MI_STATUS_OK && docsList.result->ok,
              @"docs tree list should succeed");
  const z7_mi_quicklook_item_t *readme =
      find_item_named(docsList.result, fixture[@"readme"]);
  const z7_mi_quicklook_item_t *nested =
      find_item_named(docsList.result, @"nested");
  expect_true(readme != NULL && !readme->is_dir,
              @"docs list should expose readme.md as a direct child name");
  expect_true([item_path(readme) isEqualToString:@"docs/readme.md"],
              @"docs/readme.md path should keep the virtual directory prefix");
  expect_true(nested != NULL && nested->is_dir,
              @"docs list should expose nested as a direct directory child");
  expect_true([item_path(nested) isEqualToString:@"docs/nested"],
              @"docs/nested path should keep the virtual directory prefix");
  z7_mi_destroy_quicklook_list_result(docsList.result);

  QuickLookListCallResult nestedList =
      quicklook_list(session, fixture[@"archive"], fixture[@"nested"], @"7z", @[]);
  expect_true(nestedList.status == Z7_MI_STATUS_OK && nestedList.result->ok,
              @"nested tree list should succeed");
  const z7_mi_quicklook_item_t *leaf =
      find_item_named(nestedList.result, fixture[@"leaf"]);
  expect_true(leaf != NULL && !leaf->is_dir,
              @"nested list should expose leaf.txt as a direct child name");
  expect_true([item_path(leaf) isEqualToString:@"docs/nested/leaf.txt"],
              @"nested leaf path should keep the full virtual directory prefix");

  z7_mi_destroy_quicklook_list_result(nestedList.result);
  z7_mi_session_destroy(session);
}

static void test_nested_list_supports_entries(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_nested_archive_fixture();
  z7_mi_session_t *session = z7_mi_session_create();
  expect_true(session != NULL, @"failed to create nested quicklook session");

  QuickLookListCallResult result = quicklook_list(
      session,
      fixture[@"outer"],
      @"",
      @"7z",
      @[ fixture[@"childEntry"] ]);
  expect_true(result.status == Z7_MI_STATUS_OK, @"nested list should succeed");
  expect_true(result.result != NULL && result.result->ok,
              @"nested list result should be ok");
  expect_true([collect_item_names(result.result) containsObject:fixture[@"leafName"]],
              @"nested list should contain leaf file");

  z7_mi_destroy_quicklook_list_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_encrypted_list_uses_password_prompt_and_caches_within_session(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_encrypted_archive_fixture();
  PasswordPromptScript *script = [[PasswordPromptScript alloc] init];
  script.responses = @[ @"test-password" ];

  z7_mi_session_t *session = z7_mi_session_create();
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)script);

  QuickLookListCallResult first =
      quicklook_list(session, fixture[@"archive"], @"", @"7z", @[]);
  expect_true(first.status == Z7_MI_STATUS_OK && first.result->ok,
              @"first encrypted list should succeed");
  z7_mi_destroy_quicklook_list_result(first.result);

  QuickLookListCallResult second =
      quicklook_list(session, fixture[@"archive"], @"", @"7z", @[]);
  expect_true(second.status == Z7_MI_STATUS_OK && second.result->ok,
              @"second encrypted list should reuse cached password");
  z7_mi_destroy_quicklook_list_result(second.result);

  expect_true(script.callCount == 1, @"password prompt should be cached");
  expect_true([script.reasonKeys.firstObject isEqualToString:@"password_required"],
              @"first encrypted prompt should be password_required");
  expect_true(script.nestedChains.firstObject.count == 0,
              @"top-level encrypted prompt should have empty nested chain");

  z7_mi_session_destroy(session);
}

static void test_encrypted_list_retries_wrong_password(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_encrypted_archive_fixture();
  PasswordPromptScript *script = [[PasswordPromptScript alloc] init];
  script.responses = @[ @"wrong-password", @"test-password" ];

  z7_mi_session_t *session = z7_mi_session_create();
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)script);

  QuickLookListCallResult result =
      quicklook_list(session, fixture[@"archive"], @"", @"7z", @[]);
  expect_true(result.status == Z7_MI_STATUS_OK && result.result->ok,
              @"encrypted list should succeed after retry");
  z7_mi_destroy_quicklook_list_result(result.result);

  expect_true(script.callCount == 2,
              @"wrong password flow should prompt twice");
  expect_true([script.reasonKeys[0] isEqualToString:@"password_required"] &&
                  [script.reasonKeys[1] isEqualToString:@"wrong_password"],
              @"retry should report wrong_password on second prompt");

  z7_mi_session_destroy(session);
}

static void test_encrypted_list_cancel_returns_password_required(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_encrypted_archive_fixture();
  PasswordPromptScript *script = [[PasswordPromptScript alloc] init];
  script.responses = @[ [NSNull null] ];

  z7_mi_session_t *session = z7_mi_session_create();
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)script);

  QuickLookListCallResult result =
      quicklook_list(session, fixture[@"archive"], @"", @"7z", @[]);
  expect_true(result.status == Z7_MI_STATUS_PASSWORD_REQUIRED,
              @"canceling encrypted prompt should return password required");
  expect_true(result.result != NULL && !result.result->ok,
              @"cancel result should not be ok");

  z7_mi_destroy_quicklook_list_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_nested_encrypted_list_reports_nested_password_reason(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_nested_encrypted_archive_fixture();
  PasswordPromptScript *script = [[PasswordPromptScript alloc] init];
  script.responses = @[ @"test-password" ];

  z7_mi_session_t *session = z7_mi_session_create();
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)script);

  QuickLookListCallResult result = quicklook_list(
      session,
      fixture[@"outer"],
      @"",
      @"7z",
      @[ fixture[@"childEntry"] ]);
  expect_true(result.status == Z7_MI_STATUS_OK && result.result->ok,
              @"nested encrypted list should succeed");
  z7_mi_destroy_quicklook_list_result(result.result);

  expect_true(script.callCount == 1,
              @"nested encrypted list should prompt once");
  expect_true([script.reasonKeys.firstObject isEqualToString:@"nested_password_required"],
              @"nested encrypted prompt should report nested_password_required");
  expect_true([script.nestedChains.firstObject isEqualToArray:@[ fixture[@"childEntry"] ]],
              @"nested encrypted prompt should report child archive chain");

  z7_mi_session_destroy(session);
}

static void test_nested_cancel_then_retry_succeeds(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_nested_encrypted_archive_fixture();

  PasswordPromptScript *cancelScript = [[PasswordPromptScript alloc] init];
  cancelScript.responses = @[ [NSNull null] ];
  z7_mi_session_t *session = z7_mi_session_create();
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)cancelScript);

  QuickLookListCallResult canceled = quicklook_list(
      session,
      fixture[@"outer"],
      @"",
      @"7z",
      @[ fixture[@"childEntry"] ]);
  expect_true(canceled.status == Z7_MI_STATUS_PASSWORD_REQUIRED,
              @"canceled nested prompt should report password required");
  z7_mi_destroy_quicklook_list_result(canceled.result);

  PasswordPromptScript *retryScript = [[PasswordPromptScript alloc] init];
  retryScript.responses = @[ @"test-password" ];
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)retryScript);

  QuickLookListCallResult retried = quicklook_list(
      session,
      fixture[@"outer"],
      @"",
      @"7z",
      @[ fixture[@"childEntry"] ]);
  expect_true(retried.status == Z7_MI_STATUS_OK && retried.result->ok,
              @"nested list should succeed after retrying post-cancel");
  z7_mi_destroy_quicklook_list_result(retried.result);
  z7_mi_session_destroy(session);
}

static void test_batch_export_file_uses_current_materialize_path(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_encrypted_archive_fixture();
  z7_mi_session_t *session = z7_mi_session_create();
  PasswordPromptScript *script = [[PasswordPromptScript alloc] init];
  script.responses = @[ @"test-password" ];
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)script);

  NSString *exportRoot = temporary_root(@"z7-ql-export-file");
  NSString *destination = [exportRoot stringByAppendingPathComponent:@"secret.txt"];
  QuickLookBatchExportCallResult result = quicklook_batch_export(
      session,
      fixture[@"archive"],
      @"7z",
      @[],
      fixture[@"entry"],
      destination,
      NO,
      NO);
  expect_true(result.status == Z7_MI_STATUS_OK && result.result->ok,
              @"single file export should succeed");
  NSString *content =
      [NSString stringWithContentsOfFile:destination
                                encoding:NSUTF8StringEncoding
                                   error:nil];
  expect_true([content isEqualToString:@"secret"],
              @"exported file content should match source");

  z7_mi_destroy_quicklook_batch_export_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_batch_export_nested_file_uses_current_materialize_path(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_nested_archive_fixture();
  z7_mi_session_t *session = z7_mi_session_create();

  NSString *exportRoot = temporary_root(@"z7-ql-export-nested");
  NSString *destination =
      [exportRoot stringByAppendingPathComponent:fixture[@"leafName"]];
  QuickLookBatchExportCallResult result = quicklook_batch_export(
      session,
      fixture[@"outer"],
      @"7z",
      @[ fixture[@"childEntry"] ],
      fixture[@"leafName"],
      destination,
      NO,
      NO);
  expect_true(result.status == Z7_MI_STATUS_OK && result.result->ok,
              @"nested file export should succeed");
  NSString *content =
      [NSString stringWithContentsOfFile:destination
                                encoding:NSUTF8StringEncoding
                                   error:nil];
  expect_true([content isEqualToString:@"nested-leaf"],
              @"nested file export content should match source");

  z7_mi_destroy_quicklook_batch_export_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_batch_export_directory_recursively(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_directory_archive_fixture(1);
  z7_mi_session_t *session = z7_mi_session_create();

  NSString *exportRoot = temporary_root(@"z7-ql-export-dir");
  NSString *destination = [exportRoot stringByAppendingPathComponent:@"docs"];
  QuickLookBatchExportCallResult result = quicklook_batch_export(
      session,
      fixture[@"archive"],
      @"7z",
      @[],
      fixture[@"dir"],
      destination,
      YES,
      YES);
  expect_true(result.status == Z7_MI_STATUS_OK && result.result->ok,
              @"directory export should succeed");
  expect_true([[NSFileManager defaultManager]
                  fileExistsAtPath:[destination stringByAppendingPathComponent:@"entry-0000.txt"]],
              @"recursive directory export should materialize child file");

  z7_mi_destroy_quicklook_batch_export_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_batch_export_directory_requires_recursive_flag(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_directory_archive_fixture(1);
  z7_mi_session_t *session = z7_mi_session_create();

  NSString *exportRoot = temporary_root(@"z7-ql-export-dir-no-recursive");
  NSString *destination = [exportRoot stringByAppendingPathComponent:@"docs"];
  QuickLookBatchExportCallResult result = quicklook_batch_export(
      session,
      fixture[@"archive"],
      @"7z",
      @[],
      fixture[@"dir"],
      destination,
      NO,
      YES);
  expect_true(result.status == Z7_MI_STATUS_INVALID_ARGUMENT,
              @"directory export without recursive should fail");
  NSString *message = result.result != NULL && result.result->error_message != NULL
                          ? [NSString stringWithUTF8String:result.result->error_message]
                          : @"";
  expect_true([message localizedCaseInsensitiveContainsString:@"recursive"],
              @"directory export without recursive should mention recursive requirement");

  z7_mi_destroy_quicklook_batch_export_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_batch_export_directory_rejects_hint_mismatch(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_directory_archive_fixture(1);
  z7_mi_session_t *session = z7_mi_session_create();

  NSString *exportRoot = temporary_root(@"z7-ql-export-dir-hint");
  NSString *destination = [exportRoot stringByAppendingPathComponent:@"docs"];
  QuickLookBatchExportCallResult result = quicklook_batch_export(
      session,
      fixture[@"archive"],
      @"7z",
      @[],
      fixture[@"dir"],
      destination,
      YES,
      NO);
  expect_true(result.status == Z7_MI_STATUS_INVALID_ARGUMENT,
              @"directory export hint mismatch should fail");
  NSString *message = result.result != NULL && result.result->error_message != NULL
                          ? [NSString stringWithUTF8String:result.result->error_message]
                          : @"";
  expect_true([message localizedCaseInsensitiveContainsString:@"type"],
              @"directory export hint mismatch should mention type mismatch");

  z7_mi_destroy_quicklook_batch_export_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_batch_export_directory_rejects_budget_exceeded(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_directory_archive_fixture(1001);
  z7_mi_session_t *session = z7_mi_session_create();

  NSString *exportRoot = temporary_root(@"z7-ql-export-dir-budget");
  NSString *destination = [exportRoot stringByAppendingPathComponent:@"docs"];
  QuickLookBatchExportCallResult result = quicklook_batch_export(
      session,
      fixture[@"archive"],
      @"7z",
      @[],
      fixture[@"dir"],
      destination,
      YES,
      YES);
  expect_true(result.status == Z7_MI_STATUS_INVALID_ARGUMENT,
              @"directory export budget preflight should fail");
  NSString *message = result.result != NULL && result.result->error_message != NULL
                          ? [NSString stringWithUTF8String:result.result->error_message]
                          : @"";
  expect_true([message containsString:@"1000 files"],
              @"budget rejection should mention 1000 files");

  z7_mi_destroy_quicklook_batch_export_result(result.result);
  z7_mi_session_destroy(session);
}

static void test_nested_encrypted_directory_batch_export_uses_cached_password(void) {
  NSDictionary<NSString *, NSString *> *fixture =
      create_nested_encrypted_directory_fixture();
  PasswordPromptScript *script = [[PasswordPromptScript alloc] init];
  script.responses = @[ @"test-password" ];

  z7_mi_session_t *session = z7_mi_session_create();
  z7_mi_session_set_password_prompt_callback(
      session, password_prompt_callback, (__bridge void *)script);

  QuickLookListCallResult listResult = quicklook_list(
      session,
      fixture[@"outer"],
      @"",
      @"7z",
      @[ fixture[@"childEntry"] ]);
  expect_true(listResult.status == Z7_MI_STATUS_OK && listResult.result->ok,
              @"nested encrypted directory list should succeed");
  z7_mi_destroy_quicklook_list_result(listResult.result);

  NSString *exportRoot = temporary_root(@"z7-ql-export-nested-encrypted-dir");
  NSString *destination = [exportRoot stringByAppendingPathComponent:fixture[@"dir"]];
  QuickLookBatchExportCallResult exportResult = quicklook_batch_export(
      session,
      fixture[@"outer"],
      @"7z",
      @[ fixture[@"childEntry"] ],
      fixture[@"dir"],
      destination,
      YES,
      YES);
  expect_true(exportResult.status == Z7_MI_STATUS_OK && exportResult.result->ok,
              @"nested encrypted directory export should succeed");
  expect_true([[NSFileManager defaultManager]
                  fileExistsAtPath:[destination stringByAppendingPathComponent:fixture[@"leafName"]]],
              @"nested encrypted directory export should materialize leaf file");
  expect_true(script.callCount == 1,
              @"cached password should be reused for nested directory export");

  z7_mi_destroy_quicklook_batch_export_result(exportResult.result);
  z7_mi_session_destroy(session);
}

static BOOL should_run_test(NSString *name, int argc, const char *argv[]) {
  if (argc <= 1) {
    return YES;
  }
  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], name.UTF8String) == 0) {
      return YES;
    }
  }
  return NO;
}

static void run_test(NSString *name, int argc, const char *argv[], void (^block)(void)) {
  if (!should_run_test(name, argc, argv)) {
    return;
  }
  fprintf(stdout, "%s\n", name.UTF8String);
  fflush(stdout);
  @autoreleasepool {
    block();
  }
}

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    run_test(@"quicklook_list_returns_entries", argc, argv, ^{
      test_quicklook_list_returns_entries();
    });
    run_test(@"quicklook_list_uses_direct_child_display_names", argc, argv, ^{
      test_quicklook_list_uses_direct_child_display_names();
    });
    run_test(@"nested_list_supports_entries", argc, argv, ^{
      test_nested_list_supports_entries();
    });
    run_test(@"encrypted_list_uses_password_prompt_and_caches_within_session", argc, argv, ^{
      test_encrypted_list_uses_password_prompt_and_caches_within_session();
    });
    run_test(@"encrypted_list_retries_wrong_password", argc, argv, ^{
      test_encrypted_list_retries_wrong_password();
    });
    run_test(@"encrypted_list_cancel_returns_password_required", argc, argv, ^{
      test_encrypted_list_cancel_returns_password_required();
    });
    run_test(@"nested_encrypted_list_reports_nested_password_reason", argc, argv, ^{
      test_nested_encrypted_list_reports_nested_password_reason();
    });
    run_test(@"nested_cancel_then_retry_succeeds", argc, argv, ^{
      test_nested_cancel_then_retry_succeeds();
    });
    run_test(@"batch_export_file_uses_current_materialize_path", argc, argv, ^{
      test_batch_export_file_uses_current_materialize_path();
    });
    run_test(@"batch_export_nested_file_uses_current_materialize_path", argc, argv, ^{
      test_batch_export_nested_file_uses_current_materialize_path();
    });
    run_test(@"batch_export_directory_recursively", argc, argv, ^{
      test_batch_export_directory_recursively();
    });
    run_test(@"batch_export_directory_requires_recursive_flag", argc, argv, ^{
      test_batch_export_directory_requires_recursive_flag();
    });
    run_test(@"batch_export_directory_rejects_hint_mismatch", argc, argv, ^{
      test_batch_export_directory_rejects_hint_mismatch();
    });
    run_test(@"batch_export_directory_rejects_budget_exceeded", argc, argv, ^{
      test_batch_export_directory_rejects_budget_exceeded();
    });
    run_test(@"nested_encrypted_directory_batch_export_uses_cached_password", argc, argv, ^{
      test_nested_encrypted_directory_batch_export_uses_cached_password();
    });
  }
  fflush(NULL);
  _Exit(0);
}
