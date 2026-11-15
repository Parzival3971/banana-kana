#include "app.h"
#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kt {

namespace {
const char* WEIGHT_NAMES[3] = {"Off", "Sometimes", "Often"};
const char* REVIEW_NAMES[3] = {"Off", "Some", "Lots"};
const int SESSION_LENS[4] = {5, 10, 15, 20};
int vowelCol(const std::string& romaji) {
  const std::string v = "aiueo";
  size_t p = v.find(romaji.back());
  return p == std::string::npos ? 0 : int(p);
}
std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string o; for (size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; } return o;
}
const int GOALS[4] = {5, 10, 20, 30};

// ---- kana keyboard: gojuon grid, column by column (a, ka, sa ... wa), five rows (a i u e o)
const char* KANA_GRID[10][5] = {
  {"あ", "い", "う", "え", "お"}, {"か", "き", "く", "け", "こ"}, {"さ", "し", "す", "せ", "そ"}, {"た", "ち", "つ", "て", "と"},
  {"な", "に", "ぬ", "ね", "の"}, {"は", "ひ", "ふ", "へ", "ほ"}, {"ま", "み", "む", "め", "も"}, {"や", "", "ゆ", "", "よ"},
  {"ら", "り", "る", "れ", "ろ"}, {"わ", "", "を", "", "ん"}};
std::string utf8(uint32_t c) {
  std::string o;
  if (c < 0x80) o += char(c);
  else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
  else { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
  return o;
}
std::string toKata(const std::string& s) {
  std::string o; for (uint32_t c : utf8Codepoints(s)) o += utf8(c >= 0x3041 && c <= 0x3096 ? c + 0x60 : c); return o;
}
std::string kanaKeyAt(int i, bool kata) {
  if (i < 0 || i >= 50) return "";
  std::string g = KANA_GRID[i / 5][i % 5];
  return kata ? toKata(g) : g;
}
// Dakuten / handakuten / small toggles for the last character typed.
std::string modifyLast(const std::string& t, int kind, bool kata) {  // kind 0 dakuten, 1 handakuten, 2 small
  auto cs = Data::partNames(t); if (cs.empty()) return t;
  std::string& c = cs.back();
  auto cv = [&](const std::string& x) { return kata ? toKata(x) : x; };
  auto done = [&]() { return join(cs, ""); };
  static const char* HA[5] = {"はばぱ", "ひびぴ", "ふぶぷ", "へべぺ", "ほぼぽ"};
  if (kind < 2)
    for (const char* tr : HA) {
      auto p = Data::partNames(tr); for (auto& x : p) x = cv(x);
      if (c == p[0] || c == p[1] || c == p[2]) { c = kind == 0 ? (c == p[1] ? p[0] : p[1]) : (c == p[2] ? p[0] : p[2]); return done(); }
    }
  static const char* DAK[] = {"かが", "きぎ", "くぐ", "けげ", "こご", "さざ", "しじ", "すず", "せぜ", "そぞ",
                              "ただ", "ちぢ", "つづ", "てで", "とど", "うゔ"};
  static const char* SML[] = {"やゃ", "ゆゅ", "よょ", "つっ", "あぁ", "いぃ", "うぅ", "えぇ", "おぉ", "わゎ"};
  if (kind == 1) return t;
  const char* const* tb = kind == 0 ? DAK : SML; int n = kind == 0 ? 16 : 10;
  for (int i = 0; i < n; i++) {
    auto p = Data::partNames(tb[i]); for (auto& x : p) x = cv(x);
    if (c == p[0]) { c = p[1]; return done(); }
    if (c == p[1]) { c = p[0]; return done(); }
  }
  return t;
}
void popChar(std::string& t) { auto cs = Data::partNames(t); if (!cs.empty()) { cs.pop_back(); t = join(cs, ""); } }
bool isKataText(const std::string& s) { auto c = utf8Codepoints(s); return !c.empty() && c[0] >= 0x30A0 && c[0] <= 0x30FF; }
}  // namespace

// ================================================================== flow
void App::go(Screen s) {
  screen = s; pressedId = -1; drawing = false; cur.clear(); paused = false; animStart = 0;
  if (s == Screen::Unlocked) { snd::play(snd::Sfx::Unlock); celebrate(3200, true); }
  if (s == Screen::Result && mode == Mode::Timed && timedNewBest) celebrate(2400, true);
  if (s == Screen::Stages) { mapScript = prog.settings.script; mapSel = prog.currentStage(mapScript); }
  if (s == Screen::Chart) {
    chartScroll = 0; chartSel = -1;
    auto cells = chartCells(prog.settings.script, chartTab);
    if (!cells.empty()) chartSel = cells[0].kana;
  }
  if (s == Screen::Home) homeSel = std::clamp(homeSel, 0, 7);
  if (s == Screen::SettingsGroup) { skipPending[0] = skipCurrent(Script::Hiragana); skipPending[1] = skipCurrent(Script::Katakana); resetArmed = false; setRow = std::clamp(setRow, 0, std::max(0, int(settingRows(setGroup).size()) - 1)); }
}

void App::setScript(Script s) {
  if (!prog.scriptUnlocked(s)) return;
  prog.settings.script = s; save();
}

void App::startSession(int focus) {
  if (focus >= 0) startFocus({focus}); else startMode(Mode::Normal);
}

void App::startFocus(const std::vector<int>& set) {
  focusSet.clear();
  for (int k : set) if (prog.isUnlocked(data, k)) focusSet.push_back(k);
  if (focusSet.empty()) return;
  focusKana = focusSet[0]; startMode(Mode::Focus);
}

void App::openFreeWrite(const std::vector<int>& list) {  // an empty list: draw any kana and have it guessed
  fwList.clear(); for (int k : list) if (k >= 0) fwList.push_back(k);
  fwIdx = 0; strokes.clear(); part = 0; partStrokes.clear(); partOk.clear(); strokeOk.clear(); checked = false; guesses.clear();
  go(Screen::FreeWrite); if (fwGuide == 0 && !fwList.empty()) playStrokes();
}

void App::welcomeChoice(int i) {
  firstRun = false;
  if (i == 1) { prog.skipTo(data, Script::Hiragana, SKIP_ALL); prog.settings.script = Script::Katakana; }
  if (i == 2) { prog.skipTo(data, Script::Hiragana, SKIP_ALL); prog.skipTo(data, Script::Katakana, SKIP_ALL); }
  pendingUnlocks.clear(); save();
  if (i == 3) { setGroup = 2; setRow = 1; go(Screen::SettingsGroup); return; }
  go(Screen::Home);
}

int App::skipCurrent(Script s) const {
  if (prog.knownCount(data, s) >= data.scriptCount(s)) return SKIP_ALL;
  return prog.unlocked[si(s)];
}

void App::confirmSetting(int g, int r) {
  if (g == 2 && r >= 1) {
    int i = r - 1; Script sc = i ? Script::Katakana : Script::Hiragana;
    if (skipPending[i] > skipCurrent(sc)) { prog.skipTo(data, sc, skipPending[i]); pendingUnlocks.clear(); save(); }
    skipPending[0] = skipCurrent(Script::Hiragana); skipPending[1] = skipCurrent(Script::Katakana);
    return;
  }
  changeSetting(g, r, +1);
}

bool App::scoreWriting(const std::string& glyph, bool* countsMatch, size_t* total) {
  auto parts = data.writeParts(glyph); int np = int(parts.size());
  partStrokes.push_back(strokes); partOk.clear();
  bool ok = true, counts = true; size_t tot = 0;
  for (int pi = 0; pi < np && pi < int(partStrokes.size()); pi++) {
    const auto& ss = partStrokes[pi]; const Glyph* pg = parts[pi]; std::vector<int> okv(ss.size(), 0);
    if (ss.size() != pg->strokes.size()) { ok = false; counts = false; }
    for (size_t i = 0; i < ss.size(); i++) {
      bool sOk = i < pg->strokes.size() && scoreStrokeAt(ss[i], pg->strokes[i], prog.settings.strict).ok; okv[i] = sOk; if (!sOk) ok = false;
    }
    partOk.push_back(okv); tot += ss.size();
  }
  partStrokes.pop_back(); strokeOk = partOk.empty() ? std::vector<int>{} : partOk.back();
  if (countsMatch) *countsMatch = counts;
  if (total) *total = tot;
  return ok;
}

bool App::modeAvailable(Mode m) const {
  Script s = prog.settings.script;
  if (m == Mode::Review) return !prog.dueKana(data, s).empty();
  if (m == Mode::Mixup) return !mixupSets(data, prog, s).empty();
  return true;
}

void App::startMode(Mode m) {
  if (!modeAvailable(m)) m = Mode::Normal;
  mode = m; prog.session++;
  if (m != Mode::Focus) focusKana = -1;
  sessionLen = prog.settings.sessionLen;
  if (m == Mode::Review) sessionLen = std::min(sessionLen, int(prog.dueKana(data, prog.settings.script).size()));
  if (m == Mode::Timed) { sessionLen = 0; timedLeft = TIMED_MS; timedLast = SDL_GetTicks(); timedNewBest = false; flash = 0; flashText.clear(); }
  index = 0; score = 0; results.clear(); missed.clear(); newlyKnown.clear(); lastKey.clear(); goalMet = false;
  go(Screen::Drill); nextQuestion(true); save();
}

bool App::tick(Uint32 now) {  // every timer runs every time; true if anything needs a redraw
  bool redraw = false;
  if (osdUntil && now >= osdUntil) { osdUntil = 0; redraw = true; }  // level card gone
  if (confStart && now - confLast >= 33) { confLast = now; redraw = true; }  // confetti at ~30 fps
  if (animStart && !paused && now - animLast >= 33) { animLast = now; redraw = true; }  // stroke-order animation
  if (screen == Screen::Credits && !paused && now - creditsLast >= 80) { creditsLast = now; redraw = true; }
  if (screen == Screen::Drill && mode == Mode::Timed) {
    Uint32 dt = now - timedLast; timedLast = now;
    if (!paused) {
      int before = (timedLeft + 999) / 1000;
      timedLeft -= int(std::min<Uint32>(dt, 1000));  // a long stall (sleep) doesn't eat the clock
      if (timedLeft <= 0) { timedLeft = 0; finishSession(); return true; }
      int after = (timedLeft + 999) / 1000;
      if (after != before && after <= 5) snd::play(snd::Sfx::Tick);  // last five seconds
      if (after != before) redraw = true;
    }
  }
  return redraw;
}

void App::nextQuestion(bool first) {
  if (!first) index++;
  if (mode != Mode::Timed && index >= sessionLen) { finishSession(); return; }
  for (int tries = 0; tries < 6; tries++) {
    if (mode == Mode::Focus) {
      const Settings& st = prog.settings;
      std::vector<std::pair<QType, int>> ts = {{QType::ReadKana, st.reading}, {QType::RecallKana, st.reading},
                                               {QType::Write, st.writing}, {QType::Type, st.typing}};
      std::vector<double> w; for (auto& t : ts) w.push_back(std::max(0, t.second));
      double tot = 0; for (double x : w) tot += x;
      QType t = QType::ReadKana;
      if (tot > 0) { std::discrete_distribution<int> d(w.begin(), w.end()); t = ts[d(rng)].first; }
      // the set itself; a single kana is mixed with its look-alikes and row-mates so the answer isn't always the same
      std::vector<int> pool = focusSet;
      if (focusSet.size() == 1) {
        int k0 = focusSet[0];
        for (const MixSet& m : data.mixSets)
          if (std::count(m.kana.begin(), m.kana.end(), k0))
            for (int o : m.kana) if (o != k0 && prog.isUnlocked(data, o) && !std::count(pool.begin(), pool.end(), o)) pool.push_back(o);
        for (int o : data.stages[si(data.kana[k0].script)][data.stageOfKana[k0]].kana)
          if (o != k0 && prog.isUnlocked(data, o) && !std::count(pool.begin(), pool.end(), o)) pool.push_back(o);
      }
      int target = focusSet[std::uniform_int_distribution<int>(0, int(focusSet.size()) - 1)(rng)];
      if (focusSet.size() == 1 && pool.size() > 1 && std::uniform_int_distribution<int>(0, 1)(rng))
        target = pool[std::uniform_int_distribution<int>(1, int(pool.size()) - 1)(rng)];
      q = makeKanaQuestion(data, prog, rng, target, t);
      if (!q.options.empty() && !q.isWord && !data.kana[target].special && q.prompt == (q.optionsAreKana ? data.kana[target].romaji : data.kana[target].glyph)) {  // wrong options from the set first, so it's about telling these apart
        bool kanaOpts = q.optionsAreKana; std::vector<std::string> opts{q.answer}, cand;
        for (int o : pool) if (o != target) { std::string v = kanaOpts ? data.kana[o].glyph : data.kana[o].romaji; if (v != q.answer) cand.push_back(v); }
        std::shuffle(cand.begin(), cand.end(), rng);
        for (auto& c : cand) if (opts.size() < q.options.size() && !std::count(opts.begin(), opts.end(), c)) opts.push_back(c);
        for (auto& c : q.options) if (opts.size() < q.options.size() && !std::count(opts.begin(), opts.end(), c)) opts.push_back(c);
        std::shuffle(opts.begin(), opts.end(), rng);
        q.options = opts; q.correct = int(std::find(opts.begin(), opts.end(), q.answer) - opts.begin());
      }
      if (q.key == lastKey && tries < 5) continue;
      break;
    }
    if (mode == Mode::Review) q = makeReviewQuestion(data, prog, rng);
    else if (mode == Mode::Mixup) q = makeMixupQuestion(data, prog, rng);
    else if (mode == Mode::Timed) q = makeTimedQuestion(data, prog, rng);
    else q = makeQuestion(data, prog, rng);
    if (q.key != lastKey) break;
  }
  lastKey = q.key; sel = -1; cursor = 0; checked = false; correct = false; typed.clear(); strokes.clear(); cur.clear();
  drawing = false; showHint = false; strokeOk.clear(); feedbackNote.clear(); part = 0; partStrokes.clear(); partOk.clear();
  animStart = 0; keyRow = 0; keyCol = 0;
  if (q.type == QType::Write && q.writeLevel == 0) playStrokes();  // guided: show the order first
}

