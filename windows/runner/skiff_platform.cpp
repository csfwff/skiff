#include "skiff_platform.h"

#include <windows.h>

#include <memory>
#include <variant>

namespace {
constexpr char kChannelName[] = "com.skiff/native";
std::unique_ptr<SkiffNativePlugin> g_plugin;

// Extracts an integer argument from an EncodableMap.
//
// Flutter's StandardMessageCodec encodes a Dart int as a 32-bit value when it
// fits in 32 bits and as a 64-bit value otherwise. On the C++ side this means
// the EncodableValue may hold either an int32_t or an int64_t, so we must
// accept both -- calling std::get<int64_t>() on an int32 payload throws
// std::bad_variant_access and terminates the process.
int GetIntArg(const flutter::EncodableMap* args, const char* key,
              int fallback) {
  if (!args) {
    return fallback;
  }
  auto it = args->find(flutter::EncodableValue(key));
  if (it == args->end()) {
    return fallback;
  }
  if (const auto* v = std::get_if<int32_t>(&it->second)) {
    return static_cast<int>(*v);
  }
  if (const auto* v = std::get_if<int64_t>(&it->second)) {
    return static_cast<int>(*v);
  }
  return fallback;
}

// Extracts a boolean argument from an EncodableMap, tolerating a missing key
// or an unexpected payload type instead of throwing.
bool GetBoolArg(const flutter::EncodableMap* args, const char* key,
                bool fallback) {
  if (!args) {
    return fallback;
  }
  auto it = args->find(flutter::EncodableValue(key));
  if (it == args->end()) {
    return fallback;
  }
  if (const auto* v = std::get_if<bool>(&it->second)) {
    return *v;
  }
  return fallback;
}

// Returns the Flutter top-level window for this process. The overlay window is
// created with the class name "FLUTTER_RUNNER_WIN32_WINDOW" (see
// win32_window.cpp). We restrict the search to this process so we never touch a
// window owned by another Flutter app.
HWND FindOverlayWindow() {
  HWND hwnd = nullptr;
  while ((hwnd = ::FindWindowExW(nullptr, hwnd, L"FLUTTER_RUNNER_WIN32_WINDOW",
                                 nullptr)) != nullptr) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ::GetCurrentProcessId()) {
      return hwnd;
    }
  }
  return nullptr;
}
}  // namespace

// static
void SkiffNativePlugin::RegisterWithMessenger(
    flutter::BinaryMessenger* messenger) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          messenger, kChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  g_plugin = std::make_unique<SkiffNativePlugin>(std::move(channel));
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
    int dx = GetIntArg(args, "dx", 0);
    int dy = GetIntArg(args, "dy", 0);
    scroll_simulator_.scroll(dx, dy);
    result->Success();
  } else if (method == "setMiddleClickEnabled") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    bool enabled = GetBoolArg(args, "enabled", false);
    mouse_hook_.setEnabled(enabled);
    tray_manager_.setGestureEnabled(enabled);
    result->Success();
  } else if (method == "setMiddleDragReversed") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    bool reversed = GetBoolArg(args, "reversed", false);
    tray_manager_.setMiddleDragReversed(reversed);
    result->Success();
  } else if (method == "setScrollLines") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    int lines = GetIntArg(args, "lines", 3);
    tray_manager_.setScrollLines(lines);
    result->Success();
  } else if (method == "simulateScrollDirect") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    int dx = GetIntArg(args, "dx", 0);
    int dy = GetIntArg(args, "dy", 0);
    SimulateScrollDirect(dx, dy);
    result->Success();
  } else if (method == "setOverlayMode") {
    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    bool enabled = GetBoolArg(args, "enabled", false);
    SetOverlayMode(enabled);
    result->Success();
  } else if (method == "setOverlayVisible") {
    // The overlay visibility is managed by Dart (window_manager package).
    // Nothing to do on the native side -- Dart owns the overlay window.
    result->Success();
  } else if (method == "quit") {
    result->Success();
    PostQuitMessage(0);
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
  mouse_hook_.setRightHoldCallback([this](int x, int y) {
    InvokeRightButtonHold(x, y);
  });
  mouse_hook_.start();
}

