#include "tray_manager.h"

#include "resource.h"

#include <cstdio>

namespace {
// Menu item IDs.
constexpr UINT kMenuToggleButton = 1001;
constexpr UINT kMenuToggleGesture = 1002;
constexpr UINT kMenuQuit = 1003;
constexpr UINT kMenuToggleMiddleDragReverse = 1004;
constexpr UINT kMenuScrollBase = 2000;
}  // namespace

TrayManager::TrayManager() = default;

TrayManager::~TrayManager() {
  stop();
}

bool TrayManager::start() {
  // Register the helper window class.
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = TrayManager::WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = kHelperClassName;
  RegisterClassEx(&wc);

  // Create a message-only window.
  helper_hwnd_ = CreateWindowEx(
      0, kHelperClassName, nullptr, 0,
      0, 0, 0, 0,
      HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), this);

  if (!helper_hwnd_) {
    return false;
  }

  SetWindowLongPtr(helper_hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  // Register the TaskbarCreated message so we can re-add our icon when
  // Explorer restarts.
  taskbar_created_msg_ = RegisterWindowMessage(L"TaskbarCreated");

  // Load the tray icon from the embedded resource, falling back to the
  // default application icon.
  HICON icon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_TRAY_ICON));
  if (!icon) {
    icon = LoadIcon(nullptr, IDI_APPLICATION);
  }

  // Set up the NOTIFYICONDATA structure.
  nid_.cbSize = sizeof(NOTIFYICONDATA);
  nid_.hWnd = helper_hwnd_;
  nid_.uID = 1;
  nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid_.uCallbackMessage = WM_SKIFF_TRAY;
  nid_.hIcon = icon;
  wcscpy_s(nid_.szTip, L"Skiff");

  if (!Shell_NotifyIcon(NIM_ADD, &nid_)) {
    return false;
  }
  icon_added_ = true;

  // Use the new-style icon for Windows 7+.
  nid_.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIcon(NIM_SETVERSION, &nid_);

  return true;
}

void TrayManager::stop() {
  if (icon_added_) {
    Shell_NotifyIcon(NIM_DELETE, &nid_);
    icon_added_ = false;
  }
  if (helper_hwnd_) {
    DestroyWindow(helper_hwnd_);
    helper_hwnd_ = nullptr;
  }
}

void TrayManager::setTooltip(const std::wstring& tooltip) {
  wcscpy_s(nid_.szTip, tooltip.c_str());
  if (icon_added_) {
    Shell_NotifyIcon(NIM_MODIFY, &nid_);
  }
}

void TrayManager::setGestureEnabled(bool enabled) {
  gesture_enabled_ = enabled;
}

void TrayManager::setMiddleDragReversed(bool reversed) {
  middle_drag_reversed_ = reversed;
}

void TrayManager::setScrollLines(int lines) {
  if (lines < 1) {
    lines = 1;
  } else if (lines > 10) {
    lines = 10;
  }
  scroll_lines_ = lines;
}

LRESULT CALLBACK TrayManager::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  TrayManager* self = reinterpret_cast<TrayManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  // Handle explorer restart.
  if (self && self->taskbar_created_msg_ != 0 && msg == self->taskbar_created_msg_) {
    // Re-add the tray icon.
    if (!self->icon_added_) {
      Shell_NotifyIcon(NIM_ADD, &self->nid_);
      self->icon_added_ = true;
    }
    return 0;
  }

  if (msg == WM_SKIFF_TRAY && self) {
    return self->handleTrayMessage(msg, wp, lp);
  }

  return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT TrayManager::handleTrayMessage(UINT /*msg*/, WPARAM /*wp*/, LPARAM lp) {
  switch (LOWORD(lp)) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      showContextMenu();
      break;
  }
  return 0;
}

void TrayManager::showContextMenu() {
  POINT pt;
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();

  // Menu text is in Chinese:
  //   "显示/隐藏按钮" = toggle button visibility
  //   "启用中键手势"  = toggle middle-click gesture
  //   "中键拖动反向"  = reverse middle-drag gesture
  //   "滚动行数"      = scroll lines submenu
  //   "退出"          = quit
  AppendMenuW(menu, MF_STRING, kMenuToggleButton,
              L"\x663E\x793A/\x9690\x85CF\x6309\x94AE");
  AppendMenuW(menu, MF_STRING | (gesture_enabled_ ? MF_CHECKED : MF_UNCHECKED),
              kMenuToggleGesture,
              L"\x542F\x7528\x4E2D\x952E\x624B\x52BF");
  AppendMenuW(menu,
              MF_STRING |
                  (middle_drag_reversed_ ? MF_CHECKED : MF_UNCHECKED),
              kMenuToggleMiddleDragReverse,
              L"\x4E2D\x952E\x62D6\x52A8\x53CD\x5411");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  HMENU scroll_menu = CreatePopupMenu();
  for (int i = 1; i <= 10; ++i) {
    wchar_t label[8];
    swprintf_s(label, sizeof(label) / sizeof(label[0]), L"%d", i);
    AppendMenuW(scroll_menu,
                MF_STRING | (i == scroll_lines_ ? MF_CHECKED : MF_UNCHECKED),
                kMenuScrollBase + i, label);
  }
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scroll_menu),
              L"\x6EDA\x52A8\x884C\x6570");

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit,
              L"\x9000\x51FA");

  // Required so the menu dismisses when the user clicks elsewhere.
  SetForegroundWindow(helper_hwnd_);

  UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                            pt.x, pt.y, 0, helper_hwnd_, nullptr);

  PostMessage(helper_hwnd_, WM_NULL, 0, 0);

  DestroyMenu(menu);

  if (menu_callback_) {
    if (cmd > kMenuScrollBase && cmd <= kMenuScrollBase + 10) {
      const int lines = static_cast<int>(cmd - kMenuScrollBase);
      setScrollLines(lines);
      menu_callback_("scroll_" + std::to_string(lines));
      return;
    }

    switch (cmd) {
      case kMenuToggleButton:
        menu_callback_("toggle_button");
        break;
      case kMenuToggleGesture:
        gesture_enabled_ = !gesture_enabled_;
        menu_callback_("toggle_gesture");
        break;
      case kMenuToggleMiddleDragReverse:
        middle_drag_reversed_ = !middle_drag_reversed_;
        menu_callback_("toggle_middle_drag_reverse");
        break;
      case kMenuQuit:
        menu_callback_("quit");
        break;
    }
  }
}
