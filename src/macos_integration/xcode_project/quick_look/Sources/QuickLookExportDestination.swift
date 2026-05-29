import Foundation

enum QuickLookExportDestination {
  static func defaultBaseDirectoryURL(forArchivePath archivePath: String) -> URL {
    if !archivePath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
      let parentURL = URL(fileURLWithPath: archivePath).deletingLastPathComponent()
      if !parentURL.path.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
        return parentURL
      }
    }
    return fallbackBaseDirectoryURL()
  }

  static func destinationURL(baseDirectoryURL: URL,
                             displayName: String,
                             exportsDirectory: Bool) -> URL? {
    guard !displayName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
      return nil
    }
    return baseDirectoryURL.appendingPathComponent(displayName, isDirectory: exportsDirectory)
  }

  static func successDetail(destinationPaths: [String], baseDirectoryURL: URL) -> String {
    guard destinationPaths.count == 1 else {
      return baseDirectoryURL.path
    }
    return destinationPaths.first ?? baseDirectoryURL.path
  }

  private static func fallbackBaseDirectoryURL() -> URL {
    FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first ??
      URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Downloads", isDirectory: true)
  }
}
