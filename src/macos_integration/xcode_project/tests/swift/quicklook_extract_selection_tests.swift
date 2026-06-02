import AppKit
import Darwin
import Foundation

final class Z7BrokerPasswordPromptEvent {
  let promptID: String
  let archivePath: String
  let nestedChain: [String]
  let reasonKey: String

  init(promptID: String, archivePath: String, nestedChain: [String], reasonKey: String) {
    self.promptID = promptID
    self.archivePath = archivePath
    self.nestedChain = nestedChain
    self.reasonKey = reasonKey
  }
}

final class Z7BrokerQuickLookItem {
  let path: String
  let name: String
  let directory: Bool
  let size: UInt64
  let mtimeMsUtc: Int64
  let archiveLike: Bool

  init(path: String,
       name: String,
       directory: Bool,
       size: UInt64,
       mtimeMsUtc: Int64,
       archiveLike: Bool)
  {
    self.path = path
    self.name = name
    self.directory = directory
    self.size = size
    self.mtimeMsUtc = mtimeMsUtc
    self.archiveLike = archiveLike
  }
}

final class Z7BrokerQuickLookListResult {
  let ok: Bool
  let status: Int
  let errorMessage: String?
  let items: [Z7BrokerQuickLookItem]

  init(ok: Bool, status: Int, errorMessage: String?, items: [Z7BrokerQuickLookItem]) {
    self.ok = ok
    self.status = status
    self.errorMessage = errorMessage
    self.items = items
  }
}

final class Z7BrokerQuickLookBatchExportItem {
  let entryPath: String
  let destinationPath: String
  let listedSize: UInt64
  let recursive: Bool
  let entryIsDirectory: Bool

  init(entryPath: String,
       destinationPath: String,
       listedSize: UInt64,
       recursive: Bool,
       entryIsDirectory: Bool)
  {
    self.entryPath = entryPath
    self.destinationPath = destinationPath
    self.listedSize = listedSize
    self.recursive = recursive
    self.entryIsDirectory = entryIsDirectory
  }
}

final class Z7BrokerQuickLookBatchExportProgress {
  let completedItemCount: Int
  let totalItemCount: Int
  let currentItemIndex: Int
  let currentEntryPath: String
  let currentDestinationPath: String
  let currentPercent: Int
  let totalsKnown: Bool
  let totalBytes: UInt64
  let completedBytes: UInt64
  let currentPath: String?
  let message: String?

  init(completedItemCount: Int,
       totalItemCount: Int,
       currentItemIndex: Int,
       currentEntryPath: String,
       currentDestinationPath: String,
       currentPercent: Int,
       totalsKnown: Bool,
       totalBytes: UInt64,
       completedBytes: UInt64,
       currentPath: String?,
       message: String?)
  {
    self.completedItemCount = completedItemCount
    self.totalItemCount = totalItemCount
    self.currentItemIndex = currentItemIndex
    self.currentEntryPath = currentEntryPath
    self.currentDestinationPath = currentDestinationPath
    self.currentPercent = currentPercent
    self.totalsKnown = totalsKnown
    self.totalBytes = totalBytes
    self.completedBytes = completedBytes
    self.currentPath = currentPath
    self.message = message
  }
}

final class Z7BrokerQuickLookBatchExportResult {
  let ok: Bool
  let status: Int
  let errorMessage: String?
  let completedItemCount: Int
  let totalItemCount: Int
  let failedItemIndex: Int
  let failedEntryPath: String?
  let failedDestinationPath: String?

  init(ok: Bool,
       status: Int = 0,
       errorMessage: String?,
       completedItemCount: Int,
       totalItemCount: Int,
       failedItemIndex: Int,
       failedEntryPath: String?,
       failedDestinationPath: String?)
  {
    self.ok = ok
    self.status = status
    self.errorMessage = errorMessage
    self.completedItemCount = completedItemCount
    self.totalItemCount = totalItemCount
    self.failedItemIndex = failedItemIndex
    self.failedEntryPath = failedEntryPath
    self.failedDestinationPath = failedDestinationPath
  }
}

final class BrokerClient {
  static let shared = BrokerClient()
  static var nextListResult: Z7BrokerQuickLookListResult?
  static var nextListPasswordPromptEvent: Z7BrokerPasswordPromptEvent?
  static var nextListResultAfterPasswordProvided: Z7BrokerQuickLookListResult?
  static var pendingListCompletion: ((Z7BrokerQuickLookListResult) -> Void)?
  static var nextBatchExportProgress: Z7BrokerQuickLookBatchExportProgress?
  static var nextBatchExportPasswordPromptEvent: Z7BrokerPasswordPromptEvent?
  static var nextBatchExportResult: Z7BrokerQuickLookBatchExportResult?
  static var nextBatchExportResultAfterPasswordProvided: Z7BrokerQuickLookBatchExportResult?
  static var pendingBatchExportCompletion: ((Z7BrokerQuickLookBatchExportResult) -> Void)?
  static var lastListRequestID: String?
  static var lastBatchExportRequestID: String?
  static var lastProvidedPromptID: String?
  static var lastProvidedPassword: String?
  static var lastCanceledPromptID: String?
  static var passwordPromptHandler: ((Z7BrokerPasswordPromptEvent) -> Void)?

  static func resetTestingBehavior() {
    nextListResult = nil
    nextListPasswordPromptEvent = nil
    nextListResultAfterPasswordProvided = nil
    pendingListCompletion = nil
    nextBatchExportProgress = nil
    nextBatchExportPasswordPromptEvent = nil
    nextBatchExportResult = nil
    nextBatchExportResultAfterPasswordProvided = nil
    pendingBatchExportCompletion = nil
    lastListRequestID = nil
    lastBatchExportRequestID = nil
    lastProvidedPromptID = nil
    lastProvidedPassword = nil
    lastCanceledPromptID = nil
    passwordPromptHandler = nil
  }

