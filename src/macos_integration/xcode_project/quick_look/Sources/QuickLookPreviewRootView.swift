import SwiftUI

struct QuickLookPreviewRootView: View {
  @ObservedObject var model: QuickLookPreviewViewModel
  let collectionProxy: QuickLookCollectionProxy
  let onBack: () -> Void
  let onExtractSelected: () -> Void
  let onSelectionChange: (IndexSet) -> Void
  let onPrimaryAction: () -> Void
  let onPasswordReadClipboard: () -> Void
  let onPasswordCancel: () -> Void
  let onInlinePrimaryAction: () -> Void
  let onExportPrimaryAction: () -> Void
  let onExportSecondaryAction: () -> Void

  var body: some View {
    QuickLookPreviewTheme.pageBackground
      .ignoresSafeArea()
      .overlay {
        switch model.mode {
        case .browse:
          VStack(alignment: .leading, spacing: QuickLookPreviewTheme.sectionSpacing) {
            headerSection
            controlSection
            collectionSection
          }
          .padding(QuickLookPreviewTheme.pagePadding)
        case .exportRunning:
          exportRunningSection
            .padding(24)
            .overlay(alignment: .top) {
              inlineOverlay
                .padding(16)
            }
        case .exportSuccess(let result), .exportFailure(let result):
          exportResultSection(result)
            .padding(24)
            .overlay(alignment: .top) {
              inlineOverlay
                .padding(16)
            }
        }
      }
  }

  private var headerSection: some View {
    VStack(alignment: .leading, spacing: QuickLookPreviewTheme.headerSpacing) {
      Text(model.filePath)
        .font(.callout.weight(.medium))
        .foregroundStyle(QuickLookPreviewTheme.secondaryText)
        .lineLimit(3)
        .fixedSize(horizontal: false, vertical: true)
        .textSelection(.enabled)
    }
  }

  private var controlSection: some View {
    VStack(alignment: .leading, spacing: QuickLookPreviewTheme.contentSpacing) {
      progressSection
      actionSection
    }
  }

  @ViewBuilder
  private var progressSection: some View {
    VStack(alignment: .leading, spacing: 10) {
      if let progress = model.progressState {
        HStack(alignment: .firstTextBaseline, spacing: 10) {
          Text(progress.title)
            .font(.callout.weight(.semibold))
          Spacer(minLength: 0)
          if let fraction = progress.fraction {
            Text("\(Int((fraction * 100).rounded()))%")
              .font(.caption.monospacedDigit())
              .foregroundStyle(QuickLookPreviewTheme.secondaryText)
          }
        }
        if progress.fraction != nil {
          ProgressView(value: progress.fraction ?? 0, total: 1)
            .progressViewStyle(.linear)
        } else {
          ProgressView()
            .controlSize(.small)
        }
        if !progress.detail.isEmpty {
          Text(progress.detail)
            .font(.caption)
            .foregroundStyle(QuickLookPreviewTheme.secondaryText)
            .lineLimit(2)
        }
      }
    }
  }

