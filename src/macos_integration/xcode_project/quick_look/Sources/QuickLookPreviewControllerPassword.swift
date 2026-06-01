import AppKit
import Foundation

private let passwordClipboardPollInterval: TimeInterval = 0.5

enum PasswordPromptState {
  case idle
  case showing(promptID: String, archivePath: String, nestedChain: [String], reasonKey: String)
}

extension QuickLookPreviewController {
  func showPasswordPrompt(promptID: String,
                          archivePath: String,
                          nestedChain: [String],
                          reasonKey: String,
                          clipboardText: String? = nil) {
    passwordPromptState = .showing(
      promptID: promptID,
      archivePath: archivePath,
      nestedChain: nestedChain,
      reasonKey: reasonKey)

    renderPasswordPrompt(
      promptID: promptID,
      archivePath: archivePath,
      nestedChain: nestedChain,
      reasonKey: reasonKey,
      clipboardText: clipboardText ?? currentClipboardPasswordText())
    startPasswordClipboardPolling(promptID: promptID)
  }

  func renderPasswordPrompt(promptID: String,
                            archivePath: String,
                            nestedChain: [String],
                            reasonKey: String,
                            clipboardText: String) {
    toastDismissWorkItem?.cancel()
    toastDismissWorkItem = nil
    inlineOverlayAction = nil
    viewModel.inlineOverlay = .password(
      QuickLookPasswordPromptModel(
        promptID: promptID,
        title: passwordPromptTitle(reasonKey: reasonKey),
        subtitle: passwordPromptSubtitle(
          archivePath: archivePath,
          nestedChain: nestedChain),
        showsRetryHint: reasonKey == "wrong_password",
        retryText: QuickLookLocalization.text(
          "quicklook.password_retry"),
        clipboardSourceText: QuickLookLocalization.text(
          "quicklook.password_clipboard_source"),
        clipboardText: clipboardText,
        confirmTitle: QuickLookLocalization.text(
          "quicklook.password_confirm"),
        cancelTitle: QuickLookLocalization.text(
          "quicklook.password_cancel")))
  }

  func confirmClipboardPasswordForActivePrompt() {
    guard case .showing(let promptID, let archivePath, let nestedChain, let reasonKey) = passwordPromptState else {
      return
    }

    let password = currentClipboardPasswordText()
    stopPasswordClipboardPolling()
    renderPasswordPrompt(
      promptID: promptID,
      archivePath: archivePath,
      nestedChain: nestedChain,
      reasonKey: reasonKey,
      clipboardText: password)
    passwordPromptState = .idle
    brokerClient.providePassword(promptID: promptID, password: password)
  }

  func currentClipboardPasswordText() -> String {
    clipboardPasswordText() ?? ""
  }

  func clipboardPasswordText() -> String? {
#if Z7_TESTING
    if let z7TestingClipboardPasswordProvider {
      return z7TestingClipboardPasswordProvider()
    }
#endif
    return NSPasteboard.general.string(forType: .string)
  }

  func startPasswordClipboardPolling(promptID: String) {
    stopPasswordClipboardPolling()
    schedulePasswordClipboardPolling(promptID: promptID)
  }

  func stopPasswordClipboardPolling() {
    passwordClipboardPollGeneration &+= 1
    passwordClipboardPollWorkItem?.cancel()
    passwordClipboardPollWorkItem = nil
  }

  func schedulePasswordClipboardPolling(promptID: String) {
    passwordClipboardPollGeneration &+= 1
    let generation = passwordClipboardPollGeneration
    let workItem = DispatchWorkItem { [weak self] in
      self?.refreshPasswordClipboardPreview(
        promptID: promptID,
        generation: generation)
    }
    passwordClipboardPollWorkItem = workItem
    DispatchQueue.main.asyncAfter(
      deadline: .now() + passwordClipboardPollInterval,
      execute: workItem)
  }

  func refreshPasswordClipboardPreview(promptID: String, generation: UInt64) {
    passwordClipboardPollWorkItem = nil
    guard passwordClipboardPollGeneration == generation,
          case .showing(let activePromptID, let archivePath, let nestedChain, let reasonKey) = passwordPromptState,
          activePromptID == promptID else {
      return
    }

    renderPasswordPrompt(
      promptID: activePromptID,
      archivePath: archivePath,
      nestedChain: nestedChain,
      reasonKey: reasonKey,
      clipboardText: currentClipboardPasswordText())
    schedulePasswordClipboardPolling(promptID: activePromptID)
  }

#if Z7_TESTING
  func z7TestingRefreshPasswordClipboardPreview() {
    guard case .showing(let promptID, _, _, _) = passwordPromptState else {
      return
    }
    refreshPasswordClipboardPreview(
      promptID: promptID,
      generation: passwordClipboardPollGeneration)
  }
#endif

  func passwordPromptTitle(reasonKey: String) -> String {
    if reasonKey == "wrong_password" {
      return QuickLookLocalization.text(
        "quicklook.password_wrong_title")
    }
    return QuickLookLocalization.text(
      "quicklook.password_title")
  }

  func passwordPromptSubtitle(archivePath: String, nestedChain: [String]) -> String {
    if let last = nestedChain.last {
      return QuickLookLocalization.format(
        "quicklook.password_subtitle_nested",
        [String(nestedChain.count + 1), (last as NSString).lastPathComponent])
    }
    return QuickLookLocalization.format(
      "quicklook.password_subtitle_top",
      [URL(fileURLWithPath: archivePath).lastPathComponent])
  }
}
