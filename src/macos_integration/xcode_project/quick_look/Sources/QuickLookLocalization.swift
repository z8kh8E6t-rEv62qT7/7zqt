import Foundation

private struct QuickLookLocalizationFile: Decodable {
  let quicklook: [String: String]?
}

enum QuickLookLocalization {
  private static let tableLock = NSLock()
  private static var loadedTables: [String: [String: String]] = [:]

  private static func defaultSettingsRootURL() -> URL {
    FileManager.default.homeDirectoryForCurrentUser
      .appendingPathComponent(".config", isDirectory: true)
      .appendingPathComponent("7zqt", isDirectory: true)
  }

  static func localeKey(from hint: String?) -> String {
    let locale = normalizedLanguageHint(hint)?.lowercased() ?? "en"
    if locale.hasPrefix("zh") {
      return "zh-CN"
    }
    return "en"
  }

  static func preferredLocaleKey(
    settingsRootURLs: [URL] = [defaultSettingsRootURL()]
  ) -> String {
    for rootURL in settingsRootURLs {
      if let snapshotHint = normalizedLanguageHint(
        jsonString(
          at: rootURL.appendingPathComponent("macos_integration.json"),
          path: ["locale_preferred"]))
      {
        return localeKey(from: snapshotHint)
      }
      if let settingsHint = normalizedLanguageHint(
        jsonString(
          at: rootURL.appendingPathComponent("settings.json"),
          path: ["apps", "7zFM", "Lang"]))
      {
        return localeKey(from: settingsHint)
      }
    }
    return "en"
  }

  static func loadTable(
    localeKey: String,
    bundle: Bundle = .main,
    resourceRootURL: URL? = nil
  ) -> [String: String] {
    let candidates = [
      "z7_strings_\(localeKey)",
      "z7_strings_en",
    ]

    for candidate in candidates {
      let urls = resourceURLs(
        forResource: candidate,
        bundle: bundle,
        resourceRootURL: resourceRootURL)
      for url in urls {
        guard let data = try? Data(contentsOf: url),
              let decoded = try? JSONDecoder().decode(QuickLookLocalizationFile.self, from: data),
              let quicklook = decoded.quicklook else {
          continue
        }
        return quicklook.reduce(into: [String: String]()) { partialResult, item in
          partialResult["quicklook.\(item.key)"] = item.value
        }
      }
    }

    return [:]
  }

  private static func table(localeKey: String) -> [String: String] {
    tableLock.lock()
    if let cached = loadedTables[localeKey] {
      tableLock.unlock()
      return cached
    }
    tableLock.unlock()

    let loaded = loadTable(localeKey: localeKey)

    tableLock.lock()
    loadedTables[localeKey] = loaded
    tableLock.unlock()
    return loaded
  }

  private static func resourceURLs(
    forResource resourceName: String,
    bundle: Bundle,
    resourceRootURL: URL?
  ) -> [URL] {
    var urls: [URL] = []
    if let resourceRootURL {
      urls.append(
        resourceRootURL
          .appendingPathComponent("i18n", isDirectory: true)
          .appendingPathComponent("\(resourceName).json"))
      urls.append(resourceRootURL.appendingPathComponent("\(resourceName).json"))
    }
    if let url = bundle.url(forResource: resourceName, withExtension: "json") {
      urls.append(url)
    }
    if let url = bundle.url(
      forResource: resourceName,
      withExtension: "json",
      subdirectory: "i18n")
    {
      urls.append(url)
    }
    return urls
  }

  private static func normalizedLanguageHint(_ hint: String?) -> String? {
    guard let trimmed = hint?.trimmingCharacters(in: .whitespacesAndNewlines),
          !trimmed.isEmpty,
          trimmed != "-" else {
      return nil
    }
    return trimmed
  }

  private static func jsonString(at url: URL, path: [String]) -> String? {
    guard let data = try? Data(contentsOf: url),
          let object = try? JSONSerialization.jsonObject(with: data),
          let value = jsonValue(in: object, path: path) as? String else {
      return nil
    }
    return value
  }

  private static func jsonValue(in object: Any, path: [String]) -> Any? {
    var cursor: Any? = object
    for component in path {
      guard let dictionary = cursor as? [String: Any] else {
        return nil
      }
      cursor = dictionary[component]
    }
    return cursor
  }

  static func text(_ key: String) -> String {
    return table(localeKey: preferredLocaleKey())[key] ?? "!\(key)!"
  }

  static func format(_ key: String, _ args: [String]) -> String {
    var pattern = text(key)
    for (index, value) in args.enumerated() {
      pattern = pattern.replacingOccurrences(of: "{\(index)}", with: value)
    }
    return pattern
  }
}
