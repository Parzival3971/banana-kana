// banana-kana: kana drills + stylus writing for the Anbernic RG DS Plus (BananaOS).
// Two SDL windows: top = prompt/feedback, bottom = touch. Logical size 640x480 each.
#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <csignal>
#include <sys/stat.h>

#include "core.h"

#ifdef __linux__
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
constexpr int LW = 640, LH = 480;
constexpr int SESSION_LEN = 10;

struct Col { Uint8 r, g, b, a = 255; };
bool sameCol(Col a, Col b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
constexpr Col hex(unsigned v) { return Col{Uint8(v >> 16), Uint8(v >> 8), Uint8(v)}; }

// BananaOS-style palettes: banana yellow on warm neutrals. ACCENT is a fill; ACCENT_TXT is yellow used as text
// (a dark gold on the light theme, where yellow text would be unreadable). ON_ACCENT is text placed on ACCENT.
struct Theme { Col bg, panel, panel2, line, fg, dim, accent, accentTxt, onAccent, good, goodFill, bad, badFill,
               canvas, ghost, ghostOk, disLine, disTxt, track; };
const Theme DARK_THEME{hex(0x1D1B17), hex(0x2B2722), hex(0x383229), hex(0x4D463D), hex(0xF5F0E6), hex(0xBDB3A3),
  hex(0xFFD23F), hex(0xFFD23F), hex(0x1D1B17), hex(0x7BD88F), hex(0x1F3526), hex(0xF07A4F), hex(0x3A221A),
  hex(0x15130F), hex(0x5A5246), hex(0x3E5A45), hex(0x3A342C), hex(0x6E655A), hex(0x383229)};
const Theme LIGHT_THEME{hex(0xF7F2E8), hex(0xEDE5D6), hex(0xE0D6C3), hex(0xC9BBA3), hex(0x241F18), hex(0x6B6152),
  hex(0xFFD23F), hex(0x7A5A00), hex(0x241F18), hex(0x2E7D46), hex(0xD7EEDC), hex(0xB8432A), hex(0xF6DBD2),
  hex(0xFFFDF8), hex(0xD6CCBB), hex(0xBFD9C4), hex(0xDDD3C2), hex(0xA39885), hex(0xE0D6C3)};
Col BG, PANEL, PANEL2, LINE, FG, DIM, ACCENT, ACCENT_TXT, ON_ACCENT, GOOD, GOOD_FILL, BAD, BAD_FILL, CANVAS, GHOST, GHOST_OK,
    DIS_LINE, DIS_TXT, TRACK, SEL;
void applyTheme(bool light) {
  const Theme& t = light ? LIGHT_THEME : DARK_THEME;
  BG = t.bg; PANEL = t.panel; PANEL2 = t.panel2; LINE = t.line; FG = t.fg; DIM = t.dim; ACCENT = t.accent; ACCENT_TXT = t.accentTxt;
  ON_ACCENT = t.onAccent; GOOD = t.good; GOOD_FILL = t.goodFill; BAD = t.bad; BAD_FILL = t.badFill; CANVAS = t.canvas;
  GHOST = t.ghost; GHOST_OK = t.ghostOk; DIS_LINE = t.disLine; DIS_TXT = t.disTxt; TRACK = t.track; SEL = t.accent;
}

struct Win {
  SDL_Window* w = nullptr; SDL_Renderer* r = nullptr; Uint32 id = 0;
  std::map<std::string, SDL_Texture*> cache;
};

enum FontId { F_HUGE, F_BIG, F_MID, F_SMALL, F_TINY, F_COUNT };
const int FONT_PX[F_COUNT] = {170, 100, 52, 30, 22};
TTF_Font* kanaF[F_COUNT];  // Zen Maru Gothic: kana
TTF_Font* uiF[F_COUNT];    // Pixelify Sans: English / UI
TTF_Font* fbF[F_COUNT];    // Noto subset: anything the others lack

void clearWin(Win& w, Col c) { SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, 255); SDL_RenderClear(w.r); }
void fillRound(Win& w, SDL_Rect r, int rad, Col c) {
  if (r.w <= 0 || r.h <= 0) return;
  rad = std::max(0, std::min({rad, r.w / 2, r.h / 2}));
  SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, c.a);
  for (int y = 0; y < r.h; y++) {
    int dy = y < rad ? rad - y : (y >= r.h - rad ? y - (r.h - rad - 1) : 0);
    int inset = dy ? rad - int(std::sqrt(float(rad * rad - (dy - 0.5f) * (dy - 0.5f))) + 0.5f) : 0;
    SDL_RenderDrawLine(w.r, r.x + inset, r.y + y, r.x + r.w - 1 - inset, r.y + y);
  }
}
void boxRound(Win& w, SDL_Rect r, int rad, Col fill, Col line, int t = 3) {
  fillRound(w, r, rad, line); fillRound(w, SDL_Rect{r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t}, std::max(0, rad - t), fill);
}

