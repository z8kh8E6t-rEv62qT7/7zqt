import AppKit
import Foundation

enum QuickLookLocalClickKind {
  case singleClick
  case doubleClick
}

struct QuickLookPendingLocalClick {
  let rowID: String
  let date: Date
}

enum QuickLookClickPairing {
  // Double-click is inferred from two local Button actions on the same row
  // within the system double-click interval. The second click consumes the
  // pending click, so quick repeated clicks pair as single, double, single,
  // double without relying on NSEvent.clickCount or a global event tap.
  static func consume(rowID: String,
                      now: Date,
                      pendingClick: inout QuickLookPendingLocalClick?,
                      doubleClickInterval: TimeInterval = NSEvent.doubleClickInterval) -> QuickLookLocalClickKind {
    if let currentPendingClick = pendingClick,
       currentPendingClick.rowID == rowID
    {
      let elapsed = now.timeIntervalSince(currentPendingClick.date)
      if elapsed >= 0 && elapsed <= doubleClickInterval {
        pendingClick = nil
        return .doubleClick
      }
    }

    pendingClick = QuickLookPendingLocalClick(rowID: rowID, date: now)
    return .singleClick
  }
}
