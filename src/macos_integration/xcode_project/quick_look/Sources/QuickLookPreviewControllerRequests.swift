import AppKit

private let brokerStatusPasswordRequired = 6

extension QuickLookPreviewController {
  func trackContext(_ context: AsyncTaskContext) {
    contextLock.lock()
    activeContexts[ObjectIdentifier(context)] = context
    contextLock.unlock()
  }

  func untrackContext(_ context: AsyncTaskContext) {
    contextLock.lock()
    activeContexts.removeValue(forKey: ObjectIdentifier(context))
    contextLock.unlock()
  }

  func cancelAllActiveContexts() {
    contextLock.lock()
    latestListGeneration &+= 1
    let contexts = Array(activeContexts.values)
    activeContexts.removeAll()
    contextLock.unlock()

    for context in contexts {
      context.cancelAndReleasePendingTask()
    }
  }

  func cancelActiveListContexts() {
    contextLock.lock()
    latestListGeneration &+= 1
    let keysToCancel = activeContexts.compactMap { key, context in
      context is ListRequestContext ? key : nil
    }
    let contextsToCancel = keysToCancel.compactMap { activeContexts[$0] }
    for key in keysToCancel {
      activeContexts.removeValue(forKey: key)
    }
    contextLock.unlock()

    for context in contextsToCancel {
      context.cancelAndReleasePendingTask()
    }
  }

  func nextListGeneration() -> UInt64 {
    contextLock.lock()
    latestListGeneration &+= 1
    let generation = latestListGeneration
    contextLock.unlock()
    return generation
  }

  func currentListGeneration() -> UInt64 {
    contextLock.lock()
    let generation = latestListGeneration
    contextLock.unlock()
    return generation
  }

  func isLatestListGeneration(_ generation: UInt64) -> Bool {
    contextLock.lock()
    let latest = latestListGeneration
    contextLock.unlock()
    return generation == latest
  }

