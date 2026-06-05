#ifndef SCROLL_SIMULATOR_H_
#define SCROLL_SIMULATOR_H_

#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

void scroll_simulator_scroll(int dx, int dy);
void scroll_simulator_scroll_through_window(GdkWindow* overlay_window,
                                            int dx,
                                            int dy);

#ifdef __cplusplus
}
#endif

#endif  // SCROLL_SIMULATOR_H_
