// kanji-trainer: kana drills + stylus writing for the Anbernic RG DS Plus (BananaOS).
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
#include <sys/stat.h>

#include "core.h"

using namespace kt;

namespace {
constexpr int LW = 640, LH = 480;
constexpr int SESSION_LEN = 10;

struct Col { Uint8 r, g, b, a = 255; };
const Col BG{0, 0, 0}, FG{245, 245, 245}, DIM{140, 140, 145}, PANEL{40, 40, 44}, ACCENT{236, 64, 64}, GOOD{60, 190, 100}, BAD{230, 70, 70}, SEL{70, 120, 230};

struct Win {
  SDL_Window* w = nullptr; SDL_Renderer* r = nullptr; Uint32 id = 0;
  std::map<std::string, SDL_Texture*> cache;
};

enum FontId { F_HUGE, F_BIG, F_MID, F_SMALL, F_TINY, F_COUNT };
const int FONT_PX[F_COUNT] = {170, 100, 52, 30, 22};
TTF_Font* fonts[F_COUNT];

void fillRect(Win& w, SDL_Rect r, Col c) { SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, c.a); SDL_RenderFillRect(w.r, &r); }
void frameRect(Win& w, SDL_Rect r, Col c, int t = 3) {
  SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, c.a);
  for (int i = 0; i < t; i++) { SDL_Rect q{r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i}; SDL_RenderDrawRect(w.r, &q); }
}
void clearWin(Win& w, Col c) { SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, 255); SDL_RenderClear(w.r); }

