import CoreGraphics
import Foundation

struct QuickLookVisibleWidthStability {
  static let defaultEpsilon: CGFloat = 0.5

  private let epsilon: CGFloat
  private(set) var candidateWidth: CGFloat?
  private(set) var isStable = false

  init(epsilon: CGFloat = defaultEpsilon) {
    self.epsilon = epsilon
  }

  mutating func reset() {
    candidateWidth = nil
    isStable = false
  }

  mutating func observe(width: CGFloat) -> Bool {
    guard width.isFinite, width > 0 else {
      reset()
      return false
    }

    guard let candidateWidth else {
      self.candidateWidth = width
      isStable = false
      return false
    }

    if abs(candidateWidth - width) <= epsilon {
      let becameStable = !isStable
      self.candidateWidth = width
      isStable = true
      return becameStable
    }

    self.candidateWidth = width
    isStable = false
    return false
  }
}
