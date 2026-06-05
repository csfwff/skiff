#ifndef RUNNER_MOUSE_HOOK_H_
#define RUNNER_MOUSE_HOOK_H_

#include <windows.h>

#include <functional>
#include <string>

// Installs a global low-level mouse hook to detect middle-click gestures.
//
// State machine:
//   IDLE     -- WM_MBUTTONDOWN --> record (startX, startY), PRESSED
//   PRESSED  -- WM_MOUSEMOVE   --> if distance > 20px, set triggered=true
//   PRESSED  -- WM_MBUTTONUP   --> if triggered, compute direction,
//                                  invoke callback, suppress event
//                               --> if not triggered, pass through
class MouseHook {
 public:
  // Callback invoked when a middle-click gesture is detected.
  // The |direction| argument is "up" or "down".
  using GestureCallback = std::function<void(const std::string& direction)>;

  MouseHook();
  ~MouseHook();

  // Installs the hook and creates the hidden message-only window.
  bool start();

  // Uninstalls the hook and destroys the hidden window.
  void stop();

  // Enables or disables the hook processing.
  void setEnabled(bool enabled);

  bool isEnabled() const { return enabled_; }

  // Sets the callback to invoke when a gesture is detected.
  void setGestureCallback(GestureCallback callback) {
    gesture_callback_ = std::move(callback);
  }

 private:
  enum class State { IDLE, PRESSED };

  // Hidden window procedure for async message dispatch.
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  // The low-level mouse hook callback.
  static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);

  // Processes a mouse message asynchronously (called from WndProc).
  void handleMouseMessage(WPARAM wParam, LPARAM lParam);

  // Helper: compute Euclidean distance from start point to (x, y).
  double distanceFromStart(int x, int y) const;

  HHOOK hook_ = nullptr;
  HWND helper_hwnd_ = nullptr;
  bool enabled_ = true;

  State state_ = State::IDLE;
  int start_x_ = 0;
  int start_y_ = 0;
  bool triggered_ = false;

  GestureCallback gesture_callback_;

  // Singleton pointer used by the static callbacks.
  static MouseHook* instance_;
};

#endif  // RUNNER_MOUSE_HOOK_H_