  static func sendPasswordPrompt(_ event: Z7BrokerPasswordPromptEvent) {
    passwordPromptHandler?(event)
  }

  static func finishPendingList(_ result: Z7BrokerQuickLookListResult) {
    let completion = pendingListCompletion
    pendingListCompletion = nil
    nextListResultAfterPasswordProvided = nil
    completion?(result)
  }

  static func finishPendingBatchExport(_ result: Z7BrokerQuickLookBatchExportResult) {
    let completion = pendingBatchExportCompletion
    pendingBatchExportCompletion = nil
    nextBatchExportResultAfterPasswordProvided = nil
    completion?(result)
  }

  func setPasswordPromptHandler(_ handler: ((Z7BrokerPasswordPromptEvent) -> Void)?) {
    Self.passwordPromptHandler = handler
  }

  func invalidate() {}

  func cancelRequest(withID requestID: String) {
    _ = requestID
  }

  func providePassword(promptID: String, password: String) {
    Self.lastProvidedPromptID = promptID
    Self.lastProvidedPassword = password
    if let result = Self.nextListResultAfterPasswordProvided {
      Self.finishPendingList(result)
    }
    if let result = Self.nextBatchExportResultAfterPasswordProvided {
      Self.finishPendingBatchExport(result)
    }
  }

  func cancelPasswordPrompt(promptID: String) {
    Self.lastCanceledPromptID = promptID
  }

  func list(archivePath: String,
            virtualDir: String,
            archiveTypeHint: String,
            nestedArchiveEntries: [String],
            requestID: String,
            completion: @escaping (Z7BrokerQuickLookListResult) -> Void)
  {
    _ = (archivePath, virtualDir, archiveTypeHint, nestedArchiveEntries)
    Self.lastListRequestID = requestID
    if let event = Self.nextListPasswordPromptEvent {
      Self.nextListPasswordPromptEvent = nil
      Self.sendPasswordPrompt(event)
    }
    if Self.nextListResultAfterPasswordProvided != nil {
      Self.pendingListCompletion = completion
      return
    }
    if let result = Self.nextListResult {
      Self.nextListResult = nil
      completion(result)
    }
  }

  func batchExport(archivePath: String,
                   archiveTypeHint: String,
                   nestedArchiveEntries: [String],
                   items: [Z7BrokerQuickLookBatchExportItem],
                   requestID: String,
                   progress: ((Z7BrokerQuickLookBatchExportProgress) -> Void)?,
                   completion: @escaping (Z7BrokerQuickLookBatchExportResult) -> Void)
  {
    _ = (archivePath, archiveTypeHint, nestedArchiveEntries, items)
    Self.lastBatchExportRequestID = requestID
    if let event = Self.nextBatchExportPasswordPromptEvent {
      Self.nextBatchExportPasswordPromptEvent = nil
      Self.sendPasswordPrompt(event)
    }
    if Self.nextBatchExportResultAfterPasswordProvided != nil {
      Self.pendingBatchExportCompletion = completion
      return
    }
    if let update = Self.nextBatchExportProgress {
      Self.nextBatchExportProgress = nil
      progress?(update)
    }
    if let result = Self.nextBatchExportResult {
      Self.nextBatchExportResult = nil
      completion(result)
    }
  }
}

struct TestFailure: Error, CustomStringConvertible {
  let description: String
}

private struct ExtractSelectionTestCase {
  let name: String
  let body: @MainActor () throws -> Void
}

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
  if !condition() {
    throw TestFailure(description: message)
  }
}

private func temporarySettingsRoot() throws -> URL {
  let root = FileManager.default.temporaryDirectory
    .appendingPathComponent("z7-quicklook-locale-\(UUID().uuidString)", isDirectory: true)
  try FileManager.default.createDirectory(
    at: root,
    withIntermediateDirectories: true,
    attributes: nil)
  return root
}

private func writeJSONObject(_ object: [String: Any], to url: URL) throws {
  let data = try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted])
  try FileManager.default.createDirectory(
    at: url.deletingLastPathComponent(),
    withIntermediateDirectories: true,
    attributes: nil)
  try data.write(to: url, options: .atomic)
}

private func writeQuickLookStrings(root: URL, localeKey: String, back: String) throws {
  let path = root
    .appendingPathComponent("i18n", isDirectory: true)
    .appendingPathComponent("z7_strings_\(localeKey).json")
  try writeJSONObject(["quicklook": ["back": back]], to: path)
}

private func item(path: String,
                  name: String,
                  directory: Bool,
                  size: UInt64 = 0,
                  archiveLike: Bool = false) -> QuickLookItem {
  QuickLookItem(
    path: path,
    name: name,
    isDirectory: directory,
    isArchiveLike: archiveLike,
    isSyntheticArchiveRoot: false,
    size: size,
    mtimeMsUtc: 0)
}

private func successResult(count: Int) -> Z7BrokerQuickLookBatchExportResult {
  Z7BrokerQuickLookBatchExportResult(
    ok: true,
    errorMessage: nil,
    completedItemCount: count,
    totalItemCount: count,
    failedItemIndex: -1,
    failedEntryPath: nil,
    failedDestinationPath: nil)
}

private func successListResult(_ items: [Z7BrokerQuickLookItem]) -> Z7BrokerQuickLookListResult {
  Z7BrokerQuickLookListResult(ok: true, status: 0, errorMessage: nil, items: items)
}

