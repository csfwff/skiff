#include "tray_manager.h"

// GtkStatusIcon is deprecated since GTK 3.14 but still works on most Linux
// desktops. Suppress deprecation warnings so the build succeeds with -Werror.
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

struct _TrayManager {
  GtkStatusIcon* icon;
  TrayActionCallback callback;
  gpointer user_data;
  gboolean gesture_checked;
  gboolean auto_start_checked;
  int scroll_lines;
};

static void on_menu_item_activate(GtkMenuItem* item, gpointer data) {
  const char* action = (const char*)data;
  GtkWidget* menu = gtk_widget_get_parent(GTK_WIDGET(item));
  TrayManager* tray = nullptr;
  if (GTK_IS_MENU(menu)) {
    tray = (TrayManager*)g_object_get_data(G_OBJECT(menu), "tray_manager");
  }
  if (tray && tray->callback) {
    tray->callback(action, tray->user_data);
  }
}

static void on_popup_menu(GtkStatusIcon* icon, guint button,
                           guint activate_time, gpointer data) {
  TrayManager* tray = (TrayManager*)data;
  GtkWidget* menu = gtk_menu_new();
  g_object_set_data(G_OBJECT(menu), "tray_manager", tray);

  // ── 显示/隐藏按钮 ──
  GtkWidget* toggle_btn =
      gtk_menu_item_new_with_label("\xe6\x98\xbe\xe7\xa4\xba/\xe9\x9a\x90"
                                   "\xe8\x97\x8f\xe6\x8c\x89\xe9\x92\xae");
  g_signal_connect(toggle_btn, "activate",
                   G_CALLBACK(on_menu_item_activate), (gpointer) "toggle_button");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggle_btn);

  // ── 启用中键手势 ☑/☐ ──
  const char* gesture_label = tray->gesture_checked
      ? "\xe2\x98\x91 \xe5\x90\xaf\xe7\x94\xa8\xe4\xb8\xad\xe9\x94\xae\xe6\x89\x8b\xe5\x8a\xbf"
      : "\xe2\x98\x90 \xe5\x90\xaf\xe7\x94\xa8\xe4\xb8\xad\xe9\x94\xae\xe6\x89\x8b\xe5\x8a\xbf";
  GtkWidget* gesture_item = gtk_menu_item_new_with_label(gesture_label);
  g_signal_connect(gesture_item, "activate",
                   G_CALLBACK(on_menu_item_activate), (gpointer) "toggle_gesture");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gesture_item);

  // ── 分割线 ──
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

  // ── 滚动行数 子菜单 ──
  GtkWidget* scroll_item = gtk_menu_item_new_with_label(
      "\xe6\xbb\x9a\xe5\x8a\xa8\xe8\xa1\x8c\xe6\x95\xb0");
  GtkWidget* scroll_submenu = gtk_menu_new();
  g_object_set_data(G_OBJECT(scroll_submenu), "tray_manager", tray);
  for (int i = 1; i <= 10; i++) {
    char label[16];
    if (i == tray->scroll_lines) {
      snprintf(label, sizeof(label), "\xe2\x98\x91 %d", i);
    } else {
      snprintf(label, sizeof(label), "  %d", i);
    }
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    char* action = g_strdup_printf("scroll_%d", i);
    g_signal_connect(item, "activate",
                     G_CALLBACK(on_menu_item_activate), action);
    // 注意: action 字符串不会被释放，但数量有限(10个)，可接受
    gtk_menu_shell_append(GTK_MENU_SHELL(scroll_submenu), item);
  }
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(scroll_item), scroll_submenu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), scroll_item);

  // ── 开机自启 ☑/☐ ──
  const char* autostart_label = tray->auto_start_checked
      ? "\xe2\x98\x91 \xe5\xbc\x80\xe6\x9c\xba\xe8\x87\xaa\xe5\x90\xaf"
      : "\xe2\x98\x90 \xe5\xbc\x80\xe6\x9c\xba\xe8\x87\xaa\xe5\x90\xaf";
  GtkWidget* autostart_item = gtk_menu_item_new_with_label(autostart_label);
  g_signal_connect(autostart_item, "activate",
                   G_CALLBACK(on_menu_item_activate), (gpointer) "toggle_autostart");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), autostart_item);

  // ── 分割线 ──
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

  // ── 退出 ──
  GtkWidget* quit_item =
      gtk_menu_item_new_with_label("\xe9\x80\x80\xe5\x87\xba");
  g_signal_connect(quit_item, "activate",
                   G_CALLBACK(on_menu_item_activate), (gpointer) "quit");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

  gtk_widget_show_all(menu);
  gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static void on_activate(GtkStatusIcon* icon, gpointer data) {
  TrayManager* tray = (TrayManager*)data;
  if (tray && tray->callback) {
    tray->callback("toggle_button", tray->user_data);
  }
}

TrayManager* tray_manager_new(TrayActionCallback callback, gpointer user_data) {
  TrayManager* tray = g_new0(TrayManager, 1);
  tray->callback = callback;
  tray->user_data = user_data;
  tray->gesture_checked = TRUE;
  tray->auto_start_checked = FALSE;
  tray->scroll_lines = 3;

  tray->icon = gtk_status_icon_new_from_icon_name("dialog-information");
  gtk_status_icon_set_tooltip_text(tray->icon, "Skiff");
  gtk_status_icon_set_visible(tray->icon, TRUE);

  g_signal_connect(tray->icon, "popup-menu", G_CALLBACK(on_popup_menu), tray);
  g_signal_connect(tray->icon, "activate", G_CALLBACK(on_activate), tray);

  return tray;
}

void tray_manager_set_gesture_checked(TrayManager* tray, gboolean checked) {
  if (tray) tray->gesture_checked = checked;
}

void tray_manager_set_auto_start_checked(TrayManager* tray, gboolean checked) {
  if (tray) tray->auto_start_checked = checked;
}

void tray_manager_set_scroll_lines(TrayManager* tray, int lines) {
  if (tray) tray->scroll_lines = lines;
}

void tray_manager_free(TrayManager* tray) {
  if (!tray) return;
  if (tray->icon) {
    gtk_status_icon_set_visible(tray->icon, FALSE);
    g_object_unref(tray->icon);
  }
  g_free(tray);
}

G_GNUC_END_IGNORE_DEPRECATIONS
