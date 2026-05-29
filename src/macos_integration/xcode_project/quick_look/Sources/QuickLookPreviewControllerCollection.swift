import AppKit
import UniformTypeIdentifiers

extension QuickLookPreviewController {
  func iconForItem(_ item: QuickLookItem) -> NSImage {
    if item.isDirectory || item.isSyntheticArchiveRoot {
      return NSWorkspace.shared.icon(for: .folder)
    }

    let fileExtension = (item.name as NSString).pathExtension
    let contentType = UTType(filenameExtension: fileExtension) ?? .data
    let baseImage = NSWorkspace.shared.icon(for: contentType)
    guard item.isArchiveLike else {
      return baseImage
    }

    let size = NSSize(width: 18, height: 18)
    let badgeSymbol = NSImage(
      systemSymbolName: "play.fill",
      accessibilityDescription: nil)
    let badgeConfiguration = NSImage.SymbolConfiguration(
      pointSize: 6,
      weight: .bold)
    let tintedBadge = badgeSymbol?.withSymbolConfiguration(badgeConfiguration)

    return NSImage(size: size, flipped: false) { _ in
      baseImage.draw(in: NSRect(origin: .zero, size: size))

      let badgeRect = NSRect(x: size.width - 8.5, y: 0.5, width: 8, height: 8)
      let badgeBackground = NSBezierPath(ovalIn: badgeRect)
      NSColor.controlAccentColor.setFill()
      badgeBackground.fill()

      if let tintedBadge {
        let imageRect = NSRect(
          x: badgeRect.minX + 2.2,
          y: badgeRect.minY + 1.8,
          width: 3.8,
          height: 4.2)
        NSColor.white.set()
        tintedBadge.draw(in: imageRect)
      }
      return true
    }
  }

  func activateNavigableItem(at index: Int) {
    guard !isInteractionBlocked else {
      return
    }
    guard index >= 0, index < items.count else {
      return
    }
    let item = items[index]
    guard item.isDirectory || item.isSyntheticArchiveRoot || item.isArchiveLike else {
      return
    }
    activateItem(item)
  }

  func performPrimarySelectionAction() {
    guard !isInteractionBlocked else {
      return
    }
    guard let index = selectedItemIndexes.first else {
      return
    }
    activateNavigableItem(at: index)
  }

  func activateItem(_ item: QuickLookItem) {
    if item.isSyntheticArchiveRoot {
      virtualDir = archiveBaseName
      reloadCurrentDirectory { _ in }
      return
    }
    if item.isArchiveLike {
      guard nestedStack.count < 5 else {
        showToast(
          QuickLookLocalization.text(
            "quicklook.nested_depth_exceeded"))
        return
      }
      let entry = stripBaseNamePrefixIfNeeded(item.path)
      nestedStack.append(.init(entry: entry, enteredFromVirtualDir: virtualDir))
      virtualDir = ""
      reloadCurrentDirectory { _ in }
      return
    }
    guard item.isDirectory else {
      return
    }
    virtualDir = item.path
    reloadCurrentDirectory { _ in }
  }

  func replaceDisplayedItems(_ newItems: [QuickLookItem]) {
    items = newItems
    rowModels = newItems.map { rowModel(for: $0) }
    selectedItemIndexes = []
    updatePresentation()
  }

  func showSyntheticArchiveRootRow(pendingInitialReveal: Bool) {
    let syntheticRootItem = syntheticArchiveRootItem()
    items = [syntheticRootItem]
    rowModels = [rowModel(for: syntheticRootItem, isPendingInitialReveal: pendingInitialReveal)]
    selectedItemIndexes = []
    updatePresentation()
  }

  func revealPendingInitialSyntheticRootRowIfNeeded() {
    guard items.count == 1,
          items[0].isSyntheticArchiveRoot,
          rowModels.count == 1,
          rowModels[0].isPendingInitialReveal
    else {
      return
    }

    rowModels = [rowModel(for: items[0], isPendingInitialReveal: false)]
    updatePresentation()
  }

  func setSelectedItemIndexes(_ indexes: IndexSet) {
    var sanitized = IndexSet()
    for index in indexes where index >= 0 && index < items.count {
      sanitized.insert(index)
    }
    if sanitized == selectedItemIndexes {
      return
    }
    selectedItemIndexes = sanitized
    updatePresentation()
  }