private func passwordRequiredListResult() -> Z7BrokerQuickLookListResult {
  Z7BrokerQuickLookListResult(
    ok: false,
    status: 6,
    errorMessage: "Password required",
    items: [])
}

private func passwordRequiredExportResult(totalItemCount: Int = 1) -> Z7BrokerQuickLookBatchExportResult {
  Z7BrokerQuickLookBatchExportResult(
    ok: false,
    status: 6,
    errorMessage: "Password required",
    completedItemCount: 0,
    totalItemCount: totalItemCount,
    failedItemIndex: 0,
    failedEntryPath: "notes.txt",
    failedDestinationPath: "/tmp/archive-folder/notes.txt")
}

private func passwordPromptEvent(promptID: String = "prompt-1",
                                 archivePath: String = "/tmp/secret.7z",
                                 nestedChain: [String] = [],
                                 reasonKey: String = "password_required")
  -> Z7BrokerPasswordPromptEvent
{
  Z7BrokerPasswordPromptEvent(
    promptID: promptID,
    archivePath: archivePath,
    nestedChain: nestedChain,
    reasonKey: reasonKey)
}

@MainActor
private func makeController(items: [QuickLookItem],
                            selected: IndexSet,
                            archivePath: String = "/tmp/archive-folder/demo.7z")
  -> QuickLookPreviewController
{
  let controller = QuickLookPreviewController()
  controller.archivePath = archivePath
  controller.archiveTypeHint = "7z"
  controller.archiveBaseName = "demo"
  controller.virtualDir = ""
  controller.nestedStack = []
  controller.items = items
  controller.selectedItemIndexes = selected
  controller.rowModels = []
  controller.viewModel.mode = .browse
  controller.updatePresentation()
  return controller
}

private func temporaryArchiveURL(named archiveName: String = "demo.7z") throws -> (root: URL, archive: URL) {
  let root = FileManager.default.temporaryDirectory
    .appendingPathComponent("z7-quicklook-code-flow-\(UUID().uuidString)", isDirectory: true)
  try FileManager.default.createDirectory(
    at: root,
    withIntermediateDirectories: true,
    attributes: nil)
  let archive = root.appendingPathComponent(archiveName, isDirectory: false)
  try Data("quicklook test archive".utf8).write(to: archive, options: .atomic)
  return (root, archive)
}

@MainActor
private func prepareControllerForArchive(_ controller: QuickLookPreviewController,
                                         archiveURL: URL) throws {
  var didComplete = false
  var previewError: Error?
  controller.preparePreviewOfFile(at: archiveURL) { error in
    didComplete = true
    previewError = error
  }
  try expect(didComplete, "preparePreviewOfFile should complete for the synthetic root")
  if let previewError {
    throw TestFailure(description: "preparePreviewOfFile failed: \(previewError)")
  }
}

@MainActor
private func enterSyntheticRootViaPrimaryAction(_ controller: QuickLookPreviewController) throws {
  try expect(controller.items.count == 1, "prepared preview should start with one synthetic root row")
  try expect(controller.items[0].isSyntheticArchiveRoot,
             "prepared preview should start at the synthetic archive root")
  controller.setSelectedItemIndexes(IndexSet(integer: 0))
  controller.performPrimarySelectionAction()
}

@MainActor
private func captureExtractedItems(_ controller: QuickLookPreviewController)
  -> () throws -> [QuickLookPreviewController.SelectedExportItem]
{
  var captured: [QuickLookPreviewController.SelectedExportItem]?
  controller.z7TestingExportItemsHandler = { items, completion in
    captured = items
    completion(.success(successResult(count: items.count)))
  }
  controller.onExtractSelected()
  return {
    guard let captured else {
      throw TestFailure(description: "extract action did not call export handler")
    }
    return captured
  }
}

private func expectExport(_ export: QuickLookPreviewController.SelectedExportItem,
                          entryPath: String,
                          destinationPath: String,
                          recursive: Bool,
                          entryIsDirectory: Bool,
                          listedSize: UInt64) throws {
  try expect(export.entryPath == entryPath, "unexpected entry path for \(entryPath)")
  try expect(export.destinationPath == destinationPath, "unexpected destination path for \(entryPath)")
  try expect(export.recursive == recursive, "unexpected recursive flag for \(entryPath)")
  try expect(export.entryIsDirectory == entryIsDirectory, "unexpected directory flag for \(entryPath)")
  try expect(export.listedSize == listedSize, "unexpected listed size for \(entryPath)")
}

@MainActor
private func expectExportSuccess(_ controller: QuickLookPreviewController) throws {
  guard case .exportSuccess(let result) = controller.viewModel.mode else {
    throw TestFailure(description: "extract action should finish in exportSuccess mode")
  }
  try expect(result.primaryButtonEnabled, "export success should enable the primary action")
}

@MainActor
private func currentPasswordPrompt(_ controller: QuickLookPreviewController)
  throws -> QuickLookPasswordPromptModel
{
  guard case .password(let prompt) = controller.viewModel.inlineOverlay else {
    throw TestFailure(description: "expected password prompt overlay")
  }
  return prompt
}

@MainActor
private func expectNoInlineOverlay(_ controller: QuickLookPreviewController) throws {
  if case .none = controller.viewModel.inlineOverlay {
    return
  }
  throw TestFailure(description: "expected no inline overlay")
}

private func expectActivePasswordPrompt(_ controller: QuickLookPreviewController,
                                        promptID: String,
                                        reasonKey: String) throws {
  guard case .showing(let activePromptID, _, _, let activeReasonKey) = controller.passwordPromptState else {
    throw TestFailure(description: "expected active password prompt")
  }
  try expect(activePromptID == promptID, "password prompt should keep the same prompt id")
  try expect(activeReasonKey == reasonKey, "password prompt should preserve expected reason")
}

