// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "BLETester",
    platforms: [
        .macOS(.v13)
    ],
    dependencies: [],
    targets: [
        .executableTarget(
            name: "BLETester",
            dependencies: []
        )
    ]
)
