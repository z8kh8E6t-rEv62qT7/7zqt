import AppKit
import FinderSync
import Foundation
import os.log

final class FinderSync: FIFinderSync {
  private enum LeafAction: String {
    case open
    case openAsStar = "open_as_star"
    case openAsHash = "open_as_hash"
    case openAsHashE = "open_as_hash_e"
    case openAs7z = "open_as_7z"
    case openAsZip = "open_as_zip"
    case openAsCab = "open_as_cab"
    case openAsRar = "open_as_rar"
    case extractFiles = "extract_files"
    case extractHere = "extract_here"
    case extractTo = "extract_to"
    case testArchive = "test_archive"
    case addToArchive = "add_to_archive"
    case addTo7z = "add_to_7z"
    case addToZip = "add_to_zip"
    case crc32
    case crc64
    case xxh64
    case md5
    case sha1
    case sha256
    case sha384
    case sha512
    case sha3_256
    case blake2sp
    case crcAll = "crc_all"
    case generateSha256 = "generate_sha256"
    case checksumTest = "checksum_test"

    var selector: Selector {
      switch self {
      case .open:
        return #selector(handleOpenAction(_:))
      case .openAsStar:
        return #selector(handleOpenAsStarAction(_:))
      case .openAsHash:
        return #selector(handleOpenAsHashAction(_:))
      case .openAsHashE:
        return #selector(handleOpenAsHashEAction(_:))
      case .openAs7z:
        return #selector(handleOpenAs7zAction(_:))
      case .openAsZip:
        return #selector(handleOpenAsZipAction(_:))
      case .openAsCab:
        return #selector(handleOpenAsCabAction(_:))
      case .openAsRar:
        return #selector(handleOpenAsRarAction(_:))
      case .extractFiles:
        return #selector(handleExtractFilesAction(_:))
      case .extractHere:
        return #selector(handleExtractHereAction(_:))
      case .extractTo:
        return #selector(handleExtractToAction(_:))
      case .testArchive:
        return #selector(handleTestArchiveAction(_:))
      case .addToArchive:
        return #selector(handleAddToArchiveAction(_:))
      case .addTo7z:
        return #selector(handleAddTo7zAction(_:))
      case .addToZip:
        return #selector(handleAddToZipAction(_:))
      case .crc32:
        return #selector(handleCrc32Action(_:))
      case .crc64:
        return #selector(handleCrc64Action(_:))
      case .xxh64:
        return #selector(handleXxh64Action(_:))
      case .md5:
        return #selector(handleMd5Action(_:))
      case .sha1:
        return #selector(handleSha1Action(_:))
      case .sha256:
        return #selector(handleSha256Action(_:))
      case .sha384:
        return #selector(handleSha384Action(_:))
      case .sha512:
        return #selector(handleSha512Action(_:))
      case .sha3_256:
        return #selector(handleSha3_256Action(_:))
      case .blake2sp:
        return #selector(handleBlake2spAction(_:))
      case .crcAll:
        return #selector(handleCrcAllAction(_:))
      case .generateSha256:
        return #selector(handleGenerateSha256Action(_:))
      case .checksumTest:
        return #selector(handleChecksumTestAction(_:))
      }
    }
  }

  private let logger = Logger(subsystem: "app.sevenzip.extension", category: "Menu")
  private let controller = FIFinderSyncController.default()

  override init() {
    super.init()
    controller.directoryURLs = [URL(fileURLWithPath: "/")]
  }

  override func menu(for menuKind: FIMenuKind) -> NSMenu? {
    switch menuKind {
    case .contextualMenuForItems, .contextualMenuForContainer:
      break
    default:
      return nil
    }

    let paths = selectedPaths()
    if paths.isEmpty {
      let targetedPath = controller.targetedURL()?.path ?? "(nil)"
      logger.error(
        "Finder menu skipped: no selection, kind=\(String(describing: menuKind), privacy: .public), targeted=\(targetedPath, privacy: .public)")
      return nil
    }

    guard let plan = BrokerClient.shared.fetchPlan(paths: paths, locale: Self.brokerLocaleHint()) else {
      logger.error(
        "Finder menu skipped: no plan returned, selected_count=\(paths.count), first_path=\(paths.first ?? "(nil)", privacy: .public)")
      return nil
    }

    if !plan.ok {
      logger.error(
        "Finder menu skipped: plan not ok, selected_count=\(paths.count), first_path=\(paths.first ?? "(nil)", privacy: .public), error=\(plan.errorMessage ?? "", privacy: .public)")
      return nil
    }

    if !plan.menuVisible {
      logger.error(
        "Finder menu skipped: menu hidden, selected_count=\(paths.count), first_path=\(paths.first ?? "(nil)", privacy: .public), enabled=\(String(describing: plan.menuVisible), privacy: .public), action_count=\(plan.actions.count), error=\(plan.errorMessage ?? "", privacy: .public)")
      return nil
    }

    let actions = plan.actions
    guard !actions.isEmpty else {
      logger.error(
        "Finder menu skipped: no actions, selected_count=\(paths.count), first_path=\(paths.first ?? "(nil)", privacy: .public), error=\(plan.errorMessage ?? "", privacy: .public)")
      return nil
    }

    return buildMenu(from: actions)
  }

