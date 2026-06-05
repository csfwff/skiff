import Cocoa
import CoreGraphics

class ScrollSimulator {
    func scroll(dx: Int, dy: Int) {
        let event = CGEvent(scrollWheelEventSource: nil,
                            units: .line,
                            wheelCount: 2,
                            wheel1: Int32(dy),
                            wheel2: Int32(dx))
        event?.post(tap: .cghidEventTap)
    }
}
