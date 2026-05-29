import SwiftUI

@main
struct MacOSIntegrationHostApp: App {
    var body: some Scene {
        WindowGroup {
            VStack(alignment: .leading, spacing: 10) {
                Text("MacOS Integration Host")
                    .font(.title2.weight(.semibold))
                Text("This app hosts the Finder Sync and Quick Look extensions.")
                    .foregroundStyle(.secondary)
            }
            .padding(24)
            .frame(width: 460, height: 180)
        }
    }
}
