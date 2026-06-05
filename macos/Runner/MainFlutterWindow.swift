import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)

    // Register Skiff native platform plugin
    let channel = FlutterMethodChannel(name: "com.skiff/native",
                                       binaryMessenger: flutterViewController.engine.binaryMessenger)
    let skiffPlugin = SkiffPlatform(channel: channel)
    skiffPlugin.register(with: channel)

    super.awakeFromNib()

    // Hide window at startup; Dart controls visibility
    self.orderOut(nil)
  }
}
