#include "scroll_simulator.h"

#include <cstdlib>

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

namespace {

bool set_input_passthrough(GdkWindow* window, bool passthrough) {
  if (window == nullptr || gdk_window_is_destroyed(window)) {
    return false;
  }

  GdkDisplay* display = gdk_window_get_display(window);
  if (display == nullptr) {
    return false;
  }

#ifdef GDK_WINDOWING_X11
  if (!GDK_IS_X11_DISPLAY(display)) {
    g_warning("scroll_simulator: input passthrough requires an X11 display");
    return false;
  }

  if (passthrough) {
    cairo_region_t* empty_region = cairo_region_create();
    gdk_window_input_shape_combine_region(window, empty_region, 0, 0);
    cairo_region_destroy(empty_region);
  } else {
    gdk_window_input_shape_combine_region(window, nullptr, 0, 0);
  }

  gdk_display_sync(display);
  return true;
#else
  g_warning("scroll_simulator: input passthrough was built without X11 support");
  return false;
#endif
}

}  // namespace

void scroll_simulator_scroll(int dx, int dy) {
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    g_warning("scroll_simulator: failed to open X display");
    return;
  }

  if (dy != 0) {
    int button = (dy < 0) ? 4 : 5;
    int count = abs(dy);
    for (int i = 0; i < count; i++) {
      XTestFakeButtonEvent(display, button, True, CurrentTime);
      XTestFakeButtonEvent(display, button, False, CurrentTime);
    }
  }

  if (dx != 0) {
    int button = (dx < 0) ? 6 : 7;
    int count = abs(dx);
    for (int i = 0; i < count; i++) {
      XTestFakeButtonEvent(display, button, True, CurrentTime);
      XTestFakeButtonEvent(display, button, False, CurrentTime);
    }
  }

  XFlush(display);
  XCloseDisplay(display);
}

void scroll_simulator_scroll_through_window(GdkWindow* overlay_window,
                                            int dx,
                                            int dy) {
  if (!set_input_passthrough(overlay_window, true)) {
    scroll_simulator_scroll(dx, dy);
    return;
  }

  scroll_simulator_scroll(dx, dy);
  set_input_passthrough(overlay_window, false);
}
