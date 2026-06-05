#include "skiff_platform.h"

#include <unistd.h>

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include "mouse_hook.h"
#include "scroll_simulator.h"
#include "tray_manager.h"

// ---- GObject boilerplate for the plugin ----

struct _SkiffNativePlugin {
  GObject parent_instance;
  FlMethodChannel* channel;
  FlView* view;
  TrayManager* tray;
  MouseHook* mouse_hook;
  gboolean gesture_enabled;
  gboolean auto_start;
  int scroll_lines;
};

G_DEFINE_TYPE(SkiffNativePlugin, skiff_native_plugin, G_TYPE_OBJECT)

// Forward declarations.
static void handle_set_auto_start_impl(SkiffNativePlugin* self,
                                        gboolean enabled);
static FlMethodResponse* handle_method_call(SkiffNativePlugin* self,
                                             const gchar* method,
                                             FlValue* args);

static GtkWindow* get_toplevel_window(SkiffNativePlugin* self) {
  if (self->view == nullptr) {
    return nullptr;
  }

  GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self->view));
  if (!GTK_IS_WINDOW(toplevel)) {
    return nullptr;
  }

  return GTK_WINDOW(toplevel);
}

static GdkWindow* get_toplevel_gdk_window(SkiffNativePlugin* self) {
  GtkWindow* window = get_toplevel_window(self);
  if (window == nullptr) {
    return nullptr;
  }

  return gtk_widget_get_window(GTK_WIDGET(window));
}

static void set_window_focusable(SkiffNativePlugin* self, gboolean focusable) {
  GtkWindow* window = get_toplevel_window(self);
  if (window == nullptr) {
    g_warning("skiff_platform: unable to find GTK toplevel window");
    return;
  }

  gtk_window_set_accept_focus(window, focusable);
  gtk_window_set_focus_on_map(window, focusable);

  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
  if (gdk_window != nullptr && !gdk_window_is_destroyed(gdk_window)) {
    gdk_window_set_accept_focus(gdk_window, focusable);
    gdk_window_set_focus_on_map(gdk_window, focusable);
    gdk_display_flush(gdk_window_get_display(gdk_window));
  }
}

// ---- Dart-invokable callbacks (sent via MethodChannel) ----

static void send_tray_action_to_dart(SkiffNativePlugin* self,
                                      const char* action) {
  g_autoptr(FlValue) args = fl_value_new_map();
  fl_value_set_string_take(args, "action", fl_value_new_string(action));
  fl_method_channel_invoke_method(self->channel, "onTrayAction", args,
                                   nullptr, nullptr, nullptr);
}

static void send_middle_click_gesture_to_dart(SkiffNativePlugin* self,
                                               int dx, int dy) {
  // Convert dx/dy to a direction string: "up", "down", "left", "right"
  const char* direction = "down";
  if (abs(dy) >= abs(dx)) {
    direction = (dy < 0) ? "up" : "down";
  } else {
    direction = (dx < 0) ? "left" : "right";
  }

  g_autoptr(FlValue) args = fl_value_new_map();
  fl_value_set_string_take(args, "direction", fl_value_new_string(direction));
  fl_method_channel_invoke_method(self->channel, "onMiddleClickGesture", args,
                                   nullptr, nullptr, nullptr);
}

// ---- Mouse hook callback (called from the hook thread) ----

// Data for marshalling the mouse hook callback to the main thread.
typedef struct {
  SkiffNativePlugin* self;
  int dx;
  int dy;
} GestureCallbackData;

static gboolean gesture_callback_idle(gpointer data) {
  GestureCallbackData* gd = (GestureCallbackData*)data;
  send_middle_click_gesture_to_dart(gd->self, gd->dx, gd->dy);
  g_free(gd);
  return FALSE;  // Remove from idle
}

static void on_mouse_gesture(int dx, int dy, gpointer user_data) {
  SkiffNativePlugin* self = SKIFF_NATIVE_PLUGIN(user_data);
  // Marshal to the main thread via g_idle_add.
  GestureCallbackData* gd = g_new0(GestureCallbackData, 1);
  gd->self = self;
  gd->dx = dx;
  gd->dy = dy;
  g_idle_add(gesture_callback_idle, gd);
}

// ---- Tray callback ----

