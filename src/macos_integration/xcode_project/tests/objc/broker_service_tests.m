#import <Foundation/Foundation.h>

#include <unistd.h>

#import "BrokerService.h"

@interface BrokerCallbackRecorder : NSObject <BrokerClientCallbackProtocol>
@property(nonatomic, strong) dispatch_semaphore_t listSemaphore;
@property(nonatomic, strong) Z7BrokerQuickLookListResult *listResult;
@property(nonatomic, strong) dispatch_semaphore_t batchResultSemaphore;
@property(nonatomic, strong) Z7BrokerQuickLookBatchExportResult *batchResult;
@property(nonatomic, strong) dispatch_semaphore_t passwordPromptSemaphore;
@property(nonatomic, strong) Z7BrokerPasswordPromptEvent *passwordPrompt;
@end

@implementation BrokerCallbackRecorder

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _listSemaphore = dispatch_semaphore_create(0);
    _batchResultSemaphore = dispatch_semaphore_create(0);
    _passwordPromptSemaphore = dispatch_semaphore_create(0);
  }
  return self;
}

- (void)quickLookListDidFinishWithResult:(Z7BrokerQuickLookListResult *)result {
  self.listResult = result;
  dispatch_semaphore_signal(self.listSemaphore);
}

- (void)quickLookBatchExportDidUpdateProgress:(Z7BrokerQuickLookBatchExportProgress *)progress {
  (void)progress;
}

- (void)quickLookBatchExportDidFinishWithResult:(Z7BrokerQuickLookBatchExportResult *)result {
  self.batchResult = result;
  dispatch_semaphore_signal(self.batchResultSemaphore);
}

