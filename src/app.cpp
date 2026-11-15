#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kt {

namespace {
const char* WEIGHT_NAMES[3] = {"Off", "Sometimes", "Often"};
const char* REVIEW_NAMES[3] = {"Off", "Some", "Lots"};
const int SESSION_LENS[4] = {5, 10, 15, 20};
const char* ROW_NAMES[9] = {"Script", "Review older stages", "Reading", "Word meanings", "Writing", "Typing",
                            "Session length", "Theme", "Credits"};
const char* ROW_TILE_KANA[9] = {"もじ", "ふくしゅう", "よむ", "いみ", "かく", "うつ", "ながさ", "いろ", "ありがとう"};
const char* ROW_TILE_EN[9] = {"Script", "Review", "Reading", "Meanings", "Writing", "Typing", "Length", "Theme", "Credits"};
const char* ROW_HELP[9] = {
    "Which kana you practise. Katakana unlocks after hiragana stage 10.",
    "How often kana from stages you've passed come back. Weak kana always come back.",
    "See a kana and pick its sound, or see a sound and pick the kana.",
    "Read a word and pick its meaning. Words only use kana you've unlocked.",
    "Trace the kana with the stylus, stroke by stroke.",
    "Type the romaji on the on-screen keyboard.",
    "How many questions in each practice session.",
    "Dark or light colours.",
    "Who made the data and fonts in this app."};

int vowelCol(const std::string& romaji) {
  const std::string v = "aiueo";
  size_t p = v.find(romaji.back());
  return p == std::string::npos ? 0 : int(p);
}
std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string o; for (size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; } return o;
}
}  // namespace

// ================================================================== flow
void App::go(Screen s) {
  screen = s; pressedId = -1; drawing = false; cur.clear(); paused = false;
  if (s == Screen::Stages) { mapScript = prog.settings.script; mapSel = prog.currentStage(mapScript); }
  if (s == Screen::Chart) {
    chartScroll = 0; chartSel = -1;
    auto cells = chartCells(prog.settings.script, chartTab);
    if (!cells.empty()) chartSel = cells[0].kana;
  }
  if (s == Screen::Home) homeSel = std::clamp(homeSel, 0, 3);
}

void App::setScript(Script s) {
  if (!prog.scriptUnlocked(s)) return;
  prog.settings.script = s; save();
}

void App::startSession(int focus) {
  prog.session++; focusKana = focus;
  sessionLen = focus >= 0 ? 5 : prog.settings.sessionLen;
  index = 0; score = 0; results.clear(); missed.clear(); newlyKnown.clear(); lastKey.clear();
  go(Screen::Drill); nextQuestion(true); save();
}

void App::nextQuestion(bool first) {
  if (!first) index++;
  if (index >= sessionLen) { finishSession(); return; }
  for (int tries = 0; tries < 6; tries++) {
    if (focusKana >= 0) {
      const Settings& st = prog.settings;
      std::vector<std::pair<QType, int>> ts = {{QType::ReadKana, st.reading}, {QType::RecallKana, st.reading},
                                               {QType::Write, st.writing}, {QType::Type, st.typing}};
      std::vector<double> w; for (auto& t : ts) w.push_back(std::max(0, t.second));
      double tot = 0; for (double x : w) tot += x;
      QType t = QType::ReadKana;
      if (tot > 0) { std::discrete_distribution<int> d(w.begin(), w.end()); t = ts[d(rng)].first; }
      q = makeKanaQuestion(data, prog, rng, focusKana, t); break;
    }
    q = makeQuestion(data, prog, rng);
    if (q.key != lastKey) break;
  }
  lastKey = q.key; sel = -1; cursor = 0; checked = false; correct = false; typed.clear(); strokes.clear(); cur.clear();
  drawing = false; showHint = false; strokeOk.clear(); feedbackNote.clear();
}

void App::finishSession() {
  prog.lastScore = score; prog.lastLen = sessionLen; save();
  go(Screen::Result);
}

const Glyph* App::activeGlyph() const {
  if (screen == Screen::Lesson) { int k = currentLessonKana(); return k >= 0 ? data.glyphFor(data.kana[k].glyph) : nullptr; }
  if (screen == Screen::Drill && q.type == QType::Write) return data.glyphFor(q.answer);
  return nullptr;
}

int App::currentLessonKana() const {
  const auto& st = data.stages[si(lessonScript)];
  if (lessonStage < 0 || lessonStage >= int(st.size())) return -1;
  const auto& list = st[lessonStage].kana;
  return lessonIdx < int(list.size()) ? list[lessonIdx] : -1;
}

void App::check() {
  if (checked) return;
  bool ok = false; std::string said;
  switch (q.type) {
    case QType::Write: {
      const Glyph* g = activeGlyph(); if (!g) return;
      if (strokes.empty()) return;
      strokeOk.assign(strokes.size(), 0); ok = strokes.size() == g->strokes.size();
      for (size_t i = 0; i < strokes.size(); i++) {
        bool s = i < g->strokes.size() && scoreStroke(strokes[i], g->strokes[i]).ok; strokeOk[i] = s; if (!s) ok = false;
      }
      said = strokes.size() == g->strokes.size() ? "a different shape" : std::to_string(strokes.size()) + " strokes";
      break; }
    case QType::Type: if (typed.empty()) return; ok = typedMatches(typed, q.answer); said = typed; break;
    default: if (sel < 0) return; ok = (sel == q.correct); said = q.options[sel]; break;
  }
  int before = q.isWord ? 0 : prog.step(q.key);
  checked = true; correct = ok; results.push_back(ok ? 1 : 0);
  prog.record(q.key, ok);
  int after = q.isWord ? 0 : prog.step(q.key);
  const Kana* k = q.kanaIdx >= 0 ? &data.kana[q.kanaIdx] : nullptr;
  if (ok) {
    score++;
    if (k && before < KNOWN_STEP && after >= KNOWN_STEP) { newlyKnown.push_back(k->glyph); feedbackNote = "Known! " + k->glyph + " reached step 4."; }
    else if (k) feedbackNote = after > before ? "Step " + std::to_string(after) + " of 4" : (after >= KNOWN_STEP ? "Known" : "Step " + std::to_string(after) + " of 4 (2 steps per session at most)");
  } else {
    if (k) { auto it = data.hints.find(k->glyph); feedbackNote = it != data.hints.end() ? it->second : ""; }
    missed.push_back({k ? k->glyph : q.prompt, k ? k->romaji : q.answer, said});
  }
  for (auto& u : prog.checkUnlocks(data)) pendingUnlocks.push_back(u);
  save();
}

void App::openLesson(Script s, int stage, bool replay) {
  lessonScript = s; lessonStage = std::clamp(stage, 0, STAGE_COUNT - 1); lessonIdx = 0; lessonReplay = replay;
  strokes.clear(); go(Screen::Lesson);
}

void App::finishLesson() {
  if (lessonReplay) { go(Screen::Stages); return; }
  int& done = prog.lessons[si(lessonScript)];
  done = std::max(done, lessonStage + 1);
  prog.settings.script = lessonScript; save();
  startSession();
}

void App::togglePause() {
  if (screen != Screen::Drill) return;
  paused = !paused; pauseSel = 0; pressedId = -1; drawing = false; cur.clear();
}