  private var exportRunningSection: some View {
    VStack(alignment: .leading, spacing: 16) {
      Text(model.fileName)
        .font(.title2.weight(.semibold))
        .lineLimit(2)

      Text(model.filePath)
        .font(.caption)
        .foregroundStyle(QuickLookPreviewTheme.secondaryText)
        .lineLimit(3)
        .textSelection(.enabled)

      if let progress = model.progressState {
        VStack(alignment: .leading, spacing: 10) {
          HStack(spacing: 12) {
            Text(progress.title)
              .font(.headline)
            Spacer(minLength: 0)
            if let fraction = progress.fraction {
              Text("\(Int((fraction * 100).rounded()))%")
                .font(.body.monospacedDigit())
                .foregroundStyle(QuickLookPreviewTheme.secondaryText)
            }
          }

          if let fraction = progress.fraction {
            ProgressView(value: fraction, total: 1)
              .progressViewStyle(.linear)
          } else {
            ProgressView()
              .progressViewStyle(.linear)
          }

          if !progress.counterText.isEmpty {
            Text(progress.counterText)
              .font(.callout.monospacedDigit())
              .foregroundStyle(QuickLookPreviewTheme.secondaryText)
          }

          if !progress.currentPath.isEmpty {
            Text(progress.currentPath)
              .font(.system(.body, design: .monospaced))
              .lineLimit(2)
          }

          if !progress.message.isEmpty {
            Text(progress.message)
              .font(.callout)
              .foregroundStyle(QuickLookPreviewTheme.secondaryText)
              .lineLimit(3)
          }

          if !progress.detail.isEmpty {
            Text(progress.detail)
              .font(.caption)
              .foregroundStyle(QuickLookPreviewTheme.tertiaryText)
              .lineLimit(3)
              .textSelection(.enabled)
          }
        }
      }

      Spacer(minLength: 0)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
  }

  private func exportResultSection(_ result: QuickLookExportResultModel) -> some View {
    VStack(alignment: .leading, spacing: 16) {
      Text(result.title)
        .font(.title2.weight(.semibold))

      Text(result.summary)
        .font(.body)

      if !result.detail.isEmpty {
        Text(result.detail)
          .font(.callout)
          .foregroundStyle(QuickLookPreviewTheme.secondaryText)
      }

      if let failedEntryPath = result.failedEntryPath, !failedEntryPath.isEmpty {
        Text(failedEntryPath)
          .font(.system(.callout, design: .monospaced))
          .textSelection(.enabled)
      }

      if let failedDestinationPath = result.failedDestinationPath, !failedDestinationPath.isEmpty {
        Text(failedDestinationPath)
          .font(.caption)
          .foregroundStyle(QuickLookPreviewTheme.tertiaryText)
          .textSelection(.enabled)
      }

      HStack(spacing: 12) {
        QuickLookActionButton(
          title: result.primaryButtonTitle,
          isEnabled: result.primaryButtonEnabled,
          style: .prominent,
          action: onExportPrimaryAction)

        if !result.secondaryButtonTitle.isEmpty {
          QuickLookActionButton(
            title: result.secondaryButtonTitle,
            style: .regular,
            action: onExportSecondaryAction)
        }
      }

      Spacer(minLength: 0)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
  }

  private var actionSection: some View {
    HStack(alignment: .center, spacing: 12) {
      HStack(spacing: 10) {
        QuickLookActionButton(
          title: QuickLookLocalization.text("quicklook.back"),
          systemImageName: "arrow.left",
          isEnabled: model.backEnabled,
          style: .regular,
          action: onBack)

        QuickLookActionButton(
          title: QuickLookLocalization.text("quicklook.extract_selected"),
          systemImageName: "square.and.arrow.down",
          isEnabled: model.extractEnabled,
          style: .regular,
          action: onExtractSelected)
      }

      Spacer(minLength: 0)

      if !model.infoText.isEmpty {
        Text(model.infoText)
          .font(.caption.weight(.medium))
          .foregroundStyle(QuickLookPreviewTheme.secondaryText)
          .padding(.horizontal, 8)
          .padding(.vertical, 4)
      }
    }
  }

  private var collectionSection: some View {
    ZStack(alignment: .top) {
      RoundedRectangle(cornerRadius: QuickLookPreviewTheme.cardCornerRadius)
        .fill(QuickLookPreviewTheme.cardBackground)
        .overlay(
          RoundedRectangle(cornerRadius: QuickLookPreviewTheme.cardCornerRadius)
            .stroke(QuickLookPreviewTheme.strongBorder, lineWidth: 1))

      QuickLookCollectionContainer(
        rows: model.rows,
        selection: model.selectedIndexes,
        initialSyntheticRootProbeGeneration: model.initialSyntheticRootProbeGeneration,
        proxy: collectionProxy,
        onSelectionChange: onSelectionChange,
        onPrimaryAction: onPrimaryAction)
        .padding(6)

      inlineOverlay
        .padding(16)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }

  @ViewBuilder
  private var inlineOverlay: some View {
    switch model.inlineOverlay {
    case .none:
      EmptyView()
    case .password(let prompt):
      passwordCard(prompt)
        .frame(maxWidth: 340)
    case .banner(let banner):
      messageCard(
        title: banner.title,
        message: banner.message,
        buttonTitle: banner.buttonTitle,
        action: onInlinePrimaryAction)
        .frame(maxWidth: 420)
    case .toast(let message):
      toastView(message)
        .frame(maxWidth: 380)
    }
  }

  private func passwordCard(_ prompt: QuickLookPasswordPromptModel) -> some View {
    QuickLookPasswordPromptView(
      prompt: prompt,
      onReadClipboard: onPasswordReadClipboard,
      onCancel: onPasswordCancel)
  }

  private func messageCard(title: String,
                           message: String,
                           buttonTitle: String,
                           action: @escaping () -> Void) -> some View {
    VStack(alignment: .leading, spacing: 12) {
      overlayHeader(
        iconName: "sparkles",
        label: QuickLookLocalization.text("quicklook.banner_action_label"))

      Text(title)
        .font(.headline)

      Text(message)
        .font(.callout)
        .foregroundStyle(QuickLookPreviewTheme.secondaryText)

      QuickLookActionButton(
        title: buttonTitle,
        style: .prominent,
        action: action)
    }
    .frame(maxWidth: .infinity, alignment: .leading)
    .padding(20)
    .background(QuickLookPreviewTheme.overlayMaterial, in: RoundedRectangle(cornerRadius: QuickLookPreviewTheme.overlayCornerRadius))
    .overlay(
      RoundedRectangle(cornerRadius: QuickLookPreviewTheme.overlayCornerRadius)
        .stroke(QuickLookPreviewTheme.cardBorder, lineWidth: 1))
  }

  private func toastView(_ message: String) -> some View {
    HStack(spacing: 10) {
      Image(systemName: "checkmark.circle.fill")
        .foregroundStyle(.green)
      Text(message)
        .font(.callout.weight(.medium))
        .foregroundStyle(.primary)
        .lineLimit(3)
    }
    .frame(maxWidth: .infinity, alignment: .leading)
    .padding(.horizontal, 18)
    .padding(.vertical, 12)
    .background(.thinMaterial, in: RoundedRectangle(cornerRadius: QuickLookPreviewTheme.overlayCornerRadius))
    .overlay(
      RoundedRectangle(cornerRadius: QuickLookPreviewTheme.overlayCornerRadius)
        .stroke(QuickLookPreviewTheme.cardBorder, lineWidth: 1))
  }
}

private struct QuickLookPasswordPromptView: View {
  let prompt: QuickLookPasswordPromptModel
  let onReadClipboard: () -> Void
  let onCancel: () -> Void

  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      QuickLookOverlayHeader(
        iconName: "lock.fill",
        label: QuickLookLocalization.text("quicklook.banner_protected_label"))

      Text(prompt.title)
        .font(.headline)

      Text(prompt.subtitle)
        .font(.callout)
        .foregroundStyle(QuickLookPreviewTheme.secondaryText)

      if prompt.showsRetryHint {
        Text(prompt.retryText)
          .font(.caption.weight(.medium))
          .foregroundStyle(.red)
      }

      ScrollView {
        Text(verbatim: prompt.clipboardText.isEmpty ? " " : prompt.clipboardText)
          .font(.system(.body, design: .monospaced))
          .textSelection(.enabled)
          .frame(maxWidth: .infinity, alignment: .topLeading)
      }
      .frame(maxWidth: .infinity, minHeight: 38, maxHeight: 92, alignment: .topLeading)
      .padding(8)
      .background(
        RoundedRectangle(cornerRadius: 6)
          .fill(Color.primary.opacity(0.045)))
      .overlay(
        RoundedRectangle(cornerRadius: 6)
          .stroke(QuickLookPreviewTheme.cardBorder, lineWidth: 1))

      HStack {
        QuickLookActionButton(
          title: prompt.cancelTitle,
          style: .regular,
          action: onCancel)
        Spacer(minLength: 0)
        QuickLookActionButton(
          title: prompt.readClipboardTitle,
          style: .prominent,
          action: onReadClipboard)
      }
    }
    .padding(20)
    .background(QuickLookPreviewTheme.overlayMaterial, in: RoundedRectangle(cornerRadius: QuickLookPreviewTheme.overlayCornerRadius))
    .overlay(
      RoundedRectangle(cornerRadius: QuickLookPreviewTheme.overlayCornerRadius)
        .stroke(QuickLookPreviewTheme.cardBorder, lineWidth: 1))
    .id(prompt.promptID)
  }
}

private struct QuickLookOverlayHeader: View {
  let iconName: String
  let label: String

