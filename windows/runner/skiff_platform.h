#ifndef RUNNER_SKIFF_PLATFORM_H_
#define RUNNER_SKIFF_PLATFORM_H_

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <memory>

#include "mouse_hook.h"
#include "scroll_simulator.h"
#include "tray_manager.h"

// Flutter plugin that bridges the Dart "com.skiff/native" MethodChannel to
// the native Win32 subsystems (scroll simulation, mouse hook, system tray).
class SkiffNativePlugin {
 public:
  static void RegisterWithMessenger(flutter::BinaryMessenger* messenger);

  SkiffNativePlugin(
      std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel);
  ~SkiffNativePlugin();

 private:
  // Handles incoming method calls from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  // Initialize native subsystems (tray icon, mouse hook).
  void Initialize();

  // Invoke Dart callbacks on the MethodChannel.
  void InvokeOverlayTap(const std::string& zone);
  void InvokeMiddleClickGesture(const std::string& direction);
  void InvokeRightButtonHold(int x, int y);
  void InvokeTrayAction(const std::string& action);

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;

  ScrollSimulator scroll_simulator_;
  MouseHook mouse_hook_;
  TrayManager tray_manager_;
};

#endif  // RUNNER_SKIFF_PLATFORM_H_