void App::finishSession() {
  if (mode == Mode::Timed) {
    sessionLen = int(results.size());
    int& best = prog.timedBest[si(prog.settings.script)];
    timedNewBest = score > best; if (timedNewBest) best = score;
  }
  prog.lastScore = score; prog.lastLen = sessionLen; save();
  go(Screen::Result);
}

const Glyph* App::activeGlyph() const {
  if (screen == Screen::Lesson) {
    int k = currentLessonKana(); if (k < 0) return nullptr;
    auto parts = data.writeParts(data.kana[k].glyph);
    return parts.size() == 1 ? parts[0] : nullptr;
  }
  if (screen == Screen::FreeWrite && fwList.empty())  // draw any: the best guess, once checked
    return checked && !guesses.empty() ? data.glyphFor(data.kana[guesses[0].kana].glyph) : nullptr;
  if (screen == Screen::FreeWrite && !fwList.empty()) {
    auto parts = data.writeParts(data.kana[fwList[fwIdx]].glyph);
    return parts.empty() ? nullptr : parts[std::min(part, int(parts.size()) - 1)];
  }
  if (screen == Screen::Drill && q.type == QType::Write) {
    auto parts = data.writeParts(q.answer);
    return parts.empty() ? nullptr : parts[std::min(part, int(parts.size()) - 1)];
  }
  return nullptr;
}

int App::writePartCount() const {
  if (screen == Screen::FreeWrite && fwList.empty()) return 1;
  if (screen == Screen::FreeWrite && !fwList.empty()) return int(data.writeParts(data.kana[fwList[fwIdx]].glyph).size());
  return int(data.writeParts(q.answer).size());
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
      const Glyph* g = activeGlyph(); if (!g || strokes.empty()) return;
      int np = writePartCount();
      if (part < np - 1) { partStrokes.push_back(strokes); strokes.clear(); part++; if (q.writeLevel == 0) playStrokes(); return; }  // on to the next part
      bool counts = true; size_t total = 0;
      ok = scoreWriting(q.answer, &counts, &total);
      said = counts ? "a different shape" : std::to_string(total) + " strokes";
      break; }
    case QType::Type:
      if (typed.empty()) return;
      ok = q.kanaInput ? kanaTypedMatches(data, q, typed) : typedMatches(typed, q.answer); said = typed; break;
    default: if (sel < 0) return; ok = (sel == q.correct); said = q.options[sel]; break;
  }
  int before = q.isWord ? 0 : prog.step(q.key);
  checked = true; correct = ok; results.push_back(ok ? 1 : 0);
  int wlvBefore = prog.writeLevel(q.key);
  if (q.type == QType::Write) prog.recordWrite(q.key, ok, q.writeLevel); else prog.record(q.key, ok);
  int after = q.isWord ? 0 : prog.step(q.key);
  const Kana* k = q.kanaIdx >= 0 ? &data.kana[q.kanaIdx] : nullptr;
  if (ok) {
    score++;
    if (k && before < KNOWN_STEP && after >= KNOWN_STEP) {
      newlyKnown.push_back(k->glyph); feedbackNote = "Known! " + k->glyph + " reached step 4.";
      if (mode != Mode::Timed) celebrate(1800, false);
    }
    else if (k) feedbackNote = after > before ? "Step " + std::to_string(after) + " of 4" : (after >= KNOWN_STEP ? "Known" : "Step " + std::to_string(after) + " of 4 (2 steps per session at most)");
  } else {
    if (k) { auto it = data.hints.find(k->glyph); feedbackNote = it != data.hints.end() ? it->second : ""; }
    missed.push_back({k ? k->glyph : q.prompt, k ? k->romaji : q.answer, said});
    if (k && !q.options.empty() && sel >= 0)  // remember which kana it was mixed up with
      for (size_t i = 0; i < data.kana.size(); i++) {
        const Kana& o = data.kana[i];
        if (o.script != k->script || int(i) == q.kanaIdx) continue;
        if ((q.optionsAreKana ? o.glyph : o.romaji) == said) { prog.addConfusion(q.key, Progress::keyKana(o)); break; }
      }
  }
  if (q.type == QType::Write && q.writeLevel < WRITE_MEMORY) {
    int wa = prog.writeLevel(q.key);
    if (ok) feedbackNote = wa > wlvBefore ? (wa == 1 ? "Guided practice. Next time: just the outline." : "Guided practice. Next time: from memory!")
                                          : "Guided practice: steps count once you write it from memory.";
    else feedbackNote += (feedbackNote.empty() ? "" : " ") + std::string("Guided practice, so no step lost.");
  }
  if (q.mixup) feedbackNote = q.note;
  else if (ok && k && mode == Mode::Review) {
    const Stat& st = prog.stats[q.key];
    if (st.srs > 0 && st.due > prog.now()) {
      int dd = int((st.due - prog.now() + 43200) / 86400);
      feedbackNote = "Next review in " + std::to_string(dd) + (dd == 1 ? " day." : " days.");
    }
  }
  snd::play(!newlyKnown.empty() && newlyKnown.back() == (k ? k->glyph : "") && ok ? snd::Sfx::Known : ok ? snd::Sfx::Right : snd::Sfx::Wrong);
  if (prog.goalJustMet) {
    snd::play(snd::Sfx::Goal);
    if (mode != Mode::Timed) celebrate(2400, true);
    goalMet = true;
    feedbackNote = "Daily goal reached! " + std::to_string(prog.streak) + "-day streak." + (feedbackNote.empty() ? "" : " " + feedbackNote);
  }
  for (auto& u : prog.checkUnlocks(data)) pendingUnlocks.push_back(u);
  save();
  if (mode == Mode::Timed) {  // no feedback screen: flash the result on top and move straight on
    flash = ok ? 1 : 2; flashText = q.prompt + " = " + q.answer;
    nextQuestion();
  }
}

void App::openLesson(Script s, int stage, bool replay) {
  lessonScript = s; lessonStage = std::clamp(stage, 0, STAGE_COUNT - 1); lessonIdx = 0; lessonReplay = replay;
  strokes.clear(); go(Screen::Lesson); playStrokes();
}

void App::finishLesson() {
  if (lessonReplay) { go(Screen::Stages); return; }
  int& done = prog.lessons[si(lessonScript)];
  done = std::max(done, lessonStage + 1);
  prog.settings.script = lessonScript; save();
  startSession();
}

void App::togglePause() {
  if (screen == Screen::Home || screen == Screen::Welcome) return;
  paused = !paused; pauseSel = 0; pressedId = -1; drawing = false; cur.clear();
}

// ================================================================== input
void App::press(int id) {
  Script sc = prog.settings.script;
  snd::play(snd::Sfx::Tap);  // a later sound (right, wrong...) replaces it
  if (id == B_PAUSE) { togglePause(); return; }
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
      else if (id == B_MODE0) startMode(Mode::Review);
      else if (id == B_MODE0 + 1) startMode(Mode::Mixup);
      else if (id == B_MODE0 + 2) startMode(Mode::Timed);
      else if (id == B_MODE0 + 3) go(Screen::Stats);
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
      if (id == B_LESSON_NEXT) { if (lessonIdx < n - 1) { lessonIdx++; strokes.clear(); playStrokes(); } else finishLesson(); }
      else if (id == B_LESSON_PREV) { if (lessonIdx > 0) { lessonIdx--; strokes.clear(); playStrokes(); } }
      else if (id == B_SHOWME) playStrokes();
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
      if (id >= B_TILE0 && id < B_TILE0 + 6 && !checked) { sel = id - B_TILE0; cursor = sel; if (mode == Mode::Timed) check(); }
      else if (id >= B_KEY0 && id < B_KEY0 + 26 && !checked && typed.size() < 24) typed += char('a' + id - B_KEY0);
      else if (id >= B_KANA0 && id < B_KANA0 + 50 && !checked && typed.size() < 48) typed += kanaKeyAt(id - B_KANA0, isKataText(q.answer));
      else if (id >= B_DAKU && id <= B_SMALLK && !checked) typed = modifyLast(typed, id - B_DAKU, isKataText(q.answer));
      else if (id == B_CHOON && !checked && !typed.empty()) typed += "ー";
      else if (id == B_BKSP && !checked && !typed.empty()) popChar(typed);
      else if (id == B_CHECK) check();
      else if (id == B_CONTINUE) nextQuestion();
      else if (id == B_UNDO && !checked && !strokes.empty()) strokes.pop_back();
      else if (id == B_UNDO && !checked && part > 0) { part--; strokes = partStrokes.back(); partStrokes.pop_back(); }
      else if (id == B_CLEAR && !checked) strokes.clear();
      else if (id == B_HINT && !checked) showHint = !showHint;
      else if (id == B_SHOWME && !checked && q.type == QType::Write) { showHint = true; playStrokes(); }
      break;
    case Screen::Result:
      if (id == B_AGAIN) {
        if (!pendingUnlocks.empty()) go(Screen::Unlocked);
        else if (mode == Mode::Focus) startSession(focusKana);
        else startMode(mode);  // same mode again (review falls back to practice once nothing is due)
      }
      else if (id == B_HOME) go(pendingUnlocks.empty() ? Screen::Home : Screen::Unlocked);
      break;
    case Screen::Chart:
      if (id >= B_CELL0) {
        int k = id - B_CELL0;
        if (k == chartSel && prog.isUnlocked(data, k)) {  // tap again: in or out of the practice set
          auto it = std::find(chartPick.begin(), chartPick.end(), k);
          if (it != chartPick.end()) chartPick.erase(it); else chartPick.push_back(k);
        } else chartSel = k;
      } else if (id == B_PICK_DRILL) startFocus(chartPick.empty() ? std::vector<int>{chartSel} : chartPick);
      else if (id == B_PICK_WRITE) openFreeWrite(chartPick.empty() ? std::vector<int>{chartSel} : chartPick);
      else if (id == B_PICK_CLEAR) chartPick.clear();
      else if (id == B_PICK_ANY) openFreeWrite({});
      else if (id >= B_TAB0 && id < B_TAB0 + CHART_TABS) { chartTab = id - B_TAB0; go(Screen::Chart); }
      else if (id == B_SCRIPT_H) { setScript(Script::Hiragana); go(Screen::Chart); }
      else if (id == B_SCRIPT_K) { setScript(Script::Katakana); go(Screen::Chart); }
      else if (id == B_SCROLL_UP) chartScroll = std::max(0, chartScroll - 1);
      else if (id == B_SCROLL_DOWN) chartScroll++;
      else if (id == B_BACK) go(Screen::Home);
      break;
    case Screen::Settings:
      if (id >= B_GROUP0 && id < B_GROUP0 + GROUPS) {
        int g = id - B_GROUP0;
        if (g == setGroup) { setRow = 0; go(Screen::SettingsGroup); } else setGroup = g;  // tap to select, tap again to open
      } else if (id == B_FOOT_L) go(Screen::Home);
      else if (id == B_FOOT_R) { setRow = 0; go(Screen::SettingsGroup); }
      break;
    case Screen::SettingsGroup: {
      int n = int(settingRows(setGroup).size());
      if (id >= B_SROW0 && id < B_SROW0 + n) {
        int r = id - B_SROW0;
        if (r == setRow) confirmSetting(setGroup, r); else { setRow = r; resetArmed = false; go(Screen::SettingsGroup); }
      } else if (id >= B_SDEC0 && id < B_SDEC0 + n) { setRow = id - B_SDEC0; changeSetting(setGroup, setRow, -1); }
      else if (id >= B_SINC0 && id < B_SINC0 + n) { setRow = id - B_SINC0; changeSetting(setGroup, setRow, +1); }
      else if (id == B_FOOT_L) go(Screen::Settings);
      else if (id == B_FOOT_R) confirmSetting(setGroup, setRow);
      break; }
    case Screen::Credits:
      if (id == B_BACK) go(Screen::SettingsGroup);
      break;
    case Screen::FreeWrite: {
      auto reset = [&]() { strokes.clear(); part = 0; partStrokes.clear(); partOk.clear(); strokeOk.clear(); checked = false; guesses.clear(); };
      int n = int(fwList.size());
      if (id == B_FW_GUIDE) fwGuide = (fwGuide + 1) % 3;
      else if (id == B_SHOWME) playStrokes();
      else if (id == B_UNDO && !checked) { if (!strokes.empty()) strokes.pop_back(); else if (part > 0) { part--; strokes = partStrokes.back(); partStrokes.pop_back(); } }
      else if (id == B_CLEAR && !checked) strokes.clear();
      else if (id == B_CHECK) {
        if (checked) reset();
        else if (!strokes.empty() && fwList.empty()) {  // draw any: guess it, then check the order against the guess
          guesses = recognizeKana(data, strokes); checked = true; strokeOk.assign(strokes.size(), 0);
          if (!guesses.empty()) {
            const Glyph* g = data.glyphFor(data.kana[guesses[0].kana].glyph);
            auto fit = fitStrokes(strokes, *g); int okN = 0;
            for (size_t i = 0; i < fit.size(); i++) { strokeOk[i] = i < g->strokes.size() && scoreStrokeAt(fit[i], g->strokes[i], 0).ok; okN += strokeOk[i]; }
            correct = okN == int(g->strokes.size()) && fit.size() == g->strokes.size();
          }
          snd::play(correct ? snd::Sfx::Right : snd::Sfx::Wrong);
        }
        else if (!strokes.empty()) {
          if (part < writePartCount() - 1) { partStrokes.push_back(strokes); strokes.clear(); part++; }
          else { bool counts = true; size_t total = 0; correct = scoreWriting(data.kana[fwList[fwIdx]].glyph, &counts, &total); checked = true;
                 snd::play(correct ? snd::Sfx::Right : snd::Sfx::Wrong);
                 feedbackNote = correct ? "" : counts ? "Right number of strokes; check the red ones." : std::to_string(total) + " strokes drawn."; }
        }
      } else if (id == B_FW_NEXT && n) { fwIdx = (fwIdx + 1) % n; reset(); if (fwGuide == 0) playStrokes(); }
      else if (id == B_FW_PREV && n) { fwIdx = (fwIdx + n - 1) % n; reset(); if (fwGuide == 0) playStrokes(); }
      else if (id == B_BACK) go(Screen::Chart);
      break; }
    case Screen::Welcome:
      if (id >= B_WELCOME0 && id < B_WELCOME0 + 4) welcomeChoice(id - B_WELCOME0);
      break;
    case Screen::Stats:
      if (id >= B_HEAT0) {  // open that kana in the chart
        int k = id - B_HEAT0, st = data.stageOfKana[k];
        chartTab = st < 10 ? 0 : st < 12 ? 1 : st < 14 ? 2 : 3; go(Screen::Chart); chartSel = k;
        for (auto& c : chartCells(prog.settings.script, chartTab)) if (c.kana == k) chartScroll = std::max(0, c.row - 2);
      } else if (id == B_SCRIPT_H) setScript(Script::Hiragana);
      else if (id == B_SCRIPT_K) setScript(Script::Katakana);
      else if (id == B_BACK) go(Screen::Home);
      break;
  }
}

