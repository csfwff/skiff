import Cocoa
import CoreGraphics

class MouseHook {
    private var eventTap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    private var isInstalled: Bool = false
    private var middleGestureEnabled: Bool = true

    // State machine
    enum State {
        case idle
        case pressed
    }

    private var state: State = .idle
    private var pressPosition: CGPoint = .zero
    private var triggered: Bool = false
    private var gestureCallback: ((String) -> Void)?
    private var rightHoldCallback: ((CGPoint) -> Void)?
    private var rightHoldWorkItem: DispatchWorkItem?
    private var rightHoldPosition: CGPoint = .zero

    func start(onGesture: @escaping (String) -> Void,
               onRightHold: @escaping (CGPoint) -> Void) {
        self.gestureCallback = onGesture
        self.rightHoldCallback = onRightHold

        if eventTap != nil {
            return
        }

        let eventMask: CGEventMask = (1 << CGEventType.otherMouseDown.rawValue)
            | (1 << CGEventType.otherMouseUp.rawValue)
            | (1 << CGEventType.otherMouseDragged.rawValue)
            | (1 << CGEventType.rightMouseDown.rawValue)
            | (1 << CGEventType.rightMouseUp.rawValue)
            | (1 << CGEventType.rightMouseDragged.rawValue)

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
        self.isInstalled = true
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
        isInstalled = false
        state = .idle
        triggered = false
        rightHoldWorkItem?.cancel()
        rightHoldWorkItem = nil
        rightHoldPosition = .zero
    }

    func setEnabled(_ enabled: Bool) {
        middleGestureEnabled = enabled
        if enabled && !isInstalled,
           let gestureCallback = gestureCallback,
           let rightHoldCallback = rightHoldCallback {
            start(onGesture: gestureCallback, onRightHold: rightHoldCallback)
        } else if !enabled {
            state = .idle
            triggered = false
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

        switch type {
        case .rightMouseDown:
            rightHoldWorkItem?.cancel()
            rightHoldPosition = event.location
            let workItem = DispatchWorkItem { [weak self] in
                guard let self = self else {
                    return
                }
                self.rightHoldCallback?(self.rightHoldPosition)
            }
            rightHoldWorkItem = workItem
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0, execute: workItem)
            return Unmanaged.passUnretained(event)

        case .rightMouseDragged:
            rightHoldPosition = event.location
            return Unmanaged.passUnretained(event)

        case .rightMouseUp:
            rightHoldWorkItem?.cancel()
            rightHoldWorkItem = nil
            return Unmanaged.passUnretained(event)

        default:
            break
        }

        if !middleGestureEnabled {
            return Unmanaged.passUnretained(event)
        }

        // Only handle middle button (button number 2)
        let buttonNumber = event.getIntegerValueField(.mouseEventButtonNumber)
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
                        self?.gestureCallback?(direction)
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
