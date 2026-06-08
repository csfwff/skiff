#include "mouse_hook.h"

#include <windowsx.h>

#include <cmath>

namespace {
constexpr UINT WM_SKIFF_MOUSE = WM_APP + 100;
constexpr int kGestureThreshold = 20;  // pixels
constexpr wchar_t kHelperClassName[] = L"SkiffMouseHookHelper";
}  // namespace

MouseHook* MouseHook::instance_ = nullptr;

MouseHook::MouseHook() = default;

MouseHook::~MouseHook() {
  stop();
}

bool MouseHook::start() {
  if (hook_) {
    return true;  // already running
  }

  instance_ = this;

  // Register the helper window class.
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = MouseHook::WndProc;
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

  // Store the this pointer for WndProc retrieval.
  SetWindowLongPtr(helper_hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  // Install the global low-level mouse hook.
  hook_ = SetWindowsHookEx(WH_MOUSE_LL, MouseHook::HookProc,
                           GetModuleHandle(nullptr), 0);
  if (!hook_) {
    DestroyWindow(helper_hwnd_);
    helper_hwnd_ = nullptr;
    return false;
  }

  enabled_ = true;
  return true;
}

void MouseHook::stop() {
  if (hook_) {
    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
  }
  if (helper_hwnd_) {
    DestroyWindow(helper_hwnd_);
    helper_hwnd_ = nullptr;
  }
  instance_ = nullptr;
  state_ = State::IDLE;
  triggered_ = false;
}

void MouseHook::setEnabled(bool enabled) {
  enabled_ = enabled;
  if (!enabled) {
    // Reset state when disabling.
    state_ = State::IDLE;
    triggered_ = false;
  }
}

LRESULT CALLBACK MouseHook::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_SKIFF_MOUSE) {
    MouseHook* self = reinterpret_cast<MouseHook*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) {
      self->handleMouseMessage(wp, lp);
    }
    return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MouseHook::HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0 && instance_ && instance_->enabled_) {
    auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    LPARAM packed = MAKELPARAM(ms->pt.x, ms->pt.y);

    switch (wParam) {
      case WM_MBUTTONDOWN:
      case WM_MBUTTONUP:
      case WM_MOUSEMOVE:
        // Post to the helper window for async processing.
        PostMessage(instance_->helper_hwnd_, WM_SKIFF_MOUSE, wParam, packed);
        break;
    }
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void MouseHook::handleMouseMessage(WPARAM wParam, LPARAM lParam) {
  int x = GET_X_LPARAM(lParam);
  int y = GET_Y_LPARAM(lParam);

  switch (state_) {
    case State::IDLE:
      if (wParam == WM_MBUTTONDOWN) {
        start_x_ = x;
        start_y_ = y;
        triggered_ = false;
        state_ = State::PRESSED;
        // Do NOT suppress the event; let it pass through initially.
      }
      break;

    case State::PRESSED:
      if (wParam == WM_MOUSEMOVE) {
        if (!triggered_ && distanceFromStart(x, y) > kGestureThreshold) {
          triggered_ = true;
        }
      } else if (wParam == WM_MBUTTONUP) {
        if (triggered_) {
          // Determine direction from vertical delta.
          int dy = y - start_y_;
          std::string direction = (dy >= 0) ? "down" : "up";

          if (gesture_callback_) {
            gesture_callback_(direction);
          }
          // The middle-click is consumed (suppressed) -- we do not forward it.
        }
        // If not triggered, the original WM_MBUTTONDOWN was already forwarded
        // and the WM_MBUTTONUP will have been forwarded by CallNextHookEx too.
        // Reset state.
        state_ = State::IDLE;
        triggered_ = false;
      }
      break;
  }
}

double MouseHook::distanceFromStart(int x, int y) const {
  double dx = static_cast<double>(x - start_x_);
  double dy = static_cast<double>(y - start_y_);
  return std::sqrt(dx * dx + dy * dy);
}