SDL_Rect App::canvasRect() const {
  if (screen == Screen::Lesson) return SDL_Rect{248, 16, 376, 376};
  return SDL_Rect{152, 20, 396, 396};
}

void App::pointer(int type, int x, int y) {
  bool canDraw = !paused && ((screen == Screen::Drill && q.type == QType::Write && !checked) ||
                             (screen == Screen::Lesson && activeGlyph()) || (screen == Screen::FreeWrite && !checked));
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
      if (b == L || b == R) { int row = homeSel / 4, col = (homeSel % 4 + (b == R ? 1 : 3)) % 4; homeSel = row * 4 + col; }
      else if (b == U || b == D) homeSel = (homeSel + 4) % 8;
      else if (b == A || b == ST) press(homeSel < 4 ? B_CART0 + homeSel : B_MODE0 + homeSel - 4);
      else if (b == Y) setScript(sc == Script::Hiragana ? Script::Katakana : Script::Hiragana);
      break;
    case Screen::Stages:
      if (b == L) mapSel = std::max(0, mapSel - 1); else if (b == R) mapSel = std::min(STAGE_COUNT - 1, mapSel + 1);
      else if (b == U) mapSel = std::max(0, mapSel - 8); else if (b == D) mapSel = std::min(STAGE_COUNT - 1, mapSel + 8);
      else if (b == A) press(B_STAGE_ACTION);
      else if (b == LS || b == RS) press(mapScript == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H);
      else if (b == B) press(B_BACK);
      break;
    case Screen::Lesson:
      if (b == A || b == R) press(B_LESSON_NEXT); else if (b == LS || b == L) press(B_LESSON_PREV); else if (b == ST) press(B_SHOWME);
      else if (b == X) press(B_LESSON_CLEAR); else if (b == B) press(B_BACK);
      break;
    case Screen::Unlocked:
      if (b == A) press(B_START_LESSON); else if (b == B) press(B_LATER);
      break;
    case Screen::Drill: {
      bool choice = !q.options.empty(); int n = int(q.options.size());
      if (checked) { if (b == A || b == X) press(B_CONTINUE); break; }
      if (choice) {
        if (n <= 3) { if (b == L) cursor = (cursor + n - 1) % n; else if (b == R) cursor = (cursor + 1) % n; }
        else if (b == L || b == R) cursor ^= 1;
        else if (b == U) cursor = (cursor - 2 + n) % n; else if (b == D) cursor = (cursor + 2) % n;
        if (b == A) { if (mode == Mode::Timed) press(B_TILE0 + cursor); else if (sel == cursor) check(); else sel = cursor; }
        else if (b == X && mode != Mode::Timed) check();
      } else if (q.type == QType::Type) {  // on-screen keyboard: d-pad moves, A types the key
        if (b == L) kbMove(-1, 0); else if (b == R) kbMove(1, 0); else if (b == U) kbMove(0, -1); else if (b == D) kbMove(0, 1);
        else if (b == A) { int id = kbItemId(keyRow, keyCol); if (id >= 0) press(id); }
        else if (b == X) check();
        else if (b == B || b == LS) press(B_BKSP);
      } else {
        if (b == A || b == X) check();
        else if (b == Y) press(B_HINT);
        else if (b == ST) press(B_SHOWME);
        else if (b == B || b == LS) press(q.type == QType::Type ? B_BKSP : B_UNDO);
      }
      break; }
    case Screen::Result:
      if (b == A) press(B_AGAIN); else if (b == B) press(B_HOME);
      break;
    case Screen::Chart: {
      if (b == B) { press(B_BACK); break; }
      if (b == LS || b == RS) { chartTab = (chartTab + (b == RS ? 1 : CHART_TABS - 1)) % CHART_TABS; go(Screen::Chart); break; }
      if (b == Y) { press(sc == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H); break; }
      if (b == A) { if (chartSel >= 0) press(B_CELL0 + chartSel); break; }  // chartSel is selected, so this toggles it in the set
      if (b == X) { press(B_PICK_DRILL); break; }
      if (b == ST) { press(B_PICK_WRITE); break; }
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
        if (best->row >= chartScroll + 4) chartScroll = best->row - 3;
      }
      break; }
    case Screen::Settings:
      if (b == L) setGroup = (setGroup + GROUPS - 1) % GROUPS; else if (b == R) setGroup = (setGroup + 1) % GROUPS;
      else if (b == U || b == D) setGroup = (setGroup + 3) % GROUPS;
      else if (b == A) press(B_FOOT_R); else if (b == B) press(B_FOOT_L);
      break;
    case Screen::SettingsGroup: {
      int n = int(settingRows(setGroup).size());
      if (b == U) { setRow = (setRow + n - 1) % n; go(Screen::SettingsGroup); }
      else if (b == D) { setRow = (setRow + 1) % n; go(Screen::SettingsGroup); }
      else if (b == L) changeSetting(setGroup, setRow, -1);
      else if (b == R) changeSetting(setGroup, setRow, +1);
      else if (b == A) confirmSetting(setGroup, setRow);
      else if (b == B) press(B_FOOT_L);
      else if (b == LS || b == RS) { setGroup = (setGroup + (b == RS ? 1 : GROUPS - 1)) % GROUPS; setRow = 0; go(Screen::SettingsGroup); }
      break; }
    case Screen::Credits:
      if (b == B || b == A) press(B_BACK);
      break;
    case Screen::FreeWrite:
      if (b == A) press(B_CHECK); else if (b == X) press(B_UNDO); else if (b == Y) press(B_FW_GUIDE); else if (b == ST) press(B_SHOWME);
      else if (b == L || b == LS) press(B_FW_PREV); else if (b == R || b == RS) press(B_FW_NEXT); else if (b == B) press(B_BACK);
      break;
    case Screen::Welcome:
      if (b == U) welcomeSel = (welcomeSel + 3) % 4; else if (b == D) welcomeSel = (welcomeSel + 1) % 4;
      else if (b == A || b == ST) press(B_WELCOME0 + welcomeSel);
      break;
    case Screen::Stats:
      if (b == B) press(B_BACK);
      else if (b == Y) press(sc == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H);
      break;
  }
}

void App::key(SDL_Keycode k) {  // PC convenience
  using G = SDL_GameControllerButton;
  switch (k) {
    case SDLK_ESCAPE: if (inPractice()) togglePause(); else padButton(SDL_CONTROLLER_BUTTON_B); break;
    case SDLK_TAB: togglePause(); break;
    case SDLK_RETURN: padButton(SDL_CONTROLLER_BUTTON_A); break;
    case SDLK_LEFT: padButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT); break;
    case SDLK_RIGHT: padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); break;
    case SDLK_UP: padButton(SDL_CONTROLLER_BUTTON_DPAD_UP); break;
    case SDLK_DOWN: padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN); break;
    case SDLK_PAGEUP: padButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); break;
    case SDLK_PAGEDOWN: padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); break;
    case SDLK_BACKSPACE: case SDLK_AC_BACK: padButton(SDL_CONTROLLER_BUTTON_B); break;
    default:
      if (screen == Screen::Drill && q.type == QType::Type && !q.kanaInput && !paused && k >= SDLK_a && k <= SDLK_z) press(B_KEY0 + (k - SDLK_a));
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
    if (k == "+") {  // the d-pad, drawn as a cross
      int cx = x + 3 + kw / 2, cy = 455;
      fillRound(bot, SDL_Rect{cx - 9, cy - 3, 18, 6}, 2, BG); fillRound(bot, SDL_Rect{cx - 3, cy - 9, 6, 18}, 2, BG);
    } else drawText(bot, k, SDL_Rect{x + 3, 441, kw, 28}, BG, F_MICRO);
    drawText(bot, label, SDL_Rect{x + 4 + kw + 8, 438, lw + 4, 34}, FG, F_MICRO, A_LEFT);
    if (label == "Pause" && screen == Screen::Drill && !paused) {  // tap the hint to pause, too (in case F never reaches us)
      addBtn(SDL_Rect{x - 4, 432, w + 8, 46}, B_PAUSE, ""); lastBtn().hidden = true;
    }
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

void App::drawCanvas(SDL_Rect c, const Glyph* g, int guide, bool checkedState) {
  boxRound(bot, SDL_Rect{c.x - 3, c.y - 3, c.w + 6, c.h + 6}, 14, CANVAS, LINE, 3);
  const int gl = prog.settings.guideLines;
  auto dashed = [&](Col col, int x1, int y1, int x2, int y2) {
    SDL_SetRenderDrawColor(bot.r, col.r, col.g, col.b, 255);
    if (x1 == x2) for (int t = y1; t < y2; t += 10) SDL_RenderDrawLine(bot.r, x1, t, x1, std::min(t + 4, y2));
    else for (int t = x1; t < x2; t += 10) SDL_RenderDrawLine(bot.r, t, y1, std::min(t + 4, x2), y1);
  };
  if (gl == 1) for (int q4 : {1, 3}) {  // grid: quarter lines, fainter
    dashed(mix(CANVAS, LINE, 0.55f), c.x + c.w * q4 / 4, c.y + 8, c.x + c.w * q4 / 4, c.y + c.h - 8);
    dashed(mix(CANVAS, LINE, 0.55f), c.x + 8, c.y + c.h * q4 / 4, c.x + c.w - 8, c.y + c.h * q4 / 4);
  }
  if (gl < 2) {
    dashed(LINE, c.x + c.w / 2, c.y + 8, c.x + c.w / 2, c.y + c.h - 8);
    dashed(LINE, c.x + 8, c.y + c.h / 2, c.x + c.w - 8, c.y + c.h / 2);
  }
  const float INK[3] = {0.011f, 0.016f, 0.022f};
  float big = c.w * 0.024f, ink = c.w * INK[std::clamp(prog.settings.ink, 0, 2)];
  if (g && guide) {
    Col gc = guide == 1 ? mix(CANVAS, GHOST, 0.45f) : checkedState ? GHOST_OK : GHOST;
    for (auto& s : g->strokes) drawStrokeIn(bot, s, c, gc, guide == 1 ? big * 0.8f : big);
  }
  if (g && guide == 2 && !checkedState) {  // which way each stroke goes: an arrowhead part-way along
    Col ac = mix(GHOST, FG, 0.45f);
    for (auto& st : g->strokes) {
      if (st.size() < 2) continue;
      Stroke rs = resample(st, 20);
      SDL_Point a = glyphToBox(rs[10], c), b = glyphToBox(rs[12], c);
      float dx = float(b.x - a.x), dy = float(b.y - a.y), len = std::hypot(dx, dy);
      if (len < 1) continue;
      dx /= len; dy /= len; float sz = big * 1.5f;
      SDL_Color vc{ac.r, ac.g, ac.b, 255};
      SDL_Vertex v[3] = {{{b.x + dx * sz * 0.6f, b.y + dy * sz * 0.6f}, vc, {0, 0}},
                         {{b.x - dx * sz * 0.6f - dy * sz * 0.55f, b.y - dy * sz * 0.6f + dx * sz * 0.55f}, vc, {0, 0}},
                         {{b.x - dx * sz * 0.6f + dy * sz * 0.55f, b.y - dy * sz * 0.6f - dx * sz * 0.55f}, vc, {0, 0}}};
      SDL_RenderGeometry(bot.r, nullptr, v, 3, nullptr, 0);
    }
  }
  if (g && animStart) {  // stroke-order animation: each stroke drawn in turn, a dot leading the pen
    const Uint32 PER = 650; Uint32 e = SDL_GetTicks() - animStart; int n = int(g->strokes.size());
    for (int i = 0; i < n; i++) {
      Uint32 t0 = Uint32(i) * PER; if (e < t0) break;
      float f = std::min(1.f, float(e - t0) / (PER * 0.8f));
      Stroke rs = resample(g->strokes[i], 40); int k = std::max(2, int(f * 40));
      Stroke partS(rs.begin(), rs.begin() + std::min(k, int(rs.size())));
      drawStrokeIn(bot, partS, c, ACCENT_TXT, big * 0.9f);
      if (f < 1.f) { SDL_Point hp = glyphToBox(partS.back(), c); fillCircle(bot, hp.x, hp.y, int(big * 0.9f), ACCENT); }
    }
    if (e > Uint32(n) * PER + 700) animStart = 0;
  }
  for (size_t i = 0; i < strokes.size(); i++) {
    Col col = checkedState ? (i < strokeOk.size() && strokeOk[i] ? GOOD : BAD) : FG;
    drawStrokeIn(bot, strokes[i], c, col, ink);
  }
  if (drawing) drawStrokeIn(bot, cur, c, ACCENT_TXT, ink);
  if (g && guide == 2) drawStrokeNumbers(bot, *g, c, int(strokes.size()), int(g->strokes.size()));
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
    case Screen::SettingsGroup: drawSettingsGroup(); break;
    case Screen::Credits: drawCredits(); break;
    case Screen::Stats: drawStats(); break;
    case Screen::Welcome: drawWelcome(); break;
    case Screen::FreeWrite: drawFreeWrite(); break;
  }
  drawButtons();
  drawConfetti();
  drawOsd();
  SDL_RenderPresent(top.r); SDL_RenderPresent(bot.r);
}