  private func executeAction(_ action: LeafAction) {
    let actionID = action.rawValue
    let targetedPath = controller.targetedURL()?.path ?? "(nil)"
    let paths = selectedPaths()
    if paths.isEmpty {
      logger.error(
        "Finder action skipped: no selection at click time, action=\(actionID, privacy: .public), targeted=\(targetedPath, privacy: .public)")
      return
    }
    logger.error(
      "Finder action invoked: action=\(actionID, privacy: .public), selected_count=\(paths.count), first_path=\(paths.first ?? "(nil)", privacy: .public), targeted=\(targetedPath, privacy: .public)")

    guard let response = BrokerClient.shared.run(
      actionID: actionID,
      paths: paths,
      locale: Self.brokerLocaleHint()) else {
      logger.error(
        "Finder action failed: no response from bridge, action=\(actionID, privacy: .public), selected_count=\(paths.count), first_path=\(paths.first ?? "(nil)", privacy: .public), targeted=\(targetedPath, privacy: .public)")
      return
    }
    if !response.ok {
      logger.error(
        "Finder action failed: \(actionID, privacy: .public), error=\(response.errorMessage ?? "", privacy: .public)")
    }
  }

  @objc private func handleOpenAction(_ sender: NSMenuItem) { executeAction(.open) }
  @objc private func handleOpenAsStarAction(_ sender: NSMenuItem) { executeAction(.openAsStar) }
  @objc private func handleOpenAsHashAction(_ sender: NSMenuItem) { executeAction(.openAsHash) }
  @objc private func handleOpenAsHashEAction(_ sender: NSMenuItem) { executeAction(.openAsHashE) }
  @objc private func handleOpenAs7zAction(_ sender: NSMenuItem) { executeAction(.openAs7z) }
  @objc private func handleOpenAsZipAction(_ sender: NSMenuItem) { executeAction(.openAsZip) }
  @objc private func handleOpenAsCabAction(_ sender: NSMenuItem) { executeAction(.openAsCab) }
  @objc private func handleOpenAsRarAction(_ sender: NSMenuItem) { executeAction(.openAsRar) }
  @objc private func handleExtractFilesAction(_ sender: NSMenuItem) { executeAction(.extractFiles) }
  @objc private func handleExtractHereAction(_ sender: NSMenuItem) { executeAction(.extractHere) }
  @objc private func handleExtractToAction(_ sender: NSMenuItem) { executeAction(.extractTo) }
  @objc private func handleTestArchiveAction(_ sender: NSMenuItem) { executeAction(.testArchive) }
  @objc private func handleAddToArchiveAction(_ sender: NSMenuItem) { executeAction(.addToArchive) }
  @objc private func handleAddTo7zAction(_ sender: NSMenuItem) { executeAction(.addTo7z) }
  @objc private func handleAddToZipAction(_ sender: NSMenuItem) { executeAction(.addToZip) }
  @objc private func handleCrc32Action(_ sender: NSMenuItem) { executeAction(.crc32) }
  @objc private func handleCrc64Action(_ sender: NSMenuItem) { executeAction(.crc64) }
  @objc private func handleXxh64Action(_ sender: NSMenuItem) { executeAction(.xxh64) }
  @objc private func handleMd5Action(_ sender: NSMenuItem) { executeAction(.md5) }
  @objc private func handleSha1Action(_ sender: NSMenuItem) { executeAction(.sha1) }
  @objc private func handleSha256Action(_ sender: NSMenuItem) { executeAction(.sha256) }
  @objc private func handleSha384Action(_ sender: NSMenuItem) { executeAction(.sha384) }
  @objc private func handleSha512Action(_ sender: NSMenuItem) { executeAction(.sha512) }
  @objc private func handleSha3_256Action(_ sender: NSMenuItem) { executeAction(.sha3_256) }
  @objc private func handleBlake2spAction(_ sender: NSMenuItem) { executeAction(.blake2sp) }
  @objc private func handleCrcAllAction(_ sender: NSMenuItem) { executeAction(.crcAll) }
  @objc private func handleGenerateSha256Action(_ sender: NSMenuItem) { executeAction(.generateSha256) }
  @objc private func handleChecksumTestAction(_ sender: NSMenuItem) { executeAction(.checksumTest) }