// ================================================================== input
void App::press(int id) {
  Script sc = prog.settings.script;
  if (paused) {
    if (id == B_RESUME) paused = false;
    else if (id == B_MENU) { paused = false; go(Screen::Home); }
    else if (id == B_QUIT) running = false;
    return;
  }
  switch (screen) {
    case Screen::Home:
      if (id == B_CART0) { if (prog.lessonPending(sc)) openLesson(sc, prog.lessons[si(sc)], false); else startSession(); }
      else if (id == B_CART0 + 1) go(Screen::Stages);
      else if (id == B_CART0 + 2) go(Screen::Chart);
      else if (id == B_CART0 + 3) go(Screen::Settings);
      else if (id == B_SCRIPT_H) setScript(Script::Hiragana);
      else if (id == B_SCRIPT_K) setScript(Script::Katakana);
      break;
    case Screen::Stages: {
      int curSt = prog.currentStage(mapScript);
      if (id >= B_STAGE0 && id < B_STAGE0 + STAGE_COUNT) mapSel = id - B_STAGE0;
      else if (id == B_SCRIPT_H || id == B_SCRIPT_K) {
        Script s = id == B_SCRIPT_H ? Script::Hiragana : Script::Katakana;
        if (prog.scriptUnlocked(s)) { mapScript = s; mapSel = prog.currentStage(s); }
      } else if (id == B_STAGE_ACTION) {
        if (mapSel == curSt) {
          if (prog.lessonPending(mapScript)) openLesson(mapScript, prog.lessons[si(mapScript)], false);
          else { setScript(mapScript); startSession(); }
        } else if (mapSel < curSt) openLesson(mapScript, mapSel, true);
      } else if (id == B_BACK) go(Screen::Home);
      break; }
    case Screen::Lesson: {
      int n = int(data.stages[si(lessonScript)][lessonStage].kana.size());
      if (id == B_LESSON_NEXT) { if (lessonIdx < n - 1) { lessonIdx++; strokes.clear(); } else finishLesson(); }
      else if (id == B_LESSON_PREV) { if (lessonIdx > 0) { lessonIdx--; strokes.clear(); } }
      else if (id == B_LESSON_CLEAR) strokes.clear();
      else if (id == B_BACK) go(lessonReplay ? Screen::Stages : Screen::Home);
      break; }
    case Screen::Unlocked:
      if (id == B_LATER) { if (!pendingUnlocks.empty()) pendingUnlocks.erase(pendingUnlocks.begin()); if (pendingUnlocks.empty()) go(Screen::Home); }
      else if (id == B_START_LESSON && !pendingUnlocks.empty()) {
        auto u = pendingUnlocks.front(); pendingUnlocks.clear(); openLesson(u.first, u.second, false);
      }
      break;
    case Screen::Drill:
      if (id >= B_TILE0 && id < B_TILE0 + 6 && !checked) { sel = id - B_TILE0; cursor = sel; }
      else if (id >= B_KEY0 && id < B_KEY0 + 26 && !checked && typed.size() < 24) typed += char('a' + id - B_KEY0);
      else if (id == B_BKSP && !checked && !typed.empty()) typed.pop_back();
      else if (id == B_CHECK) check();
      else if (id == B_CONTINUE) nextQuestion();
      else if (id == B_UNDO && !checked && !strokes.empty()) strokes.pop_back();
      else if (id == B_CLEAR && !checked) strokes.clear();
      else if (id == B_HINT && !checked) showHint = !showHint;
      break;
    case Screen::Result:
      if (id == B_AGAIN) { if (!pendingUnlocks.empty()) go(Screen::Unlocked); else startSession(); }
      else if (id == B_HOME) go(pendingUnlocks.empty() ? Screen::Home : Screen::Unlocked);
      break;
    case Screen::Chart:
      if (id >= B_CELL0) {
        int k = id - B_CELL0;
        if (k == chartSel && prog.isUnlocked(data, k)) startSession(k); else chartSel = k;
      } else if (id >= B_TAB0 && id < B_TAB0 + 3) { chartTab = id - B_TAB0; go(Screen::Chart); }
      else if (id == B_SCRIPT_H) { setScript(Script::Hiragana); go(Screen::Chart); }
      else if (id == B_SCRIPT_K) { setScript(Script::Katakana); go(Screen::Chart); }
      else if (id == B_SCROLL_UP) chartScroll = std::max(0, chartScroll - 1);
      else if (id == B_SCROLL_DOWN) chartScroll++;
      else if (id == B_BACK) go(Screen::Home);
      break;
    case Screen::Settings: {
      Settings& st = prog.settings;
      if (id >= B_ROW0 && id < B_ROW0 + 9) setSel = id - B_ROW0;
      else if (id >= B_OPT0 && id < B_OPT0 + 4) {
        int j = id - B_OPT0;
        switch (setSel) {
          case 0: setScript(j ? Script::Katakana : Script::Hiragana); break;
          case 1: st.review = j; break;
          case 2: st.reading = j; break;
          case 3: st.meanings = j; break;
          case 4: st.writing = j; break;
          case 5: st.typing = j; break;
          case 6: st.sessionLen = SESSION_LENS[j]; break;
          case 7: st.light = j == 1; applyTheme(st.light); break;
        }
        save();
      } else if (id == B_CREDITS) go(Screen::Credits);
      else if (id == B_BACK) go(Screen::Home);
      break; }
    case Screen::Credits:
      if (id == B_BACK) go(Screen::Settings);
      break;
  }
}

SDL_Rect App::canvasRect() const {
  if (screen == Screen::Lesson) return SDL_Rect{248, 16, 376, 376};
  return SDL_Rect{152, 20, 396, 396};
}

void App::pointer(int type, int x, int y) {
  bool canDraw = !paused && ((screen == Screen::Drill && q.type == QType::Write && !checked) ||
                             (screen == Screen::Lesson && activeGlyph()));
  if (canDraw) {
    SDL_Rect c = canvasRect();
    bool in = x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h;
    if (type == 0 && in) { drawing = true; cur.clear(); cur.push_back(boxToGlyph(x, y, c)); return; }
    if (type == 1 && drawing) { cur.push_back(boxToGlyph(x, y, c)); return; }
    if (type == 2 && drawing) { drawing = false; if (cur.size() >= 2) strokes.push_back(cur); cur.clear(); return; }
  }
  auto hit = [&](const Button& b) { return b.enabled && x >= b.r.x && x < b.r.x + b.r.w && y >= b.r.y && y < b.r.y + b.r.h; };
  if (type == 0) { pressedId = -1; for (auto& b : buttons) if (hit(b)) { pressedId = b.id; return; } }
  if (type == 1 && pressedId >= 0) { for (auto& b : buttons) if (b.id == pressedId && !hit(b)) pressedId = -1; }
  if (type == 2 && pressedId >= 0) {
    int id = pressedId; pressedId = -1;
    for (auto& b : buttons) if (b.id == id && hit(b)) { press(id); return; }
  }
}

void App::padButton(SDL_GameControllerButton b) {
  using G = SDL_GameControllerButton;
  const G L = SDL_CONTROLLER_BUTTON_DPAD_LEFT, R = SDL_CONTROLLER_BUTTON_DPAD_RIGHT, U = SDL_CONTROLLER_BUTTON_DPAD_UP,
          D = SDL_CONTROLLER_BUTTON_DPAD_DOWN, A = SDL_CONTROLLER_BUTTON_A, B = SDL_CONTROLLER_BUTTON_B,
          X = SDL_CONTROLLER_BUTTON_X, Y = SDL_CONTROLLER_BUTTON_Y, LS = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
          RS = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, ST = SDL_CONTROLLER_BUTTON_START;
  Script sc = prog.settings.script;
  if (paused) {
    const int ids[3] = {B_RESUME, B_MENU, B_QUIT};
    if (b == U) pauseSel = (pauseSel + 2) % 3; else if (b == D) pauseSel = (pauseSel + 1) % 3;
    else if (b == A) press(ids[pauseSel]); else if (b == B) press(B_RESUME);
    return;
  }
  switch (screen) {
    case Screen::Home:
      if (b == L) homeSel = (homeSel + 3) % 4; else if (b == R) homeSel = (homeSel + 1) % 4;
      else if (b == A || b == ST) press(B_CART0 + homeSel);
      else if (b == Y) setScript(sc == Script::Hiragana ? Script::Katakana : Script::Hiragana);
      break;
    case Screen::Stages:
      if (b == L) mapSel = std::max(0, mapSel - 1); else if (b == R) mapSel = std::min(STAGE_COUNT - 1, mapSel + 1);
      else if (b == U) mapSel = std::max(0, mapSel - 7); else if (b == D) mapSel = std::min(STAGE_COUNT - 1, mapSel + 7);
      else if (b == A) press(B_STAGE_ACTION);
      else if (b == LS || b == RS) press(mapScript == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H);
      else if (b == B) press(B_BACK);
      break;
    case Screen::Lesson:
      if (b == A || b == R) press(B_LESSON_NEXT); else if (b == LS || b == L) press(B_LESSON_PREV);
      else if (b == X) press(B_LESSON_CLEAR); else if (b == B) press(B_BACK);
      break;
    case Screen::Unlocked:
      if (b == A) press(B_START_LESSON); else if (b == B) press(B_LATER);
      break;
    case Screen::Drill: {
      bool choice = !q.options.empty(); int n = int(q.options.size());
      if (checked) { if (b == A || b == X) press(B_CONTINUE); break; }
      if (choice) {
        if (b == L || b == R) cursor ^= 1;
        else if (b == U) cursor = (cursor - 2 + n) % n; else if (b == D) cursor = (cursor + 2) % n;
        else if (b == A) { if (sel == cursor) check(); else sel = cursor; }
        else if (b == X) check();
      } else {
        if (b == A || b == X) check();
        else if (b == Y) press(B_HINT);
        else if (b == B || b == LS) press(q.type == QType::Type ? B_BKSP : B_UNDO);
      }
      break; }
    case Screen::Result:
      if (b == A) press(B_AGAIN); else if (b == B) press(B_HOME);
      break;
    case Screen::Chart: {
      if (b == B) { press(B_BACK); break; }
      if (b == LS || b == RS) { chartTab = (chartTab + (b == RS ? 1 : 2)) % 3; go(Screen::Chart); break; }
      if (b == Y) { press(sc == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H); break; }
      if (b == A) { if (chartSel >= 0 && prog.isUnlocked(data, chartSel)) startSession(chartSel); break; }
      auto cells = chartCells(sc, chartTab);
      const Cell* c0 = nullptr; for (auto& c : cells) if (c.kana == chartSel) c0 = &c;
      if (!c0) break;
      int dr = b == U ? -1 : b == D ? 1 : 0, dc = b == L ? -1 : b == R ? 1 : 0;
      if (!dr && !dc) break;
      const Cell* best = nullptr; int bestD = 1 << 30;
      for (auto& c : cells) {
        int r = c.row - c0->row, cc = c.col - c0->col;
        if ((dr && r * dr <= 0) || (dc && (cc * dc <= 0 || r != 0))) continue;
        int dist = dr ? std::abs(r) * 10 + std::abs(cc) : std::abs(cc);
        if (dist < bestD) { bestD = dist; best = &c; }
      }
      if (best) {
        chartSel = best->kana;
        if (best->row < chartScroll) chartScroll = best->row;
        if (best->row >= chartScroll + 5) chartScroll = best->row - 4;
      }
      break; }
    case Screen::Settings: {
      if (b == U) setSel = (setSel + 8) % 9; else if (b == D) setSel = (setSel + 1) % 9;
      else if (b == B) press(B_BACK);
      else if (setSel == 8 && b == A) press(B_CREDITS);
      else if (b == L || b == R) {
        Settings& st = prog.settings; int dir = b == R ? 1 : -1;
        int curv = setSel == 0 ? si(st.script) : setSel == 1 ? st.review : setSel == 2 ? st.reading : setSel == 3 ? st.meanings
                 : setSel == 4 ? st.writing : setSel == 5 ? st.typing : setSel == 7 ? (st.light ? 1 : 0) : 0;
        int nopt = setSel == 0 || setSel == 7 ? 2 : setSel == 6 ? 4 : 3;
        if (setSel == 6) for (int i = 0; i < 4; i++) if (SESSION_LENS[i] == st.sessionLen) curv = i;
        if (setSel < 8) press(B_OPT0 + std::clamp(curv + dir, 0, nopt - 1));
      }
      break; }
    case Screen::Credits:
      if (b == B || b == A) press(B_BACK);
      break;
  }
}