  var body: some View {
    HStack(spacing: 8) {
      Image(systemName: iconName)
        .font(.caption.weight(.semibold))
      Text(label)
        .font(.caption.weight(.semibold))
        .textCase(.uppercase)
    }
    .foregroundStyle(QuickLookPreviewTheme.tertiaryText)
  }
}

private enum QuickLookActionButtonStyle {
  case regular
  case prominent
}

private struct QuickLookActionButton: View {
  let title: String
  var systemImageName: String?
  let isEnabled: Bool
  let style: QuickLookActionButtonStyle
  let action: () -> Void

  init(title: String,
       systemImageName: String? = nil,
       isEnabled: Bool = true,
       style: QuickLookActionButtonStyle,
       action: @escaping () -> Void) {
    self.title = title
    self.systemImageName = systemImageName
    self.isEnabled = isEnabled
    self.style = style
    self.action = action
  }

  var body: some View {
    QuickLookLocalButton(
      isEnabled: isEnabled,
      accessibilityLabel: title,
      action: action) { isPressed in
      QuickLookActionButtonLabel(
        title: title,
        systemImageName: systemImageName,
        isEnabled: isEnabled,
        isPressed: isPressed,
        style: style)
    }
  }
}

private struct QuickLookActionButtonLabel: View {
  let title: String
  let systemImageName: String?
  let isEnabled: Bool
  let isPressed: Bool
  let style: QuickLookActionButtonStyle