- (void)passwordPromptDidRequestInput:(Z7BrokerPasswordPromptEvent *)event {
  self.passwordPrompt = event;
  dispatch_semaphore_signal(self.passwordPromptSemaphore);
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

static void write_text_file(NSString *path, NSString *text) {
  NSError *error = nil;
  NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
  expect_true(data != nil, @"failed to encode test text");
  BOOL ok = [data writeToURL:[NSURL fileURLWithPath:path]
                     options:NSDataWritingAtomic
                       error:&error];
  expect_true(ok,
              [NSString stringWithFormat:@"failed to write %@: %@",
                                         path,
                                         error.localizedDescription]);
}

static NSString *portable_settings_root(void) {
  const char *root = getenv("Z7_TEST_PORTABLE_SETTINGS_ROOT");
  expect_true(root != NULL && strlen(root) > 0,
              @"Z7_TEST_PORTABLE_SETTINGS_ROOT must be set");
  return [NSString stringWithUTF8String:root];
}

static void remove_file_if_exists(NSString *path) {
  NSFileManager *fm = [NSFileManager defaultManager];
  if (![fm fileExistsAtPath:path]) {
    return;
  }
  NSError *error = nil;
  expect_true([fm removeItemAtPath:path error:&error],
              [NSString stringWithFormat:@"failed to remove %@: %@",
                                         path,
                                         error.localizedDescription]);
}

static void write_json_file(NSString *path, NSDictionary *object) {
  ensure_directory(path.stringByDeletingLastPathComponent);
  NSError *error = nil;
  NSData *data = [NSJSONSerialization dataWithJSONObject:object
                                                 options:NSJSONWritingPrettyPrinted
                                                   error:&error];
  expect_true(data != nil,
              [NSString stringWithFormat:@"failed to encode JSON %@: %@",
                                         path,
                                         error.localizedDescription]);
  expect_true([data writeToFile:path options:NSDataWritingAtomic error:&error],
              [NSString stringWithFormat:@"failed to write JSON %@: %@",
                                         path,
                                         error.localizedDescription]);
}

static void write_portable_7zfm_language(NSString *language) {
  NSString *root = portable_settings_root();
  write_json_file([root stringByAppendingPathComponent:@"settings.json"],
                  @{
                    @"version" : @1,
                    @"apps" : @{ @"7zFM" : @{ @"Lang" : language } },
                    @"shared" : @{}
                  });
  remove_file_if_exists([root stringByAppendingPathComponent:@"macos_integration.json"]);
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

static NSString *create_broker_tree_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-broker-tree");
  NSString *docsDir = [root stringByAppendingPathComponent:@"docs"];
  ensure_directory(docsDir);
  write_text_file([docsDir stringByAppendingPathComponent:@"readme.md"],
                  @"broker-readme");
  write_text_file([root stringByAppendingPathComponent:@"report.txt"],
                  @"broker-report");

  NSString *archivePath = [root stringByAppendingPathComponent:@"broker.7z"];
  run_task_or_fail(sevenzz_path(),
                   @[ @"a", archivePath.lastPathComponent, @"-t7z", @"docs", @"report.txt" ],
                   root);
  return archivePath;
}

static NSString *create_broker_password_archive_fixture(void) {
  NSString *root = temporary_root(@"z7-broker-password");
  write_text_file([root stringByAppendingPathComponent:@"secret.txt"],
                  @"secret-body");

  NSString *archivePath = [root stringByAppendingPathComponent:@"protected.7z"];
  run_task_or_fail(sevenzz_path(),
                   @[ @"a", archivePath.lastPathComponent, @"-t7z", @"-psecret", @"secret.txt" ],
                   root);
  return archivePath;
}

static Z7BrokerQuickLookItem *find_item_named(NSArray<Z7BrokerQuickLookItem *> *items,
                                              NSString *name) {
  for (Z7BrokerQuickLookItem *item in items) {
    if ([item.name isEqualToString:name]) {
      return item;
    }
  }
  return nil;
}

static BOOL menu_plan_has_action(Z7BrokerMenuPlan *plan, NSString *actionID) {
  for (Z7BrokerMenuAction *action in plan.actions) {
    if ([action.actionID isEqualToString:actionID]) {
      return YES;
    }
  }
  return NO;
}

static NSString *menu_plan_action_title(Z7BrokerMenuPlan *plan, NSString *actionID) {
  for (Z7BrokerMenuAction *action in plan.actions) {
    if ([action.actionID isEqualToString:actionID]) {
      return action.title;
    }
  }
  return nil;
}

static Z7BrokerMenuPlan *fetch_menu_plan_with_locale(BrokerService *service,
                                                     NSArray<NSString *> *paths,
                                                     NSString *locale);

static Z7BrokerMenuPlan *fetch_menu_plan(BrokerService *service,
                                         NSArray<NSString *> *paths) {
  return fetch_menu_plan_with_locale(service, paths, @"en");
}

static Z7BrokerMenuPlan *fetch_menu_plan_with_locale(BrokerService *service,
                                                     NSArray<NSString *> *paths,
                                                     NSString *locale) {
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  __block Z7BrokerMenuPlan *plan = nil;
  [service fetchMenuPlanWithPaths:paths
                            locale:locale
                             reply:^(Z7BrokerMenuPlan *replyPlan) {
                               plan = replyPlan;
                               dispatch_semaphore_signal(semaphore);
                             }];

  expect_true(dispatch_semaphore_wait(
                  semaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) == 0,
              @"BrokerService menu plan timed out");
  expect_true(plan != nil, @"BrokerService menu plan should reply");
  return plan;
}

static Z7BrokerActionResult *run_menu_action(BrokerService *service,
                                             NSString *actionID,
                                             NSArray<NSString *> *paths) {
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  __block Z7BrokerActionResult *result = nil;
  [(id<BrokerXPCProtocol>)service runMenuActionWithActionID:actionID
                                                       paths:paths
                                                      locale:@"en"
                                                       reply:^(Z7BrokerActionResult *replyResult) {
                                                         result = replyResult;
                                                         dispatch_semaphore_signal(semaphore);
                                                       }];

  expect_true(dispatch_semaphore_wait(
                  semaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) == 0,
              @"BrokerService menu action timed out");
  expect_true(result != nil, @"BrokerService menu action should reply");
  return result;
}

static NSDictionary *wait_for_fake_launcher_task_log(NSString *path) {
  NSFileManager *fm = [NSFileManager defaultManager];
  for (NSInteger attempt = 0; attempt < 100; ++attempt) {
    if ([fm fileExistsAtPath:path]) {
      NSData *data = [NSData dataWithContentsOfFile:path];
      if (data.length > 0) {
        NSError *error = nil;
        id json = [NSJSONSerialization JSONObjectWithData:data
                                                  options:0
                                                    error:&error];
        expect_true([json isKindOfClass:[NSDictionary class]],
                    [NSString stringWithFormat:@"invalid fake launcher JSON: %@",
                                               error.localizedDescription]);
        return (NSDictionary *)json;
      }
    }
    usleep(50000);
  }
  fail_now([NSString stringWithFormat:@"fake launcher log not written: %@", path]);
  return @{};
}

static NSDictionary *task_payload_from_fake_log(NSString *path) {
  NSDictionary *log = wait_for_fake_launcher_task_log(path);
  NSDictionary *taskIPC = log[@"task_ipc"];
  expect_true([taskIPC isKindOfClass:[NSDictionary class]],
              @"fake launcher log should include task IPC data");
  expect_true([taskIPC[@"claimed"] boolValue],
              @"fake launcher should claim the task IPC payload");
  NSDictionary *payload = taskIPC[@"payload"];
  expect_true([payload isKindOfClass:[NSDictionary class]],
              @"fake launcher log should include task IPC payload");
  return payload;
}

static void test_broker_service_quicklook_list_delivers_callback_result(void) {
  NSString *archivePath = create_broker_tree_archive_fixture();
  BrokerCallbackRecorder *callback = [[BrokerCallbackRecorder alloc] init];
  BrokerService *service = [[BrokerService alloc] initWithCallback:callback];

  Z7BrokerQuickLookListRequest *request =
      [[Z7BrokerQuickLookListRequest alloc] initWithRequestID:@"list-docs"
                                                  archivePath:archivePath
                                                   virtualDir:@"docs"
                                              archiveTypeHint:@"7z"
                                         nestedArchiveEntries:@[]];
  [service listWithRequest:request];

  expect_true(dispatch_semaphore_wait(
                  callback.listSemaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0,
              @"BrokerService quicklook list timed out");
  expect_true(callback.listResult.ok, @"BrokerService list result should be ok");
  expect_true([callback.listResult.requestID isEqualToString:@"list-docs"],
              @"BrokerService list result should preserve request id");
  expect_true([callback.listResult.virtualDir isEqualToString:@"docs"],
              @"BrokerService list result should preserve virtual directory");

  Z7BrokerQuickLookItem *readme =
      find_item_named(callback.listResult.items, @"readme.md");
  expect_true(readme != nil, @"BrokerService list should include direct child name");
  expect_true(!readme.directory, @"BrokerService direct child file should not be a directory");
  expect_true([readme.path isEqualToString:@"docs/readme.md"],
              @"BrokerService list item path should keep virtual dir prefix");

  [service invalidate];
}

static void test_broker_service_invalidation_cancels_inflight_quicklook_export(void) {
  NSString *archivePath = create_broker_password_archive_fixture();
  NSString *outputRoot = temporary_root(@"z7-broker-export-cancel");
  NSString *destinationPath = [outputRoot stringByAppendingPathComponent:@"secret.txt"];

  BrokerCallbackRecorder *callback = [[BrokerCallbackRecorder alloc] init];
  BrokerService *service = [[BrokerService alloc] initWithCallback:callback];
  Z7BrokerTestingResetTaskRecordCounters();

  Z7BrokerQuickLookBatchExportItem *item =
      [[Z7BrokerQuickLookBatchExportItem alloc] initWithEntryPath:@"secret.txt"
                                                  destinationPath:destinationPath
                                                       listedSize:11
                                                        recursive:NO
                                                 entryIsDirectory:NO];
  Z7BrokerQuickLookBatchExportRequest *request =
      [[Z7BrokerQuickLookBatchExportRequest alloc] initWithRequestID:@"cancel-export"
                                                         archivePath:archivePath
                                                     archiveTypeHint:@"7z"
                                                nestedArchiveEntries:@[]
                                                               items:@[ item ]];
  [service batchExportWithRequest:request];

  expect_true(dispatch_semaphore_wait(
                  callback.passwordPromptSemaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) == 0,
              @"BrokerService encrypted export should request a password");
  expect_true(callback.passwordPrompt != nil,
              @"BrokerService should record password prompt before invalidation");
  expect_true(Z7BrokerTestingTaskCancelAndReleaseCount() == 0,
              @"BrokerService should not cancel the export before invalidation");

  [service invalidate];

  expect_true(Z7BrokerTestingTaskCancelAndReleaseCount() == 1,
              @"BrokerService invalidation should cancel and release the active Quick Look export task");
  expect_true(dispatch_semaphore_wait(
                  callback.batchResultSemaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC)) != 0,
              @"BrokerService invalidation should not report an export result after teardown");
  expect_true(![[NSFileManager defaultManager] fileExistsAtPath:destinationPath],
              @"Canceled encrypted export should not leave the destination file behind");
}

static void expect_menu_has_add_and_crc(Z7BrokerMenuPlan *plan, NSString *label) {
  expect_true(plan.ok,
              [NSString stringWithFormat:@"%@ menu plan should be ok: %@",
                                         label,
                                         plan.errorMessage ?: @""]);
  expect_true(plan.menuVisible,
              [NSString stringWithFormat:@"%@ menu should be visible", label]);
  expect_true(menu_plan_has_action(plan, @"add_to_archive"),
              [NSString stringWithFormat:@"%@ should expose Add to Archive", label]);
  expect_true(menu_plan_has_action(plan, @"add_to_7z"),
              [NSString stringWithFormat:@"%@ should expose Add to 7z", label]);
  expect_true(menu_plan_has_action(plan, @"add_to_zip"),
              [NSString stringWithFormat:@"%@ should expose Add to zip", label]);
  expect_true(menu_plan_has_action(plan, @"crc_sha_menu"),
              [NSString stringWithFormat:@"%@ should expose CRC/SHA menu", label]);
  expect_true(menu_plan_has_action(plan, @"crc32"),
              [NSString stringWithFormat:@"%@ should expose CRC-32", label]);
  expect_true(menu_plan_has_action(plan, @"sha256"),
              [NSString stringWithFormat:@"%@ should expose SHA-256", label]);
  expect_true(menu_plan_has_action(plan, @"xxh64"),
              [NSString stringWithFormat:@"%@ should expose XXH64", label]);
  expect_true(menu_plan_has_action(plan, @"md5"),
              [NSString stringWithFormat:@"%@ should expose MD5", label]);
  expect_true(menu_plan_has_action(plan, @"blake2sp"),
              [NSString stringWithFormat:@"%@ should expose BLAKE2sp", label]);
  expect_true(menu_plan_has_action(plan, @"generate_sha256"),
              [NSString stringWithFormat:@"%@ should expose SHA-256 generation", label]);
  expect_true(menu_plan_has_action(plan, @"checksum_test"),
              [NSString stringWithFormat:@"%@ should expose checksum test", label]);
}

static void expect_menu_hides_extract_group(Z7BrokerMenuPlan *plan, NSString *label) {
  expect_true(!menu_plan_has_action(plan, @"extract_files"),
              [NSString stringWithFormat:@"%@ should hide Extract Files", label]);
  expect_true(!menu_plan_has_action(plan, @"extract_here"),
              [NSString stringWithFormat:@"%@ should hide Extract Here", label]);
  expect_true(!menu_plan_has_action(plan, @"extract_to"),
              [NSString stringWithFormat:@"%@ should hide Extract To", label]);
  expect_true(!menu_plan_has_action(plan, @"test_archive"),
              [NSString stringWithFormat:@"%@ should hide Test Archive", label]);
}

static void test_broker_service_nil_locale_uses_7zfm_language_setting(void) {
  NSString *root = temporary_root(@"z7-broker-menu-locale");
  NSString *archive = [root stringByAppendingPathComponent:@"payload.7z"];
  write_text_file(archive, @"archive-like");

  BrokerCallbackRecorder *callback = [[BrokerCallbackRecorder alloc] init];
  BrokerService *service = [[BrokerService alloc] initWithCallback:callback];

  write_portable_7zfm_language(@"zh-cn");
  Z7BrokerMenuPlan *zhPlan = fetch_menu_plan_with_locale(service, @[ archive ], nil);
  expect_true(zhPlan.ok,
              [NSString stringWithFormat:@"zh menu plan should be ok: %@",
                                         zhPlan.errorMessage ?: @""]);
  expect_true([menu_plan_action_title(zhPlan, @"open") isEqualToString:@"打开"],
              @"nil locale should use 7zFM Lang for menu title");
  expect_true([menu_plan_action_title(zhPlan, @"extract_files") isEqualToString:@"解压文件..."],
              @"nil locale should localize extract title from 7zFM Lang");

  write_portable_7zfm_language(@"-");
  Z7BrokerMenuPlan *enPlan = fetch_menu_plan_with_locale(service, @[ archive ], nil);
  expect_true(enPlan.ok,
              [NSString stringWithFormat:@"English menu plan should be ok: %@",
                                         enPlan.errorMessage ?: @""]);
  expect_true([menu_plan_action_title(enPlan, @"open") isEqualToString:@"Open"],
              @"default 7zFM Lang marker should fall back to English");
  expect_true([menu_plan_action_title(enPlan, @"extract_files") isEqualToString:@"Extract Files..."],
              @"default 7zFM Lang marker should keep extract title English");

  [service invalidate];
}

static void test_broker_service_menu_plan_mixed_selection_keeps_crc_and_add(void) {
  NSString *root = temporary_root(@"z7-broker-menu-mixed");
  NSString *archive = [root stringByAppendingPathComponent:@"payload.7z"];
  NSString *text = [root stringByAppendingPathComponent:@"notes.txt"];
  NSString *directory = [root stringByAppendingPathComponent:@"docs"];
  write_text_file(archive, @"archive-like");
  write_text_file(text, @"notes");
  ensure_directory(directory);

  BrokerCallbackRecorder *callback = [[BrokerCallbackRecorder alloc] init];
  BrokerService *service = [[BrokerService alloc] initWithCallback:callback];

  Z7BrokerMenuPlan *archiveAndText =
      fetch_menu_plan(service, @[ archive, text ]);
  expect_menu_has_add_and_crc(archiveAndText, @"archive+text selection");
  expect_menu_hides_extract_group(archiveAndText, @"archive+text selection");

  Z7BrokerMenuPlan *archiveAndDirectory =
      fetch_menu_plan(service, @[ archive, directory ]);
  expect_menu_has_add_and_crc(archiveAndDirectory, @"archive+directory selection");
  expect_menu_hides_extract_group(archiveAndDirectory, @"archive+directory selection");

  Z7BrokerMenuPlan *fileAndDirectory =
      fetch_menu_plan(service, @[ text, directory ]);
  expect_menu_has_add_and_crc(fileAndDirectory, @"file+directory selection");
  expect_menu_hides_extract_group(fileAndDirectory, @"file+directory selection");

  Z7BrokerMenuPlan *archiveFileAndDirectory =
      fetch_menu_plan(service, @[ archive, text, directory ]);
  expect_menu_has_add_and_crc(archiveFileAndDirectory,
                              @"archive+file+directory selection");
  expect_menu_hides_extract_group(archiveFileAndDirectory,
                                  @"archive+file+directory selection");

  [service invalidate];
}

static void test_broker_service_invalidated_menu_plan_reports_error(void) {
  BrokerCallbackRecorder *callback = [[BrokerCallbackRecorder alloc] init];
  BrokerService *service = [[BrokerService alloc] initWithCallback:callback];
  [service invalidate];

  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  __block Z7BrokerMenuPlan *plan = nil;
  [service fetchMenuPlanWithPaths:@[ @"/tmp/example.7z" ]
                            locale:nil
                             reply:^(Z7BrokerMenuPlan *replyPlan) {
                               plan = replyPlan;
                               dispatch_semaphore_signal(semaphore);
                             }];

  expect_true(dispatch_semaphore_wait(
                  semaphore,
                  dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0,
              @"BrokerService invalidated menu plan timed out");
  expect_true(plan != nil, @"BrokerService invalidated menu plan should reply");
  expect_true(!plan.ok, @"BrokerService invalidated menu plan should not be ok");
  expect_true(!plan.menuVisible,
              @"BrokerService invalidated menu plan should not be visible");
  expect_true(plan.actions.count == 0,
              @"BrokerService invalidated menu plan should not expose actions");
}

static void test_broker_service_crc_special_actions_dispatch_task_payloads(void) {
  NSString *root = temporary_root(@"z7-broker-crc-special");
  NSString *text = [root stringByAppendingPathComponent:@"notes.txt"];
  write_text_file(text, @"checksum-source");

  BrokerCallbackRecorder *callback = [[BrokerCallbackRecorder alloc] init];
  BrokerService *service = [[BrokerService alloc] initWithCallback:callback];

  setenv("Z7_FAKE_TRACKER_CLAIM_TASK_IPC", "1", 1);

  NSString *generateLog = [root stringByAppendingPathComponent:@"generate.json"];
  setenv("Z7_FAKE_TRACKER_LOG", generateLog.fileSystemRepresentation, 1);
  Z7BrokerActionResult *generateResult =
      run_menu_action(service, @"generate_sha256", @[ text ]);
  expect_true(generateResult.ok,
              [NSString stringWithFormat:@"generate_sha256 should dispatch successfully: status=%ld error=%@",
                                         (long)generateResult.status,
                                         generateResult.errorMessage ?: @""]);
  NSDictionary *generatePayload = task_payload_from_fake_log(generateLog);
  expect_true([generatePayload[@"command"] isEqual:@"add"],
              @"generate_sha256 should use add/hash file generation");
  NSDictionary *add = generatePayload[@"add"];
  expect_true([add isKindOfClass:[NSDictionary class]],
              @"generate_sha256 should include add payload");
  expect_true([add[@"archive_type"] isEqual:@"hash"],
              @"generate_sha256 should request hash archive type");
  expect_true([add[@"archive_path"] hasSuffix:@"notes.txt.sha256"],
              @"generate_sha256 should write a .sha256 sidecar");
  expect_true([add[@"input_paths"] containsObject:text],
              @"generate_sha256 should pass selected input path");

  NSString *checksumLog = [root stringByAppendingPathComponent:@"checksum.json"];
  setenv("Z7_FAKE_TRACKER_LOG", checksumLog.fileSystemRepresentation, 1);
  Z7BrokerActionResult *checksumResult =
      run_menu_action(service, @"checksum_test", @[ text ]);
  expect_true(checksumResult.ok,
              [NSString stringWithFormat:@"checksum_test should dispatch successfully: status=%ld error=%@",
                                         (long)checksumResult.status,
                                         checksumResult.errorMessage ?: @""]);
  NSDictionary *checksumPayload = task_payload_from_fake_log(checksumLog);
  expect_true([checksumPayload[@"command"] isEqual:@"cli"],
              @"checksum_test should use 7zG CLI hash-archive test mode");
  NSDictionary *cli = checksumPayload[@"cli"];
  expect_true([cli isKindOfClass:[NSDictionary class]],
              @"checksum_test should include CLI payload");
  NSArray *argv = cli[@"argv"];
  expect_true([argv isKindOfClass:[NSArray class]],
              @"checksum_test argv should be present");
  expect_true(argv.count == 3,
              @"checksum_test argv should contain command, type, path");
  expect_true([argv[0] isEqual:@"t"], @"checksum_test should use test command");
  expect_true([argv[1] isEqual:@"-thash"],
              @"checksum_test should force hash archive type");
  expect_true([argv[2] isEqual:text],
              @"checksum_test should pass selected checksum file");
  expect_true([cli[@"working_dir"] isEqual:root],
              @"checksum_test should preserve the selection base folder");

  [service invalidate];
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
    run_test(@"broker_service_quicklook_list_delivers_callback_result", argc, argv, ^{
      test_broker_service_quicklook_list_delivers_callback_result();
    });
    run_test(@"broker_service_invalidation_cancels_inflight_quicklook_export", argc, argv, ^{
      test_broker_service_invalidation_cancels_inflight_quicklook_export();
    });
    run_test(@"broker_service_menu_plan_mixed_selection_keeps_crc_and_add", argc, argv, ^{
      test_broker_service_menu_plan_mixed_selection_keeps_crc_and_add();
    });
    run_test(@"broker_service_nil_locale_uses_7zfm_language_setting", argc, argv, ^{
      test_broker_service_nil_locale_uses_7zfm_language_setting();
    });
    run_test(@"broker_service_crc_special_actions_dispatch_task_payloads", argc, argv, ^{
      test_broker_service_crc_special_actions_dispatch_task_payloads();
    });
    run_test(@"broker_service_invalidated_menu_plan_reports_error", argc, argv, ^{
      test_broker_service_invalidated_menu_plan_reports_error();
    });
  }
  fflush(NULL);
  _Exit(0);
}