void App::key(SDL_Keycode k) {  // PC convenience
  using G = SDL_GameControllerButton;
  switch (k) {
    case SDLK_ESCAPE: if (screen == Screen::Drill) togglePause(); else padButton(SDL_CONTROLLER_BUTTON_B); break;
    case SDLK_TAB: togglePause(); break;
    case SDLK_RETURN: padButton(SDL_CONTROLLER_BUTTON_A); break;
    case SDLK_LEFT: padButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT); break;
    case SDLK_RIGHT: padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); break;
    case SDLK_UP: padButton(SDL_CONTROLLER_BUTTON_DPAD_UP); break;
    case SDLK_DOWN: padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN); break;
    case SDLK_PAGEUP: padButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); break;
    case SDLK_PAGEDOWN: padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); break;
    case SDLK_BACKSPACE: padButton(SDL_CONTROLLER_BUTTON_B); break;
    default:
      if (screen == Screen::Drill && q.type == QType::Type && !paused && k >= SDLK_a && k <= SDLK_z) press(B_KEY0 + (k - SDLK_a));
      (void)G{};
  }
}

// ================================================================== drawing helpers
void App::addBtn(SDL_Rect r, int id, const std::string& label, int font, bool enabled) {
  Button b; b.r = r; b.id = id; b.label = label; b.font = font; b.enabled = enabled;
  b.fill = PANEL; b.line = LINE; b.text = FG; b.subText = DIM;
  buttons.push_back(b);
}
static void accent(Button& b) { b.fill = ACCENT; b.line = ACCENT; b.text = ON_ACCENT; b.subText = ON_ACCENT; }

void App::drawButtons() {
  for (auto& b : buttons) {
    if (b.hidden) continue;
    Col fill = b.fill, line = b.enabled ? b.line : DIS_LINE, txt = b.enabled ? b.text : DIS_TXT, sub = b.enabled ? b.subText : DIS_TXT;
    if (b.id == pressedId) fill = mix(fill, FG, 0.22f);
    if (sameCol(line, fill)) fillRound(bot, b.r, b.radius, fill); else boxRound(bot, b.r, b.radius, fill, line, b.lineW);
    SDL_Rect in{b.r.x + 6, b.r.y + 3, b.r.w - 12, b.r.h - 6};
    if (b.sub.empty()) drawText(bot, b.label, in, txt, b.font);
    else {
      int sh = std::max(16, in.h / 3);
      drawText(bot, b.label, SDL_Rect{in.x, in.y + 2, in.w, in.h - sh - 2}, txt, b.font);
      drawText(bot, b.sub, SDL_Rect{in.x, in.y + in.h - sh - 2, in.w, sh}, sub, b.subFont);
    }
    if (b.icon == 1) iconLock(bot, b.r.x + b.r.w - 20, b.r.y + 8, 2, sub);
    if (b.icon == 2) iconCheck(bot, b.r.x + b.r.w - 22, b.r.y + 9, 2, txt);
  }
}

void App::header(const std::string& right) {
  iconBanana(top, 24, 18, 3);
  int x = 62;
  drawText(top, "BANANA", SDL_Rect{x, 16, 120, 30}, ACCENT_TXT, F_TINY, A_LEFT);
  x += textWidth(top, F_TINY, "BANANA") + 8;
  drawText(top, "KANA", SDL_Rect{x, 16, 100, 30}, FG, F_TINY, A_LEFT);
  drawText(top, right, SDL_Rect{300, 16, 316, 30}, DIM, F_TINY, A_RIGHT);
}

void App::chips(const std::vector<std::pair<std::string, std::string>>& items) {
  int x = 16;
  for (auto& [k, label] : items) {
    int kw = std::max(28, textWidth(bot, F_MICRO, k) + 14), lw = textWidth(bot, F_MICRO, label);
    int w = 4 + kw + 8 + lw + 14;
    if (x + w > LW - 8) break;
    fillRound(bot, SDL_Rect{x, 438, w, 34}, 17, PANEL);
    fillRound(bot, SDL_Rect{x + 3, 441, kw, 28}, 14, FG);
    drawText(bot, k, SDL_Rect{x + 3, 441, kw, 28}, BG, F_MICRO);
    drawText(bot, label, SDL_Rect{x + 4 + kw + 8, 438, lw + 4, 34}, FG, F_MICRO, A_LEFT);
    x += w + 8;
  }
}

void App::segBar(Win& w, SDL_Rect r, int total, int done, const std::vector<int>& res) {
  if (total <= 0) return;
  int gap = 6, sw = (r.w - gap * (total - 1)) / total;
  for (int i = 0; i < total; i++) {
    SDL_Rect s{r.x + i * (sw + gap), r.y, sw, r.h};
    if (i < int(res.size())) fillRound(w, s, 3, res[i] ? ACCENT : BAD);
    else if (i == done) boxRound(w, s, 3, BG, ACCENT, 2);
    else fillRound(w, s, 3, TRACK);
  }
}

void App::statBox(Win& w, SDL_Rect r, const std::string& label, const std::string& value, int font) {
  fillRound(w, r, 12, PANEL);
  drawText(w, label, SDL_Rect{r.x + 14, r.y + 8, r.w - 28, 20}, DIM, F_MICRO, A_LEFT);
  drawText(w, value, SDL_Rect{r.x + 14, r.y + 30, r.w - 28, r.h - 36}, FG, font, A_LEFT);
}

void App::bar(Win& w, SDL_Rect r, float frac) {
  fillRound(w, r, r.h / 2, TRACK);
  int fw = int(r.w * std::clamp(frac, 0.f, 1.f));
  if (fw > 0) fillRound(w, SDL_Rect{r.x, r.y, std::max(fw, r.h), r.h}, r.h / 2, ACCENT);
}

void App::stepPips(Win& w, int x, int y, int step, int size) {
  for (int j = 0; j < KNOWN_STEP; j++) fillRound(w, SDL_Rect{x + j * (size + 4), y, size, size}, 3, j < step ? ACCENT : TRACK);
}

void App::drawCanvas(SDL_Rect c, const Glyph* g, bool guide, bool checkedState) {
  boxRound(bot, SDL_Rect{c.x - 3, c.y - 3, c.w + 6, c.h + 6}, 14, CANVAS, LINE, 3);
  SDL_SetRenderDrawColor(bot.r, LINE.r, LINE.g, LINE.b, 255);
  for (int t = c.y + 8; t < c.y + c.h - 8; t += 10) SDL_RenderDrawLine(bot.r, c.x + c.w / 2, t, c.x + c.w / 2, t + 4);
  for (int t = c.x + 8; t < c.x + c.w - 8; t += 10) SDL_RenderDrawLine(bot.r, t, c.y + c.h / 2, t + 4, c.y + c.h / 2);
  float big = c.w * 0.024f, ink = c.w * 0.016f;
  if (g && guide) {
    for (auto& s : g->strokes) drawStrokeIn(bot, s, c, checkedState ? GHOST_OK : GHOST, big);
  }
  for (size_t i = 0; i < strokes.size(); i++) {
    Col col = checkedState ? (i < strokeOk.size() && strokeOk[i] ? GOOD : BAD) : FG;
    drawStrokeIn(bot, strokes[i], c, col, ink);
  }
  if (drawing) drawStrokeIn(bot, cur, c, ACCENT_TXT, ink);
  if (g && guide) drawStrokeNumbers(bot, *g, c, int(strokes.size()), int(g->strokes.size()));
}

