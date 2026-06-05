import Cocoa
import CoreGraphics

class MouseHook {
    private var eventTap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    private var isEnabled: Bool = false

    // State machine
    enum State {
        case idle
        case pressed
    }

    private var state: State = .idle
    private var pressPosition: CGPoint = .zero
    private var triggered: Bool = false
    private var callback: ((String) -> Void)?

    func start(onGesture: @escaping (String) -> Void) {
        self.callback = onGesture

        let eventMask: CGEventMask = (1 << CGEventType.otherMouseDown.rawValue)
            | (1 << CGEventType.otherMouseUp.rawValue)
            | (1 << CGEventType.otherMouseDragged.rawValue)

        guard let tap = CGEvent.tapCreate(
            tap: .cghidEventTap,
            place: .headInsertEventTap,
            options: .defaultTap,
            eventsOfInterest: eventMask,
            callback: mouseHookCallback,
            userInfo: Unmanaged.passUnretained(self).toOpaque()
        ) else {
            print("[MouseHook] Failed to create event tap. Check accessibility permissions.")
            return
        }

        self.eventTap = tap
        self.runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        self.isEnabled = true
        print("[MouseHook] Event tap installed successfully.")
    }

    func stop() {
        if let tap = eventTap {
            CGEvent.tapEnable(tap: tap, enable: false)
        }
        if let source = runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, .commonModes)
        }
        eventTap = nil
        runLoopSource = nil
        isEnabled = false
        state = .idle
        triggered = false
    }

    func setEnabled(_ enabled: Bool) {
        if enabled && !isEnabled {
            // Re-start if we have a callback
            if let cb = callback {
                start(onGesture: cb)
            }
        } else if !enabled && isEnabled {
            stop()
        }
    }

    fileprivate func handleEvent(proxy: CGEventTapProxy, type: CGEventType, event: CGEvent) -> Unmanaged<CGEvent>? {
        // If the tap gets disabled by the system, re-enable it
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            if let tap = eventTap {
                CGEvent.tapEnable(tap: tap, enable: true)
            }
            return Unmanaged.passUnretained(event)
        }

        let buttonNumber = event.getIntegerValueField(.mouseEventButtonNumber)

        // Only handle middle button (button number 2)
        guard buttonNumber == 2 else {
            return Unmanaged.passUnretained(event)
        }

        switch type {
        case .otherMouseDown:
            state = .pressed
            pressPosition = event.location
            triggered = false
            // Suppress the middle button down event
            return nil

        case .otherMouseDragged:
            if state == .pressed {
                let currentPos = event.location
                let dx = currentPos.x - pressPosition.x
                let dy = currentPos.y - pressPosition.y
                let distance = sqrt(dx * dx + dy * dy)
                if distance > 20.0 {
                    triggered = true
                }
            }
            // Suppress drag events while tracking
            return nil

        case .otherMouseUp:
            if state == .pressed {
                if triggered {
                    let currentPos = event.location
                    let dx = currentPos.x - pressPosition.x
                    let dy = currentPos.y - pressPosition.y
                    let direction = calcDirection(dx: dx, dy: dy)

                    // Send to Dart on main thread
                    DispatchQueue.main.async { [weak self] in
                        self?.callback?(direction)
                    }

                    state = .idle
                    triggered = false
                    // Suppress the event
                    return nil
                } else {
                    // Not triggered (no drag) - pass through the click
                    state = .idle
                    triggered = false
                    return Unmanaged.passUnretained(event)
                }
            }
            return Unmanaged.passUnretained(event)

        default:
            return Unmanaged.passUnretained(event)
        }
    }

    private func calcDirection(dx: CGFloat, dy: CGFloat) -> String {
        let absDx = abs(dx)
        let absDy = abs(dy)

        if absDx > absDy {
            return dx > 0 ? "right" : "left"
        } else {
            return dy > 0 ? "down" : "up"
        }
    }
}

/// C function pointer callback for CGEventTap.
/// The `refcon` parameter carries a pointer to the MouseHook instance.
private func mouseHookCallback(
    proxy: CGEventTapProxy,
    type: CGEventType,
    event: CGEvent,
    refcon: UnsafeMutableRawPointer?
) -> Unmanaged<CGEvent>? {
    guard let refcon = refcon else {
        return Unmanaged.passUnretained(event)
    }
    let hook = Unmanaged<MouseHook>.fromOpaque(refcon).takeUnretainedValue()
    return hook.handleEvent(proxy: proxy, type: type, event: event)
}
