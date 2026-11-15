// Banana Kana: kana drills + stylus writing for the Anbernic RG DS Plus (BananaOS).
// Two SDL windows: top = prompt/feedback, bottom = touch. Logical size 640x480 each.
// This file: start-up, the event loop, gamepad/touch input and device housekeeping. Screens live in app.cpp.
#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <string>
#include <sys/stat.h>

#include "app.h"
#include "sound.h"
#include "stickpad.h"

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

// The RG DS Plus's Back key (right of F) is BTN_Z on the gamepad's evdev device (BananaOS's bananad calls it "back").
// SDL's controller mapping doesn't pass it on reliably, so read it directly, like the touchscreen (evdev allows many
// readers; nothing is grabbed). KT_BACK_CODE=<evdev key code> picks another code.
struct EvBack {
  std::vector<int> fds; int code = BTN_Z;
  static bool hasKey(int fd, int code) {
    unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) return false;
    return (bits[code / (8 * sizeof(long))] >> (code % (8 * sizeof(long)))) & 1;
  }
  void open() {
    if (const char* c = getenv("KT_BACK_CODE")) code = atoi(c);
    DIR* d = opendir("/dev/input"); if (!d) return;
    while (dirent* e = readdir(d)) {
      if (strncmp(e->d_name, "event", 5)) continue;
      std::string path = std::string("/dev/input/") + e->d_name;
      int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK); if (fd < 0) continue;
      if (hasKey(fd, code)) {
        char name[128] = "?"; ioctl(fd, EVIOCGNAME(sizeof name), name);
        fprintf(stderr, "  back key: code %d on %s (%s)\n", code, path.c_str(), name);
        fds.push_back(fd);
      } else ::close(fd);
    }
    closedir(d);
    if (fds.empty()) fprintf(stderr, "  back key: no input device has code %d\n", code);
  }
  template <class F> void poll(F onPress) {
    for (int fd : fds) {
      input_event ev[32];
      for (;;) {
        ssize_t n = read(fd, ev, sizeof ev); if (n <= 0) break;
        for (int i = 0; i < int(n / sizeof(input_event)); i++)
          if (ev[i].type == EV_KEY && ev[i].code == code && ev[i].value == 1) onPress();
      }
    }
  }
};

// BananaOS's level card: after a volume key (or F + volume for brightness) bananad writes "volume 45" or
// "brightness 60" to /run/banana/osd. Only its own menu shows it, so we show it too.
struct OsdWatch {
  timespec last{}; bool primed = false;
  bool changed(std::string& what, int& pct) {
    struct stat st{}; if (stat("/run/banana/osd", &st) != 0) { primed = true; return false; }
    bool diff = st.st_mtim.tv_sec != last.tv_sec || st.st_mtim.tv_nsec != last.tv_nsec;
    last = st.st_mtim;
    if (!primed) { primed = true; return false; }  // an old card from before we started
    if (!diff) return false;
    FILE* f = fopen("/run/banana/osd", "r"); if (!f) return false;
    char w[32] = {}; int p = -1; bool ok = fscanf(f, "%31s %d", w, &p) == 2; fclose(f);
    if (!ok) return false;
    what = w; pct = p; return true;
  }
};