// ================================================================== screens
void App::render() {
  clearWin(top, BG); clearWin(bot, BG); buttons.clear();
  if (paused) drawPause();
  else switch (screen) {
    case Screen::Home: drawHome(); break;
    case Screen::Stages: drawStages(); break;
    case Screen::Lesson: drawLesson(); break;
    case Screen::Unlocked: drawUnlocked(); break;
    case Screen::Drill: drawDrill(); break;
    case Screen::Result: drawResult(); break;
    case Screen::Chart: drawChart(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::Credits: drawCredits(); break;
  }
  drawButtons();
  SDL_RenderPresent(top.r); SDL_RenderPresent(bot.r);
}

static Col stateFill(KanaState s) {
  switch (s) {
    case KanaState::Known: return ACCENT;
    case KanaState::Weak: return BAD_FILL;
    case KanaState::Learning: return PANEL2;
    case KanaState::New: return PANEL;
    default: return BG;
  }
}
static Col stateText(KanaState s) { return s == KanaState::Known ? ON_ACCENT : s == KanaState::Locked ? DIS_TXT : FG; }

void App::drawHome() {
  Script sc = prog.settings.script; int scI = si(sc);
  const auto& stages = data.stages[scI];
  int curSt = prog.currentStage(sc); const Stage& st = stages[curSt];
  header(scriptName(sc));
  // stage card
  SDL_Rect card{24, 58, 592, 206}; fillRound(top, card, 14, PANEL);
  int known = prog.stageKnown(data, sc, curSt), n = int(st.kana.size());
  drawText(top, "STAGE " + std::to_string(curSt + 1) + " OF " + std::to_string(STAGE_COUNT), SDL_Rect{44, 68, 300, 22}, ACCENT_TXT, F_MICRO, A_LEFT);
  drawText(top, std::to_string(known) + " / " + std::to_string(n) + " known", SDL_Rect{316, 68, 280, 22}, DIM, F_MICRO, A_RIGHT);
  drawText(top, st.name, SDL_Rect{44, 92, 400, 40}, FG, F_SMALL, A_LEFT);
  int show = std::min(n, 10), cs = n <= 6 ? 52 : 44;
  for (int i = 0; i < show; i++) {
    int ki = st.kana[i]; KanaState ks = prog.state(data, ki);
    SDL_Rect r{44 + i * (cs + 8), 138, cs, cs};
    if (ks == KanaState::Learning || ks == KanaState::New) boxRound(top, r, 10, PANEL2, ks == KanaState::Learning ? ACCENT : PANEL2, 2);
    else fillRound(top, r, 10, stateFill(ks));
    drawText(top, data.kana[ki].glyph, r, stateText(ks), F_SMALL);
  }
  if (n > show) drawText(top, "+" + std::to_string(n - show), SDL_Rect{44 + show * (cs + 8), 138, 60, cs}, DIM, F_TINY);
  std::string msg;
  if (prog.lessonPending(sc)) msg = "New lesson ready: " + stages[std::min(prog.lessons[scI], STAGE_COUNT - 1)].name + ". Press A to start it.";
  else if (known == n && curSt == STAGE_COUNT - 1) msg = "Every stage is unlocked and known. Keep reviewing!";
  else {
    std::vector<std::string> left;
    for (int ki : st.kana) if (prog.step(Progress::keyKana(data.kana[ki])) < KNOWN_STEP && left.size() < 5) left.push_back(data.kana[ki].glyph);
    std::string next = curSt + 1 < STAGE_COUNT ? "the " + stages[curSt + 1].name : "the last stage";
    if (sc == Script::Hiragana && curSt == KATAKANA_AFTER - 1) next = "katakana and stage 11";
    if (curSt == STAGE_COUNT - 1) msg = "Last stage! Get " + join(left, " ") + " to step 4 to finish " + scriptName(sc) + ".";
    else msg = "Get " + join(left, " ") + " to step 4 to unlock " + next + ".";
  }
  drawWrapped(top, msg, SDL_Rect{44, 200, 552, 56}, DIM, F_TINY);
  // script progress
  for (int s = 0; s < 2; s++) {
    int x = 24 + s * 304; Script ss = s ? Script::Katakana : Script::Hiragana;
    drawText(top, s ? "カタカナ Katakana" : "ひらがな Hiragana", SDL_Rect{x, 278, 200, 26}, prog.scriptUnlocked(ss) ? FG : DIM, F_MICRO, A_LEFT);
    if (prog.scriptUnlocked(ss)) {
      int k = prog.knownCount(data, ss);
      drawText(top, std::to_string(k) + " / 104", SDL_Rect{x + 190, 278, 98, 26}, DIM, F_MICRO, A_RIGHT);
      bar(top, SDL_Rect{x, 310, 288, 12}, k / 104.f);
    } else {
      iconLock(top, x + 228, 284, 2, DIM);
      drawText(top, "Locked", SDL_Rect{x + 240, 278, 48, 26}, DIM, F_MICRO, A_RIGHT);
      fillRound(top, SDL_Rect{x, 310, 288, 12}, 6, TRACK);
    }
  }
  // stats
  std::vector<std::string> weak; for (int ki : prog.weakest(data, sc, 3)) weak.push_back(data.kana[ki].glyph);
  std::string last = prog.lastScore >= 0 ? std::to_string(prog.lastScore) + " / " + std::to_string(prog.lastLen) : "-";
  std::string next = curSt + 1 < STAGE_COUNT ? stages[curSt + 1].name : "-";
  statBox(top, SDL_Rect{24, 340, 188, 120}, "LAST SESSION", last);
  statBox(top, SDL_Rect{226, 340, 188, 120}, "WEAK SPOTS", weak.empty() ? "none" : join(weak, " "));
  statBox(top, SDL_Rect{428, 340, 188, 120}, "NEXT UNLOCK", next);

  // cartridges
  bool lesson = prog.lessonPending(sc);
  const std::string glyphs[4] = {data.kana[st.kana[0]].glyph, "1-14", sc == Script::Hiragana ? "あア" : "アあ", "せってい"};
  const std::string labels[4] = {lesson ? "Lesson" : "Practice", "Stages", "Kana chart", "Settings"};
  for (int i = 0; i < 4; i++) {
    bool on = i == homeSel; int lift = on ? -8 : 0;
    SDL_Rect r{23 + i * 152, 30 + lift, 138, 214};
    Col fill = PANEL; if (pressedId == B_CART0 + i) fill = mix(fill, FG, 0.2f);
    boxRound(bot, r, 14, fill, on ? ACCENT : LINE, on ? 4 : 2);
    fillRound(bot, SDL_Rect{r.x + 4, r.y + 4, r.w - 8, 22}, 10, PANEL2);
    for (int j = 0; j < 5; j++) fillRect(bot, SDL_Rect{r.x + 26 + j * 18, r.y + 10, 8, 12}, LINE);
    SDL_Rect lab{r.x + 12, r.y + 34, r.w - 24, 128};
    fillRound(bot, lab, 10, on ? ACCENT : PANEL2);
    drawText(bot, glyphs[i], SDL_Rect{lab.x + 6, lab.y, lab.w - 12, lab.h}, on ? ON_ACCENT : FG, i == 0 ? F_MID : F_SMALL);
    drawText(bot, labels[i], SDL_Rect{r.x + 6, r.y + 168, r.w - 12, 40}, FG, F_TINY);
    addBtn(r, B_CART0 + i, ""); lastBtn().hidden = true;
  }
  // script switch
  for (int s = 0; s < 2; s++) {
    Script ss = s ? Script::Katakana : Script::Hiragana;
    addBtn(SDL_Rect{150 + s * 172, 272, 166, 54}, s ? B_SCRIPT_K : B_SCRIPT_H, s ? "カタカナ" : "ひらがな", F_TINY, prog.scriptUnlocked(ss));
    if (ss == sc) accent(lastBtn());
    if (!prog.scriptUnlocked(ss)) lastBtn().icon = 1;
  }
  drawText(bot, "Pick a script, then a cartridge", SDL_Rect{16, 340, 608, 30}, DIM, F_MICRO);
  chips({{"A", lesson ? "Lesson" : "Start"}, {"◀▶", "Choose"}, {"Y", "Script"}, {"F+SELECT", "Quit"}});
}

void App::drawStages() {
  int scI = si(mapScript); const auto& stages = data.stages[scI];
  int curSt = prog.currentStage(mapScript);
  const Stage& st = stages[mapSel];
  header(std::string("Stages · ") + scriptName(mapScript));
  std::string status = mapSel < curSt ? "Passed" : mapSel == curSt ? "In progress" : "Locked";
  drawText(top, "STAGE " + std::to_string(mapSel + 1), SDL_Rect{24, 60, 200, 22}, ACCENT_TXT, F_MICRO, A_LEFT);
  drawText(top, st.name, SDL_Rect{24, 82, 420, 44}, mapSel > curSt ? DIM : FG, F_SMALL, A_LEFT);
  drawText(top, status, SDL_Rect{424, 82, 192, 44}, DIM, F_TINY, A_RIGHT);
  int n = int(st.kana.size());
  if (n <= 6) {
    int w = std::min(110, (592 - (n - 1) * 10) / n);
    for (int i = 0; i < n; i++) {
      int ki = st.kana[i]; KanaState ks = prog.state(data, ki);
      SDL_Rect r{24 + i * (w + 10), 136, w, 150};
      fillRound(top, r, 14, ks == KanaState::Known ? PANEL2 : ks == KanaState::Locked ? PANEL : stateFill(ks));
      drawText(top, data.kana[ki].glyph, SDL_Rect{r.x, r.y + 8, r.w, 72}, ks == KanaState::Locked ? DIS_TXT : FG, F_MID);
      drawText(top, data.kana[ki].romaji, SDL_Rect{r.x, r.y + 84, r.w, 26}, DIM, F_TINY);
      stepPips(top, r.x + (r.w - (4 * 16 - 4)) / 2, r.y + 120, prog.step(Progress::keyKana(data.kana[ki])));
    }
  } else {
    int cols = 9, w = (592 - 8 * (cols - 1)) / cols;
    for (int i = 0; i < n; i++) {
      int ki = st.kana[i]; KanaState ks = prog.state(data, ki);
      SDL_Rect r{24 + (i % cols) * (w + 8), 136 + (i / cols) * 74, w, 66};
      fillRound(top, r, 10, ks == KanaState::Locked ? PANEL : stateFill(ks));
      drawText(top, data.kana[ki].glyph, SDL_Rect{r.x + 2, r.y + 4, r.w - 4, 40}, stateText(ks), F_TINY);
      drawText(top, data.kana[ki].romaji, SDL_Rect{r.x, r.y + 44, r.w, 18}, ks == KanaState::Known ? ON_ACCENT : DIM, F_MICRO);
    }
  }
  drawWrapped(top, "Each kana needs 4 steps. A right answer adds a step (2 per session at most), a miss takes one away. "
                   "Passed stages come back as review.", SDL_Rect{24, 296, 592, 70}, DIM, F_MICRO);
  std::string next = curSt + 1 < STAGE_COUNT ? stages[curSt + 1].name : "-";
  statBox(top, SDL_Rect{24, 372, 188, 92}, "PASSED", std::to_string(curSt) + " of " + std::to_string(STAGE_COUNT));
  statBox(top, SDL_Rect{226, 372, 188, 92}, "NEXT UNLOCK", next);
  statBox(top, SDL_Rect{428, 372, 188, 92}, "KATAKANA", prog.scriptUnlocked(Script::Katakana) ? "Unlocked" : "After stage 10", F_TINY);

  for (int s = 0; s < 2; s++) {
    Script ss = s ? Script::Katakana : Script::Hiragana;
    addBtn(SDL_Rect{16 + s * 150, 10, 142, 46}, s ? B_SCRIPT_K : B_SCRIPT_H, s ? "カタカナ" : "ひらがな", F_TINY, prog.scriptUnlocked(ss));
    if (ss == mapScript) accent(lastBtn());
    if (!prog.scriptUnlocked(ss)) lastBtn().icon = 1;
  }
  drawText(bot, "Basic 1-10 · Voiced 11-12 · Combos 13-14", SDL_Rect{318, 10, 306, 46}, DIM, F_MICRO, A_RIGHT);
  for (int i = 0; i < STAGE_COUNT; i++) {
    SDL_Rect r{15 + (i % 7) * 88, 70 + (i / 7) * 106, 82, 100};
    std::string first = data.kana[stages[i].kana[0]].glyph;
    addBtn(r, B_STAGE0 + i, first, F_SMALL); Button& b = lastBtn(); b.sub = std::to_string(i + 1);
    if (i < curSt) { accent(b); b.icon = 2; }
    else if (i == curSt) { b.fill = PANEL2; b.line = ACCENT; }
    else { b.fill = BG; b.line = DIS_LINE; b.text = DIS_TXT; b.subText = DIS_TXT; b.icon = 1; }
    if (i == mapSel) { b.line = FG; b.lineW = 4; }
  }
  std::string action; bool en = true;
  if (mapSel == curSt) action = prog.lessonPending(mapScript) ? "Start lesson" : "Continue stage " + std::to_string(curSt + 1);
  else if (mapSel < curSt) action = "Replay lesson";
  else { action = "Locked"; en = false; }
  addBtn(SDL_Rect{16, 292, 608, 66}, B_STAGE_ACTION, action, F_SMALL, en);
  if (en && mapSel == curSt) accent(lastBtn());
  chips({{"+", "Move"}, {"A", "Open"}, {"L/R", "Script"}, {"B", "Back"}});
}

void App::drawLesson() {
  const Stage& st = data.stages[si(lessonScript)][lessonStage];
  int n = int(st.kana.size()), ki = currentLessonKana(); const Kana& k = data.kana[ki];
  const Glyph* g = activeGlyph();
  header("Stage " + std::to_string(lessonStage + 1) + " · Lesson" + (lessonReplay ? " (replay)" : ""));
  drawText(top, "NEW KANA " + std::to_string(lessonIdx + 1) + " OF " + std::to_string(n), SDL_Rect{24, 54, 300, 22}, ACCENT_TXT, F_MICRO, A_LEFT);
  int pw = std::min(26, 280 / n);
  for (int i = 0; i < n; i++) fillRound(top, SDL_Rect{616 - n * (pw + 4) + 4 + i * (pw + 4), 61, pw, 8}, 3, i <= lessonIdx ? ACCENT : TRACK);
  SDL_Rect card{24, 84, 200, 200}; fillRound(top, card, 16, PANEL);
  drawText(top, k.glyph, card, FG, F_HUGE);
  drawText(top, k.romaji, SDL_Rect{244, 84, 372, 92}, ACCENT_TXT, F_BIG, A_LEFT);
  auto it = data.hints.find(k.glyph);
  if (it != data.hints.end()) drawWrapped(top, it->second, SDL_Rect{244, 180, 372, 104}, DIM, F_TINY);
  if (g) {
    int m = int(g->strokes.size());
    drawText(top, "STROKE ORDER · " + std::to_string(m) + (m == 1 ? " STROKE" : " STROKES"), SDL_Rect{24, 296, 400, 22}, DIM, F_MICRO, A_LEFT);
    int fs = std::min(110, (592 - (m - 1) * 10) / m);
    for (int i = 0; i < m; i++) {
      SDL_Rect f{24 + i * (fs + 10), 326, fs, fs};
      fillRound(top, f, 10, PANEL);
      SDL_Rect gbox{f.x + 6, f.y + 6, f.w - 12, f.h - 12};
      for (auto& s : g->strokes) drawStrokeIn(top, s, gbox, GHOST, fs * 0.05f);
      for (int j = 0; j <= i; j++) drawStrokeIn(top, g->strokes[j], gbox, j == i ? ACCENT_TXT : FG, fs * 0.045f);
      if (!g->strokes[i].empty()) { SDL_Point p = glyphToBox(g->strokes[i][0], gbox); fillCircle(top, p.x, p.y, std::max(4, fs / 18), ACCENT); }
    }
  } else {
    auto parts = utf8Codepoints(k.glyph);
    drawText(top, "MADE OF", SDL_Rect{24, 296, 300, 22}, DIM, F_MICRO, A_LEFT);
    std::string a = k.glyph.substr(0, 3), b = k.glyph.substr(3);
    fillRound(top, SDL_Rect{24, 326, 130, 130}, 14, PANEL); drawText(top, a, SDL_Rect{24, 326, 130, 130}, FG, F_BIG);
    drawText(top, "+", SDL_Rect{154, 326, 50, 130}, DIM, F_MID);
    fillRound(top, SDL_Rect{204, 326, 130, 130}, 14, PANEL); drawText(top, b, SDL_Rect{204, 326, 130, 130}, FG, F_MID);
    drawWrapped(top, "The small kana blends into the one before it: one beat, not two.", SDL_Rect{352, 336, 264, 110}, DIM, F_TINY);
    (void)parts;
  }
  // bottom
  drawText(bot, "Your turn", SDL_Rect{16, 14, 220, 36}, FG, F_SMALL, A_LEFT);
  drawWrapped(bot, g ? "Trace it over the guide, following the numbers. No score yet, this is just practice."
                     : "Say it out loud: " + k.glyph + " is \"" + k.romaji + "\". Combination kana have no tracing.",
              SDL_Rect{16, 56, 220, 120}, DIM, F_MICRO);
  addBtn(SDL_Rect{16, 184, 220, 58}, B_LESSON_CLEAR, "Clear", F_TINY, g && !strokes.empty());
  addBtn(SDL_Rect{16, 250, 220, 58}, B_LESSON_PREV, "Previous", F_TINY, lessonIdx > 0);
  addBtn(SDL_Rect{16, 316, 220, 76}, B_LESSON_NEXT, lessonIdx < n - 1 ? "Next kana" : (lessonReplay ? "Done" : "Start practice"), F_TINY);
  accent(lastBtn());
  if (g) drawCanvas(canvasRect(), g, true, false);
  else {
    SDL_Rect c = canvasRect(); fillRound(bot, c, 14, PANEL);
    drawText(bot, k.glyph, SDL_Rect{c.x, c.y + 40, c.w, 220}, FG, F_HUGE);
    drawText(bot, k.romaji, SDL_Rect{c.x, c.y + 270, c.w, 70}, ACCENT_TXT, F_MID);
  }
  chips({{"A", lessonIdx < n - 1 ? "Next" : "Finish"}, {"L", "Previous"}, {"X", "Clear"}, {"B", "Later"}});
}

void App::drawUnlocked() {
  if (pendingUnlocks.empty()) { go(Screen::Home); drawHome(); return; }
  auto [s, stI] = pendingUnlocks.front();
  const Stage& nst = data.stages[si(s)][stI];
  bool kat = s == Script::Katakana && stI == 0;
  Script ps = kat ? Script::Hiragana : s; int pI = kat ? KATAKANA_AFTER - 1 : stI - 1;
  const Stage& pst = data.stages[si(ps)][std::max(0, pI)];
  const int conf[10][4] = {{40, 70, 10, 0}, {110, 40, 8, 1}, {520, 60, 10, 0}, {580, 120, 8, 2}, {70, 170, 8, 2},
                           {560, 200, 10, 0}, {300, 30, 6, 1}, {470, 30, 8, 0}, {30, 250, 6, 1}, {600, 270, 6, 1}};
  for (auto& c : conf) fillRect(top, SDL_Rect{c[0], c[1], c[2], c[2]}, c[3] == 0 ? ACCENT : c[3] == 1 ? FG : GOOD);
  iconBanana(top, 290, 34, 6);
  drawText(top, kat ? "KATAKANA UNLOCKED" : "STAGE " + std::to_string(stI + 1) + " UNLOCKED", SDL_Rect{20, 104, 600, 56}, ACCENT_TXT, F_MID);
  drawText(top, "You know the whole " + pst.name + ". " + std::to_string(nst.kana.size()) + " new kana are ready.",
           SDL_Rect{20, 164, 600, 34}, DIM, F_TINY);
  int n = int(nst.kana.size());
  if (n <= 6) {
    int w = 92, x0 = (640 - (n * w + (n - 1) * 12)) / 2;
    for (int i = 0; i < n; i++) {
      SDL_Rect r{x0 + i * (w + 12), 228, w, 110}; boxRound(top, r, 14, PANEL, ACCENT, 2);
      drawText(top, data.kana[nst.kana[i]].glyph, r, FG, F_MID);
    }
  } else {
    int cols = 9, w = 60, x0 = (640 - (cols * w + (cols - 1) * 8)) / 2;
    for (int i = 0; i < n; i++) {
      SDL_Rect r{x0 + (i % cols) * (w + 8), 216 + (i / cols) * 76, w, 68}; boxRound(top, r, 10, PANEL, ACCENT, 2);
      drawText(top, data.kana[nst.kana[i]].glyph, SDL_Rect{r.x + 2, r.y, r.w - 4, r.h}, FG, F_TINY);
    }
  }
  if (pendingUnlocks.size() > 1) drawText(top, "+" + std::to_string(pendingUnlocks.size() - 1) + " more unlocked", SDL_Rect{20, 420, 600, 30}, DIM, F_MICRO);

  drawText(bot, "STAGE " + std::to_string(pI + 1) + " PASSED · " + scriptName(ps), SDL_Rect{16, 10, 608, 24}, DIM, F_MICRO, A_LEFT);
  int pn = int(pst.kana.size());
  int cols = pn <= 6 ? pn : 9, w = (608 - (cols - 1) * 8) / cols, h = pn <= 6 ? 104 : 58;
  for (int i = 0; i < pn; i++) {
    SDL_Rect r{16 + (i % cols) * (w + 8), 40 + (i / cols) * (h + 8), w, h};
    fillRound(bot, r, 12, ACCENT);
    drawText(bot, data.kana[pst.kana[i]].glyph, SDL_Rect{r.x + 2, r.y + 2, r.w - 4, r.h - (pn <= 6 ? 30 : 4)}, ON_ACCENT, pn <= 6 ? F_MID : F_TINY);
    if (pn <= 6) drawText(bot, data.kana[pst.kana[i]].romaji, SDL_Rect{r.x, r.y + r.h - 30, r.w, 26}, ON_ACCENT, F_MICRO);
  }
  statBox(bot, SDL_Rect{16, 246, 296, 84}, std::string(scriptName(s)) == "Hiragana" ? "HIRAGANA KNOWN" : "KATAKANA KNOWN",
          std::to_string(prog.knownCount(data, s)) + " / 104");
  statBox(bot, SDL_Rect{328, 246, 296, 84}, "STAGES", std::to_string(prog.unlocked[si(s)]) + " of " + std::to_string(STAGE_COUNT));
  addBtn(SDL_Rect{16, 346, 180, 76}, B_LATER, "Later", F_SMALL);
  addBtn(SDL_Rect{212, 346, 412, 76}, B_START_LESSON, "Start lesson", F_SMALL); accent(lastBtn());
  chips({{"A", "Start lesson"}, {"B", "Later"}});
}

void App::drawDrill() {
  const Kana* k = q.kanaIdx >= 0 ? &data.kana[q.kanaIdx] : nullptr;
  std::string where = focusKana >= 0 ? "Focus " + data.kana[focusKana].glyph
                                     : "Stage " + std::to_string(prog.currentStage(prog.settings.script) + 1);
  header(where + " · " + std::to_string(std::min(index + 1, sessionLen)) + " / " + std::to_string(sessionLen));
  segBar(top, SDL_Rect{24, 56, 592, 12}, sessionLen, index, results);
  drawText(top, q.hint, SDL_Rect{24, 78, 592, 36}, DIM, F_TINY);
  Col pc = checked ? (correct ? GOOD : BAD) : FG;
  if (q.type == QType::Type) {
    drawText(top, q.prompt, SDL_Rect{24, 112, 592, 180}, pc, F_HUGE);
    SDL_Rect f{140, 298, 360, 72}; fillRound(top, f, 12, PANEL); fillRect(top, SDL_Rect{f.x + 10, f.y + f.h - 6, f.w - 20, 4}, ACCENT);
    std::string t = typed + (checked ? "" : "_");
    drawText(top, typed.empty() && checked ? "-" : t, SDL_Rect{f.x + 10, f.y, f.w - 20, f.h - 6}, checked ? pc : ACCENT_TXT, F_MID);
  } else if (q.type == QType::Write) {
    const Glyph* g = activeGlyph(); int m = g ? int(g->strokes.size()) : 0;
    drawText(top, checked ? q.answer : q.prompt, SDL_Rect{24, 104, 300, 230}, pc, F_HUGE);
    drawText(top, "TRACE THE KANA", SDL_Rect{340, 120, 276, 22}, DIM, F_MICRO, A_LEFT);
    drawText(top, checked ? std::to_string(m) + " strokes" : "Stroke " + std::to_string(std::min(int(strokes.size()) + 1, m)) + " of " + std::to_string(m),
             SDL_Rect{340, 146, 276, 40}, FG, F_SMALL, A_LEFT);
    for (int i = 0; i < m; i++) {
      SDL_Rect r{340 + i * 30, 196, 22, 22};
      if (checked) fillRound(top, r, 4, i < int(strokeOk.size()) && strokeOk[i] ? GOOD : BAD);
      else if (i < int(strokes.size())) fillRound(top, r, 4, ACCENT);
      else if (i == int(strokes.size())) boxRound(top, r, 4, BG, ACCENT, 3);
      else fillRound(top, r, 4, TRACK);
    }
    drawWrapped(top, showHint ? "Hint is on: the grey guide shows the shape and stroke order."
                              : "Press Y or tap Hint to show a guide.", SDL_Rect{340, 232, 276, 90}, DIM, F_MICRO);
  } else {
    drawText(top, q.prompt, SDL_Rect{24, 112, 592, 250}, pc, F_HUGE);
  }
  if (checked) {
    SDL_Rect f{24, 376, 592, 88}; fillRound(top, f, 12, correct ? GOOD_FILL : BAD_FILL);
    if (correct) iconCheck(top, 40, 400, 6, GOOD); else iconCross(top, 42, 396, 6, BAD);
    std::string ans = q.answer;
    if (k && (q.type == QType::Write || q.type == QType::RecallKana)) ans = k->glyph + " (" + k->romaji + ")";
    drawText(top, correct ? "Correct!" : "Not quite, it's " + ans, SDL_Rect{90, 384, 510, 36}, FG, F_SMALL, A_LEFT);
    if (!feedbackNote.empty()) drawWrapped(top, feedbackNote, SDL_Rect{90, 420, 510, 42}, DIM, F_MICRO);
  }

  // bottom
  if (!q.options.empty()) {
    int n = int(q.options.size()), rows = n / 2, h = n == 6 ? 96 : 151;
    for (int i = 0; i < n; i++) {
      SDL_Rect r{16 + (i % 2) * 309, 16 + (i / 2) * (h + 10), 299, h};
      addBtn(r, B_TILE0 + i, q.options[i], F_MID); Button& b = lastBtn();
      if (checked) {
        if (i == q.correct) { b.fill = GOOD_FILL; b.line = GOOD; b.icon = 2; b.text = FG; }
        else if (i == sel) { b.fill = BAD_FILL; b.line = BAD; }
      } else if (i == sel) accent(b);
      else if (i == cursor) { b.line = DIM; }
    }
    (void)rows;
    if (checked) { addBtn(SDL_Rect{16, 340, 608, 76}, B_CONTINUE, "Continue", F_SMALL); accent(lastBtn()); }
    else { addBtn(SDL_Rect{16, 340, 608, 76}, B_CHECK, "Check answer", F_SMALL, sel >= 0); if (sel >= 0) accent(lastBtn()); }
    chips({{"A", "Choose"}, {"X", "Check"}, {"SELECT", "Pause"}});
  } else if (q.type == QType::Type) {
    const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int r = 0; r < 3; r++) {
      int len = int(strlen(rows[r])), x0 = (LW - (len * 56 + (len - 1) * 4)) / 2;
      for (int i = 0; i < len; i++) {
        char ch = rows[r][i];
        addBtn(SDL_Rect{x0 + i * 60, 16 + r * 70, 56, 64}, B_KEY0 + (ch - 'a'), std::string(1, ch), F_SMALL, !checked);
        lastBtn().radius = 10;
      }
    }
    addBtn(SDL_Rect{16, 236, 180, 68}, B_BKSP, "Delete", F_TINY, !checked && !typed.empty());
    if (checked) { addBtn(SDL_Rect{212, 236, 412, 68}, B_CONTINUE, "Continue", F_SMALL); accent(lastBtn()); }
    else { addBtn(SDL_Rect{212, 236, 412, 68}, B_CHECK, "Check answer", F_SMALL, !typed.empty()); if (!typed.empty()) accent(lastBtn()); }
    chips({{"X", "Check"}, {"B", "Delete"}, {"SELECT", "Pause"}});
  } else {
    addBtn(SDL_Rect{16, 20, 120, 90}, B_UNDO, "Undo", F_TINY, !checked && !strokes.empty());
    addBtn(SDL_Rect{16, 120, 120, 90}, B_CLEAR, "Clear", F_TINY, !checked && !strokes.empty());
    addBtn(SDL_Rect{16, 220, 120, 90}, B_HINT, showHint ? "Hint on" : "Hint", F_TINY, !checked);
    if (showHint) { lastBtn().line = ACCENT; lastBtn().text = ACCENT_TXT; }
    if (checked) { addBtn(SDL_Rect{16, 320, 120, 96}, B_CONTINUE, "Next", F_TINY); accent(lastBtn()); }
    else { addBtn(SDL_Rect{16, 320, 120, 96}, B_CHECK, "Check", F_TINY, !strokes.empty()); if (!strokes.empty()) accent(lastBtn()); }
    drawCanvas(canvasRect(), activeGlyph(), showHint || checked, checked);
    chips({{"X", "Check"}, {"Y", "Hint"}, {"B", "Undo"}, {"SELECT", "Pause"}});
  }
}

