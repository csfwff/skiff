#ifndef RUNNER_TRAY_MANAGER_H_
#define RUNNER_TRAY_MANAGER_H_

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

// Manages a system tray icon with a context menu.
//
// Context menu items:
//   "显示/隐藏按钮"   -> toggle_button
//   "启用中键手势"    -> toggle_gesture
//   "中键拖动反向"    -> toggle_middle_drag_reverse
//   "滚动行数"        -> scroll_N
//   separator
//   "退出"            -> quit
class TrayManager {
 public:
  // Callback invoked when a menu item is selected.
  using MenuCallback = std::function<void(const std::string& action)>;

  TrayManager();
  ~TrayManager();

  // Creates the hidden message-only window and the tray icon.
  bool start();

  // Removes the tray icon and destroys the helper window.
  void stop();

  // Sets the callback for menu actions.
  void setMenuCallback(MenuCallback callback) {
    menu_callback_ = std::move(callback);
  }

  // Update the tooltip text (e.g. to reflect enabled/disabled state).
  void setTooltip(const std::wstring& tooltip);

  // Updates the checked value for the middle-click gesture menu item.
  void setGestureEnabled(bool enabled);

  // Updates the checked value for the reverse middle-drag menu item.
  void setMiddleDragReversed(bool reversed);

  // Updates the checked value in the scroll-lines submenu.
  void setScrollLines(int lines);

 private:
  // Window procedure for the hidden message-only window.
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  // Handles tray notification messages.
  LRESULT handleTrayMessage(UINT msg, WPARAM wp, LPARAM lp);

  // Shows the context menu at the cursor position.
  void showContextMenu();

  HWND helper_hwnd_ = nullptr;
  NOTIFYICONDATA nid_ = {};
  UINT taskbar_created_msg_ = 0;
  bool icon_added_ = false;
  bool gesture_enabled_ = false;
  bool middle_drag_reversed_ = false;
  int scroll_lines_ = 3;

  MenuCallback menu_callback_;

  static constexpr UINT WM_SKIFF_TRAY = WM_APP + 1;
  static constexpr wchar_t kHelperClassName[] = L"SkiffTrayHelper";
};

#endif  // RUNNER_TRAY_MANAGER_H_