// Either analogue stick works like the d-pad: a direction fires when the stick passes ~half way, then repeats while held.
// Analogue sticks act as the d-pad: see stickpad.h for the anti-jitter rules.
static const SDL_GameControllerButton STICK_BUTTON[4] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                                         SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};

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
  mkdirs(stateDir); app.statePath = stateDir + "/progress.txt";
  // SD-card backup next to the app (packaged layout only: BananaKana/VERSION beside data/). KT_BACKUP=0 turns it off.
  { struct stat vs; if (stat((dataDir + "/../VERSION").c_str(), &vs) == 0 && !(getenv("KT_BACKUP") && !strcmp(getenv("KT_BACKUP"), "0")))
      app.backupPath = dataDir + "/../progress-backup.txt";
    if (getenv("KT_BACKUP_PATH")) app.backupPath = getenv("KT_BACKUP_PATH"); }
  { struct stat ps, bs;  // fresh install or reflash: bring progress back from the SD card
    if (stat(app.statePath.c_str(), &ps) != 0 && !app.backupPath.empty() && stat(app.backupPath.c_str(), &bs) == 0) {
      std::ifstream in(app.backupPath, std::ios::binary); std::ofstream out(app.statePath, std::ios::binary); out << in.rdbuf();
      fprintf(stderr, "  progress restored from backup %s\n", app.backupPath.c_str());
    } }
  { FILE* pf = fopen(app.statePath.c_str(), "r"); app.firstRun = !pf; if (pf) fclose(pf); }
  if (app.prog.load(app.statePath, &app.data) && app.prog.migrated) { fprintf(stderr, "  progress upgraded to v2\n"); app.prog.save(app.statePath); }
  applyTheme(app.prog.settings.light);
  app.prog.checkAchievements(app.data);  // anything already earned (progress from before achievements): no fanfare
  if (app.firstRun) app.go(kt::Screen::Welcome);
  { FILE* vf = fopen((dataDir + "/../VERSION").c_str(), "r"); char vb[64] = {0};
    if (vf) { if (fgets(vb, sizeof vb, vf)) { vb[strcspn(vb, "\r\n")] = 0; if (vb[0]) app.version = vb; } fclose(vf); } }

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 3; }
  { const char* vd = getenv("SDL_VIDEODRIVER"); if (vd && !strcmp(vd, "dummy") && !getenv("SDL_AUDIODRIVER")) setenv("SDL_AUDIODRIVER", "dummy", 1); }
  snd::init();
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
  setKanaHand(app.prog.settings.kanaHand);
  snd::setVolume(app.prog.settings.sound); snd::setRumble(app.prog.settings.rumble);

  if (self) { int rc = app.selfTest(shotDir); SDL_Quit(); return rc; }

  fprintf(stderr, "  sdl touch devices: %d, joysticks: %d\n", SDL_GetNumTouchDevices(), SDL_NumJoysticks());
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    fprintf(stderr, "  joystick %d: %s gamecontroller=%d\n", i, SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?", SDL_IsGameController(i));
  // RG DS Plus has ONE stick, but its SDL mapping also reports rightx/righty (a2/a3), which move with the real stick in
  // other directions (launch.log 2026-11-15) -> only the left stick by default. KT_STICKS=0 off, =both both sticks.
  kt::StickPad sticks; const char* stEnv = getenv("KT_STICKS"); int stickMode = !stEnv ? 1 : !strcmp(stEnv, "0") ? 0 : !strcmp(stEnv, "both") ? 2 : 1;
  bool sticksOn = stickMode > 0; int stickLogged = 0;
  EvBack backKey; backKey.open(); OsdWatch osdWatch; Uint32 osdChecked = 0;
  EvTouch touch; touch.open(); int sdlTouchLogged = 0, buttonsLogged = 0, rawLogged = 0; bool menuKeyDown = false, menuCombo = false;
  // The Back key is read from evdev (EvBack). KT_BACK_BUTTON=<raw SDL joystick index> also maps one, if ever needed.
  std::set<int> mappedRaw; int backRaw = getenv("KT_BACK_BUTTON") ? atoi(getenv("KT_BACK_BUTTON")) : -1; int keysLogged = 0; fflush(stderr);
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
          if (gc) { snd::setController(gc); fprintf(stderr, "  rumble: %s\n", snd::canRumble() ? "yes" : "no"); }
          if (gc) { char* m = SDL_GameControllerMapping(gc); fprintf(stderr, "  mapping: %s\n", m ? m : "none"); if (m) SDL_free(m); }
          if (gc) {
            std::string list;
            for (int cb = 0; cb < SDL_CONTROLLER_BUTTON_MAX; cb++) {
              SDL_GameControllerButtonBind bd = SDL_GameControllerGetBindForButton(gc, (SDL_GameControllerButton)cb);
              if (bd.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) { mappedRaw.insert(bd.value.button); list += " " + std::to_string(bd.value.button); }
            }
            fprintf(stderr, "  raw buttons used by the mapping:%s\n", list.c_str());
          }
          fflush(stderr); break; }
        case SDL_JOYBUTTONDOWN: {
          // Raw joystick buttons, logged even when SDL has no controller mapping: tells us what F, SELECT etc. are.
          int rb = e.jbutton.button;
          bool isBack = backRaw >= 0 && rb == backRaw;  // only when set explicitly: the Back key is read from evdev (EvBack)
          if (rawLogged < 40) { rawLogged++; fprintf(stderr, "  raw joystick %d button %d down%s\n", e.jbutton.which, rb, isBack ? " (taken as Back)" : ""); fflush(stderr); }
          if (isBack) { if (menuKeyDown) menuCombo = true; app.padButton(SDL_CONTROLLER_BUTTON_B); dirty = true; }
          break; }
        case SDL_CONTROLLERBUTTONDOWN: {
          // The RG DS Plus is labelled Nintendo-style (A = east, B = south, X = north, Y = west). SDL's mapping for its
          // retrogame_joypad gives a:b0 (BTN_SOUTH) b:b1 (BTN_EAST) x:b2 (BTN_NORTH) y:b3 (BTN_WEST): A and B come out by
          // position (swap them), but X and Y already match the labels (don't). Confirmed on device: swapping X/Y made
          // the labelled X act as Y. KT_POSITIONAL=1 disables the A/B swap; KT_SWAP_XY=1 brings the X/Y swap back.
          auto b = (SDL_GameControllerButton)e.cbutton.button;
          SDL_GameController* gc = SDL_GameControllerFromInstanceID(e.cbutton.which);
          if (buttonsLogged < 30) { buttonsLogged++; const char* bn = SDL_GameControllerGetStringForButton(b);
            fprintf(stderr, "  button %d (%s)\n", (int)b, bn ? bn : "?"); fflush(stderr); }
          // Quit to the BananaOS menu from anywhere: F + START (F is assumed to be GUIDE; SELECT + START also works).
          // F + START: quits from the menus; mid-practice it opens the pause menu instead (Quit is in there).
          if (b == SDL_CONTROLLER_BUTTON_START && gc &&
              (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_GUIDE) || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))) {
            menuCombo = true;
            if (app.inPractice()) { if (!app.paused) app.togglePause(); } else app.running = false;
            break; }
          // F (or SELECT) pressed and released on its own toggles the pause menu; with another button it is a combo.
          if (b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_BACK) {  // F + SELECT together is BananaOS's, not a pause
            if (menuKeyDown) menuCombo = true; else { menuKeyDown = true; menuCombo = false; }
            break; }
          if (menuKeyDown) menuCombo = true;
          if (!getenv("KT_POSITIONAL")) {
            if (b == SDL_CONTROLLER_BUTTON_A) b = SDL_CONTROLLER_BUTTON_B;
            else if (b == SDL_CONTROLLER_BUTTON_B) b = SDL_CONTROLLER_BUTTON_A;
          }
          if (getenv("KT_SWAP_XY")) {
            if (b == SDL_CONTROLLER_BUTTON_X) b = SDL_CONTROLLER_BUTTON_Y;
            else if (b == SDL_CONTROLLER_BUTTON_Y) b = SDL_CONTROLLER_BUTTON_X;
          }
          app.padButton(b); break; }
        case SDL_CONTROLLERAXISMOTION: {
          int a = e.caxis.axis; if (a > SDL_CONTROLLER_AXIS_RIGHTY || !sticksOn) break;  // triggers are not sticks
          int st = a >= SDL_CONTROLLER_AXIS_RIGHTX ? 1 : 0; if (st == 1 && stickMode < 2) break;
          bool isX = a == SDL_CONTROLLER_AXIS_LEFTX || a == SDL_CONTROLLER_AXIS_RIGHTX;
          sticks.axis(st, isX, e.caxis.value, SDL_GetTicks(), [&](int d) {
            if (stickLogged < 30) { stickLogged++; fprintf(stderr, "  stick %d press %s (x=%d y=%d rest %d,%d)\n", st,
                d == kt::STICK_UP ? "up" : d == kt::STICK_DOWN ? "down" : d == kt::STICK_LEFT ? "left" : "right",
                sticks.s[st].x, sticks.s[st].y, sticks.s[st].cx, sticks.s[st].cy); fflush(stderr); }
            if (menuKeyDown) { menuCombo = true; }
            app.padButton(STICK_BUTTON[d]); dirty = true; });
          break; }
        case SDL_CONTROLLERBUTTONUP: {
          auto b = (SDL_GameControllerButton)e.cbutton.button;
          if ((b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_BACK) && menuKeyDown) {
            menuKeyDown = false; if (!menuCombo) app.togglePause();
          }
          break; }
        case SDL_KEYDOWN:
          if (keysLogged < 20) { keysLogged++; fprintf(stderr, "  key %d (%s)\n", int(e.key.keysym.sym), SDL_GetKeyName(e.key.keysym.sym)); fflush(stderr); }
          app.key(e.key.keysym.sym); break;
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
    snd::tick(SDL_GetTicks());
    sticks.repeat(SDL_GetTicks(), [&](int d) { app.padButton(STICK_BUTTON[d]); dirty = true; });
    backKey.poll([&]() { if (menuKeyDown) menuCombo = true; app.padButton(SDL_CONTROLLER_BUTTON_B); dirty = true; });
    if (SDL_GetTicks() - osdChecked > 100) {
      osdChecked = SDL_GetTicks(); std::string what; int pct;
      if (osdWatch.changed(what, pct)) { app.showOsd(what, pct); dirty = true; }
    }
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
  app.prog.save(app.statePath); app.backup(true);
  fprintf(stderr, "  exit (%s)\n", gStop ? "signal" : "quit");
  snd::quit(); TTF_Quit(); SDL_Quit(); return 0;
}
