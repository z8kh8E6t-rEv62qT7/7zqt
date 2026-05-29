import AppKit
import SwiftUI

struct QuickLookListRowModel: Identifiable, Equatable {
  let id: String
  let title: String
  let detailText: String
  let sizeText: String
  let modifiedText: String
  let icon: NSImage
  let isDirectory: Bool
  let isArchiveLike: Bool
  let isSyntheticArchiveRoot: Bool
  let isPendingInitialReveal: Bool

  static func == (lhs: QuickLookListRowModel, rhs: QuickLookListRowModel) -> Bool {
    lhs.id == rhs.id &&
      lhs.title == rhs.title &&
      lhs.detailText == rhs.detailText &&
      lhs.sizeText == rhs.sizeText &&
      lhs.modifiedText == rhs.modifiedText &&
      lhs.isDirectory == rhs.isDirectory &&
      lhs.isArchiveLike == rhs.isArchiveLike &&
      lhs.isSyntheticArchiveRoot == rhs.isSyntheticArchiveRoot &&
      lhs.isPendingInitialReveal == rhs.isPendingInitialReveal
  }
}

struct QuickLookProgressState {
  let title: String
  let detail: String
  let fraction: Double?
  let counterText: String
  let currentPath: String
  let message: String
}

struct QuickLookPasswordPromptModel {
  let promptID: String
  let title: String
  let subtitle: String
  let showsRetryHint: Bool
  let retryText: String
  let clipboardText: String
  let readClipboardTitle: String
  let cancelTitle: String
}

struct QuickLookBannerModel {
  let title: String
  let message: String
  let buttonTitle: String
}

struct QuickLookExportResultModel {
  let title: String
  let summary: String
  let detail: String
  let failedEntryPath: String?
  let failedDestinationPath: String?
  let primaryButtonTitle: String
  let secondaryButtonTitle: String
  let primaryButtonEnabled: Bool
}

enum QuickLookPreviewMode {
  case browse
  case exportRunning
  case exportSuccess(QuickLookExportResultModel)
  case exportFailure(QuickLookExportResultModel)
}

enum QuickLookInlineOverlayState {
  case none
  case password(QuickLookPasswordPromptModel)
  case banner(QuickLookBannerModel)
  case toast(String)
}

@MainActor
final class QuickLookPreviewViewModel: ObservableObject {
  @Published var fileName = ""
  @Published var filePath = ""
  @Published var infoText = ""
  @Published var progressState: QuickLookProgressState?
  @Published var mode: QuickLookPreviewMode = .browse
  @Published var backEnabled = false
  @Published var extractEnabled = false
  @Published var initialSyntheticRootProbeGeneration: UInt64?
  @Published var rows = [QuickLookListRowModel]()
  @Published var selectedIndexes = IndexSet()
  @Published var inlineOverlay: QuickLookInlineOverlayState = .none
}
