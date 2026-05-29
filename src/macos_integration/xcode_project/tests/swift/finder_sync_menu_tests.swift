import AppKit
import Darwin
import Foundation

final class Z7BrokerMenuAction {
  let actionID: String
  let title: String

  init(actionID: String, title: String) {
    self.actionID = actionID
    self.title = title
  }
}

final class Z7BrokerMenuPlan {
  let ok: Bool
  let status: Int
  let errorMessage: String?
  let menuVisible: Bool
  let actions: [Z7BrokerMenuAction]

  init(ok: Bool,
       status: Int,
       errorMessage: String?,
       menuVisible: Bool,
       actions: [Z7BrokerMenuAction])
  {
    self.ok = ok
    self.status = status
    self.errorMessage = errorMessage
    self.menuVisible = menuVisible
    self.actions = actions
  }
}

final class Z7BrokerActionResult {
  let ok: Bool
  let errorMessage: String?

  init(ok: Bool, errorMessage: String?) {
    self.ok = ok
    self.errorMessage = errorMessage
  }
}

final class BrokerClient {
  static let shared = BrokerClient()
  static var fetchCalled = false
  static var runCalled = false
  static var lastFetchLocale: String?
  static var lastRunLocale: String?

  static func resetTestingBehavior() {
    fetchCalled = false
    runCalled = false
    lastFetchLocale = nil
    lastRunLocale = nil
  }

  func fetchPlan(paths: [String], locale: String?) -> Z7BrokerMenuPlan? {
    _ = paths
    Self.fetchCalled = true
    Self.lastFetchLocale = locale
    return Z7BrokerMenuPlan(
      ok: true,
      status: 0,
      errorMessage: nil,
      menuVisible: true,
      actions: [])
  }

  func run(actionID: String, paths: [String], locale: String?) -> Z7BrokerActionResult? {
    _ = (actionID, paths)
    Self.runCalled = true
    Self.lastRunLocale = locale
    return Z7BrokerActionResult(ok: true, errorMessage: nil)
  }
}

struct TestFailure: Error, CustomStringConvertible {
  let description: String
}

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
  if !condition() {
    throw TestFailure(description: message)
  }
}

private struct FinderSyncMenuTestCase {
  let name: String
  let body: () throws -> Void
}

private func action(_ actionID: String, _ title: String) -> Z7BrokerMenuAction {
  Z7BrokerMenuAction(actionID: actionID, title: title)
}

private func selectorName(_ item: NSMenuItem) -> String {
  item.action.map { NSStringFromSelector($0) } ?? ""
}

@main
enum FinderSyncMenuTestMain {
  static func main() throws {
    let tests = [
      FinderSyncMenuTestCase(
        name: "finder_sync_sends_no_system_locale_hint_to_broker",
        body: {
          BrokerClient.resetTestingBehavior()
          _ = FinderSync.z7TestingFetchPlan(paths: ["/tmp/payload.7z"])
          try expect(BrokerClient.fetchCalled, "test fetch should invoke broker fetchPlan")
          try expect(BrokerClient.lastFetchLocale == nil, "Finder Sync menu plan should not pass system locale")

          BrokerClient.resetTestingBehavior()
          _ = FinderSync.z7TestingRun(actionID: "extract_here", paths: ["/tmp/payload.7z"])
          try expect(BrokerClient.runCalled, "test run should invoke broker run")
          try expect(BrokerClient.lastRunLocale == nil, "Finder Sync action should not pass system locale")
        }),
      FinderSyncMenuTestCase(
        name: "finder_menu_wraps_actions_in_seven_zip_root_item",
        body: {
          let menu = FinderSync.z7TestingBuildMenu(from: [
            action("extract_here", "Extract Here"),
          ])

          try expect(menu.items.count == 1, "container menu should expose one 7-Zip root item")
          let rootItem = menu.items[0]
          try expect(rootItem.title == "7-Zip", "root menu item should be titled 7-Zip")
          try expect(rootItem.submenu?.items.count == 1, "root item should own the action submenu")
          try expect(
            rootItem.submenu?.items.first?.title == "Extract Here",
            "root submenu should contain broker leaf actions")
        }),
      FinderSyncMenuTestCase(
        name: "finder_menu_groups_open_as_and_crc_actions",
        body: {
          let menu = FinderSync.z7TestingBuildSevenZipSubmenu(from: [
            action("open_as", "Open As"),
            action("open_as_7z", "7z"),
            action("open_as_zip", "zip"),
            action("extract_here", "Extract Here"),
            action("crc_sha_menu", "CRC SHA"),
            action("crc32", "CRC-32"),
            action("crc64", "CRC-64"),
            action("xxh64", "XXH64"),
            action("md5", "MD5"),
            action("sha1", "SHA-1"),
            action("sha256", "SHA-256"),
            action("sha384", "SHA-384"),
            action("sha512", "SHA-512"),
            action("sha3_256", "SHA3-256"),
            action("blake2sp", "BLAKE2sp"),
            action("crc_all", "*"),
            action("generate_sha256", "SHA-256 -> notes.txt.sha256"),
            action("checksum_test", "Checksum : Test"),
            action("add_to_archive", "Add to archive"),
          ])

          try expect(menu.items.map(\.title) == [
            "Open As",
            "Extract Here",
            "CRC SHA",
            "Add to archive",
          ], "submenu roots and leaf actions should keep broker order")

          let openAsItems = menu.items[0].submenu?.items ?? []
          try expect(openAsItems.map(\.title) == ["7z", "zip"], "Open As should consume open_as_* children")
          try expect(
            openAsItems.map(selectorName) == ["handleOpenAs7zAction:", "handleOpenAsZipAction:"],
            "Open As children should map to the expected action selectors")

          let crcItems = menu.items[2].submenu?.items ?? []
          try expect(
            crcItems.map(\.title) == [
              "CRC-32",
              "CRC-64",
              "XXH64",
              "MD5",
              "SHA-1",
              "SHA-256",
              "SHA-384",
              "SHA-512",
              "SHA3-256",
              "BLAKE2sp",
              "*",
              "SHA-256 -> notes.txt.sha256",
              "Checksum : Test",
            ],
            "CRC menu should consume every declared checksum child")
          try expect(
            crcItems.map(selectorName) == [
              "handleCrc32Action:",
              "handleCrc64Action:",
              "handleXxh64Action:",
              "handleMd5Action:",
              "handleSha1Action:",
              "handleSha256Action:",
              "handleSha384Action:",
              "handleSha512Action:",
              "handleSha3_256Action:",
              "handleBlake2spAction:",
              "handleCrcAllAction:",
              "handleGenerateSha256Action:",
              "handleChecksumTestAction:",
            ],
            "CRC children should map to checksum action selectors")

          try expect(
            selectorName(menu.items[1]) == "handleExtractHereAction:",
            "ordinary leaf actions should map directly")
          try expect(
            selectorName(menu.items[3]) == "handleAddToArchiveAction:",
            "leaf action after a submenu should remain visible")
        }),
      FinderSyncMenuTestCase(
        name: "finder_menu_disables_unknown_leaf_actions",
        body: {
          let menu = FinderSync.z7TestingBuildSevenZipSubmenu(from: [
            action("future_action", "Future Action"),
          ])

          try expect(menu.items.count == 1, "unknown broker action should still be represented")
          try expect(!menu.items[0].isEnabled, "unknown broker action should be disabled")
          try expect(selectorName(menu.items[0]).isEmpty, "disabled unknown action should not have a selector")
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
