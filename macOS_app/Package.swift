// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "BLETester",
    platforms: [
        .macOS(.v13)
    ],
    dependencies: [],
    targets: [
        .target(
            name: "PSoCDriverWrapper",
            dependencies: [],
            path: "Sources/PSoCDriverWrapper",
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("../../shared_driver/include"),
                .unsafeFlags(["-I../../shared_driver/include"])
            ],
            linkerSettings: [
                .linkedLibrary("z"),  // zlib
                .unsafeFlags([
                    "-L../../shared_driver/build",
                    "-lpsoc_driver"
                ])
            ]
        ),
        .executableTarget(
            name: "BLETester",
            dependencies: ["PSoCDriverWrapper"]
        )
    ],
    cxxLanguageStandard: .cxx11
)
