import CoreGraphics
import Foundation

enum QuickLookRowLayoutMetrics {
  static let fallbackRowWidth: CGFloat = 320
  static let preferredTimestampMinWidth: CGFloat = 176
  static let minimumTimestampMinWidth: CGFloat = 108
  static let extraHorizontalPadding: CGFloat = 8
  static let timestampCompressionStartRowWidth: CGFloat = 760
  static let timestampCompressionEndRowWidth: CGFloat = 420

  static func rowWidth(
    visibleWidth: CGFloat,
    horizontalInsets: CGFloat,
    extraPadding: CGFloat = extraHorizontalPadding
  ) -> CGFloat {
    guard visibleWidth.isFinite, visibleWidth > 0 else {
      return fallbackRowWidth
    }

    let candidateWidth = visibleWidth - horizontalInsets - extraPadding
    return max(candidateWidth.rounded(.down), fallbackRowWidth)
  }

  static func timestampMinWidth(forRowWidth rowWidth: CGFloat) -> CGFloat {
    guard rowWidth.isFinite else {
      return preferredTimestampMinWidth
    }

    if rowWidth >= timestampCompressionStartRowWidth {
      return preferredTimestampMinWidth
    }
    if rowWidth <= timestampCompressionEndRowWidth {
      return minimumTimestampMinWidth
    }

    let progress =
      (rowWidth - timestampCompressionEndRowWidth) /
      (timestampCompressionStartRowWidth - timestampCompressionEndRowWidth)
    let interpolatedWidth =
      minimumTimestampMinWidth +
      (preferredTimestampMinWidth - minimumTimestampMinWidth) * progress
    return interpolatedWidth.rounded(.down)
  }
}
