#include "scroll_simulator.h"

void ScrollSimulator::scroll(int dx, int dy) {
  // Build an array of INPUT structures for any non-zero axes.
  INPUT inputs[2] = {};
  int count = 0;

  if (dy != 0) {
    inputs[count].type = INPUT_MOUSE;
    inputs[count].mi.mouseData = static_cast<DWORD>(dy * 120);  // WHEEL_DELTA = 120
    inputs[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
    ++count;
  }

  if (dx != 0) {
    inputs[count].type = INPUT_MOUSE;
    inputs[count].mi.mouseData = static_cast<DWORD>(dx * 120);  // WHEEL_DELTA = 120
    inputs[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
    ++count;
  }

  if (count > 0) {
    SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
  }
}