std::string utf8Encode(uint32_t cp) {
  std::string o;
  if (cp < 0x80) o += char(cp);
  else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  return o;
}
TTF_Font* pickFont(int size, uint32_t cp) {
  bool jp = (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
  if (jp && TTF_GlyphIsProvided32(kanaF[size], cp)) return kanaF[size];
  if (!jp && TTF_GlyphIsProvided32(uiF[size], cp)) return uiF[size];
  if (TTF_GlyphIsProvided32(kanaF[size], cp)) return kanaF[size];
  return fbF[size];
}
// Renders mixed kana/English text: each run uses the font that has its glyphs, aligned on a shared baseline.
SDL_Surface* renderMixed(int size, const std::string& s, SDL_Color col) {
  std::vector<std::pair<TTF_Font*, std::string>> runs;
  for (uint32_t cp : utf8Codepoints(s)) {
    TTF_Font* f = (cp == ' ' && !runs.empty()) ? runs.back().first : pickFont(size, cp);
    if (runs.empty() || runs.back().first != f) runs.push_back({f, ""});
    runs.back().second += utf8Encode(cp);
  }
  std::vector<SDL_Surface*> parts; int w = 0, asc = 0, desc = 0;
  for (auto& [f, txt] : runs) {
    SDL_Surface* p = TTF_RenderUTF8_Blended(f, txt.c_str(), col); parts.push_back(p);
    if (!p) continue;
    w += p->w; asc = std::max(asc, TTF_FontAscent(f)); desc = std::max(desc, p->h - TTF_FontAscent(f));
  }
  if (w <= 0) { for (auto* p : parts) if (p) SDL_FreeSurface(p); return nullptr; }
  SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, w, asc + desc, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_FillRect(out, nullptr, 0);
  int x = 0;
  for (size_t i = 0; i < parts.size(); i++) {
    SDL_Surface* p = parts[i]; if (!p) continue;
    SDL_SetSurfaceBlendMode(p, SDL_BLENDMODE_NONE);
    SDL_Rect d{x, asc - TTF_FontAscent(runs[i].first), p->w, p->h}; SDL_BlitSurface(p, nullptr, out, &d);
    x += p->w; SDL_FreeSurface(p);
  }
  return out;
}

SDL_Texture* textTex(Win& w, int font, const std::string& s, Col c, int* tw, int* th) {
  char key[64]; snprintf(key, sizeof key, "%d|%02x%02x%02x|", font, c.r, c.g, c.b);
  std::string k = std::string(key) + s;
  auto it = w.cache.find(k);
  if (it == w.cache.end()) {
    if (w.cache.size() > 400) { for (auto& [_, t] : w.cache) SDL_DestroyTexture(t); w.cache.clear(); }
    SDL_Surface* sf = renderMixed(font, s, SDL_Color{c.r, c.g, c.b, 255});
    if (!sf) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(w.r, sf); SDL_FreeSurface(sf);
    it = w.cache.emplace(k, t).first;
  }
  SDL_QueryTexture(it->second, nullptr, nullptr, tw, th);
  return it->second;
}

// Draws text centred in `box`, shrinking to the biggest font (<= maxFont) that fits.
void drawText(Win& w, const std::string& s, SDL_Rect box, Col c, int maxFont = F_MID, bool left = false) {
  if (s.empty()) return;
  for (int f = maxFont; f < F_COUNT; f++) {
    int tw, th; SDL_Texture* t = textTex(w, f, s, c, &tw, &th); if (!t) return;
    if ((tw <= box.w && th <= box.h) || f == F_COUNT - 1) {
      SDL_Rect d{left ? box.x : box.x + (box.w - tw) / 2, box.y + (box.h - th) / 2, tw, th};
      if (tw > box.w) { d.w = box.w; d.x = box.x; }
      SDL_RenderCopy(w.r, t, nullptr, &d); return;
    }
  }
}

struct Button { SDL_Rect r; int id; std::string label; bool enabled = true; int font = F_SMALL; Col fill; Col line; Col text; };
bool inRect(const SDL_Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

enum BtnId {
  B_START = 1, B_SCRIPT, B_SETTINGS, B_QUIT, B_BACK, B_RESET, B_AGAIN, B_HOME,
  B_TILE0 = 10, B_CHECK = 20, B_CONTINUE, B_UNDO, B_CLEAR, B_HINT, B_BKSP = 30, B_RATIO_M, B_RATIO_P,
  B_RESUME = 40, B_MENU, B_THEME,
  B_KEY0 = 100, B_SEG0 = 200
};

enum class Screen { Home, Settings, Drill, Result };

struct App {
  Win top, bot; Data data; Progress prog; std::string statePath, dataDir; std::mt19937 rng{(unsigned)time(nullptr)};
  Screen screen = Screen::Home; bool running = true;
  bool paused = false; int pauseSel = 0;  // F (or SELECT) during practice opens the pause menu
  // drill state
  Question q; int index = 0, score = 0, sel = -1, cursor = 0; bool checked = false, correct = false;
  std::string typed; std::vector<Stroke> strokes; Stroke cur; bool drawing = false, showHint = false; std::vector<int> strokeOk;
  std::vector<std::string> missed; std::string lastKey;
  std::vector<Button> bottomButtons;
  int pressedId = -1;  // touch buttons act on release (slide off to cancel), like Banana Player

  // ---------------------------------------------------------- session flow
  void startSession() { prog.session++; index = 0; score = 0; missed.clear(); screen = Screen::Drill; nextQuestion(true); }
  void nextQuestion(bool first = false) {
    if (!first) index++;
    if (index >= SESSION_LEN) { screen = Screen::Result; prog.save(statePath); return; }
    for (int tries = 0; tries < 6; tries++) { q = makeQuestion(data, prog, rng); if (q.key != lastKey) break; }
    lastKey = q.key; sel = -1; cursor = 0; checked = false; correct = false; typed.clear(); strokes.clear(); cur.clear();
    drawing = false; showHint = false; strokeOk.clear();
  }
  const Glyph* glyph() const { return data.glyphFor(q.answer); }

  // writing canvas: 109x109 glyph space mapped into this logical rect on the bottom screen
  SDL_Rect canvas() const { return SDL_Rect{130, 28, 380, 380}; }
  Pt toGlyph(int x, int y) const { SDL_Rect c = canvas(); float s = c.w / 109.f; return {(x - c.x) / s, (y - c.y) / s}; }
  SDL_Point toScreen(Pt p) const { SDL_Rect c = canvas(); float s = c.w / 109.f; return {int(c.x + p.x * s), int(c.y + p.y * s)}; }

  void check() {
    if (checked) return;
    bool ok = false;
    switch (q.type) {
      case QType::Write: {
        const Glyph* g = glyph(); if (!g) break;
        strokeOk.assign(strokes.size(), 0); ok = !strokes.empty() && strokes.size() == g->strokes.size();
        for (size_t i = 0; i < strokes.size(); i++) {
          bool s = i < g->strokes.size() && scoreStroke(strokes[i], g->strokes[i]).ok; strokeOk[i] = s; if (!s) ok = false;
        }
        break; }
      case QType::Type: ok = typedMatches(typed, q.answer); break;
      default: if (sel < 0) return; ok = (sel == q.correct); break;
    }
    checked = true; correct = ok; if (ok) score++; else missed.push_back(q.key);
    prog.record(q.key, ok);
    for (auto& [sc, st] : prog.checkUnlocks(data)) fprintf(stderr, "  unlocked %s stage %d\n", scriptName(sc), st + 1);
    prog.save(statePath);  // the handheld can be closed or powered off at any moment
  }

  // ---------------------------------------------------------- input
  void press(int id) {
    switch (id) {
      case B_START: startSession(); break;
      case B_AGAIN: startSession(); break;
      case B_SCRIPT: prog.settings.script = prog.settings.script == Script::Hiragana ? Script::Katakana : Script::Hiragana; prog.save(statePath); break;
      case B_SETTINGS: screen = Screen::Settings; break;
      case B_HOME: case B_BACK: screen = Screen::Home; prog.save(statePath); break;
      case B_QUIT: running = false; break;
      case B_RESUME: paused = false; break;
      case B_THEME: prog.settings.light = !prog.settings.light; applyTheme(prog.settings.light); prog.save(statePath); break;
      case B_MENU: paused = false; screen = Screen::Home; prog.save(statePath); break;
      case B_RESET: prog.settings = Settings{.script = prog.settings.script}; break;
      case B_RATIO_M: prog.settings.review = std::max(0, prog.settings.review - 1); break;
      case B_RATIO_P: prog.settings.review = std::min(2, prog.settings.review + 1); break;
      case B_CHECK: check(); break;
      case B_CONTINUE: nextQuestion(); break;
      case B_UNDO: if (!checked && !strokes.empty()) strokes.pop_back(); break;
      case B_CLEAR: if (!checked) strokes.clear(); break;
      case B_HINT: showHint = !showHint; break;
      case B_BKSP: if (!checked && !typed.empty()) typed.pop_back(); break;
      default:
        if (id >= B_TILE0 && id < B_TILE0 + 6 && !checked) { sel = id - B_TILE0; cursor = sel; }
        else if (id >= B_KEY0 && id < B_KEY0 + 26 && !checked && typed.size() < 24) typed += char('a' + id - B_KEY0);
        else if (id >= B_SEG0 && id < B_SEG0 + 12) {
          int mode = (id - B_SEG0) / 3, val = (id - B_SEG0) % 3; Settings& s = prog.settings;
          int* t[4] = {&s.reading, &s.meanings, &s.writing, &s.typing}; *t[mode] = val;
        }
    }
  }

  void pointer(int type, int x, int y) {  // type 0 down, 1 move, 2 up; coordinates in bottom-logical space
    if (!paused && screen == Screen::Drill && q.type == QType::Write && !checked) {
      SDL_Rect c = canvas();
      if (type == 0 && inRect(c, x, y)) { drawing = true; cur.clear(); cur.push_back(toGlyph(x, y)); return; }
      if (type == 1 && drawing) { cur.push_back(toGlyph(x, y)); return; }
      if (type == 2 && drawing) { drawing = false; if (cur.size() >= 2) strokes.push_back(cur); cur.clear(); return; }
    }
    if (type == 0) { pressedId = -1; for (auto& b : bottomButtons) if (b.enabled && inRect(b.r, x, y)) { pressedId = b.id; return; } }
    if (type == 1 && pressedId >= 0) {  // slid off the button: cancel
      for (auto& b : bottomButtons) if (b.id == pressedId && !inRect(b.r, x, y)) pressedId = -1;
    }
    if (type == 2 && pressedId >= 0) {
      int id = pressedId; pressedId = -1;
      for (auto& b : bottomButtons) if (b.id == id && b.enabled && inRect(b.r, x, y)) { press(id); return; }
    }
  }

  void togglePause() {
    if (screen != Screen::Drill) return;
    paused = !paused; pauseSel = 0; pressedId = -1; drawing = false; cur.clear();
  }

  void padButton(SDL_GameControllerButton b) {
    if (paused) {
      const int ids[3] = {B_RESUME, B_MENU, B_QUIT};
      if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) pauseSel = (pauseSel + 2) % 3;
      else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) pauseSel = (pauseSel + 1) % 3;
      else if (b == SDL_CONTROLLER_BUTTON_A) press(ids[pauseSel]);
      else if (b == SDL_CONTROLLER_BUTTON_B) press(B_RESUME);
      return;
    }
    bool choice = screen == Screen::Drill && !q.options.empty();
    switch (b) {
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: if (choice && !checked) cursor = (cursor % 2) ? cursor - 1 : cursor + 1; break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: if (choice && !checked) cursor = (cursor % 2) ? cursor - 1 : cursor + 1; break;
      case SDL_CONTROLLER_BUTTON_DPAD_UP: if (choice && !checked) cursor = (cursor + 4) % 6; break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: if (choice && !checked) cursor = (cursor + 2) % 6; break;
      case SDL_CONTROLLER_BUTTON_A:  // Nintendo layout: A = east = confirm
        if (screen == Screen::Home) press(B_START);
        else if (screen == Screen::Result) press(B_AGAIN);
        else if (screen == Screen::Drill) { if (checked) press(B_CONTINUE); else if (choice) { if (sel == cursor) check(); else sel = cursor; } else check(); }
        break;
      case SDL_CONTROLLER_BUTTON_B: if (screen == Screen::Drill || screen == Screen::Settings || screen == Screen::Result) press(B_HOME); break;
      case SDL_CONTROLLER_BUTTON_X: if (screen == Screen::Drill) { if (checked) press(B_CONTINUE); else check(); } break;
      case SDL_CONTROLLER_BUTTON_Y: if (screen == Screen::Drill) press(B_HINT); break;
      case SDL_CONTROLLER_BUTTON_START: if (screen == Screen::Home) press(B_START); break;
      default: break;
    }
  }

  void key(SDL_Keycode k) {  // PC convenience
    switch (k) {
      case SDLK_ESCAPE: if (paused) press(B_RESUME); else press(B_HOME); break;
      case SDLK_TAB: togglePause(); break;  // PC stand-in for the F key
      case SDLK_RETURN: padButton(SDL_CONTROLLER_BUTTON_A); break;
      case SDLK_LEFT: padButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT); break;
      case SDLK_RIGHT: padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); break;
      case SDLK_UP: padButton(SDL_CONTROLLER_BUTTON_DPAD_UP); break;
      case SDLK_DOWN: padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN); break;
      case SDLK_BACKSPACE: press(B_BKSP); break;
      default:
        if (screen == Screen::Drill && q.type == QType::Type && k >= SDLK_a && k <= SDLK_z) press(B_KEY0 + (k - SDLK_a));
    }
  }

  // ---------------------------------------------------------- drawing
  void addBtn(SDL_Rect r, int id, const std::string& label, int font = F_SMALL, bool enabled = true, Col fill = Col{}, Col line = Col{}, bool useDefaults = true) {
    (void)useDefaults;
    if (sameCol(fill, Col{})) fill = PANEL;           // unset fill: panel
    if (sameCol(line, Col{})) line = LINE;            // unset line: theme line
    if (sameCol(fill, ACCENT)) line = ACCENT;         // yellow buttons have no outline
    Col text = sameCol(fill, ACCENT) ? ON_ACCENT : FG;
    bottomButtons.push_back({r, id, label, enabled, font, fill, line, text});
  }
  void drawButtons() {
    for (auto& b : bottomButtons) {
      Col line = b.enabled ? b.line : DIS_LINE, txt = b.enabled ? b.text : DIS_TXT;
      Col fill = b.fill;
      if (b.id == pressedId)  // pressed: blend 25% towards the text colour
        fill = Col{Uint8((fill.r * 3 + FG.r) / 4), Uint8((fill.g * 3 + FG.g) / 4), Uint8((fill.b * 3 + FG.b) / 4)};
      if (sameCol(line, fill)) fillRound(bot, b.r, 12, fill); else boxRound(bot, b.r, 12, fill, line, 3);
      drawText(bot, b.label, SDL_Rect{b.r.x + 6, b.r.y + 2, b.r.w - 12, b.r.h - 4}, txt, b.font);
    }
  }

  void progressBar(Win& w, SDL_Rect r, float frac) {
    fillRound(w, r, r.h / 2, TRACK); int fw = int(r.w * std::clamp(frac, 0.f, 1.f)); if (fw > 0) fillRound(w, SDL_Rect{r.x, r.y, std::max(fw, r.h), r.h}, r.h / 2, ACCENT);
  }

  void render() {
    clearWin(top, BG); clearWin(bot, BG); bottomButtons.clear();
    if (paused) { drawPause(); drawButtons(); SDL_RenderPresent(top.r); SDL_RenderPresent(bot.r); return; }
    switch (screen) {
      case Screen::Home: drawHome(); break;
      case Screen::Settings: drawSettings(); break;
      case Screen::Drill: drawDrill(); break;
      case Screen::Result: drawResult(); break;
    }
    drawButtons();
    if (screen == Screen::Drill && q.type == QType::Write) drawWritingExtras();
    SDL_RenderPresent(top.r); SDL_RenderPresent(bot.r);
  }

  void drawPause() {
    drawText(top, "Paused", SDL_Rect{20, 120, 600, 110}, FG, F_BIG);
    drawText(top, "Question " + std::to_string(index + 1) + " of " + std::to_string(SESSION_LEN) + "  -  your answers are saved",
             SDL_Rect{20, 250, 600, 40}, DIM, F_SMALL);
    drawText(top, "Up/Down choose   A select   B resume", SDL_Rect{20, 400, 600, 36}, DIM, F_TINY);
    const char* labels[3] = {"Resume", "Back to menu", "Quit Banana Kana"};
    const int ids[3] = {B_RESUME, B_MENU, B_QUIT};
    for (int i = 0; i < 3; i++) {
      bool on = i == pauseSel;
      addBtn(SDL_Rect{80, 40 + i * 130, 480, 110}, ids[i], labels[i], F_MID, true, on ? ACCENT : PANEL, on ? FG : DIM);
    }
  }

  void drawHome() {
    drawText(top, "Banana Kana", SDL_Rect{20, 20, 600, 80}, FG, F_BIG);
    drawText(top, "kana drills for BananaOS", SDL_Rect{20, 100, 600, 40}, DIM, F_SMALL);
    int y = 190;
    for (Script s : {Script::Hiragana, Script::Katakana}) {
      int n = prog.knownCount(data, s), total = 0; for (auto& k : data.kana) if (k.script == s) total++;
      std::string label = std::string(scriptName(s)) + (s == prog.settings.script ? "  (active)" : "");
      drawText(top, label, SDL_Rect{40, y, 400, 36}, s == prog.settings.script ? FG : DIM, F_SMALL, true);
      drawText(top, std::to_string(n) + "/" + std::to_string(total), SDL_Rect{460, y, 140, 36}, DIM, F_SMALL, false);
      progressBar(top, SDL_Rect{40, y + 44, 560, 18}, float(n) / total); y += 110;
    }
    addBtn(SDL_Rect{60, 40, 520, 110}, B_START, "Start practice", F_MID, true, ACCENT);
    addBtn(SDL_Rect{60, 175, 520, 80}, B_SCRIPT, std::string("Script: ") + scriptName(prog.settings.script) + "  (tap to switch)", F_SMALL);
    addBtn(SDL_Rect{60, 275, 250, 80}, B_SETTINGS, "Practice mix", F_SMALL);
    addBtn(SDL_Rect{330, 275, 250, 80}, B_QUIT, "Quit", F_SMALL);
    drawText(bot, "A / Start: practice    B: back", SDL_Rect{20, 395, 600, 30}, DIM, F_TINY);
    drawText(bot, "Stroke data: KanjiVG (CC BY-SA 3.0)  kanjivg.tagaini.net", SDL_Rect{20, 435, 600, 30}, DIM, F_TINY);
  }

  void drawSettings() {
    drawText(top, "Practice mix", SDL_Rect{20, 30, 600, 70}, FG, F_BIG);
    drawText(top, "Kana vs word questions, and how often each", SDL_Rect{20, 110, 600, 40}, DIM, F_SMALL);
    drawText(top, "drill type shows up (0 = never, 2 = often).", SDL_Rect{20, 150, 600, 40}, DIM, F_SMALL);
    const Settings& s = prog.settings;
    const char* rv[3] = {"off", "some", "lots"};
    drawText(top, std::string("Review older stages: ") + rv[s.review], SDL_Rect{20, 230, 600, 60}, FG, F_MID);
    addBtn(SDL_Rect{20, 14, 90, 52}, B_RATIO_M, "Rev-", F_TINY); addBtn(SDL_Rect{120, 14, 90, 52}, B_RATIO_P, "Rev+", F_TINY);
    addBtn(SDL_Rect{430, 14, 190, 52}, B_BACK, "Back", F_SMALL);
    addBtn(SDL_Rect{220, 14, 200, 52}, B_THEME, prog.settings.light ? "Theme: Light" : "Theme: Dark", F_TINY);
    const char* names[4] = {"Reading", "Word meanings", "Writing", "Typing"};
    const int vals[4] = {s.reading, s.meanings, s.writing, s.typing};
    for (int i = 0; i < 4; i++) {
      int y = 100 + i * 80;
      drawText(bot, names[i], SDL_Rect{20, y, 250, 56}, FG, F_SMALL, true);
      for (int v = 0; v < 3; v++)
        addBtn(SDL_Rect{300 + v * 105, y, 95, 60}, B_SEG0 + i * 3 + v, std::to_string(v), F_SMALL, true, vals[i] == v ? SEL : PANEL);
    }
    addBtn(SDL_Rect{20, 440, 200, 34}, B_RESET, "Reset", F_TINY);
  }

  void drawTiles() {
    for (int i = 0; i < (int)q.options.size(); i++) {
      SDL_Rect r{20 + (i % 2) * 310, 14 + (i / 2) * 112, 290, 100};
      Col fill = PANEL, line = LINE;
      if (checked) { if (i == q.correct) { fill = GOOD_FILL; line = GOOD; } else if (i == sel) { fill = BAD_FILL; line = BAD; } }
      else if (i == sel) { fill = ACCENT; line = ACCENT; }
      bool isCursor = !checked && i == cursor && sel != i;
      addBtn(r, B_TILE0 + i, q.options[i], q.optionsAreKana ? F_BIG : F_MID, true, fill, isCursor ? DIM : line);
    }
  }

  void drawKeyboard() {
    const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int rI = 0; rI < 3; rI++) {
      int n = (int)strlen(rows[rI]), x0 = (LW - n * 58) / 2;
      for (int i = 0; i < n; i++) {
        char ch = rows[rI][i]; std::string lab(1, ch);
        addBtn(SDL_Rect{x0 + i * 58, 20 + rI * 74, 54, 66}, B_KEY0 + (ch - 'a'), lab, F_MID, !checked);
      }
    }
    addBtn(SDL_Rect{20, 250, 170, 66}, B_BKSP, "Delete", F_SMALL, !checked);
  }

  void drawDrill() {
    progressBar(top, SDL_Rect{20, 20, 600, 14}, float(index + (checked ? 1 : 0)) / SESSION_LEN);
    drawText(top, std::to_string(index + 1) + "/" + std::to_string(SESSION_LEN), SDL_Rect{500, 40, 120, 30}, DIM, F_TINY);
    drawText(top, q.hint, SDL_Rect{20, 40, 480, 36}, DIM, F_SMALL, true);
    Col pc = FG;
    if (checked) pc = correct ? GOOD : BAD;
    std::string prompt = q.prompt;
    if (q.type == QType::Type) {
      drawText(top, prompt, SDL_Rect{20, 90, 600, 200}, FG, F_HUGE);
      drawText(top, typed.empty() ? "..." : typed, SDL_Rect{20, 310, 600, 80}, checked ? pc : ACCENT_TXT, F_BIG);
    } else if (q.type == QType::Write) {
      drawText(top, checked ? q.answer : prompt, SDL_Rect{20, 90, 600, 200}, pc, F_HUGE);
    } else {
      drawText(top, prompt, SDL_Rect{20, 90, 600, 250}, pc, F_HUGE);
    }
    if (checked) {
      std::string msg = correct ? "Correct!" : "Not quite";
      if (!correct && q.type != QType::Write) msg += ":  " + q.answer;
      if (q.type == QType::Write) msg += "   (" + q.prompt + ")";
      if (q.type == QType::Type && !correct) msg = "Answer: " + q.answer;
      drawText(top, msg, SDL_Rect{20, 400, 600, 60}, pc, F_MID);
    }
    // bottom
    if (!q.options.empty()) {
      drawTiles();
      SDL_Rect cb{20, 352, 600, 100};
      if (checked) addBtn(cb, B_CONTINUE, "Continue", F_MID, true, ACCENT);
      else addBtn(cb, B_CHECK, "Check Answer", F_MID, sel >= 0);
    } else if (q.type == QType::Type) {
      drawKeyboard();
      if (checked) addBtn(SDL_Rect{210, 250, 410, 66}, B_CONTINUE, "Continue", F_SMALL, true, ACCENT);
      else addBtn(SDL_Rect{210, 250, 410, 66}, B_CHECK, "Check Answer", F_SMALL, !typed.empty());
    } else {  // Write: canvas drawn in drawWritingExtras (after buttons so buttons stay on top)
      addBtn(SDL_Rect{10, 60, 110, 90}, B_UNDO, "Undo", F_TINY, !checked);
      addBtn(SDL_Rect{10, 170, 110, 90}, B_CLEAR, "Clear", F_TINY, !checked);
      addBtn(SDL_Rect{10, 280, 110, 90}, B_HINT, "Hint", F_TINY, !checked);
      if (checked) addBtn(SDL_Rect{130, 416, 500, 56}, B_CONTINUE, "Continue", F_SMALL, true, ACCENT);
      else addBtn(SDL_Rect{130, 416, 500, 56}, B_CHECK, "Check Answer", F_SMALL, !strokes.empty());
    }
  }

  void drawStroke(const Stroke& s, Col c, int thick) {
    SDL_SetRenderDrawColor(bot.r, c.r, c.g, c.b, c.a);
    for (size_t i = 1; i < s.size(); i++) {
      SDL_Point a = toScreen(s[i - 1]), b = toScreen(s[i]);
      for (int dx = -thick / 2; dx <= thick / 2; dx++) for (int dy = -thick / 2; dy <= thick / 2; dy++)
        if (dx * dx + dy * dy <= thick * thick / 4) SDL_RenderDrawLine(bot.r, a.x + dx, a.y + dy, b.x + dx, b.y + dy);
    }
  }

  void drawWritingExtras() {
    SDL_Rect c = canvas();
    boxRound(bot, SDL_Rect{c.x - 3, c.y - 3, c.w + 6, c.h + 6}, 14, CANVAS, LINE, 3);
    SDL_SetRenderDrawColor(bot.r, LINE.r, LINE.g, LINE.b, 255);
    SDL_RenderDrawLine(bot.r, c.x + c.w / 2, c.y + 6, c.x + c.w / 2, c.y + c.h - 6); SDL_RenderDrawLine(bot.r, c.x + 6, c.y + c.h / 2, c.x + c.w - 6, c.y + c.h / 2);
    const Glyph* g = glyph();
    if (g && (showHint || checked)) for (auto& s : g->strokes) drawStroke(s, checked ? GHOST_OK : GHOST, 9);
    for (size_t i = 0; i < strokes.size(); i++) {
      Col col = checked ? (i < strokeOk.size() && strokeOk[i] ? GOOD : BAD) : FG;
      drawStroke(strokes[i], col, 6);
    }
    if (drawing) drawStroke(cur, ACCENT_TXT, 6);
  }

  void drawResult() {
    drawText(top, "Session complete", SDL_Rect{20, 40, 600, 80}, FG, F_BIG);
    drawText(top, std::to_string(score) + " / " + std::to_string(SESSION_LEN), SDL_Rect{20, 140, 600, 200}, score >= 8 ? GOOD : FG, F_HUGE);
    std::string m;
    for (size_t i = 0; i < missed.size() && i < 6; i++) { std::string k = missed[i]; m += k.substr(2) + "  "; }
    drawText(top, missed.empty() ? "Perfect round!" : "To review:  " + m, SDL_Rect{20, 380, 600, 60}, missed.empty() ? GOOD : DIM, F_SMALL);
    addBtn(SDL_Rect{60, 60, 520, 130}, B_AGAIN, "Practice again", F_MID, true, ACCENT);
    addBtn(SDL_Rect{60, 220, 520, 100}, B_HOME, "Home", F_MID);
  }
};