void App::celebrate(Uint32 ms, bool both) {
  confStart = SDL_GetTicks(); if (!confStart) confStart = 1;
  confDur = ms; confBoth = both;
}

void App::drawConfetti() {  // falling, fluttering paper; lands on top of whatever the screen shows
  if (!confStart) return;
  float t = (SDL_GetTicks() - confStart) / 1000.f, dur = confDur / 1000.f;
  if (t > dur) { confStart = 0; return; }
  const Col cols[6] = {ACCENT, GOOD, BAD, FG, mix(ACCENT, BAD, 0.5f), Col{110, 170, 255}};
  auto rain = [&](Win& w, unsigned seed, float delay) {
    float tt = t - delay; if (tt < 0) return;
    int n = 80;
    for (int i = 0; i < n; i++) {
      unsigned h = (2654435761u * unsigned(i + 1)) ^ (seed * 40503u);
      float x0 = float(h % unsigned(LW)), y0 = -20.f - float((h >> 9) % 260u);
      float v = 140.f + float((h >> 3) % 160u), sway = 18.f + float((h >> 13) % 30u);
      float x = x0 + sway * std::sin(tt * (2.f + (h % 3)) + i), y = y0 + v * tt + 90.f * tt * tt;
      if (y < -12 || y > 490) continue;
      float flip = std::fabs(std::sin(tt * (5.f + (h >> 7) % 5) + i));  // paper turning over
      int wdt = 6 + int((h >> 5) % 5), hgt = std::max(2, int((7 + (h >> 11) % 6) * flip));
      float fade = t > dur - 0.5f ? (dur - t) / 0.5f : 1.f;
      fillRect(w, SDL_Rect{int(x), int(y), wdt, hgt}, mix(BG, cols[h % 6], std::clamp(fade, 0.f, 1.f)));
    }
  };
  rain(top, 1, 0.f);
  if (confBoth) rain(bot, 7, 0.35f);  // the bottom screen catches the confetti falling from the top
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
      int total = data.scriptCount(ss);
      drawText(top, std::to_string(k) + " / " + std::to_string(total), SDL_Rect{x + 190, 278, 98, 26}, DIM, F_MICRO, A_RIGHT);
      bar(top, SDL_Rect{x, 310, 288, 12}, float(k) / total);
    } else {
      iconLock(top, x + 228, 284, 2, DIM);
      drawText(top, "Locked", SDL_Rect{x + 240, 278, 48, 26}, DIM, F_MICRO, A_RIGHT);
      fillRound(top, SDL_Rect{x, 310, 288, 12}, 6, TRACK);
    }
  }
  // stats: today's goal, streak, reviews due, next unlock
  int goal = prog.settings.dailyGoal, todayN = prog.today == dayNumber(prog.now()) ? prog.todayCount : 0;
  int streak = prog.currentStreak(), due = int(prog.dueKana(data, sc).size());
  std::string next = curSt + 1 < STAGE_COUNT ? stages[curSt + 1].name : "-";
  statBox(top, SDL_Rect{24, 340, 140, 120}, "TODAY", std::to_string(todayN) + " / " + std::to_string(goal));
  bar(top, SDL_Rect{38, 436, 112, 10}, float(todayN) / goal);
  statBox(top, SDL_Rect{174, 340, 140, 120}, "STREAK", std::to_string(streak) + (streak == 1 ? " day" : " days"));
  statBox(top, SDL_Rect{324, 340, 140, 120}, "REVIEWS DUE", std::to_string(due));
  statBox(top, SDL_Rect{474, 340, 140, 120}, "NEXT UNLOCK", next, F_TINY);

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
  // more ways to practise
  int mixN = int(mixupSets(data, prog, sc).size()), best = prog.timedBest[scI];
  const std::string ml[4] = {"Review", "Mix-ups", "Timed", "Stats"};
  const std::string ms[4] = {due ? std::to_string(due) + " due" : "none due", mixN ? std::to_string(mixN) + (mixN == 1 ? " set" : " sets") : "none yet",
                             best ? "best " + std::to_string(best) : "60 seconds", streak ? std::to_string(streak) + "-day streak" : "your progress"};
  const Mode mm[4] = {Mode::Review, Mode::Mixup, Mode::Timed, Mode::Normal};
  for (int i = 0; i < 4; i++) {
    addBtn(SDL_Rect{16 + i * 152, 338, 146, 88}, B_MODE0 + i, ml[i], F_TINY, modeAvailable(mm[i])); Button& b = lastBtn();
    b.sub = ms[i]; if (i == 0 && due) b.subText = ACCENT_TXT;
    if (homeSel == 4 + i) { b.line = ACCENT; b.lineW = 4; }
  }
  chips({{"A", homeSel == 0 && lesson ? "Lesson" : "Start"}, {"+", "Choose"}, {"Y", "Script"}, {"F+START", "Quit"}});
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
  drawText(bot, "Basic 1-10 · Voiced 11-12 · Combos 13-14 · Extras 15", SDL_Rect{318, 10, 306, 46}, DIM, F_MICRO, A_RIGHT);
  for (int i = 0; i < STAGE_COUNT; i++) {
    SDL_Rect r{16 + (i % 8) * 76, 70 + (i / 8) * 106, 70, 100};
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
                     : "Say it out loud: " + k.glyph + " is \"" + k.romaji + "\". In writing drills you trace it in two parts, the small kana full size.",
              SDL_Rect{16, 56, 220, 120}, DIM, F_MICRO);
  addBtn(SDL_Rect{16, 184, 106, 58}, B_LESSON_CLEAR, "Clear", F_TINY, g && !strokes.empty());
  addBtn(SDL_Rect{130, 184, 106, 58}, B_SHOWME, "Show me", F_TINY, g != nullptr);
  addBtn(SDL_Rect{16, 250, 220, 58}, B_LESSON_PREV, "Previous", F_TINY, lessonIdx > 0);
  addBtn(SDL_Rect{16, 316, 220, 76}, B_LESSON_NEXT, lessonIdx < n - 1 ? "Next kana" : (lessonReplay ? "Done" : "Start practice"), F_TINY);
  accent(lastBtn());
  if (g) drawCanvas(canvasRect(), g, 2, false);
  else {
    SDL_Rect c = canvasRect(); fillRound(bot, c, 14, PANEL);
    drawText(bot, k.glyph, SDL_Rect{c.x, c.y + 40, c.w, 220}, FG, F_HUGE);
    drawText(bot, k.romaji, SDL_Rect{c.x, c.y + 270, c.w, 70}, ACCENT_TXT, F_MID);
  }
  chips({{"A", lessonIdx < n - 1 ? "Next" : "Finish"}, {"L", "Previous"}, {"X", "Clear"}, {"START", "Show me"}, {"B", "Later"}});
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
          std::to_string(prog.knownCount(data, s)) + " / " + std::to_string(data.scriptCount(s)));
  statBox(bot, SDL_Rect{328, 246, 296, 84}, "STAGES", std::to_string(prog.unlocked[si(s)]) + " of " + std::to_string(STAGE_COUNT));
  addBtn(SDL_Rect{16, 346, 180, 76}, B_LATER, "Later", F_SMALL);
  addBtn(SDL_Rect{212, 346, 412, 76}, B_START_LESSON, "Start lesson", F_SMALL); accent(lastBtn());
  chips({{"A", "Start lesson"}, {"B", "Later"}});
}

