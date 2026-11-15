// Analogue sticks -> d-pad presses, without the jitter.
// Header-only and SDL-free so tests/test_core.cpp can drive it with fake axis values.
//
// Why the old version jumped around:
//  - near a diagonal the "bigger axis" flipped between X and Y, firing a new direction each flip;
//  - letting go, the stick springs back past centre and briefly reads the opposite way (fired as a press);
//  - a stick that does not rest at exactly 0 was never re-centred.
// Rules now:
//  - a press needs one axis past ON and clearly bigger than the other (1.5x), measured from the stick's rest point;
//  - while held, only the held axis counts: it stays held until that axis drops under OFF (sideways wobble is ignored);
//  - after a release the stick must come back to centre and stay quiet for SETTLE ms before it can press again;
//  - repeats start after DELAY ms and then come every RATE ms.
#pragma once
#include <cstdlib>

namespace kt {

enum StickDir { STICK_NONE = -1, STICK_UP = 0, STICK_DOWN = 1, STICK_LEFT = 2, STICK_RIGHT = 3 };

struct StickPad {
  int ON = 20000, OFF = 11000, CENTRE = 8000;
  unsigned SETTLE = 120, DELAY = 400, RATE = 140;
  struct One {
    int x = 0, y = 0, cx = 0, cy = 0;    // last value and rest point per axis
    bool calX = false, calY = false;     // rest point taken from the first small reading
    int dir = STICK_NONE;
    bool armed = true;                   // false until the stick has settled at centre after a release
    unsigned quietSince = 0, next = 0;
  } s[2];

  // Feed one axis event. Calls fire(dir) for a new press. Returns the press fired, or STICK_NONE.
  template <class F> int axis(int stick, bool isX, int v, unsigned now, F fire) {
    One& o = s[stick & 1];
    if (isX) { if (!o.calX && std::abs(v) < CENTRE) { o.cx = v; o.calX = true; } o.x = v; }
    else     { if (!o.calY && std::abs(v) < CENTRE) { o.cy = v; o.calY = true; } o.y = v; }
    int ax = o.x - o.cx, ay = o.y - o.cy;
    if (o.dir != STICK_NONE) {
      int along = (o.dir == STICK_LEFT) ? -ax : (o.dir == STICK_RIGHT) ? ax : (o.dir == STICK_UP) ? -ay : ay;
      if (along >= OFF) return STICK_NONE;          // still held: ignore wobble on the other axis
      o.dir = STICK_NONE; o.armed = false; o.quietSince = 0;
    }
    bool centred = std::abs(ax) < CENTRE && std::abs(ay) < CENTRE;
    if (!o.armed) {
      if (!centred) { o.quietSince = 0; return STICK_NONE; }
      if (!o.quietSince) o.quietSince = now ? now : 1;
      if (now - o.quietSince < SETTLE) return STICK_NONE;
      o.armed = true;
    }
    int d = STICK_NONE;
    if (std::abs(ax) >= ON && std::abs(ax) * 2 >= std::abs(ay) * 3) d = ax > 0 ? STICK_RIGHT : STICK_LEFT;
    else if (std::abs(ay) >= ON && std::abs(ay) * 2 >= std::abs(ax) * 3) d = ay > 0 ? STICK_DOWN : STICK_UP;
    if (d == STICK_NONE) return STICK_NONE;
    o.dir = d; o.next = now + DELAY; fire(d);
    return d;
  }
  // Call every frame: re-arms a settled stick and fires held repeats.
  template <class F> void repeat(unsigned now, F fire) {
    for (One& o : s) {
      if (!o.armed && o.dir == STICK_NONE && o.quietSince && now - o.quietSince >= SETTLE) o.armed = true;
      if (o.dir != STICK_NONE && now >= o.next) { fire(o.dir); o.next = now + RATE; }
    }
  }
};

}  // namespace kt
