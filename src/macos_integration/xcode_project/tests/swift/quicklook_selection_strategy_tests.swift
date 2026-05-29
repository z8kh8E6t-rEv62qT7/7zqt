import Darwin
import Foundation

struct TestFailure: Error, CustomStringConvertible {
  let description: String
}

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
  if !condition() {
    throw TestFailure(description: message)
  }
}

private struct SelectionTestCase {
  let name: String
  let body: () throws -> Void
}

@main
enum QuickLookSelectionStrategyTestMain {
  static func main() throws {
    let tests = [
      SelectionTestCase(
        name: "plain_click_selects_single_item_and_updates_anchor",
        body: {
          let result = QuickLookSelectionStrategy.update(
            itemIndex: 3,
            currentSelection: IndexSet([1, 2]),
            anchorIndex: 1,
            itemCount: 6,
            modifiers: [])
          try expect(result.selection == IndexSet(integer: 3), "plain click should replace selection")
          try expect(result.anchorIndex == 3, "plain click should move anchor to clicked item")
        }),
      SelectionTestCase(
        name: "command_click_toggles_selection",
        body: {
          let selected = QuickLookSelectionStrategy.update(
            itemIndex: 4,
            currentSelection: IndexSet([1, 2]),
            anchorIndex: 2,
            itemCount: 6,
            modifiers: [.command])
          try expect(selected.selection == IndexSet([1, 2, 4]), "command click should add unselected item")
          try expect(selected.anchorIndex == 4, "command add should move anchor to clicked item")

          let deselected = QuickLookSelectionStrategy.update(
            itemIndex: 2,
            currentSelection: selected.selection,
            anchorIndex: selected.anchorIndex,
            itemCount: 6,
            modifiers: [.command])
          try expect(deselected.selection == IndexSet([1, 4]), "command click should remove selected item")
          try expect(deselected.anchorIndex == 2, "command remove should keep clicked item as anchor")
        }),
      SelectionTestCase(
        name: "shift_click_selects_range_from_anchor",
        body: {
          let result = QuickLookSelectionStrategy.update(
            itemIndex: 5,
            currentSelection: IndexSet([1, 3]),
            anchorIndex: 2,
            itemCount: 8,
            modifiers: [.shift])
          try expect(result.selection == IndexSet([2, 3, 4, 5]), "shift click should replace with anchor range")
          try expect(result.anchorIndex == 2, "shift click should preserve anchor")
        }),
      SelectionTestCase(
        name: "shift_click_without_anchor_falls_back_to_single_selection",
        body: {
          let result = QuickLookSelectionStrategy.update(
            itemIndex: 4,
            currentSelection: IndexSet([1, 2]),
            anchorIndex: nil,
            itemCount: 8,
            modifiers: [.shift])
          try expect(result.selection == IndexSet(integer: 4), "shift without anchor should select clicked item")
          try expect(result.anchorIndex == 4, "shift without anchor should establish clicked item as anchor")
        }),
      SelectionTestCase(
        name: "command_shift_click_unions_range_and_preserves_anchor",
        body: {
          let result = QuickLookSelectionStrategy.update(
            itemIndex: 5,
            currentSelection: IndexSet([0, 2]),
            anchorIndex: 2,
            itemCount: 8,
            modifiers: [.command, .shift])
          try expect(result.selection == IndexSet([0, 2, 3, 4, 5]), "command+shift should union anchor range")
          try expect(result.anchorIndex == 2, "command+shift should preserve anchor")
        }),
      SelectionTestCase(
        name: "anchor_updates_after_multiselect_expansion",
        body: {
          let plain = QuickLookSelectionStrategy.update(
            itemIndex: 1,
            currentSelection: IndexSet(),
            anchorIndex: nil,
            itemCount: 8,
            modifiers: [])
          let command = QuickLookSelectionStrategy.update(
            itemIndex: 4,
            currentSelection: plain.selection,
            anchorIndex: plain.anchorIndex,
            itemCount: 8,
            modifiers: [.command])
          try expect(command.selection == IndexSet([1, 4]), "command add should retain prior selection")
          try expect(command.anchorIndex == 4, "command add should update anchor to new item")
        }),
      SelectionTestCase(
        name: "local_click_pairing_pairs_two_fast_clicks_on_same_row",
        body: {
          var pendingClick: QuickLookPendingLocalClick?
          let start = Date(timeIntervalSince1970: 100)
          let first = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start,
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)
          let second = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start.addingTimeInterval(0.2),
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)

          try expect(first == .singleClick, "first local click should be single")
          try expect(second == .doubleClick, "second fast click on same row should be double")
          try expect(pendingClick == nil, "double-click should consume the pending click")
        }),
      SelectionTestCase(
        name: "local_click_pairing_restarts_after_consumed_double_click",
        body: {
          var pendingClick: QuickLookPendingLocalClick?
          let start = Date(timeIntervalSince1970: 100)
          _ = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start,
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)
          _ = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start.addingTimeInterval(0.2),
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)
          let third = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start.addingTimeInterval(0.3),
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)

          try expect(third == .singleClick, "third click after a consumed pair should start a new pair")
        }),
      SelectionTestCase(
        name: "local_click_pairing_does_not_pair_different_rows",
        body: {
          var pendingClick: QuickLookPendingLocalClick?
          let start = Date(timeIntervalSince1970: 100)
          _ = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start,
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)
          let second = QuickLookClickPairing.consume(
            rowID: "row-b",
            now: start.addingTimeInterval(0.2),
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)

          try expect(second == .singleClick, "fast click on a different row should stay single")
          try expect(pendingClick?.rowID == "row-b", "different-row click should become the new pending click")
        }),
      SelectionTestCase(
        name: "local_click_pairing_does_not_pair_after_timeout",
        body: {
          var pendingClick: QuickLookPendingLocalClick?
          let start = Date(timeIntervalSince1970: 100)
          _ = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start,
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)
          let second = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start.addingTimeInterval(0.6),
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)

          try expect(second == .singleClick, "same-row click after timeout should stay single")
          try expect(pendingClick?.rowID == "row-a", "timed-out click should replace pending click")
        }),
      SelectionTestCase(
        name: "local_click_pairing_does_not_pair_reversed_time",
        body: {
          var pendingClick: QuickLookPendingLocalClick?
          let start = Date(timeIntervalSince1970: 100)
          _ = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start,
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)
          let second = QuickLookClickPairing.consume(
            rowID: "row-a",
            now: start.addingTimeInterval(-0.2),
            pendingClick: &pendingClick,
            doubleClickInterval: 0.5)

          try expect(second == .singleClick, "same-row click with reversed time should stay single")
          try expect(
            pendingClick?.date == start.addingTimeInterval(-0.2),
            "reversed-time click should replace the stale pending click")
        }),
      SelectionTestCase(
        name: "export_destination_puts_selected_file_directly_in_base_directory",
        body: {
          let baseDirectoryURL = URL(fileURLWithPath: "/tmp/archive-folder", isDirectory: true)
          let destination = QuickLookExportDestination.destinationURL(
            baseDirectoryURL: baseDirectoryURL,
            displayName: "notes.txt",
            exportsDirectory: false)

          try expect(
            destination?.path == "/tmp/archive-folder/notes.txt",
            "selected files should export directly into the base directory without archive or parent folders")
        }),
      SelectionTestCase(
        name: "export_destination_preserves_selected_folder_name",
        body: {
          let baseDirectoryURL = URL(fileURLWithPath: "/tmp/archive-folder", isDirectory: true)
          let destination = QuickLookExportDestination.destinationURL(
            baseDirectoryURL: baseDirectoryURL,
            displayName: "docs",
            exportsDirectory: true)

          try expect(
            destination?.path == "/tmp/archive-folder/docs",
            "selected folders should preserve the folder itself in the base directory")
        }),
      SelectionTestCase(
        name: "export_destination_rejects_blank_display_name",
        body: {
          let baseDirectoryURL = URL(fileURLWithPath: "/tmp/archive-folder", isDirectory: true)
          let emptyDestination = QuickLookExportDestination.destinationURL(
            baseDirectoryURL: baseDirectoryURL,
            displayName: "",
            exportsDirectory: false)
          let whitespaceDestination = QuickLookExportDestination.destinationURL(
            baseDirectoryURL: baseDirectoryURL,
            displayName: " \n\t",
            exportsDirectory: true)

          try expect(emptyDestination == nil, "empty display names should not produce export URLs")
          try expect(whitespaceDestination == nil, "blank display names should not produce export URLs")
        }),
      SelectionTestCase(
        name: "export_success_detail_uses_single_destination_or_base_directory",
        body: {
          let baseDirectoryURL = URL(fileURLWithPath: "/tmp/archive-folder", isDirectory: true)
          let single = QuickLookExportDestination.successDetail(
            destinationPaths: ["/tmp/archive-folder/notes.txt"],
            baseDirectoryURL: baseDirectoryURL)
          let multiple = QuickLookExportDestination.successDetail(
            destinationPaths: ["/tmp/archive-folder/a.txt", "/tmp/archive-folder/b.txt"],
            baseDirectoryURL: baseDirectoryURL)

          try expect(single == "/tmp/archive-folder/notes.txt", "single export should show the item destination")
          try expect(multiple == "/tmp/archive-folder", "multi export should show the base directory")
        }),
      SelectionTestCase(
        name: "export_default_base_directory_uses_archive_parent",
        body: {
          let baseDirectory = QuickLookExportDestination.defaultBaseDirectoryURL(
            forArchivePath: "/tmp/archive-folder/demo.zip")

          try expect(
            baseDirectory.path == "/tmp/archive-folder",
            "default Quick Look export base should be the previewed archive parent directory")
        }),
      SelectionTestCase(
        name: "row_width_uses_visible_width_and_insets",
        body: {
          let result = QuickLookRowLayoutMetrics.rowWidth(
            visibleWidth: 800,
            horizontalInsets: 12)
          try expect(result == 780, "row width should subtract insets and extra padding from visible width")
        }),
      SelectionTestCase(
        name: "row_width_falls_back_when_visible_width_is_invalid",
        body: {
          let zeroResult = QuickLookRowLayoutMetrics.rowWidth(
            visibleWidth: 0,
            horizontalInsets: 12)
          try expect(
            zeroResult == QuickLookRowLayoutMetrics.fallbackRowWidth,
            "invalid visible width should use fallback row width")

          let nanResult = QuickLookRowLayoutMetrics.rowWidth(
            visibleWidth: .nan,
            horizontalInsets: 12)
          try expect(
            nanResult == QuickLookRowLayoutMetrics.fallbackRowWidth,
            "non-finite visible width should use fallback row width")
        }),
      SelectionTestCase(
        name: "timestamp_width_stays_preferred_for_wide_rows",
        body: {
          let result = QuickLookRowLayoutMetrics.timestampMinWidth(forRowWidth: 900)
          try expect(
            result == QuickLookRowLayoutMetrics.preferredTimestampMinWidth,
            "wide rows should keep the preferred timestamp width")
        }),
      SelectionTestCase(
        name: "timestamp_width_compresses_for_narrow_rows",
        body: {
          let result = QuickLookRowLayoutMetrics.timestampMinWidth(forRowWidth: 560)
          try expect(
            result < QuickLookRowLayoutMetrics.preferredTimestampMinWidth,
            "narrow rows should compress timestamp width")
          try expect(
            result > QuickLookRowLayoutMetrics.minimumTimestampMinWidth,
            "mid-width rows should stay above the minimum timestamp width")
        }),
      SelectionTestCase(
        name: "timestamp_width_clamps_at_minimum_for_tight_rows",
        body: {
          let result = QuickLookRowLayoutMetrics.timestampMinWidth(forRowWidth: 320)
          try expect(
            result == QuickLookRowLayoutMetrics.minimumTimestampMinWidth,
            "tight rows should clamp to the minimum timestamp width")
        }),
      SelectionTestCase(
        name: "item_width_stability_requires_two_matching_samples",
        body: {
          var stability = QuickLookVisibleWidthStability()
          try expect(!stability.observe(width: 820), "first valid width should not immediately become stable")
          try expect(
            stability.observe(width: 820),
            "second matching item width should mark the probe as stable")
        }),
      SelectionTestCase(
        name: "item_width_stability_resets_when_width_changes",
        body: {
          var stability = QuickLookVisibleWidthStability()
          _ = stability.observe(width: 820)
          try expect(!stability.observe(width: 760), "width change should reset stability candidate")
          try expect(
            stability.observe(width: 760),
            "after reset, a second matching sample should become stable")
        }),
      SelectionTestCase(
        name: "item_width_stability_resets_on_zero_width",
        body: {
          var stability = QuickLookVisibleWidthStability()
          _ = stability.observe(width: 820)
          try expect(!stability.observe(width: 0), "zero width should reset stability")
          try expect(!stability.observe(width: 820), "after reset, next valid width should be treated as first sample")
        }),
      SelectionTestCase(
        name: "item_width_stability_resets_on_non_finite_width",
        body: {
          var stability = QuickLookVisibleWidthStability()
          _ = stability.observe(width: 820)
          try expect(!stability.observe(width: .nan), "non-finite width should reset stability")
          try expect(!stability.observe(width: 820), "after a non-finite sample, the next width should restart sampling")
        }),
      SelectionTestCase(
        name: "item_width_stability_tolerates_small_epsilon_differences",
        body: {
          var stability = QuickLookVisibleWidthStability()
          _ = stability.observe(width: 820)
          try expect(
            stability.observe(width: 820.4),
            "width changes within epsilon should still count as stable")
        }),
    ]

    for test in tests {
      do {
        try test.body()
        print("PASS \(test.name)")
      } catch {
        fputs("FAIL \(test.name): \(error)\n", stderr)
        Darwin.exit(1)
      }
    }
  }
}
