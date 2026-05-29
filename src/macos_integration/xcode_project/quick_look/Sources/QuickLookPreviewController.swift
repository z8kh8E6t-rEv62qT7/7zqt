import AppKit
import Foundation
import QuickLookUI
import SwiftUI

final class QuickLookPreviewController: NSViewController, QLPreviewingController {
  let brokerClient = BrokerClient()
  let viewModel = QuickLookPreviewViewModel()
  let collectionProxy = QuickLookCollectionProxy()

  var archivePath = ""
  var archiveTypeHint = ""
  var archiveBaseName = ""
  var virtualDir = ""
  var nestedStack = [NestedFrame]()
  var passwordPromptState = PasswordPromptState.idle
  var items = [QuickLookItem]()
  var rowModels = [QuickLookListRowModel]()
  var selectedItemIndexes = IndexSet()
  let contextLock = NSLock()
  var activeContexts = [ObjectIdentifier: AsyncTaskContext]()
  var latestListGeneration: UInt64 = 0
  var extractSelectedBatchRunning = false
  var pendingExportNavigationSnapshot: QuickLookNavigationSnapshot?
  var inlineOverlayAction: (() -> Void)?
  var toastDismissWorkItem: DispatchWorkItem?
  var hasCompletedInitialSyntheticRootReveal = false
  var pendingInitialSyntheticRootRevealGeneration: UInt64?
#if Z7_TESTING
  var z7TestingClipboardPasswordProvider: (() -> String?)?
  var z7TestingExportItemsHandler:
    (([SelectedExportItem],
      @escaping (Result<Z7BrokerQuickLookBatchExportResult, QuickLookOperationFailure>) -> Void) -> Void)?
#endif
  let dateFormatter: DateFormatter = {
    let formatter = DateFormatter()
    formatter.dateStyle = .short
    formatter.timeStyle = .medium
    return formatter
  }()

  struct SelectedExportItem {
    let entryPath: String
    let nestedEntries: [String]
    let recursive: Bool
    let entryIsDirectory: Bool
    let destinationPath: String
    let listedSize: UInt64
  }

  deinit {
    toastDismissWorkItem?.cancel()
    cancelActivePasswordPrompt()
    cancelAllActiveContexts()
    brokerClient.invalidate()
  }

  override func viewDidLoad() {
    super.viewDidLoad()
    collectionProxy.onInitialSyntheticRootProbeWidthStable = { [weak self] generation in
      self?.handleInitialSyntheticRootProbeWidthStable(generation: generation)
    }
    brokerClient.setPasswordPromptHandler { [weak self] event in
      self?.showPasswordPrompt(
        promptID: event.promptID,
        archivePath: event.archivePath,
        nestedChain: event.nestedChain,
        reasonKey: event.reasonKey)
    }
  }

  override func loadView() {
    let rootView = QuickLookPreviewRootView(
      model: viewModel,
      collectionProxy: collectionProxy,
      onBack: { [weak self] in self?.onBack() },
      onExtractSelected: { [weak self] in self?.onExtractSelected() },
      onSelectionChange: { [weak self] indexes in self?.setSelectedItemIndexes(indexes) },
      onPrimaryAction: { [weak self] in self?.performPrimarySelectionAction() },
      onPasswordReadClipboard: { [weak self] in self?.readClipboardPasswordForActivePrompt() },
      onPasswordCancel: { [weak self] in self?.cancelActivePasswordPrompt() },
      onInlinePrimaryAction: { [weak self] in self?.performInlineOverlayPrimaryAction() },
      onExportPrimaryAction: { [weak self] in self?.acknowledgeExportResult(primary: true) },
      onExportSecondaryAction: { [weak self] in self?.acknowledgeExportResult(primary: false) }
    )
    let hostingView = NSHostingView(rootView: rootView)
    view = hostingView
  }