void App::drawResult() {
  header("Session complete");
  Script sc = prog.settings.script; int curSt = prog.currentStage(sc); const Stage& st = data.stages[si(sc)][curSt];
  drawText(top, std::to_string(score), SDL_Rect{24, 60, 150, 190}, ACCENT_TXT, F_HUGE, A_RIGHT);
  drawText(top, "/ " + std::to_string(sessionLen), SDL_Rect{184, 150, 130, 70}, DIM, F_MID, A_LEFT);
  std::string head = score == sessionLen ? "Perfect run!" : score * 10 >= sessionLen * 7 ? "Nice run!" : "Keep going!";
  drawText(top, head, SDL_Rect{330, 96, 286, 44}, FG, F_SMALL, A_LEFT);
  drawWrapped(top, missed.empty() ? "Every answer was right." : std::to_string(missed.size()) + (missed.size() == 1 ? " kana goes" : " answers go") + " back into review.",
              SDL_Rect{330, 144, 286, 90}, DIM, F_TINY);
  segBar(top, SDL_Rect{24, 262, 592, 14}, sessionLen, sessionLen, results);
  std::vector<std::string> left;
  for (int ki : st.kana) if (prog.step(Progress::keyKana(data.kana[ki])) < KNOWN_STEP && left.size() < 4) left.push_back(data.kana[ki].glyph);
  statBox(top, SDL_Rect{24, 300, 188, 140}, "NEWLY KNOWN", newlyKnown.empty() ? "-" : join(newlyKnown, " "));
  statBox(top, SDL_Rect{226, 300, 188, 140}, "STAGE " + std::to_string(curSt + 1),
          std::to_string(prog.stageKnown(data, sc, curSt)) + " / " + std::to_string(st.kana.size()) + " known", F_TINY);
  statBox(top, SDL_Rect{428, 300, 188, 140}, "STILL TO LEARN", left.empty() ? "all done" : join(left, " "));

  drawText(bot, "TO REVIEW", SDL_Rect{16, 10, 300, 24}, DIM, F_MICRO, A_LEFT);
  if (missed.empty()) drawText(bot, "Nothing to review", SDL_Rect{16, 40, 608, 150}, DIM, F_SMALL);
  for (size_t i = 0; i < missed.size() && i < 3; i++) {
    SDL_Rect r{16 + int(i) * 206, 40, 196, 170}; boxRound(bot, r, 12, BAD_FILL, BAD, 2);
    drawText(bot, missed[i].prompt, SDL_Rect{r.x + 6, r.y + 8, r.w - 12, 70}, FG, F_MID);
    drawText(bot, missed[i].answer, SDL_Rect{r.x + 6, r.y + 84, r.w - 12, 40}, ACCENT_TXT, F_SMALL);
    drawText(bot, "You said " + missed[i].said, SDL_Rect{r.x + 6, r.y + 128, r.w - 12, 30}, DIM, F_MICRO);
  }
  if (missed.size() > 3) drawText(bot, "+" + std::to_string(missed.size() - 3) + " more", SDL_Rect{16, 214, 608, 24}, DIM, F_MICRO);
  bool unlock = !pendingUnlocks.empty();
  addBtn(SDL_Rect{16, 330, 180, 84}, B_HOME, "Home", F_SMALL);
  addBtn(SDL_Rect{212, 330, 412, 84}, B_AGAIN, unlock ? "Continue" : "Practice again", F_SMALL); accent(lastBtn());
  chips({{"A", unlock ? "Continue" : "Again"}, {"B", "Home"}});
}

