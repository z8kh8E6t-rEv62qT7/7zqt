import Foundation

struct QuickLookItem {
  let path: String
  let name: String
  let isDirectory: Bool
  let isArchiveLike: Bool
  let isSyntheticArchiveRoot: Bool
  let size: UInt64
  let mtimeMsUtc: Int64
}

struct NestedFrame {
  let entry: String
  let enteredFromVirtualDir: String
}

struct QuickLookNavigationSnapshot {
  let virtualDir: String
  let nestedStack: [NestedFrame]
}

struct QuickLookOperationFailure: Error {
  let message: String
  let completedItemCount: Int
  let totalItemCount: Int
  let failedItemIndex: Int
  let failedEntryPath: String?
  let failedDestinationPath: String?

  var isDirectoryBudgetExceeded: Bool {
    let lowered = message.lowercased()
    return lowered.contains("budget exceeded") ||
      lowered.contains("directory too large") ||
      lowered.contains("max_files") ||
      lowered.contains("max_bytes") ||
      lowered.contains("1000 files") ||
      lowered.contains("1 gib")
  }
}

class AsyncTaskContext {
  weak var owner: QuickLookPreviewController?
  let requestID: String

  private let lock = NSLock()
  private let brokerClient: BrokerClient
  private var callbackFinished = false

  init(owner: QuickLookPreviewController?, requestID: String, brokerClient: BrokerClient) {
    self.owner = owner
    self.requestID = requestID
    self.brokerClient = brokerClient
  }

  func markCallbackFinished() -> Bool {
    lock.lock()
    if callbackFinished {
      lock.unlock()
      return false
    }
    callbackFinished = true
    lock.unlock()
    return true
  }

  func cancelAndReleasePendingTask() {
    lock.lock()
    if callbackFinished {
      lock.unlock()
      return
    }
    callbackFinished = true
    lock.unlock()
    brokerClient.cancelRequest(withID: requestID)
  }
}

final class ListRequestContext: AsyncTaskContext {
  let generation: UInt64
  let nestedEntries: [String]
  let completion: (Bool) -> Void

  init(owner: QuickLookPreviewController,
       requestID: String,
       generation: UInt64,
       nestedEntries: [String],
       completion: @escaping (Bool) -> Void)
  {
    self.generation = generation
    self.nestedEntries = nestedEntries
    self.completion = completion
    super.init(owner: owner, requestID: requestID, brokerClient: owner.brokerClient)
  }
}

final class ExportRequestContext: AsyncTaskContext {
  let completion: (Result<Z7BrokerQuickLookBatchExportResult, QuickLookOperationFailure>) -> Void

  init(owner: QuickLookPreviewController?,
       requestID: String,
       completion: @escaping (Result<Z7BrokerQuickLookBatchExportResult, QuickLookOperationFailure>) -> Void) {
    self.completion = completion
    super.init(owner: owner, requestID: requestID, brokerClient: owner?.brokerClient ?? BrokerClient.shared)
  }
}
