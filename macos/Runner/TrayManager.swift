import Cocoa
import FlutterMacOS

class TrayManager {
    private var statusItem: NSStatusItem?
    private var channel: FlutterMethodChannel?
    private var toggleGestureItem: NSMenuItem?

    func setup(channel: FlutterMethodChannel) {
        self.channel = channel
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)

        if let button = statusItem?.button {
            if let image = NSImage(systemSymbolName: "arrow.up.arrow.down",
                                   accessibilityDescription: "Skiff") {
                image.isTemplate = true
                button.image = image
            } else {
                // Fallback for older macOS versions
                button.title = "S"
            }
        }

        let menu = NSMenu()

        let toggleButton = NSMenuItem(title: "Toggle Overlay", action: #selector(toggleOverlay(_:)), keyEquivalent: "")
        toggleButton.target = self
        menu.addItem(toggleButton)

        toggleGestureItem = NSMenuItem(title: "Enable Middle-Click Gesture", action: #selector(toggleGesture(_:)), keyEquivalent: "")
        toggleGestureItem?.target = self
        toggleGestureItem?.state = .off
        menu.addItem(toggleGestureItem!)

        menu.addItem(NSMenuItem.separator())

        let quitItem = NSMenuItem(title: "Quit", action: #selector(quitApp(_:)), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        statusItem?.menu = menu
    }

    func updateGestureState(enabled: Bool) {
        toggleGestureItem?.state = enabled ? .on : .off
    }

    @objc private func toggleOverlay(_ sender: NSMenuItem) {
        channel?.invokeMethod("onTrayAction", arguments: ["action": "toggle_button"])
    }

    @objc private func toggleGesture(_ sender: NSMenuItem) {
        channel?.invokeMethod("onTrayAction", arguments: ["action": "toggle_gesture"])
    }

    @objc private func quitApp(_ sender: NSMenuItem) {
        channel?.invokeMethod("onTrayAction", arguments: ["action": "quit"])
    }
}