static void on_tray_action(const char* action, gpointer user_data) {
  SkiffNativePlugin* self = SKIFF_NATIVE_PLUGIN(user_data);

  if (g_strcmp0(action, "toggle_gesture") == 0) {
    self->gesture_enabled = !self->gesture_enabled;
    tray_manager_set_gesture_checked(self->tray, self->gesture_enabled);
    if (self->mouse_hook) {
      mouse_hook_set_enabled(self->mouse_hook, self->gesture_enabled);
    }
  } else if (g_strcmp0(action, "toggle_autostart") == 0) {
    self->auto_start = !self->auto_start;
    tray_manager_set_auto_start_checked(self->tray, self->auto_start);
    // 调用原生设置开机自启
    handle_set_auto_start_impl(self, self->auto_start);
  } else if (g_str_has_prefix(action, "scroll_")) {
    int lines = atoi(action + 7);  // "scroll_N" → N
    if (lines >= 1 && lines <= 10) {
      self->scroll_lines = lines;
      tray_manager_set_scroll_lines(self->tray, lines);
    }
  }

  send_tray_action_to_dart(self, action);
}

// ---- Method channel handler ----

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                            gpointer user_data) {
  SkiffNativePlugin* self = SKIFF_NATIVE_PLUGIN(user_data);

  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);

  FlMethodResponse* response = handle_method_call(self, method, args);
  fl_method_call_respond(method_call, response, nullptr);
}

static FlMethodResponse* handle_initialize(SkiffNativePlugin* self) {
  // Create the system tray icon.
  if (!self->tray) {
    self->tray = tray_manager_new(on_tray_action, self);
  }

  // Install the mouse hook for middle-click gestures.
  if (!self->mouse_hook) {
    self->mouse_hook = mouse_hook_new(on_mouse_gesture, self);
    if (self->mouse_hook) {
      mouse_hook_set_enabled(self->mouse_hook, self->gesture_enabled);
    }
  }

  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_simulate_scroll(SkiffNativePlugin* self,
                                                  FlValue* args) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Expected map with dx and dy", nullptr));
  }

  FlValue* dx_val = fl_value_lookup_string(args, "dx");
  FlValue* dy_val = fl_value_lookup_string(args, "dy");

  if (!dx_val || !dy_val) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Missing dx or dy parameter", nullptr));
  }

  int dx = (int)fl_value_get_int(dx_val);
  int dy = (int)fl_value_get_int(dy_val);

  scroll_simulator_scroll(dx, dy);

  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_simulate_scroll_direct(SkiffNativePlugin* self,
                                                        FlValue* args) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Expected map with dx and dy", nullptr));
  }

  FlValue* dx_val = fl_value_lookup_string(args, "dx");
  FlValue* dy_val = fl_value_lookup_string(args, "dy");

  if (!dx_val || !dy_val) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Missing dx or dy parameter", nullptr));
  }

  int dx = (int)fl_value_get_int(dx_val);
  int dy = (int)fl_value_get_int(dy_val);

  scroll_simulator_scroll_through_window(get_toplevel_gdk_window(self), dx, dy);

  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_set_overlay_mode(SkiffNativePlugin* self,
                                                  FlValue* args) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Expected map with enabled", nullptr));
  }

  FlValue* enabled_val = fl_value_lookup_string(args, "enabled");
  if (!enabled_val) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Missing enabled parameter", nullptr));
  }

  gboolean overlay_mode = fl_value_get_bool(enabled_val);
  set_window_focusable(self, !overlay_mode);

  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_set_middle_click_enabled(
    SkiffNativePlugin* self, FlValue* args) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Expected map with enabled", nullptr));
  }

  FlValue* enabled_val = fl_value_lookup_string(args, "enabled");
  if (!enabled_val) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Missing enabled parameter", nullptr));
  }

  gboolean enabled = fl_value_get_bool(enabled_val);
  self->gesture_enabled = enabled;

  if (self->mouse_hook) {
    mouse_hook_set_enabled(self->mouse_hook, enabled);
  }
  if (self->tray) {
    tray_manager_set_gesture_checked(self->tray, enabled);
  }

  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_set_overlay_visible(SkiffNativePlugin* self,
                                                      FlValue* args) {
  // Overlay visibility is managed by Dart via window_manager.
  // No native action needed.
  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

// ---- 开机自启（Linux: ~/.config/autostart/xxx.desktop）----

static const char* get_desktop_file_path() {
  static char path[1024];
  const char* config = g_get_user_config_dir();
  g_snprintf(path, sizeof(path), "%s/autostart/skiff.desktop", config);
  return path;
}

// 内部实现：设置开机自启（可从托盘菜单和方法通道调用）
static void handle_set_auto_start_impl(SkiffNativePlugin* self,
                                        gboolean enabled) {
  const char* path = get_desktop_file_path();
  if (enabled) {
    char* exe = g_file_read_link("/proc/self/exe", nullptr);
    if (!exe) return;
    char* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    char* content = g_strdup_printf(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Skiff\n"
        "Comment=Mouse wheel scroll simulator\n"
        "Exec=%s\n"
        "Hidden=false\n"
        "NoDisplay=true\n"
        "X-GNOME-Autostart-enabled=true\n",
        exe);
    g_file_set_contents(path, content, -1, nullptr);
    g_free(content);
    g_free(exe);
  } else {
    unlink(path);
  }
}

static FlMethodResponse* handle_set_auto_start(SkiffNativePlugin* self,
                                                FlValue* args) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Expected map", nullptr));
  }
  FlValue* enabled_val = fl_value_lookup_string(args, "enabled");
  if (!enabled_val) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "INVALID_ARGS", "Missing enabled", nullptr));
  }
  gboolean enabled = fl_value_get_bool(enabled_val);
  self->auto_start = enabled;
  tray_manager_set_auto_start_checked(self->tray, enabled);
  handle_set_auto_start_impl(self, enabled);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_get_auto_start(SkiffNativePlugin* self) {
  const char* path = get_desktop_file_path();
  gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);

  FlValue* result = fl_value_new_map();
  fl_value_set_string_take(result, "enabled", fl_value_new_bool(exists));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* handle_quit(SkiffNativePlugin* self) {
  // Clean up resources before quitting.
  if (self->mouse_hook) {
    mouse_hook_free(self->mouse_hook);
    self->mouse_hook = nullptr;
  }
  if (self->tray) {
    tray_manager_free(self->tray);
    self->tray = nullptr;
  }

  // Quit the application.
  GApplication* app = g_application_get_default();
  if (app) {
    g_application_quit(app);
  }

  return FL_METHOD_RESPONSE(fl_method_success_response_new(
      fl_value_new_null()));
}