  private func buildMenu(from actions: [Z7BrokerMenuAction]) -> NSMenu {
    let containerMenu = NSMenu(title: "7-Zip")
    let rootItem = NSMenuItem(title: "7-Zip", action: nil, keyEquivalent: "")
    let menu = buildSevenZipSubmenu(from: actions)
    containerMenu.addItem(rootItem)
    containerMenu.setSubmenu(menu, for: rootItem)
    return containerMenu
  }

  private func buildSevenZipSubmenu(from actions: [Z7BrokerMenuAction]) -> NSMenu {
    let menu = NSMenu(title: "7-Zip")
    var index = 0

    while index < actions.count {
      let action = actions[index]

      if action.actionID == "open_as" {
        let (submenu, consumed) = consumeSubmenu(
          title: action.title,
          source: actions,
          startIndex: index + 1,
          includeItem: { $0.hasPrefix("open_as_") })
        if let submenu {
          let rootItem = NSMenuItem(title: action.title, action: nil, keyEquivalent: "")
          menu.addItem(rootItem)
          menu.setSubmenu(submenu, for: rootItem)
        }
        index += consumed + 1
        continue
      }

      if action.actionID == "crc_sha_menu" {
        let (submenu, consumed) = consumeSubmenu(
          title: action.title,
          source: actions,
          startIndex: index + 1,
          includeItem: { id in
            switch id {
            case "crc32",
              "crc64",
              "xxh64",
              "md5",
              "sha1",
              "sha256",
              "sha384",
              "sha512",
              "sha3_256",
              "blake2sp",
              "crc_all",
              "generate_sha256",
              "checksum_test":
              return true
            default:
              return false
            }
          })
        if let submenu {
          let rootItem = NSMenuItem(title: action.title, action: nil, keyEquivalent: "")
          menu.addItem(rootItem)
          menu.setSubmenu(submenu, for: rootItem)
        }
        index += consumed + 1
        continue
      }

      menu.addItem(makeActionItem(action))
      index += 1
    }

    return menu
  }

  private func consumeSubmenu(
    title: String,
    source: [Z7BrokerMenuAction],
    startIndex: Int,
    includeItem: (String) -> Bool
  ) -> (NSMenu?, Int) {
    let submenu = NSMenu(title: title)
    var consumed = 0
    var cursor = startIndex

    while cursor < source.count {
      let action = source[cursor]
      if !includeItem(action.actionID) {
        break
      }
      submenu.addItem(makeActionItem(action))
      cursor += 1
      consumed += 1
    }

    return submenu.items.isEmpty ? (nil, consumed) : (submenu, consumed)
  }

  private func makeActionItem(_ action: Z7BrokerMenuAction) -> NSMenuItem {
    guard let leafAction = LeafAction(rawValue: action.actionID) else {
      logger.error("Finder menu item disabled: unknown leaf action id \(action.actionID, privacy: .public)")
      let disabled = NSMenuItem(title: action.title, action: nil, keyEquivalent: "")
      disabled.isEnabled = false
      return disabled
    }

    let item = NSMenuItem(title: action.title, action: leafAction.selector, keyEquivalent: "")
    return item
  }

  private static func brokerLocaleHint() -> String? {
    nil
  }

  private func selectedPaths() -> [String] {
    if let urls = controller.selectedItemURLs(), !urls.isEmpty {
      return urls.map(\.path).sorted()
    }
    if let targeted = controller.targetedURL() {
      return [targeted.path]
    }
    return []
  }
}

#if Z7_TESTING
extension FinderSync {
  static func z7TestingBuildMenu(from actions: [Z7BrokerMenuAction]) -> NSMenu {
    FinderSync().buildMenu(from: actions)
  }

  static func z7TestingBuildSevenZipSubmenu(from actions: [Z7BrokerMenuAction]) -> NSMenu {
    FinderSync().buildSevenZipSubmenu(from: actions)
  }

  static func z7TestingFetchPlan(paths: [String]) -> Z7BrokerMenuPlan? {
    BrokerClient.shared.fetchPlan(paths: paths, locale: brokerLocaleHint())
  }

  static func z7TestingRun(actionID: String, paths: [String]) -> Z7BrokerActionResult? {
    BrokerClient.shared.run(actionID: actionID, paths: paths, locale: brokerLocaleHint())
  }
}
#endif
