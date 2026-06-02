import Darwin
import Foundation

enum QuickLookRealUserHome {
  static func settingsRootURL() -> URL? {
    settingsRootURL(
      posixHome: posixUserHomeDirectory(),
      environmentHome: ProcessInfo.processInfo.environment["HOME"])
  }

  static func downloadsURL() -> URL? {
    homeURL(
      posixHome: posixUserHomeDirectory(),
      environmentHome: ProcessInfo.processInfo.environment["HOME"])?
      .appendingPathComponent("Downloads", isDirectory: true)
  }

  private static func settingsRootURL(posixHome: String?, environmentHome: String?) -> URL? {
    homeURL(posixHome: posixHome, environmentHome: environmentHome)?
      .appendingPathComponent(".config", isDirectory: true)
      .appendingPathComponent("7zqt", isDirectory: true)
      .standardizedFileURL
  }

  private static func homeURL(posixHome: String?, environmentHome: String?) -> URL? {
    if let posixHomeURL = realUserHomeURL(from: posixHome) {
      return posixHomeURL
    }
    return realUserHomeURL(from: environmentHome)
  }

  private static func posixUserHomeDirectory() -> String? {
    var pwd = passwd()
    var result: UnsafeMutablePointer<passwd>?
    var buffer = [CChar](repeating: 0, count: 16_384)
    let status = getpwuid_r(getuid(), &pwd, &buffer, buffer.count, &result)
    guard status == 0, result != nil, let directory = pwd.pw_dir else {
      return nil
    }
    return String(cString: directory)
  }

  private static func realUserHomeURL(from value: String?) -> URL? {
    guard let trimmed = value?.trimmingCharacters(in: .whitespacesAndNewlines),
          !trimmed.isEmpty,
          trimmed.hasPrefix("/") else {
      return nil
    }
    let url = URL(fileURLWithPath: trimmed, isDirectory: true).standardizedFileURL
    return isSandboxContainerHome(url.path) ? nil : url
  }

  private static func isSandboxContainerHome(_ path: String) -> Bool {
    path.contains("/Library/Containers/") ||
      path.contains("/Library/Group Containers/")
  }
}

#if Z7_TESTING
extension QuickLookRealUserHome {
  static func z7TestingSettingsRootURL(
    posixHome: String?,
    environmentHome: String?
  ) -> URL? {
    settingsRootURL(posixHome: posixHome, environmentHome: environmentHome)
  }
}
#endif