@main
enum QuickLookExtractSelectionTestMain {
  @MainActor
  static func main() throws {
    let tests = [
      ExtractSelectionTestCase(
        name: "quicklook_settings_root_resolver_uses_real_home_sources_only",
        body: {
          let posixRoot = QuickLookRealUserHome.z7TestingSettingsRootURL(
            posixHome: "/Users/real",
            environmentHome: "/Users/other")
          try expect(
            posixRoot?.path == "/Users/real/.config/7zqt",
            "POSIX user home should win over HOME")

          let homeFallback = QuickLookRealUserHome.z7TestingSettingsRootURL(
            posixHome: nil,
            environmentHome: "/Users/from-home")
          try expect(
            homeFallback?.path == "/Users/from-home/.config/7zqt",
            "HOME should be used when POSIX home is unavailable")

          let posixBeatsSandboxHome = QuickLookRealUserHome.z7TestingSettingsRootURL(
            posixHome: "/Users/real",
            environmentHome: "/Users/real/Library/Containers/app.sevenzip.extension/Data")
          try expect(
            posixBeatsSandboxHome?.path == "/Users/real/.config/7zqt",
            "sandbox HOME should not replace POSIX user home")

          let rejectedSandboxHome = QuickLookRealUserHome.z7TestingSettingsRootURL(
            posixHome: nil,
            environmentHome: "/Users/real/Library/Containers/app.sevenzip.extension/Data")
          try expect(
            rejectedSandboxHome == nil,
            "sandbox HOME should not be treated as a real user home")

          let rejectedRelativeHome = QuickLookRealUserHome.z7TestingSettingsRootURL(
            posixHome: nil,
            environmentHome: "relative/home")
          try expect(
            rejectedRelativeHome == nil,
            "relative HOME should not be treated as a real user home")
        }),
      ExtractSelectionTestCase(
        name: "quicklook_localization_ignores_legacy_snapshot_and_reads_settings",
        body: {
          let root = try temporarySettingsRoot()
          defer { try? FileManager.default.removeItem(at: root) }
          try writeJSONObject(
            ["locale_preferred": "zh-cn"],
            to: root.appendingPathComponent("macos_integration.json"))
          try writeJSONObject(
            ["apps": ["7zFM": ["Lang": "-"]]],
            to: root.appendingPathComponent("settings.json"))
          try writeQuickLookStrings(root: root, localeKey: "en", back: "Back")
          try writeQuickLookStrings(root: root, localeKey: "zh-CN", back: "返回")

          let localeKey = QuickLookLocalization.preferredLocaleKey(settingsRootURLs: [root])
          try expect(localeKey == "en", "legacy macOS integration snapshot should be ignored")
          let table = QuickLookLocalization.loadTable(localeKey: localeKey, resourceRootURL: root)
          try expect(table["quicklook.back"] == "Back", "English table should load from settings fallback")
        }),
      ExtractSelectionTestCase(
        name: "quicklook_localization_reads_7zfm_lang_and_defaults_to_english",
        body: {
          let root = try temporarySettingsRoot()
          defer { try? FileManager.default.removeItem(at: root) }
          try writeJSONObject(
            ["apps": ["7zFM": ["Lang": "zh-CN"]]],
            to: root.appendingPathComponent("settings.json"))
          try expect(
            QuickLookLocalization.preferredLocaleKey(settingsRootURLs: [root]) == "zh-CN",
            "portable 7zFM Lang should select Quick Look locale")

          try writeJSONObject(
            ["apps": ["7zFM": ["Lang": "-"]]],
            to: root.appendingPathComponent("settings.json"))
          try expect(
            QuickLookLocalization.preferredLocaleKey(settingsRootURLs: [root]) == "en",
            "default 7zFM Lang marker should use English")

          try Data("{".utf8).write(to: root.appendingPathComponent("settings.json"))
          try expect(
            QuickLookLocalization.preferredLocaleKey(settingsRootURLs: [root]) == "en",
            "invalid settings JSON should use English")

          try FileManager.default.removeItem(at: root.appendingPathComponent("settings.json"))
          try expect(
            QuickLookLocalization.preferredLocaleKey(settingsRootURLs: [root]) == "en",
            "missing 7zFM Lang should use English")
          try expect(
            !FileManager.default.fileExists(atPath: root.appendingPathComponent("settings.json").path),
            "Quick Look localization should not create missing settings.json")
        }),
      ExtractSelectionTestCase(
        name: "extract_button_callback_invokes_controller_action",
        body: {
          var tapCount = 0
          let view = QuickLookPreviewRootView(
            model: QuickLookPreviewViewModel(),
            collectionProxy: QuickLookCollectionProxy(),
            onBack: {},
            onExtractSelected: { tapCount += 1 },
            onSelectionChange: { _ in },
            onPrimaryAction: {},
            onPasswordConfirm: {},
            onPasswordCancel: {},
            onInlinePrimaryAction: {},
            onExportPrimaryAction: {},
            onExportSecondaryAction: {})

          view.z7TestingTapExtractSelected()
          try expect(tapCount == 1, "testing tap should invoke the same extract callback used by the button")
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_confirm_callback_invokes_controller_action",
        body: {
          var tapCount = 0
          let view = QuickLookPreviewRootView(
            model: QuickLookPreviewViewModel(),
            collectionProxy: QuickLookCollectionProxy(),
            onBack: {},
            onExtractSelected: {},
            onSelectionChange: { _ in },
            onPrimaryAction: {},
            onPasswordConfirm: { tapCount += 1 },
            onPasswordCancel: {},
            onInlinePrimaryAction: {},
            onExportPrimaryAction: {},
            onExportSecondaryAction: {})

          view.z7TestingTapPasswordConfirm()
          try expect(tapCount == 1, "testing tap should invoke the password confirm callback")
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_handler_is_installed_before_view_did_load",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = QuickLookPreviewController()
          controller.z7TestingClipboardPasswordProvider = { "" }
          BrokerClient.sendPasswordPrompt(passwordPromptEvent(promptID: "prompt-early"))

          try expectActivePasswordPrompt(
            controller,
            promptID: "prompt-early",
            reasonKey: "password_required")
          let prompt = try currentPasswordPrompt(controller)
          try expect(prompt.promptID == "prompt-early",
                     "early broker password prompt should be visible before viewDidLoad")
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_confirms_clipboard_and_submits_untrimmed_text",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          controller.z7TestingClipboardPasswordProvider = { "  secret\n" }
          controller.showPasswordPrompt(
            promptID: "prompt-1",
            archivePath: "/tmp/secret.7z",
            nestedChain: [],
            reasonKey: "password_required")

          var prompt = try currentPasswordPrompt(controller)
          try expect(prompt.clipboardText == "  secret\n",
                     "password prompt should immediately show the clipboard text")
          try expect(prompt.clipboardSourceText == QuickLookLocalization.text("quicklook.password_clipboard_source"),
                     "password prompt should explain that the preview comes from the clipboard")
          try expect(prompt.confirmTitle == QuickLookLocalization.text("quicklook.password_confirm"),
                     "password prompt should use the confirm action title")

          controller.confirmClipboardPasswordForActivePrompt()

          try expect(BrokerClient.lastProvidedPromptID == "prompt-1",
                     "clipboard password should be submitted for the active prompt")
          try expect(BrokerClient.lastProvidedPassword == "  secret\n",
                     "clipboard password should be submitted without trimming")
          prompt = try currentPasswordPrompt(controller)
          try expect(prompt.clipboardText == "  secret\n",
                     "submitted clipboard password should remain visible in the prompt")
          if case .idle = controller.passwordPromptState {
          } else {
            throw TestFailure(description: "password prompt state should be idle after submission")
          }
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_confirm_submits_empty_clipboard",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          controller.z7TestingClipboardPasswordProvider = { nil }
          controller.showPasswordPrompt(
            promptID: "prompt-empty",
            archivePath: "/tmp/secret.7z",
            nestedChain: [],
            reasonKey: "password_required")

          let prompt = try currentPasswordPrompt(controller)
          try expect(prompt.clipboardText.isEmpty,
                     "missing clipboard text should be shown as an empty clipboard preview")

          controller.confirmClipboardPasswordForActivePrompt()

          try expect(BrokerClient.lastProvidedPromptID == "prompt-empty",
                     "empty clipboard should still submit for the active prompt")
          try expect(BrokerClient.lastProvidedPassword == "",
                     "nil clipboard text should be submitted as an empty string")
          if case .idle = controller.passwordPromptState {
          } else {
            throw TestFailure(description: "empty clipboard submission should finish the active prompt")
          }
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_polling_refreshes_visible_clipboard_text",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          var clipboardValue: String? = "first password"
          let controller = makeController(items: [], selected: [])
          controller.z7TestingClipboardPasswordProvider = { clipboardValue }
          controller.showPasswordPrompt(
            promptID: "prompt-poll",
            archivePath: "/tmp/secret.7z",
            nestedChain: [],
            reasonKey: "password_required")

          var prompt = try currentPasswordPrompt(controller)
          try expect(prompt.clipboardText == "first password",
                     "prompt should show the clipboard text as soon as it appears")

          clipboardValue = "new password"
          controller.z7TestingRefreshPasswordClipboardPreview()

          prompt = try currentPasswordPrompt(controller)
          try expect(prompt.clipboardText == "new password",
                     "polling should refresh the visible clipboard text")

          controller.cancelActivePasswordPrompt()
          clipboardValue = "after cancel"
          controller.z7TestingRefreshPasswordClipboardPreview()
          try expectNoInlineOverlay(controller)
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_wrong_password_confirm_submits_latest_clipboard",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          controller.z7TestingClipboardPasswordProvider = { "new password" }
          controller.showPasswordPrompt(
            promptID: "prompt-retry",
            archivePath: "/tmp/secret.7z",
            nestedChain: [],
            reasonKey: "wrong_password")

          let prompt = try currentPasswordPrompt(controller)
          try expect(prompt.showsRetryHint,
                     "broker wrong-password prompt should keep current wrong-password presentation")
          try expect(prompt.clipboardText == "new password",
                     "wrong-password prompt should show the latest clipboard value")

          controller.confirmClipboardPasswordForActivePrompt()

          try expect(BrokerClient.lastProvidedPromptID == "prompt-retry",
                     "retry should submit for the same prompt id")
          try expect(BrokerClient.lastProvidedPassword == "new password",
                     "retry should submit the latest clipboard value")
        }),
      ExtractSelectionTestCase(
        name: "password_prompt_cancel_cancels_broker_prompt_and_hides_overlay",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          controller.z7TestingClipboardPasswordProvider = { "" }
          controller.showPasswordPrompt(
            promptID: "prompt-cancel",
            archivePath: "/tmp/secret.7z",
            nestedChain: [],
            reasonKey: "password_required")

          controller.cancelActivePasswordPrompt()

          try expect(BrokerClient.lastCanceledPromptID == "prompt-cancel",
                     "canceling the active prompt should cancel the broker prompt")
          if case .idle = controller.passwordPromptState {
          } else {
            throw TestFailure(description: "password prompt state should be idle after cancel")
          }
          if case .none = controller.viewModel.inlineOverlay {
          } else {
            throw TestFailure(description: "canceling the active prompt should hide the password overlay")
          }
        }),
      ExtractSelectionTestCase(
        name: "password_required_list_result_preserves_active_prompt_overlay",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          controller.z7TestingClipboardPasswordProvider = { "" }
          controller.virtualDir = "docs"
          BrokerClient.nextListPasswordPromptEvent = passwordPromptEvent(promptID: "prompt-list")
          BrokerClient.nextListResult = passwordRequiredListResult()

          var completed: Bool?
          controller.reloadCurrentDirectory { succeeded in
            completed = succeeded
          }

          try expect(completed == true,
                     "active password prompt should keep Quick Look preview alive")
          try expect(controller.virtualDir == "docs",
                     "active password prompt should not force navigation fallback")
          try expectActivePasswordPrompt(
            controller,
            promptID: "prompt-list",
            reasonKey: "password_required")
          let prompt = try currentPasswordPrompt(controller)
          try expect(prompt.promptID == "prompt-list",
                     "password-required list completion should not clear the prompt overlay")
        }),
      ExtractSelectionTestCase(
        name: "code_level_list_password_prompt_confirms_clipboard_and_finishes_listing",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let archiveFixture = try temporaryArchiveURL()
          defer { try? FileManager.default.removeItem(at: archiveFixture.root) }

          var showErrors = [String]()
          let controller = QuickLookPreviewController()
          controller.z7TestingClipboardPasswordProvider = { "list secret" }
          controller.z7TestingShowErrorHandler = { showErrors.append($0) }
          try prepareControllerForArchive(controller, archiveURL: archiveFixture.archive)

          BrokerClient.nextListPasswordPromptEvent = passwordPromptEvent(
            promptID: "prompt-list-flow",
            archivePath: archiveFixture.archive.path)
          BrokerClient.nextListResultAfterPasswordProvided = successListResult([
            Z7BrokerQuickLookItem(
              path: "payload.txt",
              name: "payload.txt",
              directory: false,
              size: 17,
              mtimeMsUtc: 0,
              archiveLike: false),
          ])

          try enterSyntheticRootViaPrimaryAction(controller)

          try expect(BrokerClient.pendingListCompletion != nil,
                     "list request should stay pending while the password prompt is active")
          try expect(controller.virtualDir == "demo",
                     "activating the synthetic root should enter the archive listing")
          try expect(controller.viewModel.progressState != nil,
                     "list should keep its loading state while waiting for the password")
          try expectActivePasswordPrompt(
            controller,
            promptID: "prompt-list-flow",
            reasonKey: "password_required")
          let prompt = try currentPasswordPrompt(controller)
          try expect(prompt.promptID == "prompt-list-flow",
                     "list password prompt should be visible before clipboard submission")
          try expect(showErrors.isEmpty,
                     "list password prompt should not be rendered as a normal error")

          controller.confirmClipboardPasswordForActivePrompt()

          try expect(BrokerClient.lastProvidedPromptID == "prompt-list-flow",
                     "list password flow should submit the active prompt id")
          try expect(BrokerClient.lastProvidedPassword == "list secret",
                     "list password flow should submit the clipboard password")
          try expect(BrokerClient.pendingListCompletion == nil,
                     "list completion should finish after password submission")
          try expect(controller.activeContexts.isEmpty,
                     "list context should be untracked after password-backed success")
          try expect(controller.viewModel.progressState == nil,
                     "successful list completion should clear the loading state")
          try expect(controller.items.map(\.name) == ["payload.txt"],
                     "password-backed list completion should populate archive rows")
          try expect(controller.viewModel.rows.map(\.title) == ["payload.txt"],
                     "password-backed list completion should update the visible row models")
          try expectNoInlineOverlay(controller)
          try expect(showErrors.isEmpty,
                     "successful password-backed list should never call showError")
        }),
      ExtractSelectionTestCase(
        name: "export_running_password_prompt_confirms_clipboard_and_submits",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(
            items: [item(path: "notes.txt", name: "notes.txt", directory: false, size: 12)],
            selected: IndexSet(integer: 0))
          controller.z7TestingClipboardPasswordProvider = { "export secret" }
          BrokerClient.nextBatchExportPasswordPromptEvent = passwordPromptEvent(promptID: "prompt-export")

          controller.onExtractSelected()

          try expect(BrokerClient.lastBatchExportRequestID?.isEmpty == false,
                     "export request should be started before waiting for password")
          if case .exportRunning = controller.viewModel.mode {
          } else {
            throw TestFailure(description: "password prompt should be shown while export is running")
          }
          try expectActivePasswordPrompt(
            controller,
            promptID: "prompt-export",
            reasonKey: "password_required")

          controller.confirmClipboardPasswordForActivePrompt()

          try expect(BrokerClient.lastProvidedPromptID == "prompt-export",
                     "export password prompt should submit to the broker")
          try expect(BrokerClient.lastProvidedPassword == "export secret",
                     "export password prompt should submit the clipboard password")
          controller.cancelAllActiveContexts()
        }),
      ExtractSelectionTestCase(
        name: "code_level_export_password_prompt_confirms_clipboard_and_finishes_export",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          var showErrors = [String]()
          let controller = makeController(
            items: [item(path: "notes.txt", name: "notes.txt", directory: false, size: 12)],
            selected: IndexSet(integer: 0))
          controller.z7TestingClipboardPasswordProvider = { "export secret" }
          controller.z7TestingShowErrorHandler = { showErrors.append($0) }
          BrokerClient.nextBatchExportPasswordPromptEvent = passwordPromptEvent(promptID: "prompt-export-flow")
          BrokerClient.nextBatchExportResultAfterPasswordProvided = successResult(count: 1)

          controller.onExtractSelected()

          try expect(BrokerClient.pendingBatchExportCompletion != nil,
                     "export request should stay pending while the password prompt is active")
          try expect(controller.extractSelectedBatchRunning,
                     "export batch should remain running while waiting for the password")
          if case .exportRunning = controller.viewModel.mode {
          } else {
            throw TestFailure(description: "export should stay in running mode while the password prompt is visible")
          }
          try expectActivePasswordPrompt(
            controller,
            promptID: "prompt-export-flow",
            reasonKey: "password_required")
          try expect(showErrors.isEmpty,
                     "export password prompt should not be rendered as a normal error")

          controller.confirmClipboardPasswordForActivePrompt()

          try expect(BrokerClient.lastProvidedPromptID == "prompt-export-flow",
                     "export password flow should submit the active prompt id")
          try expect(BrokerClient.lastProvidedPassword == "export secret",
                     "export password flow should submit the clipboard password")
          try expect(BrokerClient.pendingBatchExportCompletion == nil,
                     "export completion should finish after password submission")
          try expect(!controller.extractSelectedBatchRunning,
                     "successful export should reset the running guard")
          try expect(controller.activeContexts.isEmpty,
                     "export context should be untracked after password-backed success")
          try expectNoInlineOverlay(controller)
          guard case .exportSuccess(let result) = controller.viewModel.mode else {
            throw TestFailure(description: "password-backed export should finish in exportSuccess mode")
          }
          try expect(result.primaryButtonEnabled,
                     "password-backed export success should enable its primary action")
          try expect(showErrors.isEmpty,
                     "successful password-backed export should never call showError")
        }),
      ExtractSelectionTestCase(
        name: "code_level_cancel_list_and_export_password_prompts_do_not_show_error",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let archiveFixture = try temporaryArchiveURL()
          defer { try? FileManager.default.removeItem(at: archiveFixture.root) }

          var listShowErrors = [String]()
          let listController = QuickLookPreviewController()
          listController.z7TestingClipboardPasswordProvider = { "" }
          listController.z7TestingShowErrorHandler = { listShowErrors.append($0) }
          try prepareControllerForArchive(listController, archiveURL: archiveFixture.archive)
          BrokerClient.nextListPasswordPromptEvent = passwordPromptEvent(
            promptID: "prompt-list-cancel",
            archivePath: archiveFixture.archive.path)
          BrokerClient.nextListResultAfterPasswordProvided = successListResult([])

          try enterSyntheticRootViaPrimaryAction(listController)
          listController.cancelActivePasswordPrompt()

          try expect(BrokerClient.lastCanceledPromptID == "prompt-list-cancel",
                     "canceling a list password prompt should cancel the broker prompt")
          try expectNoInlineOverlay(listController)
          BrokerClient.finishPendingList(passwordRequiredListResult())
          try expect(listController.activeContexts.isEmpty,
                     "canceled list context should be untracked after broker completion")
          try expect(listController.virtualDir.isEmpty,
                     "canceled list password failure should return to the synthetic root")
          try expect(listController.items.count == 1 && listController.items[0].isSyntheticArchiveRoot,
                     "canceled list password failure should restore the synthetic root row")
          try expectNoInlineOverlay(listController)
          try expect(listShowErrors.isEmpty,
                     "canceled list password prompt should not call showError")

          BrokerClient.resetTestingBehavior()

          var exportShowErrors = [String]()
          let exportController = makeController(
            items: [item(path: "notes.txt", name: "notes.txt", directory: false, size: 12)],
            selected: IndexSet(integer: 0))
          exportController.z7TestingClipboardPasswordProvider = { "" }
          exportController.z7TestingShowErrorHandler = { exportShowErrors.append($0) }
          BrokerClient.nextBatchExportPasswordPromptEvent = passwordPromptEvent(promptID: "prompt-export-cancel")
          BrokerClient.nextBatchExportResultAfterPasswordProvided = successResult(count: 1)

          exportController.onExtractSelected()
          exportController.cancelActivePasswordPrompt()

          try expect(BrokerClient.lastCanceledPromptID == "prompt-export-cancel",
                     "canceling an export password prompt should cancel the broker prompt")
          try expectNoInlineOverlay(exportController)
          BrokerClient.finishPendingBatchExport(passwordRequiredExportResult())
          try expect(exportController.activeContexts.isEmpty,
                     "canceled export context should be untracked after broker completion")
          try expect(!exportController.extractSelectedBatchRunning,
                     "canceled export completion should reset the running guard")
          guard case .exportFailure(let result) = exportController.viewModel.mode else {
            throw TestFailure(description: "canceled export password completion should surface as exportFailure")
          }
          try expect(!result.primaryButtonEnabled,
                     "canceled export password failure should keep the done action disabled")
          try expectNoInlineOverlay(exportController)
          try expect(exportShowErrors.isEmpty,
                     "canceled export password prompt should not call showError")
        }),
      ExtractSelectionTestCase(
        name: "extract_selected_single_file_exports_file_destination",
        body: {
          let controller = makeController(
            items: [item(path: "notes.txt", name: "notes.txt", directory: false, size: 12)],
            selected: IndexSet(integer: 0))
          let captured = try captureExtractedItems(controller)()

          try expect(captured.count == 1, "single selected file should export one item")
          try expectExport(
            captured[0],
            entryPath: "notes.txt",
            destinationPath: "/tmp/archive-folder/notes.txt",
            recursive: false,
            entryIsDirectory: false,
            listedSize: 12)
          try expectExportSuccess(controller)
        }),
      ExtractSelectionTestCase(
        name: "extract_selected_single_folder_exports_recursive_directory",
        body: {
          let controller = makeController(
            items: [item(path: "docs", name: "docs", directory: true, size: 4096)],
            selected: IndexSet(integer: 0))
          let captured = try captureExtractedItems(controller)()

          try expect(captured.count == 1, "single selected folder should export one item")
          try expectExport(
            captured[0],
            entryPath: "docs",
            destinationPath: "/tmp/archive-folder/docs",
            recursive: true,
            entryIsDirectory: true,
            listedSize: 4096)
          try expectExportSuccess(controller)
        }),
      ExtractSelectionTestCase(
        name: "extract_selected_multiple_files_exports_all_selected_files",
        body: {
          let controller = makeController(
            items: [
              item(path: "a.txt", name: "a.txt", directory: false, size: 1),
              item(path: "b.txt", name: "b.txt", directory: false, size: 2),
              item(path: "c.txt", name: "c.txt", directory: false, size: 3),
            ],
            selected: IndexSet([0, 2]))
          let captured = try captureExtractedItems(controller)()

          try expect(captured.count == 2, "multi-selection should export every selected item")
          try expectExport(
            captured[0],
            entryPath: "a.txt",
            destinationPath: "/tmp/archive-folder/a.txt",
            recursive: false,
            entryIsDirectory: false,
            listedSize: 1)
          try expectExport(
            captured[1],
            entryPath: "c.txt",
            destinationPath: "/tmp/archive-folder/c.txt",
            recursive: false,
            entryIsDirectory: false,
            listedSize: 3)
          try expectExportSuccess(controller)
        }),
      ExtractSelectionTestCase(
        name: "extract_selected_mixed_file_and_folder_preserves_per_item_flags",
        body: {
          let controller = makeController(
            items: [
              item(path: "docs", name: "docs", directory: true, size: 100),
              item(path: "report.txt", name: "report.txt", directory: false, size: 20),
            ],
            selected: IndexSet([0, 1]))
          let captured = try captureExtractedItems(controller)()

          try expect(captured.count == 2, "mixed selection should export every selected item")
          try expectExport(
            captured[0],
            entryPath: "docs",
            destinationPath: "/tmp/archive-folder/docs",
            recursive: true,
            entryIsDirectory: true,
            listedSize: 100)
          try expectExport(
            captured[1],
            entryPath: "report.txt",
            destinationPath: "/tmp/archive-folder/report.txt",
            recursive: false,
            entryIsDirectory: false,
            listedSize: 20)
          try expectExportSuccess(controller)
        }),
      ExtractSelectionTestCase(
        name: "reload_current_directory_handles_synchronous_list_completion",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          controller.virtualDir = "docs"
          BrokerClient.nextListResult = successListResult([
            Z7BrokerQuickLookItem(
              path: "docs/readme.md",
              name: "readme.md",
              directory: false,
              size: 42,
              mtimeMsUtc: 0,
              archiveLike: false),
          ])

          var completed: Bool?
          controller.reloadCurrentDirectory { succeeded in
            completed = succeeded
          }

          try expect(completed == true, "synchronous list completion should not be dropped")
          try expect(BrokerClient.lastListRequestID?.isEmpty == false, "list request id should be caller generated")
          try expect(controller.activeContexts.isEmpty, "list context should be untracked after completion")
          try expect(controller.items.map(\.name) == ["readme.md"], "list completion should update displayed items")
        }),
      ExtractSelectionTestCase(
        name: "export_items_handles_synchronous_progress_and_completion",
        body: {
          BrokerClient.resetTestingBehavior()
          defer { BrokerClient.resetTestingBehavior() }

          let controller = makeController(items: [], selected: [])
          let export = QuickLookPreviewController.SelectedExportItem(
            entryPath: "docs/readme.md",
            nestedEntries: [],
            recursive: false,
            entryIsDirectory: false,
            destinationPath: "/tmp/archive-folder/readme.md",
            listedSize: 42)
          BrokerClient.nextBatchExportProgress = Z7BrokerQuickLookBatchExportProgress(
            completedItemCount: 0,
            totalItemCount: 1,
            currentItemIndex: 0,
            currentEntryPath: "docs/readme.md",
            currentDestinationPath: "/tmp/archive-folder/readme.md",
            currentPercent: 50,
            totalsKnown: true,
            totalBytes: 84,
            completedBytes: 42,
            currentPath: nil,
            message: "copying")
          BrokerClient.nextBatchExportResult = successResult(count: 1)

          var completed: Result<Z7BrokerQuickLookBatchExportResult, QuickLookOperationFailure>?
          controller.exportItems([export]) { result in
            completed = result
          }

          try expect(BrokerClient.lastBatchExportRequestID?.isEmpty == false, "export request id should be caller generated")
          try expect(controller.activeContexts.isEmpty, "export context should be untracked after completion")
          try expect(controller.viewModel.progressState?.detail == "/tmp/archive-folder/readme.md",
                     "synchronous export progress should not be dropped")
          guard case .success(let result) = completed else {
            throw TestFailure(description: "synchronous export completion should not be dropped")
          }
          try expect(result.completedItemCount == 1, "export completion should preserve result payload")
        }),
    ]

    for test in tests {
      do {
        try test.body()
        print("PASS \(test.name)")
      } catch {
        fputs("FAIL \(test.name): \(error)\n", stderr)
        Darwin.exit(1)
      }
    }
  }
}