void SkiffNativePlugin::SetOverlayMode(bool enabled) {
  HWND hwnd = FindOverlayWindow();
  if (!hwnd) {
    return;
  }

  LONG_PTR ex_style = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (enabled) {
    // WS_EX_NOACTIVATE: clicking the overlay never activates it, so the target
    // application keeps keyboard focus and WM_MOUSEWHEEL keeps flowing to it.
    ex_style |= WS_EX_NOACTIVATE;
  } else {
    ex_style &= ~WS_EX_NOACTIVATE;
  }
  ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style);
}

void SkiffNativePlugin::SimulateScrollDirect(int dx, int dy) {
  HWND overlay = FindOverlayWindow();

  // Temporarily make the overlay click-through so WindowFromPoint skips it and
  // returns the window directly beneath the cursor. WS_EX_TRANSPARENT windows
  // are ignored by hit-testing, so this is reliable and synchronous.
  LONG_PTR original_ex_style = 0;
  bool toggled = false;
  if (overlay) {
    original_ex_style = ::GetWindowLongPtrW(overlay, GWL_EXSTYLE);
    if ((original_ex_style & WS_EX_TRANSPARENT) == 0) {
      ::SetWindowLongPtrW(overlay, GWL_EXSTYLE,
                          original_ex_style | WS_EX_TRANSPARENT);
      toggled = true;
    }
  }

  POINT cursor;
  ::GetCursorPos(&cursor);
  HWND target = ::WindowFromPoint(cursor);

  // Restore the overlay's interactivity immediately; we already captured the
  // target, so the buttons keep working for the next tap.
  if (toggled) {
    ::SetWindowLongPtrW(overlay, GWL_EXSTYLE, original_ex_style);
  }

  // Never deliver the wheel to our own overlay.
  if (!target || target == overlay) {
    // Fall back to the focus/hover routing of a plain injected wheel.
    scroll_simulator_.scroll(dx, dy);
    return;
  }

  // Resolve to the deepest child under the cursor so apps that handle the
  // wheel on an inner scroll view receive it.
  POINT client = cursor;
  ::ScreenToClient(target, &client);
  HWND child = ::RealChildWindowFromPoint(target, client);
  if (child) {
    target = child;
  }

  const LPARAM pos = MAKELPARAM(cursor.x, cursor.y);
  if (dy != 0) {
    // Match the injected-wheel convention in ScrollSimulator::scroll:
    // positive delta == dy * WHEEL_DELTA.
    const WPARAM wparam = MAKEWPARAM(0, dy * WHEEL_DELTA);
    ::SendMessageW(target, WM_MOUSEWHEEL, wparam, pos);
  }
  if (dx != 0) {
    const WPARAM wparam = MAKEWPARAM(0, dx * WHEEL_DELTA);
    ::SendMessageW(target, WM_MOUSEHWHEEL, wparam, pos);
  }
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

void SkiffNativePlugin::InvokeRightButtonHold(int x, int y) {
  flutter::EncodableMap args;
  args[flutter::EncodableValue("x")] =
      flutter::EncodableValue(static_cast<int64_t>(x));
  args[flutter::EncodableValue("y")] =
      flutter::EncodableValue(static_cast<int64_t>(y));
  channel_->InvokeMethod("onRightButtonHold",
                         std::make_unique<flutter::EncodableValue>(args));
}

void SkiffNativePlugin::InvokeTrayAction(const std::string& action) {
  flutter::EncodableMap args;
  args[flutter::EncodableValue("action")] = flutter::EncodableValue(action);
  channel_->InvokeMethod("onTrayAction",
                         std::make_unique<flutter::EncodableValue>(args));
}