void App::drawDrill() {
  const Kana* k = q.kanaIdx >= 0 ? &data.kana[q.kanaIdx] : nullptr;
  const bool timed = mode == Mode::Timed;
  std::string fs; for (size_t i = 0; i < focusSet.size() && i < 3; i++) fs += (i ? " " : "") + data.kana[focusSet[i]].glyph;
  if (focusSet.size() > 3) fs += " +" + std::to_string(focusSet.size() - 3);
  std::string where = mode == Mode::Focus ? "Practice set " + fs : mode == Mode::Review ? "Review"
                    : mode == Mode::Mixup ? "Mix-ups" : "Stage " + std::to_string(prog.currentStage(prog.settings.script) + 1);
  if (timed) {
    header("Timed challenge · score " + std::to_string(score));
    bar(top, SDL_Rect{24, 56, 592, 12}, float(timedLeft) / TIMED_MS);
    drawText(top, std::to_string((timedLeft + 999) / 1000) + " s", SDL_Rect{24, 76, 160, 38}, timedLeft < 10000 ? BAD : ACCENT_TXT, F_SMALL, A_LEFT);
    drawText(top, q.hint, SDL_Rect{184, 78, 432, 36}, DIM, F_TINY, A_RIGHT);
  } else {
    header(where + " · " + std::to_string(std::min(index + 1, sessionLen)) + " / " + std::to_string(sessionLen));
    segBar(top, SDL_Rect{24, 56, 592, 12}, sessionLen, index, results);
    drawText(top, q.hint, SDL_Rect{24, 78, 592, 36}, DIM, F_TINY);
  }
  Col pc = checked ? (correct ? GOOD : BAD) : FG;
  auto meaningLine = [&](int y) {  // a word's English meaning, under the prompt
    if (q.meaning.empty()) return;
    int w = std::min(560, textWidth(top, F_SMALL, q.meaning) + 60);
    SDL_Rect r{(LW - w) / 2, y, w, 46}; fillRound(top, r, 23, PANEL);
    drawText(top, q.meaning, r, ACCENT_TXT, F_SMALL);
  };
  if (q.type == QType::Type) {
    drawText(top, q.prompt, SDL_Rect{24, 104, 592, q.meaning.empty() ? 188 : 140}, pc, F_HUGE);
    meaningLine(246);
    SDL_Rect f{140, 298, 360, 72}; fillRound(top, f, 12, PANEL); fillRect(top, SDL_Rect{f.x + 10, f.y + f.h - 6, f.w - 20, 4}, ACCENT);
    std::string t = typed + (checked ? "" : "_");
    drawText(top, typed.empty() && checked ? "-" : t, SDL_Rect{f.x + 10, f.y, f.w - 20, f.h - 6}, checked ? pc : ACCENT_TXT, F_MID);
  } else if (q.type == QType::Write) {
    const Glyph* g = activeGlyph(); int m = g ? int(g->strokes.size()) : 0, np = writePartCount();
    auto names = Data::partNames(q.answer);
    drawText(top, checked ? q.answer : q.prompt, SDL_Rect{24, 104, 300, 230}, pc, F_HUGE);
    std::string what = q.writeLevel == 0 ? "GUIDED" : q.writeLevel == 1 ? "OUTLINE" : "FROM MEMORY";
    drawText(top, np > 1 ? "PART " + std::to_string(part + 1) + " OF " + std::to_string(np) + " · " + names[std::min(part, np - 1)] + " · " + what
                         : what, SDL_Rect{340, 120, 276, 22}, DIM, F_MICRO, A_LEFT);
    drawText(top, checked ? std::to_string(m) + " strokes" : "Stroke " + std::to_string(std::min(int(strokes.size()) + 1, m)) + " of " + std::to_string(m),
             SDL_Rect{340, 146, 276, 40}, FG, F_SMALL, A_LEFT);
    for (int i = 0; i < m; i++) {
      SDL_Rect r{340 + i * 30, 196, 22, 22};
      if (checked) fillRound(top, r, 4, i < int(strokeOk.size()) && strokeOk[i] ? GOOD : BAD);
      else if (i < int(strokes.size())) fillRound(top, r, 4, ACCENT);
      else if (i == int(strokes.size())) boxRound(top, r, 4, BG, ACCENT, 3);
      else fillRound(top, r, 4, TRACK);
    }
    std::string help = q.writeLevel == 0 ? "Trace over the grey guide, following the numbers. Steps count once you write it from memory."
                     : showHint ? "Hint is on: the grey guide shows the shape and stroke order."
                     : q.writeLevel == 1 ? "Just the outline now, no numbers. Press Y for the full guide." : "Press Y or tap Hint to show a guide.";
    if (np > 1 && !checked)
      help = part == 0 ? "A combination: trace " + names[0] + " first, then the small " + names[1] + ". " + help
                       : "Now the small " + names[1] + ", traced full size. B goes back to " + names[0] + ".";
    if (np > 1 && checked) {
      std::vector<std::string> rs;
      for (int i = 0; i < np && i < int(partOk.size()); i++) {
        bool pOK = !partOk[i].empty() && std::all_of(partOk[i].begin(), partOk[i].end(), [](int x) { return x; }) &&
                   partOk[i].size() == data.writeParts(q.answer)[i]->strokes.size();
        rs.push_back(names[i] + (pOK ? " ✓" : " ✗"));
      }
      help = "Parts: " + join(rs, "  ");
    }
    drawWrapped(top, help, SDL_Rect{340, 232, 276, 120}, DIM, F_MICRO);
  } else {
    drawText(top, q.prompt, SDL_Rect{24, 112, 592, q.meaning.empty() ? 250 : 190}, pc, F_HUGE);
    meaningLine(306);
  }
  if (timed && flash) {
    SDL_Rect f{24, 392, 592, 64}; fillRound(top, f, 12, flash == 1 ? GOOD_FILL : BAD_FILL);
    if (flash == 1) iconCheck(top, 40, 408, 4, GOOD); else iconCross(top, 42, 406, 4, BAD);
    drawText(top, (flash == 1 ? "Right · " : "Missed · ") + flashText, SDL_Rect{84, 392, 516, 64}, FG, F_TINY, A_LEFT);
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
    int n = int(q.options.size()), cols = n <= 3 ? n : 2, rows = (n + cols - 1) / cols;
    int areaH = timed ? 400 : 314, w = (608 - (cols - 1) * 10) / cols, h = (areaH - (rows - 1) * 10) / rows;
    for (int i = 0; i < n; i++) {
      SDL_Rect r{16 + (i % cols) * (w + 10), 16 + (i / cols) * (h + 10), w, h};
      addBtn(r, B_TILE0 + i, q.options[i], F_MID); Button& b = lastBtn();
      if (checked) {
        if (i == q.correct) { b.fill = GOOD_FILL; b.line = GOOD; b.icon = 2; b.text = FG; }
        else if (i == sel) { b.fill = BAD_FILL; b.line = BAD; }
      } else if (i == sel) accent(b);
      else if (i == cursor) { b.line = DIM; }
      if (q.mixup && q.optionsAreKana) b.font = F_BIG;
    }
    if (timed) { chips({{"A", "Answer"}, {"+", "Choose"}, {"F", "Pause"}}); return; }
    if (checked) { addBtn(SDL_Rect{16, 340, 608, 76}, B_CONTINUE, "Continue", F_SMALL); accent(lastBtn()); }
    else { addBtn(SDL_Rect{16, 340, 608, 76}, B_CHECK, "Check answer", F_SMALL, sel >= 0); if (sel >= 0) accent(lastBtn()); }
    chips({{"A", "Choose"}, {"X", "Check"}, {"F", "Pause"}});
  } else if (q.type == QType::Type && q.kanaInput) {
    drawKanaKeyboard();
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
    if (!checked) { int cid = kbItemId(keyRow, keyCol); for (auto& b : buttons) if (b.id == cid) { b.line = FG; b.lineW = 4; } }
    chips({{"A", "Type"}, {"X", "Check"}, {"B", "Delete"}, {"F", "Pause"}, {"+", "Move"}});
  } else {
    addBtn(SDL_Rect{16, 16, 120, 74}, B_UNDO, part > 0 && strokes.empty() ? "Back" : "Undo", F_TINY, !checked && (!strokes.empty() || part > 0));
    addBtn(SDL_Rect{16, 98, 120, 74}, B_CLEAR, "Clear", F_TINY, !checked && !strokes.empty());
    addBtn(SDL_Rect{16, 180, 120, 74}, B_HINT, q.writeLevel == 0 ? "Guide on" : showHint ? "Hint on" : "Hint", F_TINY, !checked && q.writeLevel > 0);
    if (showHint) { lastBtn().line = ACCENT; lastBtn().text = ACCENT_TXT; }
    addBtn(SDL_Rect{16, 262, 120, 74}, B_SHOWME, "Show me", F_TINY, !checked);
    if (checked) { addBtn(SDL_Rect{16, 344, 120, 74}, B_CONTINUE, "Next", F_TINY); accent(lastBtn()); }
    else { addBtn(SDL_Rect{16, 344, 120, 74}, B_CHECK, "Check", F_TINY, !strokes.empty()); if (!strokes.empty()) accent(lastBtn()); }
    drawCanvas(canvasRect(), activeGlyph(), checked || showHint || q.writeLevel == 0 ? 2 : q.writeLevel == 1 ? 1 : 0, checked);
    chips({{"X", "Check"}, {"Y", "Hint"}, {"START", "Show me"}, {"B", "Undo"}, {"F", "Pause"}});
  }
}

void App::drawWelcome() {
  iconBanana(top, 290, 40, 6);
  drawText(top, "Welcome to Banana Kana", SDL_Rect{20, 112, 600, 60}, FG, F_MID);
  drawWrapped(top, "Learn hiragana, then katakana, a few kana at a time: a short lesson, then drills. "
                   "Already know some? Skip ahead. Kana you skip count as known, with a quick review soon to check.",
              SDL_Rect{60, 190, 520, 140}, DIM, F_TINY);
  drawText(top, "You can change this later in Settings > Script.", SDL_Rect{20, 400, 600, 30}, DIM, F_MICRO);
  const char* l[4] = {"New to kana", "I know hiragana", "I know both", "I know some"};
  const char* sub[4] = {"start at \u3042 \u3044 \u3046 \u3048 \u304a", "all 105, start on katakana", "straight to reviews", "choose the stages in Settings"};
  for (int i = 0; i < 4; i++) {
    addBtn(SDL_Rect{16, 16 + i * 102, 608, 94}, B_WELCOME0 + i, l[i], F_SMALL); lastBtn().sub = sub[i];
    if (i == welcomeSel) accent(lastBtn());
  }
  chips({{"+", "Choose"}, {"A", "Start"}});
}

void App::drawFreeWrite() {
  if (fwList.empty()) {  // draw any kana: it guesses what you drew
    header("Draw any kana");
    const Glyph* g = activeGlyph();
    if (!checked) {
      drawText(top, "Write any kana from memory", SDL_Rect{24, 70, 592, 50}, FG, F_SMALL, A_LEFT);
      drawWrapped(top, "Hiragana or katakana, in the right stroke order. Then tap \"What is it?\": Banana Kana guesses what you "
                       "drew and checks your strokes against it. Nothing here changes your progress.", SDL_Rect{24, 130, 592, 140}, DIM, F_TINY);
    } else if (guesses.empty()) {
      drawText(top, "Couldn't match that. Try again?", SDL_Rect{24, 70, 592, 60}, FG, F_SMALL, A_LEFT);
    } else {
      const Kana& k = data.kana[guesses[0].kana]; float sc = guesses[0].score;
      SDL_Rect card{24, 58, 200, 200}; fillRound(top, card, 16, PANEL);
      drawText(top, k.glyph, card, sc < 24 ? FG : DIM, F_HUGE);
      drawText(top, sc < 14 ? "Looks like" : sc < 24 ? "Probably" : "Not sure. Closest:", SDL_Rect{244, 62, 372, 30}, DIM, F_TINY, A_LEFT);
      drawText(top, k.romaji + " · " + scriptName(k.script), SDL_Rect{244, 92, 372, 70}, ACCENT_TXT, F_MID, A_LEFT);
      int okN = 0; for (int x : strokeOk) okN += x;
      std::string res = g && strokes.size() == g->strokes.size()
          ? (okN == int(strokes.size()) ? "All " + std::to_string(okN) + " strokes in the right order and direction."
                                        : std::to_string(int(strokes.size()) - okN) + " of " + std::to_string(strokes.size()) + " strokes are off (red). Show me plays the order.")
          : "It has " + std::to_string(g ? g->strokes.size() : 0) + " strokes; you drew " + std::to_string(strokes.size()) + ".";
      drawWrapped(top, res, SDL_Rect{244, 168, 372, 90}, correct ? GOOD : FG, F_TINY);
      std::vector<std::string> alt;
      for (size_t i = 1; i < guesses.size(); i++) alt.push_back(data.kana[guesses[i].kana].glyph + " " + data.kana[guesses[i].kana].romaji);
      if (!alt.empty()) drawText(top, "Or maybe: " + join(alt, " · "), SDL_Rect{24, 280, 592, 34}, DIM, F_TINY, A_LEFT);
    }
    addBtn(SDL_Rect{16, 16, 120, 62}, B_SHOWME, "Show me", F_TINY, g != nullptr);
    addBtn(SDL_Rect{16, 84, 120, 62}, B_UNDO, "Undo", F_TINY, !checked && !strokes.empty());
    addBtn(SDL_Rect{16, 152, 120, 62}, B_CLEAR, "Clear", F_TINY, !checked && !strokes.empty());
    addBtn(SDL_Rect{16, 220, 120, 130}, B_CHECK, checked ? "Again" : "What is it?", F_TINY, checked || !strokes.empty());
    if (checked || !strokes.empty()) accent(lastBtn());
    addBtn(SDL_Rect{16, 356, 120, 62}, B_BACK, "Back", F_TINY);
    drawCanvas(canvasRect(), g, checked ? 2 : 0, checked);
    chips({{"A", checked ? "Again" : "What is it?"}, {"X", "Undo"}, {"START", "Show me"}, {"B", "Back"}});
    return;
  }
  const Kana& k = data.kana[fwList[fwIdx]]; const Glyph* g = activeGlyph();
  int np = writePartCount(); auto names = Data::partNames(k.glyph);
  header("Free writing · " + std::to_string(fwIdx + 1) + " / " + std::to_string(fwList.size()));
  SDL_Rect card{24, 58, 200, 200}; fillRound(top, card, 16, PANEL);
  drawText(top, k.glyph, card, checked ? (correct ? GOOD : BAD) : FG, F_HUGE);
  drawText(top, k.romaji, SDL_Rect{244, 58, 372, 80}, ACCENT_TXT, F_BIG, A_LEFT);
  std::string info = np > 1 ? "Part " + std::to_string(part + 1) + " of 2: " + names[std::min(part, np - 1)] + (part ? ", traced full size" : "")
                            : std::to_string(g ? g->strokes.size() : 0) + " strokes";
  drawText(top, info, SDL_Rect{244, 140, 372, 32}, FG, F_TINY, A_LEFT);
  std::string msg = checked ? (correct ? "Looks right!" : "Not quite. " + feedbackNote)
                            : "Just practice: nothing here changes your progress. Y switches the guide.";
  drawWrapped(top, msg, SDL_Rect{244, 180, 372, 80}, checked ? (correct ? GOOD : BAD) : DIM, F_TINY);
  if (g) {
    int m = int(g->strokes.size());
    drawText(top, "STROKE ORDER", SDL_Rect{24, 280, 400, 22}, DIM, F_MICRO, A_LEFT);
    int fs = std::min(110, (592 - (m - 1) * 10) / m);
    for (int i = 0; i < m; i++) {
      SDL_Rect f{24 + i * (fs + 10), 310, fs, fs}; fillRound(top, f, 10, PANEL);
      SDL_Rect gbox{f.x + 6, f.y + 6, f.w - 12, f.h - 12};
      for (auto& st : g->strokes) drawStrokeIn(top, st, gbox, GHOST, fs * 0.05f);
      for (int j = 0; j <= i; j++) drawStrokeIn(top, g->strokes[j], gbox, j == i ? ACCENT_TXT : FG, fs * 0.045f);
      if (!g->strokes[i].empty()) { SDL_Point pt = glyphToBox(g->strokes[i][0], gbox); fillCircle(top, pt.x, pt.y, std::max(4, fs / 18), ACCENT); }
    }
  }
  const char* gl[3] = {"Guide", "Outline", "No guide"};
  addBtn(SDL_Rect{16, 16, 120, 62}, B_FW_GUIDE, gl[fwGuide], F_TINY);
  addBtn(SDL_Rect{16, 84, 120, 62}, B_SHOWME, "Show me", F_TINY);
  addBtn(SDL_Rect{16, 152, 120, 62}, B_UNDO, part > 0 && strokes.empty() ? "Back" : "Undo", F_TINY, !checked && (!strokes.empty() || part > 0));
  addBtn(SDL_Rect{16, 220, 120, 62}, B_CLEAR, "Clear", F_TINY, !checked && !strokes.empty());
  addBtn(SDL_Rect{16, 288, 120, 62}, B_CHECK, checked ? "Again" : (part < np - 1 ? "Next part" : "Check"), F_TINY, checked || !strokes.empty());
  if (checked || !strokes.empty()) accent(lastBtn());
  addBtn(SDL_Rect{16, 356, 120, 62}, B_FW_NEXT, fwList.size() > 1 ? "Next kana" : "Back", F_TINY);
  if (fwList.size() <= 1) lastBtn().id = B_BACK;
  drawCanvas(canvasRect(), g, checked ? 2 : fwGuide == 0 ? 2 : fwGuide == 1 ? 1 : 0, checked);
  chips({{"A", checked ? "Again" : "Check"}, {"Y", "Guide"}, {"START", "Show me"}, {"L/R", "Kana"}, {"B", "Back"}});
}