SDL_Texture* textTex(Win& w, int font, const std::string& s, Col c, int* tw, int* th) {
  char key[64]; snprintf(key, sizeof key, "%d|%02x%02x%02x|", font, c.r, c.g, c.b);
  std::string k = std::string(key) + s;
  auto it = w.cache.find(k);
  if (it == w.cache.end()) {
    if (w.cache.size() > 400) { for (auto& [_, t] : w.cache) SDL_DestroyTexture(t); w.cache.clear(); }
    SDL_Surface* sf = TTF_RenderUTF8_Blended(fonts[font], s.c_str(), SDL_Color{c.r, c.g, c.b, 255});
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

struct Button { SDL_Rect r; int id; std::string label; bool enabled = true; int font = F_SMALL; Col fill = PANEL; Col line = FG; };
bool inRect(const SDL_Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

enum BtnId {
  B_START = 1, B_SCRIPT, B_SETTINGS, B_QUIT, B_BACK, B_RESET, B_AGAIN, B_HOME,
  B_TILE0 = 10, B_CHECK = 20, B_CONTINUE, B_UNDO, B_CLEAR, B_HINT, B_BKSP = 30, B_RATIO_M, B_RATIO_P,
  B_KEY0 = 100, B_SEG0 = 200
};

enum class Screen { Home, Settings, Drill, Result };

struct App {
  Win top, bot; Data data; Progress prog; std::string statePath, dataDir; std::mt19937 rng{(unsigned)time(nullptr)};
  Screen screen = Screen::Home; bool running = true;
  // drill state
  Question q; int index = 0, score = 0, sel = -1, cursor = 0; bool checked = false, correct = false;
  std::string typed; std::vector<Stroke> strokes; Stroke cur; bool drawing = false, showHint = false; std::vector<int> strokeOk;
  std::vector<std::string> missed; std::string lastKey;
  std::vector<Button> bottomButtons;

  // ---------------------------------------------------------- session flow
  void startSession() { index = 0; score = 0; missed.clear(); screen = Screen::Drill; nextQuestion(true); }
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
      case B_RESET: prog.settings = Settings{.script = prog.settings.script}; break;
      case B_RATIO_M: prog.settings.kanaPct = std::max(0, prog.settings.kanaPct - 10); break;
      case B_RATIO_P: prog.settings.kanaPct = std::min(100, prog.settings.kanaPct + 10); break;
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
    if (screen == Screen::Drill && q.type == QType::Write && !checked) {
      SDL_Rect c = canvas();
      if (type == 0 && inRect(c, x, y)) { drawing = true; cur.clear(); cur.push_back(toGlyph(x, y)); return; }
      if (type == 1 && drawing) { cur.push_back(toGlyph(x, y)); return; }
      if (type == 2 && drawing) { drawing = false; if (cur.size() >= 2) strokes.push_back(cur); cur.clear(); return; }
    }
    if (type == 0) for (auto& b : bottomButtons) if (b.enabled && inRect(b.r, x, y)) { press(b.id); return; }
  }

  void padButton(SDL_GameControllerButton b) {
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
      case SDLK_ESCAPE: press(B_HOME); if (screen == Screen::Home) {} break;
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
  void addBtn(SDL_Rect r, int id, const std::string& label, int font = F_SMALL, bool enabled = true, Col fill = PANEL, Col line = FG) {
    bottomButtons.push_back({r, id, label, enabled, font, fill, line});
  }
  void drawButtons() {
    for (auto& b : bottomButtons) {
      Col line = b.enabled ? b.line : Col{70, 70, 74}, txt = b.enabled ? FG : Col{90, 90, 94};
      fillRect(bot, b.r, b.fill); frameRect(bot, b.r, line, 3);
      drawText(bot, b.label, SDL_Rect{b.r.x + 6, b.r.y + 2, b.r.w - 12, b.r.h - 4}, txt, b.font);
    }
  }

  void progressBar(Win& w, SDL_Rect r, float frac) {
    fillRect(w, r, Col{60, 60, 64}); fillRect(w, SDL_Rect{r.x, r.y, int(r.w * std::clamp(frac, 0.f, 1.f)), r.h}, ACCENT);
  }

  void render() {
    clearWin(top, BG); clearWin(bot, BG); bottomButtons.clear();
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

  void drawHome() {
    drawText(top, "Kanji Trainer", SDL_Rect{20, 20, 600, 80}, FG, F_BIG);
    drawText(top, "kana drills for BananaOS", SDL_Rect{20, 100, 600, 40}, DIM, F_SMALL);
    int y = 190;
    for (Script s : {Script::Hiragana, Script::Katakana}) {
      int n = prog.learned(data, s), total = 0; for (auto& k : data.kana) if (k.script == s) total++;
      std::string label = std::string(scriptName(s)) + (s == prog.settings.script ? "  (active)" : "");
      drawText(top, label, SDL_Rect{40, y, 400, 36}, s == prog.settings.script ? FG : DIM, F_SMALL, true);
      drawText(top, std::to_string(n) + "/" + std::to_string(total), SDL_Rect{460, y, 140, 36}, DIM, F_SMALL, false);
      progressBar(top, SDL_Rect{40, y + 44, 560, 18}, float(n) / total); y += 110;
    }
    addBtn(SDL_Rect{60, 40, 520, 110}, B_START, "Start practice", F_MID, true, ACCENT, FG);
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
    drawText(top, "Kana " + std::to_string(s.kanaPct) + "%   Word " + std::to_string(100 - s.kanaPct) + "%", SDL_Rect{20, 230, 600, 60}, FG, F_MID);
    progressBar(top, SDL_Rect{40, 320, 560, 18}, s.kanaPct / 100.f);
    addBtn(SDL_Rect{20, 14, 90, 52}, B_RATIO_M, "Kana-", F_TINY); addBtn(SDL_Rect{120, 14, 90, 52}, B_RATIO_P, "Kana+", F_TINY);
    addBtn(SDL_Rect{430, 14, 190, 52}, B_BACK, "Back", F_SMALL);
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
      Col fill = PANEL, line = FG;
      if (checked) { if (i == q.correct) { fill = Col{25, 80, 45}; line = GOOD; } else if (i == sel) { fill = Col{90, 30, 30}; line = BAD; } }
      else if (i == sel) { fill = Col{35, 55, 110}; line = SEL; }
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
      drawText(top, typed.empty() ? "..." : typed, SDL_Rect{20, 310, 600, 80}, checked ? pc : ACCENT, F_BIG);
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
    fillRect(bot, c, Col{18, 18, 22}); frameRect(bot, c, FG, 3);
    SDL_SetRenderDrawColor(bot.r, 70, 70, 76, 255);
    SDL_RenderDrawLine(bot.r, c.x + c.w / 2, c.y + 6, c.x + c.w / 2, c.y + c.h - 6); SDL_RenderDrawLine(bot.r, c.x + 6, c.y + c.h / 2, c.x + c.w - 6, c.y + c.h / 2);
    const Glyph* g = glyph();
    if (g && (showHint || checked)) for (auto& s : g->strokes) drawStroke(s, checked ? Col{60, 90, 70} : Col{60, 60, 70}, 9);
    for (size_t i = 0; i < strokes.size(); i++) {
      Col col = checked ? (i < strokeOk.size() && strokeOk[i] ? GOOD : BAD) : FG;
      drawStroke(strokes[i], col, 6);
    }
    if (drawing) drawStroke(cur, ACCENT, 6);
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

bool makeWin(Win& w, const char* title, int x, int y, int ww, int wh, bool fullscreenOnDisplay, int display) {
  Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
  if (fullscreenOnDisplay) { SDL_Rect b; SDL_GetDisplayBounds(display, &b); x = b.x; y = b.y; ww = b.w; wh = b.h; flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS; }
  w.w = SDL_CreateWindow(title, x, y, ww, wh, flags); if (!w.w) return false;
  bool soft = getenv("KT_SOFTWARE") != nullptr;
  w.r = SDL_CreateRenderer(w.w, -1, soft ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!w.r) w.r = SDL_CreateRenderer(w.w, -1, SDL_RENDERER_SOFTWARE);
  if (!w.r) return false;
  SDL_RenderSetLogicalSize(w.r, LW, LH); w.id = SDL_GetWindowID(w.w); return true;
}

bool saveShot(Win& w, const std::string& path) {
  int ow = 0, oh = 0; SDL_GetRendererOutputSize(w.r, &ow, &oh);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888); if (!s) return false;
  int ok = SDL_RenderReadPixels(w.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  int saved = ok == 0 ? SDL_SaveBMP(s, path.c_str()) : -1; SDL_FreeSurface(s); return saved == 0;
}
}  // namespace

// ------------------------------------------------------------------ scripted self-test
// Drives the real UI with synthetic input: correct answers for every question type.
static int selfTest(App& app, const std::string& shotDir) {
  int fails = 0; auto CHECK = [&](bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); fails++; } };
  auto frame = [&]() { app.render(); };
  auto click = [&](int id) { frame(); for (auto& b : app.bottomButtons) if (b.id == id && b.enabled) { app.pointer(0, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2); return true; } return false; };
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
  // a wrong answer is recorded and scored as wrong
  CHECK(click(B_AGAIN), "again"); frame();
  while (app.q.options.empty()) { app.press(B_CHECK); app.checked = false; app.nextQuestion(); frame(); }
  int wrong = (app.q.correct + 1) % 6; CHECK(click(B_TILE0 + wrong), "wrong tile"); CHECK(click(B_CHECK), "check wrong");
  CHECK(app.checked && !app.correct, "wrong answer must be marked wrong"); CHECK(app.prog.stats[app.q.key].wrong >= 1, "wrong recorded");
  shot("q_wrong");
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
    stateDir = x ? std::string(x) + "/kanji-trainer" : std::string(h ? h : ".") + "/.config/kanji-trainer";
  }
  App app; app.dataDir = dataDir; std::string err;
  if (!app.data.load(dataDir, &err)) { fprintf(stderr, "cannot load data from '%s': %s\n", dataDir.c_str(), err.c_str()); return 2; }
  mkdirs(stateDir); app.statePath = stateDir + "/progress.txt"; app.prog.load(app.statePath);

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 3; }
  if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 3; }
  for (int i = 0; i < F_COUNT; i++) {
    fonts[i] = TTF_OpenFont((dataDir + "/font.ttf").c_str(), FONT_PX[i]);
    if (!fonts[i]) { fprintf(stderr, "font: %s\n", TTF_GetError()); return 3; }
  }

  int displays = SDL_GetNumVideoDisplays();
  bool dual = (displays >= 2) || forceDual;
  if (!makeWin(app.top, "Kanji Trainer - top", 80, 40, 560, 420, dual && displays >= 2, 0) ||
      !makeWin(app.bot, "Kanji Trainer - bottom (touch)", 80, 500, 560, 420, dual && displays >= 2, displays >= 2 ? 1 : 0)) {
    fprintf(stderr, "window: %s\n", SDL_GetError()); return 3;
  }
  SDL_ShowCursor(SDL_ENABLE);

  if (self) { int rc = selfTest(app, shotDir); SDL_Quit(); return rc; }

  while (app.running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: app.running = false; break;
        case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(e.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN: app.padButton((SDL_GameControllerButton)e.cbutton.button); break;
        case SDL_KEYDOWN: app.key(e.key.keysym.sym); break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION: {
          // Touch is synthesised into mouse events by SDL. Only the bottom window is interactive.
          // NOTE: on the handheld the bottom touchscreen may need an evdev reader (see CLAUDE.md).
          if (e.type == SDL_MOUSEMOTION && !(e.motion.state & SDL_BUTTON_LMASK)) break;
          Uint32 wid = e.type == SDL_MOUSEMOTION ? e.motion.windowID : e.button.windowID; if (wid != app.bot.id) break;
          int wx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, wy = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
          float lx, ly; SDL_RenderWindowToLogical(app.bot.r, wx, wy, &lx, &ly);
          app.pointer(e.type == SDL_MOUSEBUTTONDOWN ? 0 : e.type == SDL_MOUSEMOTION ? 1 : 2, (int)lx, (int)ly); break; }
        default: break;
      }
    }
    app.render(); SDL_Delay(8);
  }
  app.prog.save(app.statePath);
  TTF_Quit(); SDL_Quit(); return 0;
}
