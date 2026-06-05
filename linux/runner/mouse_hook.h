#ifndef MOUSE_HOOK_H_
#define MOUSE_HOOK_H_

#include <glib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback invoked when a middle-click gesture is completed.
// dx and dy represent the scroll direction derived from the gesture.
typedef void (*MouseHookGestureCallback)(int dx, int dy, gpointer user_data);

// Opaque handle for the mouse hook instance.
typedef struct _MouseHook MouseHook;

// Create and start the mouse hook. Returns NULL if not on X11 or on failure.
// The callback is invoked from the hook thread; the caller must ensure
// thread-safe handling (e.g. using g_idle_add to marshal to the main thread).
MouseHook* mouse_hook_new(MouseHookGestureCallback callback,
                           gpointer user_data);

// Enable or disable the mouse hook's gesture detection.
void mouse_hook_set_enabled(MouseHook* hook, bool enabled);

// Stop the hook thread and free resources.
void mouse_hook_free(MouseHook* hook);

#ifdef __cplusplus
}
#endif

#endif  // MOUSE_HOOK_H_