int App::kbRows() const { return q.kanaInput ? 7 : 4; }
int App::kbRowLen(int r) const {
  if (q.kanaInput) return r < 5 ? 10 : r == 5 ? 5 : 1;
  const int L[4] = {10, 9, 7, 2}; return L[std::clamp(r, 0, 3)];
}
int App::kbItemId(int r, int c) const {
  if (c < 0 || c >= kbRowLen(r)) return -1;
  if (q.kanaInput) {
    if (r < 5) return kanaKeyAt(c * 5 + r, false).empty() ? -1 : B_KANA0 + c * 5 + r;
    if (r == 5) { const int ids[5] = {B_DAKU, B_HANDAKU, B_SMALLK, B_CHOON, B_BKSP}; return ids[c]; }
    return B_CHECK;
  }
  const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  if (r < 3) return B_KEY0 + (rows[r][c] - 'a');
  return c == 0 ? B_BKSP : B_CHECK;
}
void App::kbMove(int dc, int dr) {
  int r = keyRow, c = keyCol;
  if (dr) {
    r = std::clamp(r + dr, 0, kbRows() - 1); c = std::min(c, kbRowLen(r) - 1);
    for (int d = 0; d < 10 && kbItemId(r, c) < 0; d++) c = (c + 1) % kbRowLen(r);  // skip gaps (the ya and wa columns)
  } else {
    int n = kbRowLen(r);
    do { c = (c + dc + n) % n; } while (kbItemId(r, c) < 0);
  }
  keyRow = r; keyCol = c;
}

void App::drawKanaKeyboard() {
  bool kata = isKataText(q.answer);
  for (int i = 0; i < 50; i++) {
    std::string g = kanaKeyAt(i, kata); if (g.empty()) continue;
    addBtn(SDL_Rect{17 + (i / 5) * 61, 12 + (i % 5) * 54, 57, 50}, B_KANA0 + i, g, F_TINY, !checked);
    lastBtn().radius = 10;
  }
  const char* mg[4] = {"が", "ぱ", "ゃ", "ー"}; const char* ms[4] = {"dakuten", "handakuten", "small", "long"};
  for (int i = 0; i < 4; i++) {
    addBtn(SDL_Rect{16 + i * 108, 286, 100, 62}, B_DAKU + i, kata ? toKata(mg[i]) : mg[i], F_TINY, !checked && !typed.empty());
    lastBtn().sub = ms[i];
  }
  addBtn(SDL_Rect{448, 286, 176, 62}, B_BKSP, "Delete", F_TINY, !checked && !typed.empty());
  if (checked) { addBtn(SDL_Rect{16, 356, 608, 72}, B_CONTINUE, "Continue", F_SMALL); accent(lastBtn()); }
  else { addBtn(SDL_Rect{16, 356, 608, 72}, B_CHECK, "Check answer", F_SMALL, !typed.empty()); if (!typed.empty()) accent(lastBtn()); }
  if (!checked) { int cid = kbItemId(keyRow, keyCol); for (auto& b : buttons) if (b.id == cid) { b.line = FG; b.lineW = 4; } }
  chips({{"A", "Type"}, {"X", "Check"}, {"B", "Delete"}, {"F", "Pause"}, {"+", "Move"}});
}

void App::drawStats() {
  Script sc = prog.settings.script; int scI = si(sc);
  header(std::string("Stats · ") + scriptName(sc));
  int tot = 0, cor = 0; for (auto& [k, st] : prog.stats) { tot += st.seen; cor += st.correct; }
  int streak = prog.currentStreak(), goal = prog.settings.dailyGoal;
  statBox(top, SDL_Rect{24, 58, 140, 96}, "ANSWERS", std::to_string(tot));
  statBox(top, SDL_Rect{174, 58, 140, 96}, "ACCURACY", tot ? std::to_string(cor * 100 / tot) + "%" : "-");
  statBox(top, SDL_Rect{324, 58, 140, 96}, "STREAK", std::to_string(streak) + " · best " + std::to_string(std::max(streak, prog.bestStreak)), F_TINY);
  statBox(top, SDL_Rect{474, 58, 140, 96}, "TIMED BEST", std::to_string(prog.timedBest[scI]));
  // last 14 days: right answers (yellow) under misses (red), goal line dashed
  drawText(top, "LAST 14 DAYS", SDL_Rect{24, 168, 200, 22}, DIM, F_MICRO, A_LEFT);
  int lx = 300;
  for (auto [c, l] : std::vector<std::pair<Col, std::string>>{{ACCENT, "right"}, {BAD, "missed"}, {DIM, "goal " + std::to_string(goal)}}) {
    if (l.rfind("goal", 0) == 0) fillRect(top, SDL_Rect{lx, 178, 16, 3}, c); else fillRound(top, SDL_Rect{lx, 172, 14, 14}, 3, c);
    drawText(top, l, SDL_Rect{lx + 20, 168, 120, 22}, DIM, F_MICRO, A_LEFT);
    lx += 28 + textWidth(top, F_MICRO, l) + 18;
  }
  const int y0 = 404, H = 196; int mx = goal; std::vector<Progress::Day> ds; std::vector<int> dn;
  for (int i = 13; i >= 0; i--) {
    int d = dayNumber(prog.now() - Time(i) * 86400); auto it = prog.days.find(d);
    ds.push_back(it != prog.days.end() ? it->second : Progress::Day{}); dn.push_back(d); mx = std::max(mx, ds.back().answers);
  }
  for (int i = 0; i < 14; i++) {
    int slot = 592 / 14, bw = 30, x = 24 + i * slot + (slot - bw) / 2;
    fillRound(top, SDL_Rect{x, y0 - H, bw, H}, 6, PANEL);
    int hc = H * ds[i].correct / mx, hw = H * (ds[i].answers - ds[i].correct) / mx;
    if (hc > 0) fillRound(top, SDL_Rect{x, y0 - hc, bw, hc}, std::min(6, hc / 2), ACCENT);
    if (hw > 0) fillRound(top, SDL_Rect{x, y0 - hc - hw, bw, hw}, std::min(6, hw / 2), BAD);
    drawText(top, std::to_string(dn[i] % 100), SDL_Rect{x - 6, y0 + 4, bw + 12, 24}, i == 13 ? ACCENT_TXT : DIM, F_MICRO);
  }
  int gy = y0 - H * goal / mx;
  for (int x = 24; x < 616; x += 14) fillRect(top, SDL_Rect{x, gy - 1, 8, 3}, DIM);
  drawText(top, ds[13].answers ? "Today: " + std::to_string(ds[13].answers) + " answers, " + std::to_string(ds[13].correct * 100 / ds[13].answers) + "% right"
                              : std::string("No answers yet today"), SDL_Rect{24, 434, 592, 30}, DIM, F_MICRO, A_LEFT);

  // bottom: accuracy heatmap of every kana in this script, in stage order
  drawText(bot, "ACCURACY BY KANA · tap one to see it in the chart", SDL_Rect{16, 6, 608, 30}, DIM, F_MICRO, A_LEFT);
  int idx = 0;
  for (const Stage& st : data.stages[scI])
    for (int ki : st.kana) {
      SDL_Rect r{16 + (idx % 15) * 40, 40 + (idx / 15) * 41, 37, 38}; idx++;
      addBtn(r, B_HEAT0 + ki, data.kana[ki].glyph, F_MICRO); Button& b = lastBtn(); b.radius = 8; b.lineW = 2;
      auto it = prog.stats.find(Progress::keyKana(data.kana[ki]));
      int seen = it != prog.stats.end() ? it->second.seen : 0;
      if (!prog.isUnlocked(data, ki)) { b.fill = BG; b.line = DIS_LINE; b.text = DIS_TXT; }
      else if (!seen) { b.fill = PANEL; b.line = PANEL; b.text = DIM; }
      else {
        float acc = float(it->second.correct) / seen;
        if (acc < 0.6f) { b.fill = BAD_FILL; b.line = BAD; }
        else if (acc < 0.85f) { b.fill = mix(PANEL, ACCENT, 0.4f); b.line = b.fill; }
        else { b.fill = ACCENT; b.line = ACCENT; b.text = ON_ACCENT; }
      }
    }
  int x = 16;
  const std::pair<Col, std::string> leg[4] = {{PANEL, "Not seen"}, {BAD_FILL, "Under 60%"}, {mix(PANEL, ACCENT, 0.4f), "60-85%"}, {ACCENT, "85%+"}};
  for (auto& [c, l] : leg) {
    fillRound(bot, SDL_Rect{x, 396, 22, 22}, 5, c); drawText(bot, l, SDL_Rect{x + 28, 392, 130, 30}, DIM, F_MICRO, A_LEFT);
    x += 28 + textWidth(bot, F_MICRO, l) + 24;
  }
  chips({{"Y", "Script"}, {"B", "Back"}});
}

void App::drawResult() {
  if (mode == Mode::Timed) {
    header("Timed challenge");
    int answered = int(results.size()), best = prog.timedBest[si(prog.settings.script)];
    drawText(top, std::to_string(score), SDL_Rect{24, 60, 150, 190}, ACCENT_TXT, F_HUGE, A_RIGHT);
    drawText(top, "right", SDL_Rect{184, 150, 130, 70}, DIM, F_MID, A_LEFT);
    drawText(top, timedNewBest ? "New best!" : score == best ? "Equal best!" : "Best: " + std::to_string(best), SDL_Rect{330, 96, 286, 44}, FG, F_SMALL, A_LEFT);
    drawWrapped(top, "In 60 seconds, out of " + std::to_string(answered) + " answered." + (goalMet ? " Daily goal reached!" : ""),
                SDL_Rect{330, 144, 286, 90}, DIM, F_TINY);
    if (answered > 0 && answered <= 40) segBar(top, SDL_Rect{24, 262, 592, 14}, answered, answered, results);
    statBox(top, SDL_Rect{24, 300, 188, 140}, "ANSWERED", std::to_string(answered));
    statBox(top, SDL_Rect{226, 300, 188, 140}, "ACCURACY", answered ? std::to_string(score * 100 / answered) + "%" : "-");
    statBox(top, SDL_Rect{428, 300, 188, 140}, "BEST", std::to_string(best));
  } else {
  header("Session complete");
  Script sc = prog.settings.script; int curSt = prog.currentStage(sc); const Stage& st = data.stages[si(sc)][curSt];
  drawText(top, std::to_string(score), SDL_Rect{24, 60, 150, 190}, ACCENT_TXT, F_HUGE, A_RIGHT);
  drawText(top, "/ " + std::to_string(sessionLen), SDL_Rect{184, 150, 130, 70}, DIM, F_MID, A_LEFT);
  std::string head = score == sessionLen ? "Perfect run!" : score * 10 >= sessionLen * 7 ? "Nice run!" : "Keep going!";
  drawText(top, head, SDL_Rect{330, 96, 286, 44}, FG, F_SMALL, A_LEFT);
  std::string body = missed.empty() ? "Every answer was right." : std::to_string(missed.size()) + (missed.size() == 1 ? " kana goes" : " answers go") + " back into review.";
  if (goalMet) body += " Daily goal reached: " + std::to_string(prog.streak) + "-day streak!";
  drawWrapped(top, body, SDL_Rect{330, 144, 286, 110}, DIM, F_TINY);
  segBar(top, SDL_Rect{24, 262, 592, 14}, sessionLen, sessionLen, results);
  std::vector<std::string> left;
  for (int ki : st.kana) if (prog.step(Progress::keyKana(data.kana[ki])) < KNOWN_STEP && left.size() < 4) left.push_back(data.kana[ki].glyph);
  statBox(top, SDL_Rect{24, 300, 188, 140}, "NEWLY KNOWN", newlyKnown.empty() ? "-" : join(newlyKnown, " "));
  statBox(top, SDL_Rect{226, 300, 188, 140}, "STAGE " + std::to_string(curSt + 1),
          std::to_string(prog.stageKnown(data, sc, curSt)) + " / " + std::to_string(st.kana.size()) + " known", F_TINY);
  statBox(top, SDL_Rect{428, 300, 188, 140}, "STILL TO LEARN", left.empty() ? "all done" : join(left, " "));
  }

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
  std::string again = unlock ? "Continue" : mode == Mode::Timed ? "Try again" : mode == Mode::Review ? (modeAvailable(Mode::Review) ? "More reviews" : "Practice")
                   : mode == Mode::Mixup ? "More mix-ups" : "Practice again";
  addBtn(SDL_Rect{212, 330, 412, 84}, B_AGAIN, again, F_SMALL); accent(lastBtn());
  chips({{"A", unlock ? "Continue" : "Again"}, {"B", "Home"}});
}

void App::drawOsd() {  // BananaOS's volume / brightness card, across the top of the top screen
  if (!osdUntil) return;
  bool vol = osdWhat == "volume";
  SDL_Rect r{120, 8, 400, 52}; boxRound(top, r, 26, PANEL2, ACCENT, 2);
  drawText(top, vol ? (osdPct == 0 ? "Muted" : "Volume") : "Brightness", SDL_Rect{r.x + 22, r.y, 130, r.h}, FG, F_TINY, A_LEFT);
  bar(top, SDL_Rect{r.x + 152, r.y + 20, 168, 12}, std::clamp(osdPct, 0, 100) / 100.f);
  drawText(top, std::to_string(osdPct) + "%", SDL_Rect{r.x + 326, r.y, 60, r.h}, ACCENT_TXT, F_TINY, A_RIGHT);
}

void App::drawPause() {
  drawText(top, "Paused", SDL_Rect{20, 110, 600, 110}, FG, F_BIG);
  drawText(top, screen == Screen::Drill && mode != Mode::Timed
                    ? "Question " + std::to_string(std::min(index + 1, sessionLen)) + " of " + std::to_string(sessionLen) + " · your answers are saved"
                    : std::string("Your progress is saved"), SDL_Rect{20, 240, 600, 40}, DIM, F_TINY);
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
  } else if (tab == 2) {
    int row = 0;
    for (int st = 12; st < 14; st++) {
      const auto& list = stages[st].kana;
      for (size_t i = 0; i < list.size(); i++) out.push_back({list[i], row + int(i) / 3, int(i) % 3});
      row += int(list.size()) / 3;
    }
  } else {
    const auto& list = stages[STAGE_COUNT - 1].kana;
    for (size_t i = 0; i < list.size(); i++) out.push_back({list[i], int(i) / 5, int(i) % 5});
  }
  return out;
}