bool mkdirs(const std::string& p) {
  std::string cur;
  for (size_t i = 0; i < p.size(); i++) { cur += p[i]; if ((p[i] == '/' && i > 0) || i + 1 == p.size()) mkdir(cur.c_str(), 0755); }
  struct stat st; return stat(p.c_str(), &st) == 0;
}

bool makeWin(Win& w, const char* title, int x, int y, int ww, int wh, bool onDisplay, int display) {
  Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
  if (onDisplay) {
    SDL_Rect b{}; SDL_GetDisplayBounds(display, &b); x = b.x; y = b.y; ww = b.w; wh = b.h;
    // KMSDRM only puts a window on a panel when it is fullscreen. KT_NOFS=1 falls back to borderless.
    flags = SDL_WINDOW_SHOWN | (getenv("KT_NOFS") ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_FULLSCREEN_DESKTOP);
  }
  w.w = SDL_CreateWindow(title, x, y, ww, wh, flags);
  if (!w.w) { fprintf(stderr, "  create window '%s' failed: %s\n", title, SDL_GetError()); return false; }
  bool soft = getenv("KT_SOFTWARE") != nullptr;
  // With two windows on two CRTCs, per-window vsync can stall the other panel: off by default on device.
  bool vsync = !onDisplay || getenv("KT_VSYNC");
  Uint32 rf = soft ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
  w.r = SDL_CreateRenderer(w.w, -1, rf);
  if (!w.r) { fprintf(stderr, "  accelerated renderer for '%s' failed (%s), trying software\n", title, SDL_GetError()); w.r = SDL_CreateRenderer(w.w, -1, SDL_RENDERER_SOFTWARE); }
  if (!w.r) { fprintf(stderr, "  create renderer '%s' failed: %s\n", title, SDL_GetError()); return false; }
  SDL_RenderSetLogicalSize(w.r, LW, LH); w.id = SDL_GetWindowID(w.w);
  SDL_RendererInfo ri{}; SDL_GetRendererInfo(w.r, &ri); int aw = 0, ah = 0; SDL_GetWindowSize(w.w, &aw, &ah);
  fprintf(stderr, "  window '%s': id=%u display=%d size=%dx%d flags=0x%x renderer=%s vsync=%d\n", title, w.id,
          SDL_GetWindowDisplayIndex(w.w), aw, ah, SDL_GetWindowFlags(w.w), ri.name, vsync ? 1 : 0);
  return true;
}

