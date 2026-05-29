import SwiftUI

struct QuickLookLocalButton<Content: View>: View {
  let isEnabled: Bool
  let accessibilityLabel: String?
  let action: () -> Void
  @ViewBuilder let content: (_ isPressed: Bool) -> Content

  init(isEnabled: Bool = true,
       accessibilityLabel: String? = nil,
       action: @escaping () -> Void,
       @ViewBuilder content: @escaping (_ isPressed: Bool) -> Content) {
    self.isEnabled = isEnabled
    self.accessibilityLabel = accessibilityLabel
    self.action = action
    self.content = content
  }

  var body: some View {
    Button {
      guard isEnabled else {
        return
      }
      action()
    } label: {
      QuickLookLocalButtonLabel(content: content)
    }
    .buttonStyle(QuickLookLocalButtonStyle())
    .disabled(!isEnabled)
    .contentShape(Rectangle())
    .modifier(QuickLookOptionalAccessibilityLabel(label: accessibilityLabel))
  }
}

private struct QuickLookLocalButtonLabel<Content: View>: View {
  @Environment(\.quickLookLocalButtonPressed) private var isPressed
  @ViewBuilder let content: (_ isPressed: Bool) -> Content

  var body: some View {
    content(isPressed)
  }
}

private struct QuickLookLocalButtonStyle: ButtonStyle {
  func makeBody(configuration: Configuration) -> some View {
    configuration.label
      .environment(\.quickLookLocalButtonPressed, configuration.isPressed)
  }
}

private struct QuickLookLocalButtonPressedKey: EnvironmentKey {
  static let defaultValue = false
}

private extension EnvironmentValues {
  var quickLookLocalButtonPressed: Bool {
    get { self[QuickLookLocalButtonPressedKey.self] }
    set { self[QuickLookLocalButtonPressedKey.self] = newValue }
  }
}

private struct QuickLookOptionalAccessibilityLabel: ViewModifier {
  let label: String?

  func body(content: Content) -> some View {
    if let label, !label.isEmpty {
      content.accessibilityLabel(Text(label))
    } else {
      content
    }
  }
}