void App::drawChart() {
  Script sc = prog.settings.script;
  auto cells = chartCells(sc, chartTab);
  if (chartSel < 0 || std::none_of(cells.begin(), cells.end(), [&](const Cell& c) { return c.kana == chartSel; }))
    chartSel = cells.empty() ? -1 : cells[0].kana;
  int maxRow = 0; for (auto& c : cells) maxRow = std::max(maxRow, c.row);
  const int VIS = 4; chartScroll = std::clamp(chartScroll, 0, std::max(0, maxRow + 1 - VIS));

  header(std::string("Kana chart · ") + scriptName(sc));
  if (chartSel >= 0) {
    const Kana& k = data.kana[chartSel]; KanaState ks = prog.state(data, chartSel);
    const Glyph* g = data.glyphFor(k.glyph); int stI = data.stageOfKana[chartSel];
    SDL_Rect card{24, 58, 220, 240}; fillRound(top, card, 16, PANEL);
    drawText(top, k.glyph, card, ks == KanaState::Locked ? DIM : FG, F_HUGE);
    drawText(top, k.romaji, SDL_Rect{264, 58, 352, 84}, ACCENT_TXT, F_BIG, A_LEFT);
    drawText(top, (g ? std::to_string(g->strokes.size()) + (g->strokes.size() == 1 ? " stroke" : " strokes") : std::string("Combination")) + " · Stage " + std::to_string(stI + 1),
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
    bool picked = std::count(chartPick.begin(), chartPick.end(), chartSel) > 0;
    drawText(top, ks == KanaState::Locked ? "Unlocks with stage " + std::to_string(stI + 1) + ". You can still write it freely."
                  : picked ? "In your practice set. Tap it again or press A to take it out."
                           : "Tap it again or press A to add it to your practice set.", SDL_Rect{24, 418, 592, 34}, DIM, F_MICRO, A_LEFT);
  }
  const char* tabs[CHART_TABS] = {"Basic", "Voiced", "Combos", "Extras"};
  for (int i = 0; i < CHART_TABS; i++) { addBtn(SDL_Rect{16 + i * 116, 10, 108, 44}, B_TAB0 + i, tabs[i], F_TINY); if (i == chartTab) accent(lastBtn()); }
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
    if (std::count(chartPick.begin(), chartPick.end(), c.kana)) { b.icon = 2; b.line = ACCENT_TXT; b.lineW = 3; }
    if (c.kana == chartSel) { b.line = FG; b.lineW = 4; }
  }
  addBtn(SDL_Rect{572, 66, 52, 130}, B_SCROLL_UP, "▲", F_TINY, chartScroll > 0);
  addBtn(SDL_Rect{572, 204, 52, 130}, B_SCROLL_DOWN, "▼", F_TINY, chartScroll + VIS <= maxRow);
  // practice set: drill it or write it freely (with nothing picked, the selected kana)
  int np = int(chartPick.size());
  bool selOk = chartSel >= 0 && prog.isUnlocked(data, chartSel);
  std::string target = np ? std::to_string(np) + " kana" : chartSel >= 0 ? data.kana[chartSel].glyph : "";
  addBtn(SDL_Rect{16, 346, 196, 78}, B_PICK_DRILL, "Drill " + target, F_TINY, np > 0 || selOk);
  lastBtn().sub = np ? "your practice set" : "with look-alikes"; if (np || selOk) accent(lastBtn());
  addBtn(SDL_Rect{220, 346, 156, 78}, B_PICK_WRITE, "Write " + target, F_TINY, np > 0 || chartSel >= 0); lastBtn().sub = "free practice";
  addBtn(SDL_Rect{384, 346, 128, 78}, B_PICK_ANY, "Draw any", F_TINY); lastBtn().sub = "it guesses";
  addBtn(SDL_Rect{520, 346, 104, 78}, B_PICK_CLEAR, "Clear", F_TINY, np > 0); lastBtn().sub = "the set";
  chips({{"+", "Move"}, {"A", "Pick"}, {"X", "Drill"}, {"START", "Write"}, {"B", "Back"}});
}

// ------------------------------------------------------------------ settings (modelled on BananaOS Settings)
std::string App::groupName(int g) const {
  static const char* N[GROUPS] = {"Practice", "Sessions", "Script", "Display", "Progress", "About"};
  return N[g];
}

std::vector<App::SRow> App::settingRows(int g) const {
  const Settings& st = prog.settings; std::vector<SRow> r;
  auto choice = [&](const std::string& label, int cur, int n, const std::string& value, const std::string& help) {
    SRow x; x.label = label; x.value = value; x.help = help; x.kind = 0; x.canDec = cur > 0; x.canInc = cur < n - 1; r.push_back(x);
  };
  auto info = [&](const std::string& label, const std::string& value, const std::string& help) {
    SRow x; x.label = label; x.value = value; x.help = help; x.kind = 2; r.push_back(x);
  };
  auto action = [&](const std::string& label, const std::string& value, const std::string& help) {
    SRow x; x.label = label; x.value = value; x.help = help; x.kind = 1; r.push_back(x);
  };
  switch (g) {
    case 0:
      choice("Reading", st.reading, 3, WEIGHT_NAMES[st.reading], "See a kana and pick its sound, or see a sound and pick the kana.");
      choice("Word meanings", st.meanings, 3, WEIGHT_NAMES[st.meanings], "Read a word and pick its meaning. Words only use kana you've unlocked.");
      choice("Writing", st.writing, 3, WEIGHT_NAMES[st.writing], "Trace the kana with the stylus, stroke by stroke.");
      choice("Typing", st.typing, 3, WEIGHT_NAMES[st.typing], "Type the answer on the on-screen keyboard.");
      choice("Typing keyboard", st.kanaKeys ? 1 : 0, 2, st.kanaKeys ? "Kana" : "Romaji",
             "Romaji: see the kana, type its romaji. Kana: see the romaji, type the kana on a kana keyboard.");
      break;
    case 1: {
      int li = 1; for (int i = 0; i < 4; i++) if (SESSION_LENS[i] == st.sessionLen) li = i;
      choice("Session length", li, 4, std::to_string(st.sessionLen) + " questions", "How many questions in each practice session.");
      choice("Review older stages", st.review, 3, REVIEW_NAMES[st.review],
             "How often kana from stages you've passed come back. Weak kana and reviews that are due always come back.");
      int gi = 1; for (int i = 0; i < 4; i++) if (GOALS[i] == st.dailyGoal) gi = i;
      choice("Daily goal", gi, 4, std::to_string(st.dailyGoal) + " answers", "Answer this many questions in a day to keep your streak going.");
      const char* SN[3] = {"Relaxed", "Normal", "Strict"};
      choice("Writing check", st.strict, 3, SN[st.strict],
             "How closely your strokes must follow the real ones. Strict also wants the right length and position; relaxed forgives wobbly lines.");
      break; }
    case 2: {
      bool kat = prog.scriptUnlocked(Script::Katakana);
      SRow x; x.label = "Practise"; x.value = scriptName(st.script); x.kind = 0;
      x.help = kat ? "Which kana your practice sessions use." : "Katakana unlocks after hiragana stage 10, or skip ahead below.";
      x.canDec = kat && st.script == Script::Katakana; x.canInc = kat && st.script == Script::Hiragana; r.push_back(x);
      for (int i = 0; i < 2; i++) {
        Script sc = i ? Script::Katakana : Script::Hiragana; int cur = skipCurrent(sc), pend = std::max(skipPending[i], cur);
        std::string lower = scriptName(sc); lower[0] = char(std::tolower(lower[0]));
        SRow y; y.kind = 3; y.label = scriptName(sc); y.canDec = pend > cur; y.canInc = pend < SKIP_ALL;
        auto name = [](int v) { return v == 0 ? std::string("Locked") : v >= SKIP_ALL ? std::string("All known") : "Stage " + std::to_string(v); };
        y.value = pend <= cur ? name(cur) : pend >= SKIP_ALL ? "Skip: all known" : cur == 0 && pend == 1 ? "Unlock" : "Skip to stage " + std::to_string(pend);
        y.help = pend > cur ? (pend == 1 ? "Press A to unlock katakana now, starting with its first lesson."
                                         : "Press A to confirm. Kana in the stages before it count as known, with a quick review in a day or two to check.")
                            : cur >= SKIP_ALL ? "Every " + lower + " stage is open and known. Reviews keep it fresh."
                            : "Already know some " + lower + "? Use ◀ ▶ to choose where to start, then A to confirm. "
                              "It only moves forwards: Reset progress starts again.";
        r.push_back(y);
      }
      break; }
    case 3: {
      choice("Theme", st.light ? 1 : 0, 2, st.light ? "Light" : "Dark", "Dark or light colours, like BananaOS's own themes.");
      choice("Kana font", st.kanaHand ? 1 : 0, 2, st.kanaHand ? "Handwriting" : "Rounded",
             "Rounded print (Zen Maru Gothic) or a textbook handwriting style (Klee One), closer to how kana are written by hand.");
      const char* GL[3] = {"Cross", "Grid", "None"}; const char* IN[3] = {"Thin", "Normal", "Thick"};
      choice("Canvas lines", st.guideLines, 3, GL[st.guideLines], "Guide lines on the writing canvas: a centre cross, a finer grid, or none.");
      choice("Ink", st.ink, 3, IN[st.ink], "How thick your strokes are drawn.");
      const char* SO[4] = {"Off", "Quiet", "Normal", "Loud"};
      choice("Sound", st.sound, 4, SO[st.sound], "Sound effects for taps, right and wrong answers, unlocks and your daily goal.");
      break; }
    case 4:
      info("Sessions played", std::to_string(prog.session), "Every practice session you've started.");
      info("Last session", prog.lastScore >= 0 ? std::to_string(prog.lastScore) + " / " + std::to_string(prog.lastLen) : "-",
           "Score from your most recent session.");
      info("Streak", std::to_string(prog.currentStreak()) + (prog.currentStreak() == 1 ? " day" : " days") + " · best " + std::to_string(prog.bestStreak),
           "Days in a row you've reached your daily goal.");
      info("Timed best", "H " + std::to_string(prog.timedBest[0]) + " · K " + std::to_string(prog.timedBest[1]),
           "Best 60-second challenge score for each script.");
      action("Reset progress", resetArmed ? "Press A again to confirm" : "Reset",
             "Forget every answer, streak and best, and lock all stages again. Your settings stay. This can't be undone.");
      break;
    case 5:
      info("Version", version, "Banana Kana for BananaOS on the Anbernic RG DS Plus.");
      action("Credits", "Open", "Stroke data, fonts and licences.");
      break;
  }
  return r;
}

std::string App::groupSummary(int g) const {
  const Settings& st = prog.settings;
  switch (g) {
    case 0: { std::vector<std::string> on; const char* n[4] = {"reading", "meanings", "writing", "typing"};
              int w[4] = {st.reading, st.meanings, st.writing, st.typing};
              for (int i = 0; i < 4; i++) if (w[i] > 0) on.push_back(n[i]);
              return on.empty() ? "all off" : on.size() == 4 ? "all four on" : join(on, ", "); }
    case 1: return std::to_string(st.sessionLen) + " questions, goal " + std::to_string(st.dailyGoal) + " a day";
    case 2: { std::string s = scriptName(st.script); s[0] = char(std::tolower(s[0])); return s; }
    case 3: return std::string(st.light ? "light" : "dark") + (st.kanaHand ? ", handwriting" : ", rounded") + (st.sound ? ", sound on" : ", sound off");
    case 4: return std::to_string(prog.knownCount(data, Script::Hiragana) + prog.knownCount(data, Script::Katakana)) + " kana known";
    default: return "version, credits";
  }
}

void App::changeSetting(int g, int r, int dir) {
  Settings& st = prog.settings;
  auto step3 = [&](int& v) { v = std::clamp(v + dir, 0, 2); };
  switch (g) {
    case 0: { int* w[4] = {&st.reading, &st.meanings, &st.writing, &st.typing}; if (r < 4) step3(*w[r]); else st.kanaKeys = dir > 0; break; }
    case 1:
      if (r == 0) { int li = 1; for (int i = 0; i < 4; i++) if (SESSION_LENS[i] == st.sessionLen) li = i; st.sessionLen = SESSION_LENS[std::clamp(li + dir, 0, 3)]; }
      else if (r == 1) step3(st.review);
      else if (r == 2) { int gi = 1; for (int i = 0; i < 4; i++) if (GOALS[i] == st.dailyGoal) gi = i; st.dailyGoal = GOALS[std::clamp(gi + dir, 0, 3)]; }
      else st.strict = std::clamp(st.strict + dir, 0, 2);
      break;
    case 2:
      if (r == 0) setScript(dir > 0 ? Script::Katakana : Script::Hiragana);
      else { int i = r - 1, cur = skipCurrent(i ? Script::Katakana : Script::Hiragana);
             skipPending[i] = std::clamp(std::max(skipPending[i], cur) + dir, cur, SKIP_ALL); return; }  // confirmed with A
      break;
    case 3:
      if (r == 0) { st.light = dir > 0; applyTheme(st.light); }
      else if (r == 1) { st.kanaHand = dir > 0; setKanaHand(st.kanaHand); }
      else if (r == 2) st.guideLines = std::clamp(st.guideLines + dir, 0, 2);
      else if (r == 3) st.ink = std::clamp(st.ink + dir, 0, 2);
      else { st.sound = std::clamp(st.sound + dir, 0, 3); snd::setVolume(st.sound); snd::play(snd::Sfx::Right); }
      break;
    case 4:
      if (r == 4) {
        if (!resetArmed) { resetArmed = true; return; }
        Settings keep = prog.settings; prog = Progress{}; prog.settings = keep; prog.settings.script = Script::Hiragana;
        pendingUnlocks.clear(); resetArmed = false;
      }
      break;
    case 5: if (r == 1) { go(Screen::Credits); return; } break;
  }
  save();
}

