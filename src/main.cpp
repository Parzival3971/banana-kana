// Banana Kana: kana drills + stylus writing for the Anbernic RG DS Plus (BananaOS).
// Two SDL windows: top = prompt/feedback, bottom = touch. Logical size 640x480 each.
// This file: start-up, the event loop, gamepad/touch input and device housekeeping. Screens live in app.cpp.
#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "app.h"

#ifdef __linux__
#include <cerrno>
#include <climits>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

using namespace kt;

namespace {
volatile std::sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

bool mkdirs(const std::string& p) {
  std::string cur;
  for (size_t i = 0; i < p.size(); i++) { cur += p[i]; if ((p[i] == '/' && i > 0) || i + 1 == p.size()) mkdir(cur.c_str(), 0755); }
  struct stat st; return stat(p.c_str(), &st) == 0;
}
}  // namespace

// ------------------------------------------------------------------ bottom touchscreen via evdev
// SDL can't tell the RG DS Plus touch devices apart, so (like Banana Player) read the bottom panel's
// touchscreen directly: the input device whose sysfs path contains fe5e0000.i2c. KT_TOUCH_DEV overrides it,
// KT_TOUCH=sdl disables this, KT_TOUCH_SWAPXY / KT_TOUCH_FLIPX / KT_TOUCH_FLIPY fix orientation.
struct EvTouch {
  int fd = -1, minX = 0, maxX = 1, minY = 0, maxY = 1, x = 0, y = 0, slot = 0, logged = 0;
  bool down = false, wasDown = false, swapXY = false, flipX = false, flipY = false;
#ifdef __linux__
  static bool absRange(int fd, int code, int* mn, int* mx) {
    struct input_absinfo ai{}; if (ioctl(fd, EVIOCGABS(code), &ai) < 0 || ai.maximum <= ai.minimum) return false;
    *mn = ai.minimum; *mx = ai.maximum; return true;
  }
  bool open() {
    const char* force = getenv("KT_TOUCH"); if (force && std::string(force) == "sdl") { fprintf(stderr, "  touch: KT_TOUCH=sdl, evdev off\n"); return false; }
    swapXY = getenv("KT_TOUCH_SWAPXY"); flipX = getenv("KT_TOUCH_FLIPX"); flipY = getenv("KT_TOUCH_FLIPY");
    std::string dev = getenv("KT_TOUCH_DEV") ? getenv("KT_TOUCH_DEV") : "";
    if (dev.empty()) {
      DIR* d = opendir("/sys/class/input"); if (!d) return false;
      while (dirent* e = readdir(d)) {
        std::string n = e->d_name; if (n.rfind("event", 0) != 0) continue;
        char real[PATH_MAX]; std::string link = "/sys/class/input/" + n + "/device";
        if (!realpath(link.c_str(), real)) continue;
        char name[128] = "?"; FILE* f = fopen((link + "/name").c_str(), "r"); if (f) { if (!fgets(name, sizeof name, f)) name[0] = 0; fclose(f); name[strcspn(name, "\n")] = 0; }
        fprintf(stderr, "  input %s: %s  [%s]\n", n.c_str(), name, real);
        if (dev.empty() && strstr(real, "fe5e0000.i2c")) dev = "/dev/input/" + n;
      }
      closedir(d);
    }
    if (dev.empty()) { fprintf(stderr, "  touch: no fe5e0000.i2c input device found, using SDL touch\n"); return false; }
    fd = ::open(dev.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) { fprintf(stderr, "  touch: cannot open %s: %s, using SDL touch\n", dev.c_str(), strerror(errno)); return false; }
    if (!absRange(fd, ABS_MT_POSITION_X, &minX, &maxX)) absRange(fd, ABS_X, &minX, &maxX);
    if (!absRange(fd, ABS_MT_POSITION_Y, &minY, &maxY)) absRange(fd, ABS_Y, &minY, &maxY);
    fprintf(stderr, "  touch: evdev %s x=%d..%d y=%d..%d swap=%d flipx=%d flipy=%d\n", dev.c_str(), minX, maxX, minY, maxY, swapXY, flipX, flipY);
    return true;
  }
  template <class F> void poll(F&& pointer) {
    if (fd < 0) return;
    struct input_event ev[64];
    for (;;) {
      ssize_t n = read(fd, ev, sizeof ev); if (n <= 0) break;
      for (size_t i = 0; i < size_t(n) / sizeof(ev[0]); i++) {
        const auto& e = ev[i];
        if (e.type == EV_ABS) {
          if (e.code == ABS_MT_SLOT) slot = e.value;
          else if (slot != 0) continue;
          else if (e.code == ABS_MT_POSITION_X || e.code == ABS_X) x = e.value;
          else if (e.code == ABS_MT_POSITION_Y || e.code == ABS_Y) y = e.value;
          else if (e.code == ABS_MT_TRACKING_ID) down = e.value >= 0;
        } else if (e.type == EV_KEY && e.code == BTN_TOUCH) {
          down = e.value != 0;
        } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
          float fx = float(x - minX) / float(maxX - minX), fy = float(y - minY) / float(maxY - minY);
          if (swapXY) std::swap(fx, fy);
          if (flipX) fx = 1 - fx;
          if (flipY) fy = 1 - fy;
          int lx = int(std::clamp(fx, 0.f, 1.f) * (LW - 1)), ly = int(std::clamp(fy, 0.f, 1.f) * (LH - 1));
          int type = (down && !wasDown) ? 0 : down ? 1 : (wasDown ? 2 : -1);
          if (type == 0 && logged < 8) { logged++; fprintf(stderr, "  touch down raw=(%d,%d) -> logical (%d,%d)\n", x, y, lx, ly); fflush(stderr); }
          if (type >= 0) pointer(type, lx, ly);
          wasDown = down;
        }
      }
    }
  }