void App::drawPause() {
  drawText(top, "Paused", SDL_Rect{20, 110, 600, 110}, FG, F_BIG);
  drawText(top, "Question " + std::to_string(std::min(index + 1, sessionLen)) + " of " + std::to_string(sessionLen) + " · your answers are saved",
           SDL_Rect{20, 240, 600, 40}, DIM, F_TINY);
  drawText(top, "Up/Down choose · A select · B resume", SDL_Rect{20, 400, 600, 36}, DIM, F_MICRO);
  const char* labels[3] = {"Resume", "Back to menu", "Quit Banana Kana"};
  const int ids[3] = {B_RESUME, B_MENU, B_QUIT};
  for (int i = 0; i < 3; i++) {
    addBtn(SDL_Rect{80, 30 + i * 130, 480, 110}, ids[i], labels[i], F_SMALL);
    if (i == pauseSel) accent(lastBtn());
  }
}

std::vector<App::Cell> App::chartCells(Script s, int tab) const {
  std::vector<Cell> out; const auto& stages = data.stages[si(s)];
  if (tab == 0) {
    for (int st = 0; st < 10; st++)
      for (int ki : stages[st].kana) {
        const Kana& k = data.kana[ki]; int col = vowelCol(k.romaji);
        if (st == 9) col = k.romaji == "wa" ? 0 : k.romaji == "n" ? 2 : 4;
        out.push_back({ki, st, col});
      }
  } else if (tab == 1) {
    int row = 0;
    for (int st = 10; st < 12; st++) {
      const auto& list = stages[st].kana;
      for (size_t i = 0; i < list.size(); i++) out.push_back({list[i], row + int(i) / 5, int(i) % 5});
      row += int(list.size()) / 5;
    }
  } else {
    int row = 0;
    for (int st = 12; st < 14; st++) {
      const auto& list = stages[st].kana;
      for (size_t i = 0; i < list.size(); i++) out.push_back({list[i], row + int(i) / 3, int(i) % 3});
      row += int(list.size()) / 3;
    }
  }
  return out;
}

