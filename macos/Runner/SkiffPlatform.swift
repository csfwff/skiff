import Cocoa
import FlutterMacOS

class SkiffPlatform: NSObject, FlutterPlugin {
    private let channel: FlutterMethodChannel
    private let scrollSimulator = ScrollSimulator()
    private let mouseHook = MouseHook()
    private let trayManager = TrayManager()

    init(channel: FlutterMethodChannel) {
        self.channel = channel
        super.init()
    }

    static func register(with registrar: FlutterPluginRegistrar) {
        // Not used — we register manually from MainFlutterWindow
    }

    /// Called from MainFlutterWindow to set up the plugin with a channel.
    func register(with channel: FlutterMethodChannel) {
        channel.setMethodCallHandler(handle)
        trayManager.setup(channel: channel)
    }

    func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "initialize":
            handleInitialize(result: result)

        case "simulateScroll":
            guard let args = call.arguments as? [String: Any],
                  let dx = args["dx"] as? Int,
                  let dy = args["dy"] as? Int else {
                result(FlutterError(code: "INVALID_ARGS",
                                    message: "Expected dx and dy",
                                    details: nil))
                return
            }
            scrollSimulator.scroll(dx: dx, dy: dy)
            result(nil)

        case "setMiddleClickEnabled":
            guard let args = call.arguments as? [String: Any],
                  let enabled = args["enabled"] as? Bool else {
                result(FlutterError(code: "INVALID_ARGS",
                                    message: "Expected enabled bool",
                                    details: nil))
                return
            }
            if enabled {
                mouseHook.start(onGesture: { [weak self] direction in
                    self?.channel.invokeMethod("onMiddleClickGesture",
                                               arguments: ["direction": direction])
                })
            } else {
                mouseHook.stop()
            }
            trayManager.updateGestureState(enabled: enabled)
            result(nil)

        case "setOverlayVisible":
            // Dart side handles window show/hide.
            result(nil)

        case "checkAccessibilityPermission":
            let granted = AccessibilityHelper.checkPermission()
            result(["granted": granted])

        case "requestAccessibilityPermission":
            AccessibilityHelper.requestPermission()
            result(nil)

        case "quit":
            NSApplication.shared.terminate(nil)
            result(nil)

        default:
            result(FlutterMethodNotImplemented)
        }
    }

    private func handleInitialize(result: @escaping FlutterResult) {
        // Check accessibility permission
        let granted = AccessibilityHelper.checkPermission()

        if granted {
            // Install mouse hook
            mouseHook.start(onGesture: { [weak self] direction in
                self?.channel.invokeMethod("onMiddleClickGesture",
                                           arguments: ["direction": direction])
            })
        }

        result(["accessibilityGranted": granted])
    }
}
