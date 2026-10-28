// swift-tools-version: 5.10
// Rec Cue! for macOS. Built with the Command Line Tools only (no Xcode): scripts/build.sh runs `swift build` and
// puts the executable into "build/Rec Cue.app".
import PackageDescription

let package = Package(
    name: "RecCue",
    platforms: [.macOS("15.0")],
    products: [.executable(name: "rec-cue", targets: ["RecCue"])],
    targets: [
        // catching an Objective-C exception (Swift cannot): only around the window's text
        .target(name: "RecCueObjC", path: "Sources/RecCueObjC", exclude: ["DEVELOPMENT.md"]),
        .executableTarget(name: "RecCue", dependencies: ["RecCueObjC"], path: "Sources/RecCue"),
    ]
)