  func updatePresentation() {
    viewModel.fileName = displayArchiveName()
    viewModel.filePath = currentAbsolutePathDisplay()
    viewModel.initialSyntheticRootProbeGeneration = pendingInitialSyntheticRootRevealGeneration
    viewModel.rows = rowModels
    viewModel.selectedIndexes = selectedItemIndexes
    viewModel.backEnabled =
      !isInteractionBlocked &&
      !extractSelectedBatchRunning &&
      (!virtualDir.isEmpty || !nestedStack.isEmpty)
    viewModel.extractEnabled =
      !isInteractionBlocked && !extractSelectedBatchRunning && !selectedItemIndexes.isEmpty
    if case .browse = viewModel.mode, viewModel.progressState == nil {
      viewModel.infoText = infoText()
    }
  }

  func setLoadingProgressState(_ detail: String) {
    viewModel.progressState = QuickLookProgressState(
      title: QuickLookLocalization.text(
        "quicklook.quicklook_loading"),
      detail: detail,
      fraction: nil,
      counterText: "",
      currentPath: "",
      message: "")
  }

  func setExportProgressState(title: String,
                              detail: String,
                              fraction: Double?,
                              counterText: String,
                              currentPath: String,
                              message: String) {
    viewModel.progressState = QuickLookProgressState(
      title: title,
      detail: detail,
      fraction: fraction,
      counterText: counterText,
      currentPath: currentPath,
      message: message)
  }

  func clearProgressState() {
    viewModel.progressState = nil
    viewModel.infoText = infoText()
  }

  private func displayArchiveName() -> String {
    return URL(fileURLWithPath: archivePath).lastPathComponent
  }

  private func currentAbsolutePathDisplay() -> String {
    var components = [archivePath]

    if nestedStack.isEmpty {
      if !virtualDir.isEmpty {
        components.append(virtualDir)
      }
      return components.joined(separator: "/")
    }

    let chain = nestedStack.compactMap { frame -> String? in
      let lastComponent = (frame.entry as NSString).lastPathComponent
      return lastComponent.isEmpty ? nil : lastComponent
    }
    components.append(contentsOf: chain)

    if !virtualDir.isEmpty {
      let trimmed = virtualDir.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
      if !trimmed.isEmpty {
        components.append(trimmed)
      }
    }

    return components.joined(separator: "/")
  }

  func pathDisplayString() -> String {
    let archiveDisplayName = URL(fileURLWithPath: archivePath).lastPathComponent
    if nestedStack.isEmpty && virtualDir.isEmpty {
      return archiveDisplayName
    }
    if nestedStack.isEmpty {
      return "\(archiveDisplayName) : /\(virtualDir)"
    }
    let chain = nestedStack
      .map { frame in
        let lastComponent = (frame.entry as NSString).lastPathComponent
        return lastComponent.isEmpty ? frame.entry : lastComponent
      }
      .joined(separator: " › ")
    let displayDir = virtualDir.isEmpty ? "/" : "/\(virtualDir)"
    return "\(archiveDisplayName) › \(chain) : \(displayDir)"
  }

  private func infoText() -> String {
    if selectedItemIndexes.count > 1 {
      return QuickLookLocalization.format(
        "quicklook.info_selected_count",
        [String(selectedItemIndexes.count)])
    }
    if selectedItemIndexes.count == 1 {
      return QuickLookLocalization.text("quicklook.info_selected_one")
    }
    if nestedStack.isEmpty && virtualDir.isEmpty {
      return QuickLookLocalization.text("quicklook.info_archive_ready")
    }
    let displayDir = virtualDir.isEmpty ? "/" : "/\(virtualDir)"
    return QuickLookLocalization.format("quicklook.info_browsing", [displayDir])
  }

  private func rowModel(for item: QuickLookItem,
                        isPendingInitialReveal: Bool = false) -> QuickLookListRowModel {
    QuickLookListRowModel(
      id: [
        item.path,
        item.isDirectory ? "dir" : "file",
        item.isArchiveLike ? "archive" : "plain",
        item.isSyntheticArchiveRoot ? "root" : "node",
      ].joined(separator: "|"),
      title: item.isDirectory ? "\(item.name)/" : item.name,
      detailText: detailText(for: item),
      sizeText: sizeText(for: item),
      modifiedText: modifiedText(for: item),
      icon: iconForItem(item),
      isDirectory: item.isDirectory,
      isArchiveLike: item.isArchiveLike,
      isSyntheticArchiveRoot: item.isSyntheticArchiveRoot,
      isPendingInitialReveal: isPendingInitialReveal)
  }