  func preparePreviewOfFile(at url: URL, completionHandler handler: @escaping (Error?) -> Void) {
    cancelActivePasswordPrompt()

    pendingExportNavigationSnapshot = nil
    archivePath = url.path
    archiveTypeHint = url.pathExtension.lowercased()
    archiveBaseName = url.deletingPathExtension().lastPathComponent
    if archiveBaseName.isEmpty {
      archiveBaseName = url.lastPathComponent
    }

    virtualDir = ""
    nestedStack = []
    passwordPromptState = .idle
    extractSelectedBatchRunning = false
    viewModel.mode = .browse
    items = []
    rowModels = []
    selectedItemIndexes = []
    hasCompletedInitialSyntheticRootReveal = false
    clearInitialSyntheticRootProbeGeneration()
    clearProgressState()
    updatePresentation()

    reloadCurrentDirectory { succeeded in
      if succeeded {
        handler(nil)
        return
      }
      let message = QuickLookLocalization.text(
        "quicklook.preview_load_failed")
      handler(
        NSError(
          domain: "app.sevenzip.quicklook",
          code: 1,
          userInfo: [NSLocalizedDescriptionKey: message]))
    }
  }

  override func viewWillDisappear() {
    clearInitialSyntheticRootProbeGeneration()
    super.viewWillDisappear()
  }

  func onBack() {
    guard !isInteractionBlocked else {
      return
    }

    if !virtualDir.isEmpty {
      let normalized = virtualDir.split(separator: "/")
      if normalized.count <= 1 {
        virtualDir = ""
      } else {
        virtualDir = normalized.dropLast().joined(separator: "/")
      }
      reloadCurrentDirectory { _ in }
      return
    }

    guard !nestedStack.isEmpty else {
      return
    }
    let frame = nestedStack.removeLast()
    virtualDir = frame.enteredFromVirtualDir
    reloadCurrentDirectory { _ in }
  }

  func onExtractSelected() {
    guard !isInteractionBlocked else {
      return
    }
    guard !extractSelectedBatchRunning else {
      return
    }

    let exports = selectedExportItems()
    guard !exports.isEmpty else {
      updatePresentation()
      return
    }

    extractSelectedBatchRunning = true
    pendingExportNavigationSnapshot = QuickLookNavigationSnapshot(
      virtualDir: virtualDir,
      nestedStack: nestedStack)
    let exportBaseDirectoryURL = QuickLookExportDestination.defaultBaseDirectoryURL(
      forArchivePath: archivePath)
    let exportSuccessDetail = QuickLookExportDestination.successDetail(
      destinationPaths: exports.map(\.destinationPath),
      baseDirectoryURL: exportBaseDirectoryURL)
    viewModel.mode = .exportRunning
    setExportProgressState(
      title: QuickLookLocalization.text(
        "quicklook.quicklook_exporting"),
      detail: "",
      fraction: nil,
      counterText: "0 / \(exports.count)",
      currentPath: "",
      message: "")
    updatePresentation()

    let completion: (Result<Z7BrokerQuickLookBatchExportResult, QuickLookOperationFailure>) -> Void = { [weak self] result in
      guard let self else {
        return
      }
      switch result {
      case .success(let batchResult):
        self.finishExtractSelectedBatch()
        self.viewModel.mode = .exportSuccess(
          QuickLookExportResultModel(
            title: QuickLookLocalization.text(
              "quicklook.quicklook_export_success_title"),
            summary: QuickLookLocalization.format(
              "quicklook.quicklook_export_success_summary",
              [String(batchResult.completedItemCount)]),
            detail: exportSuccessDetail,
            failedEntryPath: nil,
            failedDestinationPath: nil,
            primaryButtonTitle: QuickLookLocalization.text(
              "quicklook.quicklook_export_done"),
            secondaryButtonTitle: "",
            primaryButtonEnabled: true))
      case .failure(let failure):
        self.finishExtractSelectedBatch()
        let summary = failure.isDirectoryBudgetExceeded
          ? QuickLookLocalization.text(
              "quicklook.folder_export_limited_message")
          : failure.message
        self.viewModel.mode = .exportFailure(
          QuickLookExportResultModel(
            title: QuickLookLocalization.text(
              "quicklook.quicklook_export_failure_title"),
            summary: summary,
            detail: "\(failure.completedItemCount) / \(failure.totalItemCount)",
            failedEntryPath: failure.failedEntryPath,
            failedDestinationPath: failure.failedDestinationPath,
            primaryButtonTitle: QuickLookLocalization.text(
              "quicklook.quicklook_export_done"),
            secondaryButtonTitle: QuickLookLocalization.text(
              "quicklook.password_cancel"),
            primaryButtonEnabled: false))
      }
    }

#if Z7_TESTING
    if let z7TestingExportItemsHandler {
      z7TestingExportItemsHandler(exports, completion)
      return
    }
#endif

    exportItems(exports, completion: completion)
  }

