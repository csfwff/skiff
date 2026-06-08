import Cocoa
import CoreGraphics

class ScrollSimulator {
    func scroll(dx: Int, dy: Int) {
        let event = CGEvent(scrollWheelEvent2Source: nil,
                            units: CGScrollEventUnit.line,
                            wheelCount: 2,
                            wheel1: Int32(dy),
                            wheel2: Int32(dx),
                            wheel3: 0)
        event?.post(tap: CGEventTapLocation.cghidEventTap)
    }
}
