import Cocoa
import FlutterMacOS

class TrayManager {
    private var statusItem: NSStatusItem?
    private var channel: FlutterMethodChannel?
    private var toggleGestureItem: NSMenuItem?
    private var reverseMiddleDragItem: NSMenuItem?
    private var scrollLineItems: [NSMenuItem] = []
    private var scrollLines = 3
    private var middleDragReversed = false

    func setup(channel: FlutterMethodChannel) {
        self.channel = channel
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)

        if let button = statusItem?.button {
            if #available(macOS 11.0, *),
               let image = NSImage(systemSymbolName: "arrow.up.arrow.down",
                                    accessibilityDescription: "Skiff") {
                image.isTemplate = true
                button.image = image
            } else {
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

        reverseMiddleDragItem = NSMenuItem(title: "Reverse Middle Drag", action: #selector(toggleMiddleDragReverse(_:)), keyEquivalent: "")
        reverseMiddleDragItem?.target = self
        reverseMiddleDragItem?.state = .off
        menu.addItem(reverseMiddleDragItem!)

        menu.addItem(NSMenuItem.separator())

        let scrollItem = NSMenuItem(title: "Scroll Lines", action: nil, keyEquivalent: "")
        let scrollMenu = NSMenu()
        scrollLineItems.removeAll()
        for lines in 1...10 {
            let item = NSMenuItem(title: "\(lines)", action: #selector(selectScrollLines(_:)), keyEquivalent: "")
            item.target = self
            item.tag = lines
            item.state = lines == scrollLines ? .on : .off
            scrollMenu.addItem(item)
            scrollLineItems.append(item)
        }
        scrollItem.submenu = scrollMenu
        menu.addItem(scrollItem)

        menu.addItem(NSMenuItem.separator())

        let quitItem = NSMenuItem(title: "Quit", action: #selector(quitApp(_:)), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        statusItem?.menu = menu
    }

    func updateGestureState(enabled: Bool) {
        toggleGestureItem?.state = enabled ? .on : .off
    }

    func updateScrollLines(lines: Int) {
        scrollLines = min(max(lines, 1), 10)
        for item in scrollLineItems {
            item.state = item.tag == scrollLines ? .on : .off
        }
    }

    func updateMiddleDragReversed(reversed: Bool) {
        middleDragReversed = reversed
        reverseMiddleDragItem?.state = reversed ? .on : .off
    }

    @objc private func toggleOverlay(_ sender: NSMenuItem) {
        channel?.invokeMethod("onTrayAction", arguments: ["action": "toggle_button"])
    }

    @objc private func toggleGesture(_ sender: NSMenuItem) {
        channel?.invokeMethod("onTrayAction", arguments: ["action": "toggle_gesture"])
    }

    @objc private func toggleMiddleDragReverse(_ sender: NSMenuItem) {
        updateMiddleDragReversed(reversed: !middleDragReversed)
        channel?.invokeMethod("onTrayAction", arguments: ["action": "toggle_middle_drag_reverse"])
    }

    @objc private func selectScrollLines(_ sender: NSMenuItem) {
        updateScrollLines(lines: sender.tag)
        channel?.invokeMethod("onTrayAction", arguments: ["action": "scroll_\(sender.tag)"])
    }

    @objc private func quitApp(_ sender: NSMenuItem) {
        channel?.invokeMethod("onTrayAction", arguments: ["action": "quit"])
    }
}