  var body: some View {
    HStack(spacing: 6) {
      if let systemImageName {
        Image(systemName: systemImageName)
          .font(.system(size: 13, weight: .medium))
      }

      Text(title)
        .font(.system(size: 13, weight: .medium))
        .lineLimit(1)
    }
    .foregroundStyle(foregroundColor)
    .padding(.horizontal, horizontalPadding)
    .padding(.vertical, verticalPadding)
    .frame(minWidth: minimumWidth, minHeight: minimumHeight)
    .background(
      RoundedRectangle(cornerRadius: cornerRadius)
        .fill(backgroundColor))
    .overlay(
      RoundedRectangle(cornerRadius: cornerRadius)
        .stroke(borderColor, lineWidth: borderWidth))
    .opacity(isEnabled ? 1 : 0.62)
    .contentShape(RoundedRectangle(cornerRadius: cornerRadius))
  }

  private var horizontalPadding: CGFloat {
    style == .prominent ? 12 : 10
  }

  private var verticalPadding: CGFloat {
    style == .prominent ? 7 : 5
  }

  private var minimumWidth: CGFloat {
    style == .prominent ? 54 : 0
  }

  private var minimumHeight: CGFloat {
    style == .prominent ? 28 : 24
  }

  private var cornerRadius: CGFloat {
    style == .prominent ? 7 : 6
  }

  private var foregroundColor: Color {
    switch style {
    case .regular:
      return isEnabled ? .primary : QuickLookPreviewTheme.tertiaryText
    case .prominent:
      return .white.opacity(isEnabled ? 1 : 0.72)
    }
  }

  private var backgroundColor: Color {
    switch style {
    case .regular:
      if !isEnabled {
        return Color.primary.opacity(0.025)
      }
      return Color.primary.opacity(isPressed ? 0.10 : 0.045)
    case .prominent:
      let base = Color.accentColor
      if !isEnabled {
        return base.opacity(0.35)
      }
      return base.opacity(isPressed ? 0.82 : 1)
    }
  }

  private var borderColor: Color {
    switch style {
    case .regular:
      return QuickLookPreviewTheme.cardBorder.opacity(isEnabled ? 1 : 0.55)
    case .prominent:
      return .clear
    }
  }

  private var borderWidth: CGFloat {
    style == .regular ? 1 : 0
  }
}

#if Z7_TESTING
extension QuickLookPreviewRootView {
  func z7TestingTapExtractSelected() {
    onExtractSelected()
  }

  func z7TestingTapPasswordReadClipboard() {
    onPasswordReadClipboard()
  }
}
#endif

private extension QuickLookPreviewRootView {
  func overlayHeader(iconName: String, label: String) -> some View {
    QuickLookOverlayHeader(iconName: iconName, label: label)
  }
}