void App::drawChart() {
  Script sc = prog.settings.script;
  auto cells = chartCells(sc, chartTab);
  if (chartSel < 0 || std::none_of(cells.begin(), cells.end(), [&](const Cell& c) { return c.kana == chartSel; }))
    chartSel = cells.empty() ? -1 : cells[0].kana;
  int maxRow = 0; for (auto& c : cells) maxRow = std::max(maxRow, c.row);
  const int VIS = 5; chartScroll = std::clamp(chartScroll, 0, std::max(0, maxRow + 1 - VIS));

  header(std::string("Kana chart · ") + scriptName(sc));
  if (chartSel >= 0) {
    const Kana& k = data.kana[chartSel]; KanaState ks = prog.state(data, chartSel);
    const Glyph* g = data.glyphFor(k.glyph); int stI = data.stageOfKana[chartSel];
    SDL_Rect card{24, 58, 220, 240}; fillRound(top, card, 16, PANEL);
    drawText(top, k.glyph, card, ks == KanaState::Locked ? DIM : FG, F_HUGE);
    drawText(top, k.romaji, SDL_Rect{264, 58, 352, 84}, ACCENT_TXT, F_BIG, A_LEFT);
    drawText(top, (g ? std::to_string(g->strokes.size()) + " strokes" : std::string("Combination")) + " · Stage " + std::to_string(stI + 1),
             SDL_Rect{264, 144, 352, 30}, DIM, F_TINY, A_LEFT);
    const char* badge = ks == KanaState::Weak ? "WEAK SPOT" : ks == KanaState::Known ? "KNOWN" : ks == KanaState::Learning ? "LEARNING"
                      : ks == KanaState::New ? "NEW" : "LOCKED";
    Col bf = ks == KanaState::Weak ? BAD_FILL : ks == KanaState::Known ? ACCENT : PANEL2;
    Col bt = ks == KanaState::Weak ? BAD : ks == KanaState::Known ? ON_ACCENT : FG;
    int bw = textWidth(top, F_MICRO, badge) + 24;
    fillRound(top, SDL_Rect{264, 182, bw, 30}, 15, bf); drawText(top, badge, SDL_Rect{264, 182, bw, 30}, bt, F_MICRO);
    stepPips(top, 264 + bw + 16, 191, prog.step(Progress::keyKana(k)));
    auto it = prog.stats.find(Progress::keyKana(k)); Stat s = it != prog.stats.end() ? it->second : Stat{};
    statBox(top, SDL_Rect{264, 222, 110, 76}, "SEEN", std::to_string(s.seen));
    statBox(top, SDL_Rect{385, 222, 110, 76}, "RIGHT", std::to_string(s.correct));
    statBox(top, SDL_Rect{506, 222, 110, 76}, "MISSED", std::to_string(s.wrong));
    auto h = data.hints.find(k.glyph);
    if (h != data.hints.end()) drawWrapped(top, h->second, SDL_Rect{24, 312, 592, 92}, DIM, F_TINY);
    drawText(top, ks == KanaState::Locked ? "Unlocks with stage " + std::to_string(stI + 1) + "."
                                          : "Tap it again or press A to drill just this kana.", SDL_Rect{24, 418, 592, 34}, DIM, F_MICRO, A_LEFT);
  }
  const char* tabs[3] = {"Basic", "Voiced", "Combos"};
  for (int i = 0; i < 3; i++) { addBtn(SDL_Rect{16 + i * 116, 10, 108, 44}, B_TAB0 + i, tabs[i], F_TINY); if (i == chartTab) accent(lastBtn()); }
  for (int s = 0; s < 2; s++) {
    Script ss = s ? Script::Katakana : Script::Hiragana;
    addBtn(SDL_Rect{516 + s * 56, 10, 52, 44}, s ? B_SCRIPT_K : B_SCRIPT_H, s ? "ア" : "あ", F_TINY, prog.scriptUnlocked(ss));
    if (ss == sc) accent(lastBtn());
  }
  int cols = chartCols(chartTab), gx = 16, gw = 548, cw = (gw - (cols - 1) * 8) / cols, ch = 60;
  for (auto& c : cells) {
    if (c.row < chartScroll || c.row >= chartScroll + VIS) continue;
    SDL_Rect r{gx + c.col * (cw + 8), 66 + (c.row - chartScroll) * (ch + 8), cw, ch};
    KanaState ks = prog.state(data, c.kana);
    addBtn(r, B_CELL0 + c.kana, data.kana[c.kana].glyph, F_TINY); Button& b = lastBtn();
    b.sub = data.kana[c.kana].romaji; b.radius = 10; b.subFont = F_MICRO;
    b.fill = stateFill(ks); b.text = stateText(ks); b.subText = ks == KanaState::Known ? ON_ACCENT : DIM;
    b.line = ks == KanaState::Weak ? BAD : ks == KanaState::Locked ? DIS_LINE : b.fill;
    if (ks == KanaState::Locked) { b.subText = DIS_TXT; b.line = DIS_LINE; }
    if (c.kana == chartSel) { b.line = FG; b.lineW = 4; }
  }
  addBtn(SDL_Rect{572, 66, 52, 160}, B_SCROLL_UP, "▲", F_TINY, chartScroll > 0);
  addBtn(SDL_Rect{572, 236, 52, 160}, B_SCROLL_DOWN, "▼", F_TINY, chartScroll + VIS <= maxRow);
  chips({{"+", "Move"}, {"A", "Drill this"}, {"L/R", "Tab"}, {"Y", "Script"}, {"B", "Back"}});
}

