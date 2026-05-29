import Foundation

struct QuickLookSelectionModifiers: OptionSet {
  let rawValue: Int

  static let shift = QuickLookSelectionModifiers(rawValue: 1 << 0)
  static let command = QuickLookSelectionModifiers(rawValue: 1 << 1)
}

struct QuickLookSelectionUpdate: Equatable {
  let selection: IndexSet
  let anchorIndex: Int?
}

enum QuickLookSelectionStrategy {
  static func update(itemIndex index: Int,
                     currentSelection: IndexSet,
                     anchorIndex: Int?,
                     itemCount: Int,
                     modifiers: QuickLookSelectionModifiers) -> QuickLookSelectionUpdate {
    let normalizedSelection = sanitize(currentSelection, itemCount: itemCount)
    let normalizedAnchor = sanitize(anchorIndex: anchorIndex, itemCount: itemCount)

    guard index >= 0, index < itemCount else {
      return QuickLookSelectionUpdate(
        selection: normalizedSelection,
        anchorIndex: normalizedAnchor)
    }

    if modifiers.contains(.shift) {
      guard let anchor = normalizedAnchor else {
        return QuickLookSelectionUpdate(
          selection: IndexSet(integer: index),
          anchorIndex: index)
      }

      let rangeSelection = IndexSet(integersIn: min(anchor, index)...max(anchor, index))
      if modifiers.contains(.command) {
        var nextSelection = normalizedSelection
        nextSelection.formUnion(rangeSelection)
        return QuickLookSelectionUpdate(selection: nextSelection, anchorIndex: anchor)
      }
      return QuickLookSelectionUpdate(selection: rangeSelection, anchorIndex: anchor)
    }

    if modifiers.contains(.command) {
      var nextSelection = normalizedSelection
      if nextSelection.contains(index) {
        nextSelection.remove(index)
      } else {
        nextSelection.insert(index)
      }
      return QuickLookSelectionUpdate(selection: nextSelection, anchorIndex: index)
    }

    return QuickLookSelectionUpdate(
      selection: IndexSet(integer: index),
      anchorIndex: index)
  }

  private static func sanitize(_ selection: IndexSet, itemCount: Int) -> IndexSet {
    guard itemCount > 0 else {
      return IndexSet()
    }

    var sanitized = IndexSet()
    for index in selection where index >= 0 && index < itemCount {
      sanitized.insert(index)
    }
    return sanitized
  }

  private static func sanitize(anchorIndex: Int?, itemCount: Int) -> Int? {
    guard let anchorIndex, anchorIndex >= 0, anchorIndex < itemCount else {
      return nil
    }
    return anchorIndex
  }
}
