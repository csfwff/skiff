#ifndef TRAY_MANAGER_H_
#define TRAY_MANAGER_H_

#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TrayActionCallback)(const char* action, gpointer user_data);
typedef struct _TrayManager TrayManager;

TrayManager* tray_manager_new(TrayActionCallback callback, gpointer user_data);

void tray_manager_set_gesture_checked(TrayManager* tray, gboolean checked);
void tray_manager_set_middle_drag_reversed(TrayManager* tray, gboolean reversed);
void tray_manager_set_auto_start_checked(TrayManager* tray, gboolean checked);
void tray_manager_set_scroll_lines(TrayManager* tray, int lines);

void tray_manager_free(TrayManager* tray);

#ifdef __cplusplus
}
#endif

#endif  // TRAY_MANAGER_H_