bool saveShot(Win& w, const std::string& path) {
  int ow = 0, oh = 0; SDL_GetRendererOutputSize(w.r, &ow, &oh);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888); if (!s) return false;
  int ok = SDL_RenderReadPixels(w.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  int saved = ok == 0 ? SDL_SaveBMP(s, path.c_str()) : -1; SDL_FreeSurface(s); return saved == 0;
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

// ------------------------------------------------------------------ scripted self-test
// Drives the real UI with synthetic input: correct answers for every question type.
static int selfTest(App& app, const std::string& shotDir) {
  int fails = 0; auto CHECK = [&](bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); fails++; } };
  auto frame = [&]() { app.render(); };
  auto click = [&](int id) { frame(); for (auto& b : app.bottomButtons) if (b.id == id && b.enabled) { int cx = b.r.x + b.r.w / 2, cy = b.r.y + b.r.h / 2; app.pointer(0, cx, cy); app.pointer(2, cx, cy); return true; } return false; };
  auto shot = [&](const char* name) { if (shotDir.empty()) return; frame(); saveShot(app.top, shotDir + "/" + name + "_top.bmp"); saveShot(app.bot, shotDir + "/" + name + "_bot.bmp"); };
  frame(); shot("home");
  app.prog.settings.reading = 2; app.prog.settings.meanings = 2; app.prog.settings.writing = 2; app.prog.settings.typing = 2;
  CHECK(click(B_SETTINGS), "open settings"); shot("settings"); CHECK(click(B_BACK), "back from settings");
  std::map<int, int> seen; bool shotOf[5] = {};
  for (int round = 0; round < 3; round++) {
    CHECK(click(round == 0 ? B_START : B_AGAIN), "start session");
    for (int i = 0; i < SESSION_LEN; i++) {
      frame(); QType t = app.q.type; seen[(int)t]++;
      const char* names[5] = {"read", "recall", "meaning", "write", "type"};
      if (!shotOf[(int)t] && !shotDir.empty()) { shotOf[(int)t] = true; shot((std::string("q_") + names[(int)t] + "_a").c_str()); }
      if (!app.q.options.empty()) { CHECK(click(B_TILE0 + app.q.correct), "tile"); CHECK(click(B_CHECK), "check"); }
      else if (t == QType::Type) { for (char ch : app.q.answer) click(B_KEY0 + (ch - 'a')); CHECK(click(B_CHECK), "check typed"); }
      else {  // Write: replay the reference strokes with small jitter, as mouse events on the canvas
        const Glyph* g = app.glyph(); CHECK(g != nullptr, "glyph");
        for (auto& s : g->strokes) {
          bool first = true;
          for (auto& p : resample(s, 30)) { SDL_Point sp = app.toScreen({p.x + 1.5f, p.y - 1.0f}); app.pointer(first ? 0 : 1, sp.x, sp.y); first = false; }
          SDL_Point e = app.toScreen(s.back()); app.pointer(2, e.x, e.y);
        }
        CHECK(click(B_CHECK), "check write");
      }
      CHECK(app.checked, "checked"); CHECK(app.correct, "scripted answer should be correct");
      if (!shotOf[(int)t] || true) { static bool done[5]; if (!done[(int)t] && !shotDir.empty()) { done[(int)t] = true; shot((std::string("q_") + names[(int)t] + "_b").c_str()); } }
      CHECK(click(B_CONTINUE), "continue");
    }
    CHECK(app.screen == Screen::Result, "result screen"); CHECK(app.score == SESSION_LEN, "perfect score");
    if (round == 0) shot("result");
  }
  for (int t = 0; t < 5; t++) if (!seen[t]) { printf("never saw question type %d\n", t); fails++; }
  // pause menu: resume keeps the question, back to menu goes home
  CHECK(click(B_AGAIN), "again for pause test"); frame();
  std::string before = app.q.key; app.togglePause(); CHECK(app.paused, "paused"); shot("pause");
  CHECK(click(B_RESUME), "resume"); CHECK(!app.paused && app.screen == Screen::Drill && app.q.key == before, "resume keeps question");
  app.togglePause(); app.padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN); app.padButton(SDL_CONTROLLER_BUTTON_A);
  CHECK(!app.paused && app.screen == Screen::Home, "back to menu via pad");
  app.screen = Screen::Result;
  // a wrong answer is recorded and scored as wrong
  CHECK(click(B_AGAIN), "again"); frame();
  while (app.q.options.empty()) { app.press(B_CHECK); app.checked = false; app.nextQuestion(); frame(); }
  int wrong = (app.q.correct + 1) % int(app.q.options.size()); CHECK(click(B_TILE0 + wrong), "wrong tile"); CHECK(click(B_CHECK), "check wrong");
  CHECK(app.checked && !app.correct, "wrong answer must be marked wrong"); CHECK(app.prog.stats[app.q.key].wrong >= 1, "wrong recorded");
  shot("q_wrong");
  // theme: toggle to light from settings, screens still render, setting persists
  app.paused = false; app.screen = Screen::Home; frame();
  CHECK(click(B_SETTINGS), "settings for theme"); CHECK(click(B_THEME), "toggle theme");
  CHECK(app.prog.settings.light && sameCol(BG, LIGHT_THEME.bg), "light theme applied"); shot("light_settings");
  CHECK(click(B_BACK), "back"); shot("light_home"); CHECK(click(B_START), "start light"); shot("light_drill");
  { Progress re; re.load(app.statePath); CHECK(re.settings.light, "theme saved"); }
  app.screen = Screen::Settings; frame(); CHECK(click(B_THEME), "toggle back"); CHECK(!app.prog.settings.light, "dark again");
  CHECK(app.prog.save(app.statePath), "save progress");
  printf(fails ? "SELFTEST: %d failure(s)\n" : "SELFTEST: all passed (types seen: read=%d recall=%d meaning=%d write=%d type=%d)\n", fails ? fails : seen[0], seen[1], seen[2], seen[3], seen[4]);
  return fails ? 1 : 0;
}

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

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 3; }
  if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 3; }
  for (int i = 0; i < F_COUNT; i++) {
    kanaF[i] = TTF_OpenFont((dataDir + "/kana.ttf").c_str(), FONT_PX[i]);
    uiF[i] = TTF_OpenFont((dataDir + "/ui.ttf").c_str(), FONT_PX[i]);
    fbF[i] = TTF_OpenFont((dataDir + "/fallback.ttf").c_str(), FONT_PX[i]);
    if (!kanaF[i] || !uiF[i] || !fbF[i]) { fprintf(stderr, "font: %s\n", TTF_GetError()); return 3; }
  }

  int displays = SDL_GetNumVideoDisplays();
  { SDL_version v; SDL_GetVersion(&v);
    fprintf(stderr, "banana-kana: SDL %d.%d.%d video=%s displays=%d data=%s\n", v.major, v.minor, v.patch,
            SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?", displays, dataDir.c_str());
    for (int i = 0; i < displays; i++) { SDL_Rect b{}; SDL_GetDisplayBounds(i, &b);
      const char* n = SDL_GetDisplayName(i); fprintf(stderr, "  display %d: %s %dx%d at (%d,%d)\n", i, n ? n : "?", b.w, b.h, b.x, b.y); } }
  bool dual = (displays >= 2) || forceDual;
  if (!makeWin(app.top, "Banana Kana - top", 80, 40, 560, 420, dual && displays >= 2, 0) ||
      !makeWin(app.bot, "Banana Kana - bottom (touch)", 80, 500, 560, 420, dual && displays >= 2, displays >= 2 ? 1 : 0)) {
    fprintf(stderr, "window: %s\n", SDL_GetError()); return 3;
  }
  SDL_ShowCursor(SDL_ENABLE);

  if (self) { int rc = selfTest(app, shotDir); SDL_Quit(); return rc; }

  fprintf(stderr, "  sdl touch devices: %d\n", SDL_GetNumTouchDevices());
  EvTouch touch; touch.open(); int sdlTouchLogged = 0, buttonsLogged = 0; bool menuKeyDown = false, menuCombo = false; fflush(stderr);
  std::signal(SIGTERM, onSignal); std::signal(SIGINT, onSignal); std::signal(SIGHUP, onSignal);
  // Keep the bottom backlight on: BananaOS can switch it off again after sleep (/run/banana/panel-off).
  std::string blPower;
  { FILE* f = fopen("/run/banana/backlight-bottom", "r"); char buf[256] = {0};
    if (f) { if (fgets(buf, sizeof buf, f)) { buf[strcspn(buf, "\n")] = 0; std::string p = buf;
        if (!p.empty()) blPower = (p[0] == '/' ? p : "/sys/class/backlight/" + p) + "/bl_power"; } fclose(f); } }
  Uint32 lastBlCheck = 0; int blFixes = 0;
  int frames = 0;
  while (app.running && !gStop) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: app.running = false; break;
        case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(e.cdevice.which); break;
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
    touch.poll([&](int t, int x, int y) { app.pointer(t, x, y); });
    if (!blPower.empty() && SDL_GetTicks() - lastBlCheck > 2000) {
      lastBlCheck = SDL_GetTicks();
      FILE* f = fopen(blPower.c_str(), "r"); int v = 0;
      if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
      if (v != 0) { FILE* w = fopen(blPower.c_str(), "w"); if (w) { fputs("0", w); fclose(w); }
        if (blFixes++ < 5) { fprintf(stderr, "  bottom backlight was off (bl_power=%d), switched on\n", v); fflush(stderr); } }
    }
    app.render(); SDL_Delay(8);
    if (++frames == 90) {
      fprintf(stderr, "  after 90 frames: top flags=0x%x bottom flags=0x%x sdl_error='%s'\n",
              SDL_GetWindowFlags(app.top.w), SDL_GetWindowFlags(app.bot.w), SDL_GetError());
    }
  }
  app.prog.save(app.statePath);
  fprintf(stderr, "  exit (%s)\n", gStop ? "signal" : "quit");
  TTF_Quit(); SDL_Quit(); return 0;
}