void App::drawSettings() {
  const Settings& st = prog.settings;
  auto value = [&](int row) -> std::string {
    switch (row) {
      case 0: return scriptName(st.script);
      case 1: return REVIEW_NAMES[st.review];
      case 2: return WEIGHT_NAMES[st.reading];
      case 3: return WEIGHT_NAMES[st.meanings];
      case 4: return WEIGHT_NAMES[st.writing];
      case 5: return WEIGHT_NAMES[st.typing];
      case 6: return std::to_string(st.sessionLen) + " questions";
      case 7: return st.light ? "Light" : "Dark";
      default: return "";
    }
  };
  header("Settings");
  for (int i = 0; i < 9; i++) {
    SDL_Rect r{24, 54 + i * 40, 592, 38};
    bool on = i == setSel;
    if (on) fillRound(top, r, 10, ACCENT);
    Col t = on ? ON_ACCENT : FG, v = on ? ON_ACCENT : DIM;
    drawText(top, ROW_NAMES[i], SDL_Rect{r.x + 16, r.y, 330, r.h}, t, F_TINY, A_LEFT);
    std::string val = value(i);
    drawText(top, i == 8 ? ">" : "<  " + val + "  >", SDL_Rect{r.x + 300, r.y, 276, r.h}, v, F_TINY, A_RIGHT);
  }
  drawWrapped(top, ROW_HELP[setSel], SDL_Rect{24, 420, 592, 50}, DIM, F_MICRO);

  drawText(bot, std::string(ROW_NAMES[setSel]), SDL_Rect{16, 8, 608, 26}, DIM, F_MICRO, A_LEFT);
  std::vector<std::string> opts; int curv = 0; std::vector<bool> en;
  switch (setSel) {
    case 0: opts = {"Hiragana", "Katakana"}; curv = si(st.script); en = {true, prog.scriptUnlocked(Script::Katakana)}; break;
    case 1: opts = {"Off", "Some", "Lots"}; curv = st.review; break;
    case 2: case 3: case 4: case 5: opts = {"Off", "Sometimes", "Often"};
      curv = setSel == 2 ? st.reading : setSel == 3 ? st.meanings : setSel == 4 ? st.writing : st.typing; break;
    case 6: opts = {"5", "10", "15", "20"}; for (int i = 0; i < 4; i++) if (SESSION_LENS[i] == st.sessionLen) curv = i; break;
    case 7: opts = {"Dark", "Light"}; curv = st.light ? 1 : 0; break;
    default: break;
  }
  if (setSel == 8) { addBtn(SDL_Rect{16, 36, 608, 68}, B_CREDITS, "Open credits", F_TINY); accent(lastBtn()); }
  else {
    fillRound(bot, SDL_Rect{16, 36, 608, 68}, 12, PANEL);
    int n = int(opts.size()), w = (600 - (n - 1) * 4) / n;
    for (int i = 0; i < n; i++) {
      bool e = en.empty() || en[i];
      addBtn(SDL_Rect{20 + i * (w + 4), 40, w, 60}, B_OPT0 + i, opts[i], F_TINY, e);
      Button& b = lastBtn(); b.radius = 10;
      if (i == curv) accent(b); else { b.fill = PANEL; b.line = PANEL; }
      if (!e) b.icon = 1;
    }
  }
  for (int i = 0; i < 9; i++) {
    SDL_Rect r{16 + (i % 3) * 206, 118 + (i / 3) * 102, 196, 94};
    addBtn(r, B_ROW0 + i, ROW_TILE_KANA[i], F_TINY); Button& b = lastBtn(); b.sub = ROW_TILE_EN[i]; b.subFont = F_MICRO;
    if (i == setSel) { b.fill = PANEL2; b.line = ACCENT; b.text = ACCENT_TXT; }
  }
  chips({{"▲▼", "Choose"}, {"◀▶", "Change"}, {"B", "Back"}});
}

void App::drawCredits() {
  header("Credits");
  int y = 62;
  auto para = [&](const std::string& t, Col c, int font) { y += drawWrapped(top, t, SDL_Rect{24, y, 592, 120}, c, font) + 12; };
  para("Banana Kana: hiragana and katakana practice for BananaOS on the RG DS Plus.", FG, F_TINY);
  para("Stroke data: KanjiVG (c) Ulrich Apel, Creative Commons BY-SA 3.0. kanjivg.tagaini.net", DIM, F_TINY);
  para("Fonts: Zen Maru Gothic and Noto Sans CJK JP (fallback), both under the SIL Open Font License 1.1. "
       "Full licence texts are in data/licenses.", DIM, F_TINY);
  para("Lesson hints and the word list were written for Banana Kana.", DIM, F_TINY);
  addBtn(SDL_Rect{16, 16, 608, 80}, B_BACK, "Back", F_SMALL); accent(lastBtn());
  chips({{"B", "Back"}});
}

}  // namespace kt