static FlMethodResponse* handle_method_call(SkiffNativePlugin* self,
                                             const gchar* method,
                                             FlValue* args) {
  if (g_strcmp0(method, "initialize") == 0) {
    return handle_initialize(self);
  } else if (g_strcmp0(method, "simulateScroll") == 0) {
    return handle_simulate_scroll(self, args);
  } else if (g_strcmp0(method, "simulateScrollDirect") == 0) {
    return handle_simulate_scroll_direct(self, args);
  } else if (g_strcmp0(method, "setOverlayMode") == 0) {
    return handle_set_overlay_mode(self, args);
  } else if (g_strcmp0(method, "setMiddleClickEnabled") == 0) {
    return handle_set_middle_click_enabled(self, args);
  } else if (g_strcmp0(method, "setOverlayVisible") == 0) {
    return handle_set_overlay_visible(self, args);
  } else if (g_strcmp0(method, "setAutoStart") == 0) {
    return handle_set_auto_start(self, args);
  } else if (g_strcmp0(method, "getAutoStart") == 0) {
    return handle_get_auto_start(self);
  } else if (g_strcmp0(method, "quit") == 0) {
    return handle_quit(self);
  } else {
    return FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }
}

// ---- GObject lifecycle ----

static void skiff_native_plugin_dispose(GObject* object) {
  SkiffNativePlugin* self = SKIFF_NATIVE_PLUGIN(object);

  if (self->mouse_hook) {
    mouse_hook_free(self->mouse_hook);
    self->mouse_hook = nullptr;
  }
  if (self->tray) {
    tray_manager_free(self->tray);
    self->tray = nullptr;
  }
  g_clear_object(&self->channel);
  g_clear_object(&self->view);

  G_OBJECT_CLASS(skiff_native_plugin_parent_class)->dispose(object);
}

static void skiff_native_plugin_class_init(SkiffNativePluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = skiff_native_plugin_dispose;
}

static void skiff_native_plugin_init(SkiffNativePlugin* self) {
  self->gesture_enabled = TRUE;
}

// ---- Plugin registration ----

void skiff_native_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  SkiffNativePlugin* plugin = SKIFF_NATIVE_PLUGIN(
      g_object_new(skiff_native_plugin_get_type(), nullptr));
  plugin->view = fl_plugin_registrar_get_view(registrar);
  if (plugin->view != nullptr) {
    g_object_ref(plugin->view);
  }

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  plugin->channel = fl_method_channel_new(
      fl_plugin_registrar_get_messenger(registrar),
      "com.skiff/native",
      FL_METHOD_CODEC(codec));

  fl_method_channel_set_method_call_handler(
      plugin->channel, method_call_cb, g_object_ref(plugin),
      g_object_unref);

  g_object_unref(plugin);
}