  func reloadCurrentDirectory(completion: @escaping (Bool) -> Void) {
    if virtualDir.isEmpty && nestedStack.isEmpty {
      cancelActiveListContexts()
      clearProgressState()
      if hasCompletedInitialSyntheticRootReveal {
        clearInitialSyntheticRootProbeGeneration()
        showSyntheticArchiveRootRow(pendingInitialReveal: false)
      } else {
        let generation = currentListGeneration()
        setInitialSyntheticRootProbeGeneration(generation)
        showSyntheticArchiveRootRow(pendingInitialReveal: true)
      }
      completion(true)
      return
    }

    clearInitialSyntheticRootProbeGeneration()
    cancelActiveListContexts()
    let generation = nextListGeneration()
    let nestedEntries = currentNestedEntries()
    setLoadingProgressState(pathDisplayString())

    let requestID = UUID().uuidString
    let context = ListRequestContext(
      owner: self,
      requestID: requestID,
      generation: generation,
      nestedEntries: nestedEntries,
      completion: completion)
    trackContext(context)
    brokerClient.list(
      archivePath: archivePath,
      virtualDir: stripBaseNamePrefixIfNeeded(virtualDir),
      archiveTypeHint: archiveTypeHint,
      nestedArchiveEntries: nestedEntries,
      requestID: requestID
    ) { [weak self, context] result in
      guard context.markCallbackFinished() else {
        return
      }
      defer {
        context.owner?.untrackContext(context)
      }

      var nextItems = [QuickLookItem]()
      var errorMessage: String?
      var succeeded = false

      if result.ok {
        succeeded = true
        for item in result.items {
          nextItems.append(
            QuickLookItem(
              path: context.owner?.prefixBaseNameIfNeeded(item.path) ?? item.path,
              name: item.name,
              isDirectory: item.directory,
              isArchiveLike: item.archiveLike,
              isSyntheticArchiveRoot: false,
              size: item.size,
              mtimeMsUtc: item.mtimeMsUtc))
        }
        nextItems.sort {
          if $0.isDirectory != $1.isDirectory {
            return $0.isDirectory && !$1.isDirectory
          }
          return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
      } else {
        errorMessage = result.errorMessage ?? "Unknown list error"
      }

      guard let owner = context.owner ?? self else {
        context.completion(false)
        return
      }
      guard owner.isLatestListGeneration(context.generation) else {
        context.completion(true)
        return
      }

      owner.clearProgressState()
      if succeeded {
        owner.hideInlineOverlay()
        owner.replaceDisplayedItems(nextItems)
        context.completion(true)
        return
      }

      let message = errorMessage ?? QuickLookLocalization.text("quicklook.unknown_list_error")
      if result.status == brokerStatusPasswordRequired {
        owner.hideInlineOverlay()
        if !owner.nestedStack.isEmpty {
          let frame = owner.nestedStack.removeLast()
          owner.virtualDir = frame.enteredFromVirtualDir
          owner.reloadCurrentDirectory { _ in }
        } else {
          owner.virtualDir = ""
          owner.showSyntheticArchiveRootRow(pendingInitialReveal: false)
        }
        context.completion(false)
        return
      }

      if !context.nestedEntries.isEmpty &&
        message.localizedCaseInsensitiveContains("nested level")
      {
        owner.showInlineBanner(
          title: QuickLookLocalization.text(
            "quicklook.nested_open_failed_title"),
          message: message,
          buttonTitle: QuickLookLocalization.text(
            "quicklook.nested_open_failed_back")
        ) { [weak owner] in
          guard let owner else {
            return
          }
          owner.hideInlineOverlay()
          guard !owner.nestedStack.isEmpty else {
            return
          }
          let frame = owner.nestedStack.removeLast()
          owner.virtualDir = frame.enteredFromVirtualDir
          owner.reloadCurrentDirectory { _ in }
        }
      } else {
        owner.showError(message)
      }
      context.completion(false)
    }
  }

  func showError(_ message: String) {
    DispatchQueue.main.async { [weak self] in
      guard let self else {
        return
      }
      let alert = NSAlert()
      alert.messageText = QuickLookLocalization.text("quicklook.title")
      alert.informativeText = message
      alert.alertStyle = .warning
      alert.addButton(withTitle: QuickLookLocalization.text("quicklook.button_ok"))
      alert.beginSheetModal(for: self.view.window ?? NSWindow())
    }
  }

  func handleInitialSyntheticRootProbeWidthStable(generation: UInt64) {
    guard pendingInitialSyntheticRootRevealGeneration == generation else {
      return
    }

    guard isLatestListGeneration(generation) else {
      clearInitialSyntheticRootProbeGeneration()
      return
    }
    guard case .browse = viewModel.mode else {
      clearInitialSyntheticRootProbeGeneration()
      return
    }
    guard virtualDir.isEmpty, nestedStack.isEmpty else {
      clearInitialSyntheticRootProbeGeneration()
      return
    }

    hasCompletedInitialSyntheticRootReveal = true
    clearInitialSyntheticRootProbeGeneration()
    revealPendingInitialSyntheticRootRowIfNeeded()
  }

  func exportItems(_ items: [SelectedExportItem],
                   completion: @escaping (Result<Z7BrokerQuickLookBatchExportResult, QuickLookOperationFailure>) -> Void) {
    guard !items.isEmpty else {
      completion(.failure(QuickLookOperationFailure(
        message: QuickLookLocalization.text("quicklook.no_items_selected"),
        completedItemCount: 0,
        totalItemCount: 0,
        failedItemIndex: -1,
        failedEntryPath: nil,
        failedDestinationPath: nil)))
      return
    }

    let nestedEntries = items.first?.nestedEntries ?? []
    let batchItems = items.map {
      Z7BrokerQuickLookBatchExportItem(
        entryPath: archiveRelativeEntryPath($0.entryPath, nestedEntries: $0.nestedEntries),
        destinationPath: $0.destinationPath,
        listedSize: $0.listedSize,
        recursive: $0.recursive,
        entryIsDirectory: $0.entryIsDirectory)
    }

    let requestID = UUID().uuidString
    let context = ExportRequestContext(
      owner: self,
      requestID: requestID,
      completion: completion)
    trackContext(context)
    brokerClient.batchExport(
      archivePath: archivePath,
      archiveTypeHint: archiveTypeHint,
      nestedArchiveEntries: nestedEntries,
      items: batchItems,
      requestID: requestID,
      progress: { [weak self, context] progress in
        guard let owner = context.owner ?? self else {
          return
        }
        let total = max(progress.totalItemCount, 1)
        let counterText = "\(progress.completedItemCount) / \(progress.totalItemCount)"
        let fraction: Double?
        if progress.currentPercent >= 0 {
          fraction =
            (Double(progress.completedItemCount) + Double(progress.currentPercent) / 100.0) /
            Double(total)
        } else {
          fraction = nil
        }
        owner.setExportProgressState(
          title: QuickLookLocalization.text(
            "quicklook.quicklook_exporting"),
          detail: progress.currentDestinationPath,
          fraction: fraction,
          counterText: counterText,
          currentPath: progress.currentPath ?? progress.currentEntryPath,
          message: progress.message ?? "")
      },
      completion: { [context] result in
        guard context.markCallbackFinished() else {
          return
        }
        defer {
          context.owner?.untrackContext(context)
        }

        guard result.ok else {
          let message = result.errorMessage ?? QuickLookLocalization.text(
            "quicklook.quicklook_export_failed")
          context.completion(.failure(QuickLookOperationFailure(
            message: message,
            completedItemCount: result.completedItemCount,
            totalItemCount: result.totalItemCount,
            failedItemIndex: result.failedItemIndex,
            failedEntryPath: result.failedEntryPath,
            failedDestinationPath: result.failedDestinationPath)))
          return
        }

        context.completion(.success(result))
      })
  }
}
