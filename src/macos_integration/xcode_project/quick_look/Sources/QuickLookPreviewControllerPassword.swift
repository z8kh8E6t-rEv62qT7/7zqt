import AppKit
import Foundation

enum PasswordPromptState {
  case idle
  case showing(promptID: String, archivePath: String, nestedChain: [String], reasonKey: String)
}

extension QuickLookPreviewController {
  func showPasswordPrompt(promptID: String,
                          archivePath: String,
                          nestedChain: [String],
                          reasonKey: String,
                          clipboardText: String = "") {
    passwordPromptState = .showing(
      promptID: promptID,
      archivePath: archivePath,
      nestedChain: nestedChain,
      reasonKey: reasonKey)

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
        clipboardText: clipboardText,
        readClipboardTitle: QuickLookLocalization.text(
          "quicklook.password_read_clipboard"),
        cancelTitle: QuickLookLocalization.text(
          "quicklook.password_cancel")))
  }

  func readClipboardPasswordForActivePrompt() {
    guard case .showing(let promptID, let archivePath, let nestedChain, let reasonKey) = passwordPromptState else {
      return
    }

    guard let password = clipboardPasswordText(), !password.isEmpty else {
      showPasswordPrompt(
        promptID: promptID,
        archivePath: archivePath,
        nestedChain: nestedChain,
        reasonKey: "wrong_password")
      return
    }

    showPasswordPrompt(
      promptID: promptID,
      archivePath: archivePath,
      nestedChain: nestedChain,
      reasonKey: reasonKey,
      clipboardText: password)
    brokerClient.providePassword(promptID: promptID, password: password)
    passwordPromptState = .idle
  }

  func clipboardPasswordText() -> String? {
#if Z7_TESTING
    if let z7TestingClipboardPasswordProvider {
      return z7TestingClipboardPasswordProvider()
    }
#endif
    return NSPasteboard.general.string(forType: .string)
  }

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