  private func detailText(for item: QuickLookItem) -> String {
    if item.isSyntheticArchiveRoot {
      return QuickLookLocalization.text(
        "quicklook.quicklook_root_item")
    }
    if item.isArchiveLike && !item.isDirectory {
      return QuickLookLocalization.text(
        "quicklook.quicklook_nested_archive")
    }
    if item.isDirectory {
      return QuickLookLocalization.text(
        "quicklook.quicklook_folder_item")
    }
    return ""
  }

  private func sizeText(for item: QuickLookItem) -> String {
    ByteCountFormatter.string(
      fromByteCount: Int64(item.size),
      countStyle: .file)
  }

  private func modifiedText(for item: QuickLookItem) -> String {
    let normalizedMtimeMsUtc = item.mtimeMsUtc < 0 ? Int64(0) : item.mtimeMsUtc
    let date = Date(timeIntervalSince1970: Double(normalizedMtimeMsUtc) / 1000.0)
    return dateFormatter.string(from: date)
  }

  func selectedExportItems() -> [SelectedExportItem] {
    let baseDirectoryURL = QuickLookExportDestination.defaultBaseDirectoryURL(
      forArchivePath: archivePath)
    let nestedEntries = currentNestedEntries()
    return selectedItemIndexes.compactMap { index in
      guard index >= 0, index < items.count else {
        return nil
      }
      let item = items[index]
      let displayName = item.isSyntheticArchiveRoot ? archiveBaseName : item.name
      guard !displayName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
        return nil
      }
      let exportsDirectory = item.isSyntheticArchiveRoot || item.isDirectory
      guard let destinationURL = QuickLookExportDestination.destinationURL(
        baseDirectoryURL: baseDirectoryURL,
        displayName: displayName,
        exportsDirectory: exportsDirectory)
      else {
        return nil
      }
      return SelectedExportItem(
        entryPath: item.path,
        nestedEntries: nestedEntries,
        recursive: exportsDirectory,
        entryIsDirectory: exportsDirectory,
        destinationPath: destinationURL.path,
        listedSize: item.size)
    }
  }

  func stripBaseNamePrefixIfNeeded(_ path: String) -> String {
    guard nestedStack.isEmpty else {
      return path
    }
    return stripBaseNamePrefix(path)
  }

  func archiveRelativeEntryPath(_ path: String, nestedEntries: [String]) -> String {
    guard nestedEntries.isEmpty else {
      return path
    }
    return stripBaseNamePrefix(path)
  }

  private func stripBaseNamePrefix(_ path: String) -> String {
    guard !archiveBaseName.isEmpty else {
      return path
    }
    if path == archiveBaseName {
      return ""
    }
    let prefix = archiveBaseName + "/"
    if path.hasPrefix(prefix) {
      return String(path.dropFirst(prefix.count))
    }
    return path
  }

  func prefixBaseNameIfNeeded(_ path: String) -> String {
    guard nestedStack.isEmpty else {
      return path
    }
    guard !archiveBaseName.isEmpty else {
      return path
    }
    if path.isEmpty {
      return archiveBaseName
    }
    return archiveBaseName + "/" + path
  }

  func syntheticArchiveRootItem() -> QuickLookItem {
    let attributes = (try? FileManager.default.attributesOfItem(atPath: archivePath)) ?? [:]
    let size = (attributes[.size] as? NSNumber)?.uint64Value ?? 0
    let modifiedDate = attributes[.modificationDate] as? Date
    let mtimeMsUtc = modifiedDate.map { Int64($0.timeIntervalSince1970 * 1000.0) } ?? -1
    return QuickLookItem(
      path: archiveBaseName,
      name: archiveBaseName,
      isDirectory: true,
      isArchiveLike: false,
      isSyntheticArchiveRoot: true,
      size: size,
      mtimeMsUtc: mtimeMsUtc)
  }
}
