#include "skiff_platform.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <memory>
#include <string>
#include <variant>

namespace {
constexpr char kChannelName[] = "com.skiff/native";
std::unique_ptr<SkiffNativePlugin> g_plugin;

// Appends a line to %APPDATA%\Skiff\native.log. Release builds have no console,
// so this file is the only way to inspect what the native layer is doing.
// Best-effort: any failure is silently ignored.
void NativeLog(const char* fmt, ...) {
  wchar_t* appdata = nullptr;
  if (::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata) !=
      S_OK) {
    return;
  }
  std::wstring path = std::wstring(appdata) + L"\\Skiff\\native.log";
  ::CoTaskMemFree(appdata);

  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"a") != 0 || !f) {
    return;
  }

  SYSTEMTIME st;
  ::GetLocalTime(&st);
  std::fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds);

  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);

  std::fprintf(f, "\n");
  std::fclose(f);
}

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

// Makes the overlay click-through (or restores it). The cursor's hit-test for
// real mouse input passes through to the window beneath ONLY when the window is
// both WS_EX_LAYERED and WS_EX_TRANSPARENT -- WS_EX_TRANSPARENT alone is not
// enough. This is the Win32 equivalent of X11's empty input shape used on
// Linux. We toggle it on the top-level overlay window; mouse input to its
// Flutter child passes through as well.
//
// NOTE: WindowFromPoint() does NOT honour these styles (it returns the window
// under the point regardless of transparency), so it cannot be used to verify
// the effect -- only an actual injected wheel will route correctly.
void SetOverlayClickThrough(HWND overlay, bool through) {
  if (!overlay) {
    return;
  }
  LONG_PTR ex = ::GetWindowLongPtrW(overlay, GWL_EXSTYLE);
  if (through) {
    ex |= (WS_EX_LAYERED | WS_EX_TRANSPARENT);
  } else {
    // Keep WS_EX_LAYERED (Flutter's transparent window relies on it); only drop
    // the click-through bit so the buttons become interactive again.
    ex &= ~WS_EX_TRANSPARENT;
  }
  ::SetWindowLongPtrW(overlay, GWL_EXSTYLE, ex);
}

// Describes a window for logging: "class#pid(self?)". Lets us tell at a glance
// whether a handle is our own Flutter view or the target application.
std::string DescribeWindow(HWND hwnd) {
  if (!hwnd) {
    return "null";
  }
  wchar_t cls[128] = {0};
  ::GetClassNameW(hwnd, cls, 128);
  char cls_utf8[256] = {0};
  ::WideCharToMultiByte(CP_UTF8, 0, cls, -1, cls_utf8, sizeof(cls_utf8), nullptr,
                        nullptr);
  DWORD pid = 0;
  ::GetWindowThreadProcessId(hwnd, &pid);
  bool is_self = (pid == ::GetCurrentProcessId());
  char buf[320];
  std::snprintf(buf, sizeof(buf), "%s#%lu%s", cls_utf8, pid,
                is_self ? "(SELF)" : "");
  return std::string(buf);
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
  NativeLog("SetOverlayMode enabled=%d overlay=%p", enabled ? 1 : 0, hwnd);
}

void SkiffNativePlugin::SimulateScrollDirect(int dx, int dy) {
  // Scroll whatever window sits beneath the overlay (same behaviour as the
  // Linux build). The overlay is at the cursor, so we make it click-through and
  // inject a real wheel event at the current cursor position; Windows then
  // routes the wheel to the window directly under the overlay.
  HWND overlay = FindOverlayWindow();

  POINT cursor;
  ::GetCursorPos(&cursor);
  HWND under = ::WindowFromPoint(cursor);  // logging only; ignores transparency

  // Enable click-through (WS_EX_LAYERED | WS_EX_TRANSPARENT) so the injected
  // wheel hit-tests through to the window beneath the overlay.
  SetOverlayClickThrough(overlay, true);

  NativeLog("SimulateScrollDirect dx=%d dy=%d cursor=(%ld,%ld) overlay=%s "
            "windowfrompoint=%s",
            dx, dy, cursor.x, cursor.y, DescribeWindow(overlay).c_str(),
            DescribeWindow(under).c_str());

  // Inject at the cursor position via SendInput (the path the middle-click
  // gesture uses -- confirmed working). MOUSEEVENTF_ABSOLUTE is not needed; the
  // wheel is delivered to the window under the current pointer position.
  scroll_simulator_.scroll(dx, dy);

  // Let the input thread hit-test and deliver before we become opaque again,
  // otherwise the restored overlay could swallow the in-flight event.
  ::Sleep(20);
  SetOverlayClickThrough(overlay, false);
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
