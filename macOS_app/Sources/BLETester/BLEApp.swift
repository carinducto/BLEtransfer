import SwiftUI

@main
struct BLEApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 1000, minHeight: 700)
        }
        .windowResizability(.contentSize)
    }
}