  private func finishExtractSelectedBatch() {
    extractSelectedBatchRunning = false
    clearProgressState()
    updatePresentation()
  }

  func acknowledgeExportResult(primary: Bool) {
    switch viewModel.mode {
    case .exportSuccess:
      resetToBrowseAfterExport()
    case .exportFailure:
      if !primary {
        resetToBrowseAfterExport()
      }
    case .browse, .exportRunning:
      break
    }
  }

  func resetToBrowseAfterExport() {
    viewModel.mode = .browse
    let navigationSnapshot = pendingExportNavigationSnapshot
    pendingExportNavigationSnapshot = nil
    selectedItemIndexes = []

    guard let navigationSnapshot else {
      updatePresentation()
      return
    }

    virtualDir = navigationSnapshot.virtualDir
    nestedStack = navigationSnapshot.nestedStack
    reloadCurrentDirectory { _ in }
    updatePresentation()
  }

  func currentNestedEntries() -> [String] {
    nestedStack.map(\.entry)
  }

  var isInteractionBlocked: Bool {
    switch viewModel.mode {
    case .browse:
      return false
    case .exportRunning, .exportSuccess, .exportFailure:
      return true
    }
  }

  func performInlineOverlayPrimaryAction() {
    let action = inlineOverlayAction
    inlineOverlayAction = nil
    action?()
  }

  func hideInlineOverlay() {
    toastDismissWorkItem?.cancel()
    toastDismissWorkItem = nil
    inlineOverlayAction = nil
    viewModel.inlineOverlay = .none
  }

  func showInlineBanner(title: String = QuickLookLocalization.text("quicklook.title"),
                        message: String,
                        buttonTitle: String = QuickLookLocalization.text("quicklook.button_ok"),
                        action: (() -> Void)? = nil) {
    toastDismissWorkItem?.cancel()
    toastDismissWorkItem = nil
    inlineOverlayAction = action ?? { [weak self] in
      self?.hideInlineOverlay()
    }
    viewModel.inlineOverlay = .banner(
      QuickLookBannerModel(
        title: title,
        message: message,
        buttonTitle: buttonTitle))
  }

  func showToast(_ message: String, duration: TimeInterval = 2.2) {
    toastDismissWorkItem?.cancel()
    viewModel.inlineOverlay = .toast(message)
    let workItem = DispatchWorkItem { [weak self] in
      self?.hideInlineOverlay()
    }
    toastDismissWorkItem = workItem
    DispatchQueue.main.asyncAfter(deadline: .now() + duration, execute: workItem)
  }

  func cancelActivePasswordPrompt() {
    if case .showing(let promptID, _, _, _) = passwordPromptState {
      brokerClient.cancelPasswordPrompt(promptID: promptID)
    }
    passwordPromptState = .idle
    hideInlineOverlay()
  }

  func setInitialSyntheticRootProbeGeneration(_ generation: UInt64) {
    pendingInitialSyntheticRootRevealGeneration = generation
    collectionProxy.beginInitialSyntheticRootProbe(generation: generation)
  }

  func clearInitialSyntheticRootProbeGeneration() {
    pendingInitialSyntheticRootRevealGeneration = nil
    collectionProxy.cancelInitialSyntheticRootProbe()
    viewModel.initialSyntheticRootProbeGeneration = nil
  }
}
