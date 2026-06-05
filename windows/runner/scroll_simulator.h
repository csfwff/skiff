#ifndef RUNNER_SCROLL_SIMULATOR_H_
#define RUNNER_SCROLL_SIMULATOR_H_

#include <windows.h>

// Simulates system-level mouse wheel scrolling using Win32 SendInput.
class ScrollSimulator {
 public:
  ScrollSimulator() = default;
  ~ScrollSimulator() = default;

  // Sends wheel events for the given deltas.
  // dy > 0 scrolls down, dy < 0 scrolls up.
  // dx > 0 scrolls right, dx < 0 scrolls left.
  void scroll(int dx, int dy);
};

#endif  // RUNNER_SCROLL_SIMULATOR_H_