#else
  bool open() { return false; }
  template <class F> void poll(F&&) {}
#endif
};

int main(int argc, char** argv) {
  bool self = false, forceDual = false; std::string shotDir, dataDir, stateDir;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--selftest") self = true;
    else if (a == "--dual") forceDual = true;
    else if (a == "--shots" && i + 1 < argc) shotDir = argv[++i];
    else if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
    else if (a == "--state-dir" && i + 1 < argc) stateDir = argv[++i];
  }
  if (dataDir.empty()) {
    char* base = SDL_GetBasePath(); std::string b = base ? base : "./"; if (base) SDL_free(base);
    for (std::string c : {b + "data", b + "../app/data", std::string("app/data"), std::string("data")}) { struct stat st; if (stat((c + "/kana.tsv").c_str(), &st) == 0) { dataDir = c; break; } }
  }
  if (stateDir.empty()) {
    const char* x = getenv("XDG_CONFIG_HOME"); const char* h = getenv("HOME");
    stateDir = x ? std::string(x) + "/banana-kana" : std::string(h ? h : ".") + "/.config/banana-kana";
  }
  App app; app.dataDir = dataDir; std::string err; applyTheme(false);
  if (!app.data.load(dataDir, &err)) { fprintf(stderr, "cannot load data from '%s': %s\n", dataDir.c_str(), err.c_str()); return 2; }
  mkdirs(stateDir); app.statePath = stateDir + "/progress.txt"; if (app.prog.load(app.statePath, &app.data) && app.prog.migrated) { fprintf(stderr, "  progress upgraded to v2\n"); app.prog.save(app.statePath); }
  applyTheme(app.prog.settings.light);
  { FILE* vf = fopen((dataDir + "/../VERSION").c_str(), "r"); char vb[64] = {0};
    if (vf) { if (fgets(vb, sizeof vb, vf)) { vb[strcspn(vb, "\r\n")] = 0; if (vb[0]) app.version = vb; } fclose(vf); } }

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 3; }
  if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 3; }
  int displays = SDL_GetNumVideoDisplays();
  { SDL_version v; SDL_GetVersion(&v);
    fprintf(stderr, "banana-kana: SDL %d.%d.%d video=%s displays=%d data=%s\n", v.major, v.minor, v.patch,
            SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?", displays, dataDir.c_str());
    for (int i = 0; i < displays; i++) { SDL_Rect b{}; SDL_GetDisplayBounds(i, &b);
      const char* n = SDL_GetDisplayName(i); fprintf(stderr, "  display %d: %s %dx%d at (%d,%d)\n", i, n ? n : "?", b.w, b.h, b.x, b.y); } }
  bool dual = (displays >= 2) || forceDual;
  int pcW = 560, pcH = 420;  // PC window size; KT_WINSIZE=1024x768 renders like the device panels (screenshots)
  if (const char* ws = getenv("KT_WINSIZE")) { int a = 0, b = 0; if (sscanf(ws, "%dx%d", &a, &b) == 2 && a >= 320 && b >= 240) { pcW = a; pcH = b; } }
  if (!makeWin(app.top, "Banana Kana - top", 80, 40, pcW, pcH, dual && displays >= 2, 0) ||
      !makeWin(app.bot, "Banana Kana - bottom (touch)", 80, 60 + pcH, pcW, pcH, dual && displays >= 2, displays >= 2 ? 1 : 0)) {
    fprintf(stderr, "window: %s\n", SDL_GetError()); return 3;
  }
  SDL_ShowCursor(SDL_ENABLE);
  if (!loadFonts(dataDir, app.top.scale, &err)) { fprintf(stderr, "font: %s\n", err.c_str()); return 3; }

  if (self) { int rc = app.selfTest(shotDir); SDL_Quit(); return rc; }

  fprintf(stderr, "  sdl touch devices: %d, joysticks: %d\n", SDL_GetNumTouchDevices(), SDL_NumJoysticks());
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    fprintf(stderr, "  joystick %d: %s gamecontroller=%d\n", i, SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?", SDL_IsGameController(i));
  EvTouch touch; touch.open(); int sdlTouchLogged = 0, buttonsLogged = 0, rawLogged = 0; bool menuKeyDown = false, menuCombo = false; fflush(stderr);
  std::signal(SIGTERM, onSignal); std::signal(SIGINT, onSignal); std::signal(SIGHUP, onSignal);
  // Keep the bottom backlight on: BananaOS can switch it off again after sleep (/run/banana/panel-off).
  std::string blPower;
  { FILE* f = fopen("/run/banana/backlight-bottom", "r"); char buf[256] = {0};
    if (f) { if (fgets(buf, sizeof buf, f)) { buf[strcspn(buf, "\n")] = 0; std::string p = buf;
        if (!p.empty()) blPower = (p[0] == '/' ? p : "/sys/class/backlight/" + p) + "/bl_power"; } fclose(f); } }
  Uint32 lastBlCheck = 0; int blFixes = 0;
  int frames = 0; bool dirty = true; Uint32 lastRender = 0;
  while (app.running && !gStop) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      dirty = true;
      switch (e.type) {
        case SDL_QUIT: app.running = false; break;
        case SDL_CONTROLLERDEVICEADDED: {
          SDL_GameController* gc = SDL_GameControllerOpen(e.cdevice.which);
          fprintf(stderr, "  controller added: index %d %s (%s)\n", e.cdevice.which, gc ? SDL_GameControllerName(gc) : "?",
                  gc ? "opened" : SDL_GetError());
          if (gc) { char* m = SDL_GameControllerMapping(gc); fprintf(stderr, "  mapping: %s\n", m ? m : "none"); if (m) SDL_free(m); }
          fflush(stderr); break; }
        case SDL_JOYBUTTONDOWN:
          // Raw joystick buttons, logged even when SDL has no controller mapping: tells us what F, SELECT etc. are.
          if (rawLogged < 40) { rawLogged++; fprintf(stderr, "  raw joystick %d button %d down\n", e.jbutton.which, e.jbutton.button); fflush(stderr); }
          break;
        case SDL_CONTROLLERBUTTONDOWN: {
          // SDL names face buttons by position (A = south). The RG DS Plus is labelled Nintendo-style
          // (A = east, B = south, X = north, Y = west), so translate to the printed labels. KT_POSITIONAL=1 disables.
          auto b = (SDL_GameControllerButton)e.cbutton.button;
          SDL_GameController* gc = SDL_GameControllerFromInstanceID(e.cbutton.which);
          if (buttonsLogged < 30) { buttonsLogged++; const char* bn = SDL_GameControllerGetStringForButton(b);
            fprintf(stderr, "  button %d (%s)\n", (int)b, bn ? bn : "?"); fflush(stderr); }
          // Quit to the BananaOS menu from anywhere: F + START (F is assumed to be GUIDE; SELECT + START also works).
          if (b == SDL_CONTROLLER_BUTTON_START && gc &&
              (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_GUIDE) || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))) {
            app.running = false; break; }
          // F (or SELECT) pressed and released on its own toggles the pause menu; with another button it is a combo.
          if (b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_BACK) { menuKeyDown = true; menuCombo = false; break; }
          if (menuKeyDown) menuCombo = true;
          if (!getenv("KT_POSITIONAL")) {
            if (b == SDL_CONTROLLER_BUTTON_A) b = SDL_CONTROLLER_BUTTON_B;
            else if (b == SDL_CONTROLLER_BUTTON_B) b = SDL_CONTROLLER_BUTTON_A;
            else if (b == SDL_CONTROLLER_BUTTON_X) b = SDL_CONTROLLER_BUTTON_Y;
            else if (b == SDL_CONTROLLER_BUTTON_Y) b = SDL_CONTROLLER_BUTTON_X;
          }
          app.padButton(b); break; }
        case SDL_CONTROLLERBUTTONUP: {
          auto b = (SDL_GameControllerButton)e.cbutton.button;
          if ((b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_BACK) && menuKeyDown) {
            menuKeyDown = false; if (!menuCombo) app.togglePause();
          }
          break; }
        case SDL_KEYDOWN: app.key(e.key.keysym.sym); break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION: {
          // Touch is synthesised into mouse events by SDL. Only the bottom window is interactive.
          // NOTE: on the handheld the bottom touchscreen may need an evdev reader (see CLAUDE.md).
          if ((e.type == SDL_MOUSEMOTION ? e.motion.which : e.button.which) == SDL_TOUCH_MOUSEID) break;  // touch: see finger/evdev
          if (e.type == SDL_MOUSEMOTION && !(e.motion.state & SDL_BUTTON_LMASK)) break;
          Uint32 wid = e.type == SDL_MOUSEMOTION ? e.motion.windowID : e.button.windowID; if (wid != app.bot.id) break;
          int wx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, wy = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
          float lx, ly; SDL_RenderWindowToLogical(app.bot.r, wx, wy, &lx, &ly);
          app.pointer(e.type == SDL_MOUSEBUTTONDOWN ? 0 : e.type == SDL_MOUSEMOTION ? 1 : 2, (int)lx, (int)ly); break; }
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: {
          // SDL touch fallback: only the bottom panel has touch, so map the whole surface onto it.
          if (touch.fd >= 0) break;
          int lx = int(std::clamp(e.tfinger.x, 0.f, 1.f) * (LW - 1)), ly = int(std::clamp(e.tfinger.y, 0.f, 1.f) * (LH - 1));
          if (e.type == SDL_FINGERDOWN && sdlTouchLogged < 8) { sdlTouchLogged++; fprintf(stderr, "  sdl finger down (%.3f,%.3f) -> (%d,%d)\n", e.tfinger.x, e.tfinger.y, lx, ly); fflush(stderr); }
          app.pointer(e.type == SDL_FINGERDOWN ? 0 : e.type == SDL_FINGERMOTION ? 1 : 2, lx, ly); break; }
        default: break;
      }
    }
    touch.poll([&](int t, int x, int y) { app.pointer(t, x, y); dirty = true; });
    if (!blPower.empty() && SDL_GetTicks() - lastBlCheck > 2000) {
      lastBlCheck = SDL_GetTicks();
      FILE* f = fopen(blPower.c_str(), "r"); int v = 0;
      if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
      if (v != 0) { FILE* w = fopen(blPower.c_str(), "w"); if (w) { fputs("0", w); fclose(w); }
        if (blFixes++ < 5) { fprintf(stderr, "  bottom backlight was off (bl_power=%d), switched on\n", v); fflush(stderr); } }
    }
    // Redraw only when something changed (plus a slow refresh): far less GPU work, and each frame finishes before it shows.
    if (app.tick(SDL_GetTicks())) dirty = true;
    if (dirty || SDL_GetTicks() - lastRender > 500) { app.render(); dirty = false; lastRender = SDL_GetTicks(); }
    SDL_Delay(8);
    if (++frames == 90) {
      fprintf(stderr, "  after 90 frames: top flags=0x%x bottom flags=0x%x sdl_error='%s'\n",
              SDL_GetWindowFlags(app.top.w), SDL_GetWindowFlags(app.bot.w), SDL_GetError());
    }
  }
  app.prog.save(app.statePath);
  fprintf(stderr, "  exit (%s)\n", gStop ? "signal" : "quit");
  TTF_Quit(); SDL_Quit(); return 0;
}