void App::settingIcon(Win& w, int g, int x, int y, int px, Col a, Col b) {
  static const std::vector<const char*> ICONS[GROUPS] = {
    {"........##..", ".......#BB#.", "......#BB#..", ".....#BB#...", "....#BB#....", "...#BB#.....", "..#BB#......", ".#BB#.......", ".##.#.......", ".###........"},  // pencil
    {"...######...", "..#......#..", ".#...BB...#.", "#....BB....#", "#....BB....#", "#....BBBB..#", "#..........#", ".#........#.", "..#......#..", "...######..."},  // clock
    {},  // script: drawn as a kana
    {"....####....", "..###.......", ".###........", ".##.........", "###.........", "###......B..", "###.....BBB.", ".###.....B..", "..#######...", "....####...."},  // moon
    {"..........##", "..........##", "......##..##", "......##..##", "..##..##..##", "..##..##..##", "..##..##..##", "BBBBBBBBBBBB"},  // chart
    {"...######...", "..#......#..", ".#...BB...#.", "#..........#", "#....BB....#", "#....BB....#", "#....BB....#", ".#...BB...#.", "..#......#..", "...######..."},  // info
  };
  if (g == 2) { drawText(w, prog.settings.script == Script::Katakana ? "ア" : "あ", SDL_Rect{x, y - px, 12 * px, 12 * px}, a, px >= 6 ? F_BIG : F_MID); return; }
  pixIcon(w, ICONS[g], x, y, px, a, b);
}

void App::footer(const std::string& lk, const std::string& ll, const std::string& rk, const std::string& rl) {
  auto pill = [&](int x, int w, const std::string& k, const std::string& l, int id) {
    SDL_Rect r{x, 420, w, 46};
    boxRound(bot, r, 12, pressedId == id ? mix(BG, FG, 0.2f) : BG, mix(BG, FG, 0.7f), 2);
    int kw = std::max(30, textWidth(bot, F_MICRO, k) + 14);
    fillRound(bot, SDL_Rect{r.x + 9, r.y + 9, kw, 28}, 7, FG);
    drawText(bot, k, SDL_Rect{r.x + 9, r.y + 9, kw, 28}, BG, F_MICRO);
    drawText(bot, l, SDL_Rect{r.x + 18 + kw, r.y, r.w - kw - 24, r.h}, FG, F_TINY, A_LEFT);
    addBtn(r, id, ""); lastBtn().hidden = true;
  };
  int lw = textWidth(bot, F_TINY, ll) + 80, rw = textWidth(bot, F_TINY, rl) + 80;
  pill(16, lw, lk, ll, B_FOOT_L);
  if (!rl.empty()) pill(624 - rw, rw, rk, rl, B_FOOT_R);
}

void App::settingsPanel(int g, const std::string& label, const std::string& title, const std::string& accentLine, const std::string& body) {
  SDL_Rect p{28, 24, 584, 432};
  boxRound(top, p, 18, PANEL, mix(BG, FG, 0.7f), 3);
  settingIcon(top, g, 56, 64, 7, FG, ACCENT);
  drawText(top, label, SDL_Rect{170, 54, 420, 24}, DIM, F_MICRO, A_LEFT);
  drawText(top, title, SDL_Rect{170, 78, 420, 56}, FG, F_MID, A_LEFT);
  drawText(top, accentLine, SDL_Rect{170, 136, 420, 34}, ACCENT_TXT, F_TINY, A_LEFT);
  drawWrapped(top, body, SDL_Rect{56, 204, 528, 230}, DIM, F_TINY);
}

void App::drawSettings() {
  auto rows = settingRows(setGroup);
  std::vector<std::string> names; for (auto& r : rows) names.push_back(r.label);
  settingsPanel(setGroup, "SETTINGS", groupName(setGroup), std::to_string(rows.size()) + (rows.size() == 1 ? " setting" : " settings"),
                join(names, ", ") + ".");
  boxRound(bot, SDL_Rect{16, 10, 608, 48}, 12, PANEL, mix(BG, FG, 0.55f), 2);
  drawText(bot, "Settings", SDL_Rect{34, 10, 300, 48}, FG, F_TINY, A_LEFT);
  drawText(bot, "Choose a group", SDL_Rect{300, 10, 306, 48}, DIM, F_MICRO, A_RIGHT);
  for (int g = 0; g < GROUPS; g++) {
    SDL_Rect r{16 + (g % 3) * 206, 70 + (g / 3) * 170, 196, 160};
    bool on = g == setGroup;
    Col fill = on ? ACCENT : PANEL; if (pressedId == B_GROUP0 + g) fill = mix(fill, FG, 0.2f);
    if (on) fillRound(bot, r, 14, fill); else boxRound(bot, r, 14, fill, mix(BG, FG, 0.55f), 2);
    settingIcon(bot, g, r.x + (r.w - 48) / 2, r.y + 18, 4, on ? ON_ACCENT : FG, on ? ON_ACCENT : ACCENT);
    drawText(bot, groupName(g), SDL_Rect{r.x + 8, r.y + 84, r.w - 16, 34}, on ? ON_ACCENT : FG, F_TINY);
    drawText(bot, groupSummary(g), SDL_Rect{r.x + 8, r.y + 120, r.w - 16, 26}, on ? ON_ACCENT : DIM, F_MICRO);
    addBtn(r, B_GROUP0 + g, ""); lastBtn().hidden = true;
  }
  footer("B", "Close", "A", "Open");
}

void App::drawSettingsGroup() {
  auto rows = settingRows(setGroup);
  setRow = std::clamp(setRow, 0, int(rows.size()) - 1);
  const SRow& cur = rows[setRow];
  std::string upper = groupName(setGroup); for (auto& c : upper) c = char(std::toupper(c));
  settingsPanel(setGroup, upper, cur.label, cur.value, cur.help);
  boxRound(bot, SDL_Rect{16, 10, 608, 48}, 12, PANEL, mix(BG, FG, 0.55f), 2);
  drawText(bot, groupName(setGroup), SDL_Rect{34, 10, 300, 48}, FG, F_TINY, A_LEFT);
  drawText(bot, "L / R: other groups", SDL_Rect{300, 10, 306, 48}, DIM, F_MICRO, A_RIGHT);
  for (int i = 0; i < int(rows.size()); i++) {
    const SRow& row = rows[i];
    SDL_Rect r{16, 70 + i * 68, 608, 58};
    bool on = i == setRow;
    Col fill = on ? ACCENT : PANEL; if (pressedId == B_SROW0 + i) fill = mix(fill, FG, 0.2f);
    if (on) fillRound(bot, r, 12, fill); else boxRound(bot, r, 12, fill, mix(BG, FG, 0.55f), 2);
    Col t = on ? ON_ACCENT : FG, v = on ? ON_ACCENT : (row.kind == 2 ? DIM : FG);
    drawText(bot, row.label, SDL_Rect{r.x + 18, r.y, 250, r.h}, t, F_TINY, A_LEFT);
    if (row.kind == 0 || row.kind == 3) {
      Col ad = row.canDec ? t : (on ? mix(ACCENT, ON_ACCENT, 0.35f) : DIS_TXT), ai = row.canInc ? t : (on ? mix(ACCENT, ON_ACCENT, 0.35f) : DIS_TXT);
      drawText(bot, "◀", SDL_Rect{r.x + r.w - 300, r.y, 36, r.h}, ad, F_MICRO);
      drawText(bot, row.value, SDL_Rect{r.x + r.w - 264, r.y, 220, r.h}, v, F_TINY);
      drawText(bot, "▶", SDL_Rect{r.x + r.w - 44, r.y, 36, r.h}, ai, F_MICRO);
      addBtn(SDL_Rect{r.x + r.w - 310, r.y, 70, r.h}, B_SDEC0 + i, ""); lastBtn().hidden = true;
      addBtn(SDL_Rect{r.x + r.w - 70, r.y, 70, r.h}, B_SINC0 + i, ""); lastBtn().hidden = true;
    } else if (row.kind == 1) {
      drawText(bot, row.value, SDL_Rect{r.x + r.w - 330, r.y, 280, r.h}, row.label == "Reset progress" && resetArmed ? (on ? ON_ACCENT : BAD) : v, F_TINY, A_RIGHT);
      drawText(bot, "▶", SDL_Rect{r.x + r.w - 44, r.y, 36, r.h}, t, F_MICRO);
    } else {
      drawText(bot, row.value, SDL_Rect{r.x + r.w - 350, r.y, 332, r.h}, v, F_TINY, A_RIGHT);
    }
    addBtn(r, B_SROW0 + i, ""); lastBtn().hidden = true;
  }
  std::string act = cur.kind == 0 ? "Change" : cur.kind == 1 ? (cur.label == "Reset progress" ? "Reset" : "Open")
                  : cur.kind == 3 ? (cur.canDec ? "Confirm" : "") : "";
  footer("B", "Back", "A", act);
}

void App::drawCredits() {
  const float t = SDL_GetTicks() / 1000.f;
  // top: twinkling confetti, a wavy thank-you and a banana parade
  const Col conf[4] = {ACCENT, GOOD, BAD, FG};
  for (int i = 0; i < 26; i++) {
    unsigned h = 2654435761u * unsigned(i + 7);
    int x = int(h % 600) + 12, y = int((h >> 10) % 300) + 46, sz = 4 + int((h >> 20) % 7);
    float tw = 0.5f + 0.5f * std::sin(t * 2.2f + i);
    if (tw > 0.25f) fillRect(top, SDL_Rect{x, y, sz, sz}, mix(BG, conf[i % 4], tw));
  }
  const std::vector<std::string> thanks = Data::partNames("ありがとう!");
  int tw = 0; for (auto& c : thanks) tw += textWidth(top, F_BIG, c);
  int x = (LW - tw) / 2;
  for (size_t i = 0; i < thanks.size(); i++) {
    int w = textWidth(top, F_BIG, thanks[i]), dy = int(10 * std::sin(t * 4.f - i * 0.7f));
    drawText(top, thanks[i], SDL_Rect{x, 62 + dy, w, 110}, i % 2 ? FG : ACCENT_TXT, F_BIG);
    x += w;
  }
  drawText(top, "arigatou: thank you for playing!", SDL_Rect{20, 186, 600, 34}, DIM, F_TINY);
  fillRound(top, SDL_Rect{170, 232, 300, 44}, 22, ACCENT);
  drawText(top, "BANANA KANA  " + version, SDL_Rect{170, 232, 300, 44}, ON_ACCENT, F_TINY);
  for (int i = 0; i < 7; i++) {
    int bx = 40 + i * 82, by = 318 + int(14 * std::sin(t * 3.f + i * 0.9f));
    iconBanana(top, bx, by, 4);
  }
  drawText(top, "Made for the Anbernic RG DS Plus · code MIT · licences in BananaKana/data/licenses",
           SDL_Rect{20, 420, 600, 34}, DIM, F_MICRO);

  // bottom: sticker cards
  struct Card { const char* title; const char* role; const char* who; Col stripe; };
  const Card cards[6] = {
    {"BananaOS", "the OS it all runs on", "JorreFog · github.com/JorreFog/BananaLinux", ACCENT},
    {"KanjiVG", "stroke order data", "Ulrich Apel · CC BY-SA 3.0", GOOD},
    {"Zen Maru Gothic · Klee One", "rounded and handwriting fonts", "SIL Open Font License 1.1", BAD},
    {"Noto Sans CJK JP", "fallback font", "Google and Adobe · OFL 1.1", mix(ACCENT, BAD, 0.5f)},
    {"Banana Player", "showed how BananaOS apps work", "tonywei92 · github.com/tonywei92/banana-player", mix(GOOD, ACCENT, 0.5f)},
    {"Banana Kana", "hints, words and code", "Parzival3971 and Claude · MIT", FG},
  };
  for (int i = 0; i < 6; i++) {
    SDL_Rect r{16 + (i % 2) * 310, 12 + (i / 2) * 110, 298, 100};
    fillRound(bot, r, 14, PANEL);
    fillRound(bot, SDL_Rect{r.x, r.y, 12, r.h}, 6, cards[i].stripe);
    fillCircle(bot, r.x + r.w - 20, r.y + 20, 6, cards[i].stripe);
    drawText(bot, cards[i].title, SDL_Rect{r.x + 26, r.y + 8, r.w - 56, 32}, FG, F_TINY, A_LEFT);
    drawText(bot, cards[i].role, SDL_Rect{r.x + 26, r.y + 40, r.w - 36, 24}, ACCENT_TXT, F_MICRO, A_LEFT);
    drawText(bot, cards[i].who, SDL_Rect{r.x + 26, r.y + 66, r.w - 36, 24}, DIM, F_MICRO, A_LEFT);
  }
  addBtn(SDL_Rect{16, 350, 608, 74}, B_BACK, "Back", F_SMALL); accent(lastBtn());
  chips({{"B", "Back"}});
}

}  // namespace kt
