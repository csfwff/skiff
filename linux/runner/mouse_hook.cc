#include "mouse_hook.h"

#include <cmath>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>

static const int GESTURE_THRESHOLD = 20;
static const gint64 RIGHT_HOLD_THRESHOLD_US = 2 * G_USEC_PER_SEC;

typedef enum {
  HOOK_STATE_IDLE,
  HOOK_STATE_PRESSED,
} HookState;

struct _MouseHook {
  GThread* thread;
  volatile gboolean running;
  volatile gboolean enabled;

  MouseHookGestureCallback gesture_callback;
  MouseHookRightHoldCallback right_hold_callback;
  gpointer user_data;

  HookState state;
  int press_x;
  int press_y;
  gboolean triggered;

  gboolean right_pressed;
  gboolean right_hold_triggered;
  gint64 right_press_time_us;
};

static bool is_x11() {
  GdkDisplay* display = gdk_display_get_default();
  return GDK_IS_X11_DISPLAY(display);
}

static gpointer mouse_hook_thread(gpointer data) {
  MouseHook* hook = (MouseHook*)data;

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    g_warning("mouse_hook: failed to open X display");
    return nullptr;
  }

  g_message("mouse_hook: polling thread started");

  while (hook->running) {
    // 每 10ms 轮询一次鼠标状态
    g_usleep(10 * 1000);  // 10ms

    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;

    if (!XQueryPointer(display, DefaultRootWindow(display), &root_ret,
                       &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
      continue;
    }

    bool right_pressed = (mask & Button3Mask) != 0;
    if (right_pressed) {
      if (!hook->right_pressed) {
        hook->right_pressed = TRUE;
        hook->right_hold_triggered = FALSE;
        hook->right_press_time_us = g_get_monotonic_time();
        g_message("mouse_hook: right button pressed at (%d, %d)",
                  root_x, root_y);
      } else if (!hook->right_hold_triggered &&
                 g_get_monotonic_time() - hook->right_press_time_us >=
                     RIGHT_HOLD_THRESHOLD_US) {
        hook->right_hold_triggered = TRUE;
        g_message("mouse_hook: right button hold detected at (%d, %d)",
                  root_x, root_y);
        if (hook->right_hold_callback) {
          hook->right_hold_callback(root_x, root_y, hook->user_data);
        }
      }
    } else if (hook->right_pressed) {
      hook->right_pressed = FALSE;
      hook->right_hold_triggered = FALSE;
    }

    if (!hook->enabled) {
      if (hook->state != HOOK_STATE_IDLE) {
        hook->state = HOOK_STATE_IDLE;
        hook->triggered = FALSE;
      }
      continue;
    }

    bool middle_pressed = (mask & Button2Mask) != 0;

    switch (hook->state) {
      case HOOK_STATE_IDLE:
        if (middle_pressed) {
          hook->press_x = root_x;
          hook->press_y = root_y;
          hook->triggered = FALSE;
          hook->state = HOOK_STATE_PRESSED;
          g_message("mouse_hook: middle button pressed at (%d, %d)",
                    root_x, root_y);
        }
        break;

      case HOOK_STATE_PRESSED:
        if (!middle_pressed) {
          // 中键松开
          if (hook->triggered) {
            int dx = root_x - hook->press_x;
            int dy = root_y - hook->press_y;
            int scroll_dx = 0, scroll_dy = 0;
            if (abs(dx) > GESTURE_THRESHOLD)
              scroll_dx = (dx > 0) ? 1 : -1;
            if (abs(dy) > GESTURE_THRESHOLD)
              scroll_dy = (dy > 0) ? 1 : -1;

            if (scroll_dx != 0 || scroll_dy != 0) {
              g_message("mouse_hook: gesture detected dx=%d dy=%d",
                        scroll_dx, scroll_dy);
              if (hook->gesture_callback) {
                hook->gesture_callback(scroll_dx, scroll_dy, hook->user_data);
              }
            }
          } else {
            g_message("mouse_hook: middle click (no gesture)");
          }
          hook->state = HOOK_STATE_IDLE;
          hook->triggered = FALSE;
        } else {
          // 中键仍按住，检查是否超过阈值
          int dx = abs(root_x - hook->press_x);
          int dy = abs(root_y - hook->press_y);
          if (!hook->triggered &&
              (dx > GESTURE_THRESHOLD || dy > GESTURE_THRESHOLD)) {
            hook->triggered = TRUE;
            g_message("mouse_hook: gesture threshold reached");
          }
        }
        break;
    }
  }

  XCloseDisplay(display);
  g_message("mouse_hook: polling thread stopped");
  return nullptr;
}

MouseHook* mouse_hook_new(MouseHookGestureCallback gesture_callback,
                          MouseHookRightHoldCallback right_hold_callback,
                          gpointer user_data) {
  if (!is_x11()) {
    g_warning("mouse_hook: not running on X11, disabled");
    return nullptr;
  }

  MouseHook* hook = g_new0(MouseHook, 1);
  hook->gesture_callback = gesture_callback;
  hook->right_hold_callback = right_hold_callback;
  hook->user_data = user_data;
  hook->running = TRUE;
  hook->enabled = TRUE;
  hook->state = HOOK_STATE_IDLE;

  hook->thread = g_thread_new("skiff-mouse-hook", mouse_hook_thread, hook);
  return hook;
}

void mouse_hook_set_enabled(MouseHook* hook, bool enabled) {
  if (hook) hook->enabled = enabled ? TRUE : FALSE;
}

void mouse_hook_free(MouseHook* hook) {
  if (!hook) return;
  hook->running = FALSE;
  if (hook->thread) g_thread_join(hook->thread);
  g_free(hook);
}
