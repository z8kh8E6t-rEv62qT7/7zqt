import SwiftUI

enum QuickLookPreviewTheme {
  static let pagePadding: CGFloat = 18
  static let sectionSpacing: CGFloat = 12
  static let headerSpacing: CGFloat = 8
  static let contentSpacing: CGFloat = 10
  static let cardCornerRadius: CGFloat = 14
  static let overlayCornerRadius: CGFloat = 14
  static let rowCornerRadius: CGFloat = 10
  static let rowHeight: CGFloat = 60

  static var pageBackground: Color {
    .clear
  }

  static var cardBackground: Color {
    Color(nsColor: .textBackgroundColor)
  }

  static var panelBackground: Color {
    .clear
  }

  static var cardBorder: Color {
    Color(nsColor: .separatorColor).opacity(0.28)
  }

  static var strongBorder: Color {
    Color(nsColor: .separatorColor).opacity(0.36)
  }

  static var secondaryText: Color {
    Color(nsColor: .secondaryLabelColor)
  }

  static var tertiaryText: Color {
    Color(nsColor: .tertiaryLabelColor)
  }

  static var accentTint: Color {
    Color(nsColor: .selectedContentBackgroundColor).opacity(0.18)
  }

  static var accentStroke: Color {
    Color(nsColor: .selectedContentBackgroundColor).opacity(0.38)
  }

  static var hoverTint: Color {
    Color(nsColor: .selectedContentBackgroundColor).opacity(0.08)
  }

  static var hoverStroke: Color {
    Color(nsColor: .separatorColor).opacity(0.20)
  }

  static var overlayHighlight: Color {
    Color.white.opacity(0.16)
  }

  static var overlayMaterial: Material {
    .regularMaterial
  }
}
