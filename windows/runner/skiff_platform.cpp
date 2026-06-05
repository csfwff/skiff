#include "skiff_platform.h"

#include <windows.h>

#include <memory>
#include <variant>

namespace {
constexpr char kChannelName[] = "com.skiff/native";
}  // namespace

// static
void SkiffNativePlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), kChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<SkiffNativePlugin>(std::move(channel));

  channel = nullptr;  // moved into plugin

  registrar->AddPlugin(std::move(plugin));
}

SkiffNativePlugin::SkiffNativePlugin(
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel)
    : channel_(std::move(channel)) {
  channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<flutter::EncodableValue>& call,
             std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
                 result) {
        HandleMethodCall(call, std::move(result));
      });
}

SkiffNativePlugin::~SkiffNativePlugin() {
  mouse_hook_.stop();
  tray_manager_.stop();
}

void SkiffNativePlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const std::string& method = call.method_name();

  if (method == "initialize") {
    Initialize();
    result->Success();
  } else if (method == "simulateScroll") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    int dx = 0;
    int dy = 0;
    if (args) {
      auto it = args->find(flutter::EncodableValue("dx"));
      if (it != args->end()) {
        dx = static_cast<int>(std::get<int64_t>(it->second));
      }
      it = args->find(flutter::EncodableValue("dy"));
      if (it != args->end()) {
        dy = static_cast<int>(std::get<int64_t>(it->second));
      }
    }
    scroll_simulator_.scroll(dx, dy);
    result->Success();
  } else if (method == "setMiddleClickEnabled") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    bool enabled = false;
    if (args) {
      auto it = args->find(flutter::EncodableValue("enabled"));
      if (it != args->end()) {
        enabled = std::get<bool>(it->second);
      }
    }
    mouse_hook_.setEnabled(enabled);
    result->Success();
  } else if (method == "setOverlayVisible") {
    // The overlay visibility is managed by Dart (window_manager package).
    // We simply forward the request back to Dart so it can act on it.
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    bool visible = false;
    if (args) {
      auto it = args->find(flutter::EncodableValue("visible"));
      if (it != args->end()) {
        visible = std::get<bool>(it->second);
      }
    }
    // Nothing to do on the native side -- Dart owns the overlay window.
    result->Success();
  } else if (method == "quit") {
    result->Success();
    PostQuitMessage(0);
  } else if (method == "showMainWindow") {
    // Show the main Flutter window.
    HWND hwnd = ::FindWindow(L"FLUTTER_RUNNER_WIN32_WINDOW", nullptr);
    if (hwnd) {
      ::ShowWindow(hwnd, SW_SHOWNORMAL);
      ::SetForegroundWindow(hwnd);
    }
    result->Success();
  } else if (method == "hideMainWindow") {
    HWND hwnd = ::FindWindow(L"FLUTTER_RUNNER_WIN32_WINDOW", nullptr);
    if (hwnd) {
      ::ShowWindow(hwnd, SW_HIDE);
    }
    result->Success();
  } else if (method == "checkAccessibilityPermission") {
    // Windows does not require special accessibility permissions for
    // global hooks the way macOS does.
    flutter::EncodableMap response;
    response[flutter::EncodableValue("granted")] = flutter::EncodableValue(true);
    result->Success(flutter::EncodableValue(response));
  } else if (method == "requestAccessibilityPermission") {
    // No-op on Windows.
    result->Success();
  } else {
    result->NotImplemented();
  }
}

void SkiffNativePlugin::Initialize() {
  // Set up the tray icon.
  tray_manager_.setMenuCallback([this](const std::string& action) {
    InvokeTrayAction(action);
  });
  tray_manager_.start();

  // Install the mouse hook for middle-click gesture detection.
  mouse_hook_.setGestureCallback([this](const std::string& direction) {
    InvokeMiddleClickGesture(direction);
  });
  mouse_hook_.start();
}

void SkiffNativePlugin::InvokeOverlayTap(const std::string& zone) {
  flutter::EncodableMap args;
  args[flutter::EncodableValue("zone")] = flutter::EncodableValue(zone);
  channel_->InvokeMethod("onOverlayTap",
                         std::make_unique<flutter::EncodableValue>(args));
}

void SkiffNativePlugin::InvokeMiddleClickGesture(const std::string& direction) {
  flutter::EncodableMap args;
  args[flutter::EncodableValue("direction")] = flutter::EncodableValue(direction);
  channel_->InvokeMethod("onMiddleClickGesture",
                         std::make_unique<flutter::EncodableValue>(args));
}

void SkiffNativePlugin::InvokeTrayAction(const std::string& action) {
  flutter::EncodableMap args;
  args[flutter::EncodableValue("action")] = flutter::EncodableValue(action);
  channel_->InvokeMethod("onTrayAction",
                         std::make_unique<flutter::EncodableValue>(args));
}
