#include "app.h"
#include "sound.h"
#include <ctime>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kt {

namespace {
const char* WEIGHT_NAMES[3] = {"Off", "Sometimes", "Often"};
const char* REVIEW_NAMES[3] = {"Off", "Some", "Lots"};
const int SESSION_LENS[4] = {5, 10, 15, 20};
 // home mode row, left to right: Review, Mix-ups, Words, Timed, Stats (B_MODE0 + id)
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
  if (s == Screen::Home && petEvolvePending()) s = Screen::PetEvolve;  // a new stage is celebrated on the way Home
  if (motion && s != screen) transStart = SDL_GetTicks() | 1;  // soft fade-in of the new screen
  if (s == Screen::Home && petHopPending) { petHopPending = false; petHopStart = SDL_GetTicks() | 1; }
  if (s == Screen::PetEvolve && screen != Screen::PetEvolve) { snd::play(snd::Sfx::Unlock); celebrate(3200, true); }
  if (s == Screen::PetName) {  // reuse the drill's kana keyboard: a blank hiragana typing question
    q = Question{}; q.type = QType::Type; q.kanaInput = true; checked = false; keyRow = keyCol = 0;
    typed = isKataText(prog.pet.name) || utf8Codepoints(prog.pet.name).empty() || utf8Codepoints(prog.pet.name)[0] >= 0x3040 ? prog.pet.name : "";
  }
  if (s == Screen::Welcome && screen != Screen::Welcome) welcomeStep = 0;
  if (s == Screen::SettingsGroup && screen != Screen::SettingsGroup) setFirst = std::max(0, setRow - 2);
  screen = s; pressedId = -1; drawing = false; cur.clear(); paused = false; animStart = 0;
  if (s == Screen::Unlocked) { snd::play(snd::Sfx::Unlock); celebrate(3200, true); }
  if (s == Screen::Result || s == Screen::Unlocked) backup(true);
  if (s == Screen::Result) countStart = motion ? (SDL_GetTicks() | 1) : 0;  // the score counts up
  if (s == Screen::Result && mode == Mode::Timed && timedNewBest) celebrate(2400, true);
  if (s == Screen::Stages) { mapScript = prog.settings.script; mapSel = prog.currentStage(mapScript); }
  if (s == Screen::Chart) {
    chartScroll = 0; chartSel = -1;
    auto cells = chartCells(prog.settings.script, chartTab);
    if (!cells.empty()) chartSel = cells[0].kana;
  }
  if (s == Screen::Home) { homeSel = std::clamp(homeSel, 0, int(homeFocus().size()) - 1); earned(prog.checkAchievements(data)); }  // e.g. after skipping ahead
  if (s == Screen::SettingsGroup) { skipPending[0] = skipCurrent(Script::Hiragana); skipPending[1] = skipCurrent(Script::Katakana); resetArmed = resetSetArmed = false; setRow = std::clamp(setRow, 0, std::max(0, int(settingRows(setGroup).size()) - 1)); }
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
  firstRun = false; welcomeAgain = false;
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
  if (m == Mode::Review) return prog.dueCount(data, s) > 0;
  if (m == Mode::Mixup) return !mixupSets(data, prog, s).empty();
  if (m == Mode::Pack) return packIdx >= 0 && int(readyWords(data, prog, packIdx).size()) >= PACK_MIN;
  return true;
}

void App::earned(const std::vector<int>& newly) {
  if (newly.empty()) return;
  for (int i : newly) toastQ.push_back(i);
  snd::play(snd::Sfx::Goal); celebrate(2400, true);
  if (!toastUntil) { toastAch = toastQ.front(); toastQ.erase(toastQ.begin()); toastUntil = SDL_GetTicks() + 3500; if (!toastUntil) toastUntil = 1; }
}

void App::startPack(int pack) {
  if (pack < 0 || pack >= int(data.packs.size()) || int(readyWords(data, prog, pack).size()) < PACK_MIN) return;
  packIdx = pack; packSel = pack; startMode(Mode::Pack);
}

void App::holdSession() {
  held.on = false;
  if (screen != Screen::Drill || mode == Mode::Timed) return;  // the timed clock can't wait
  int idx = index + (checked && !q.intro ? 1 : 0);  // an answered question already counts
  held.on = true; held.mode = mode; held.script = prog.settings.script; held.session = prog.session; held.index = idx;
  held.score = score; held.sessionLen = sessionLen; held.focusKana = focusKana; held.packIdx = packIdx; held.goalMet = goalMet;
  held.results = results; held.focusSet = focusSet; held.missed = missed; held.newlyKnown = newlyKnown;
  held.packSession = packSession; held.lastKey = lastKey;
}

bool App::resumeSession() {
  if (!sessionHeld()) { held.on = false; return false; }
  held.on = false;
  mode = held.mode; index = held.index; score = held.score; sessionLen = held.sessionLen; focusKana = held.focusKana;
  packIdx = held.packIdx; goalMet = held.goalMet; results = held.results; focusSet = held.focusSet; missed = held.missed;
  newlyKnown = held.newlyKnown; packSession = held.packSession; lastKey = held.lastKey;
  go(Screen::Drill);
  if (index >= sessionLen) finishSession(); else nextQuestion(true);
  return true;
}

void App::chipTap(int i) {
  static const SDL_GameControllerButton B[7] = {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
  if (i >= 0 && i < 7) padButton(B[i]);
}

void App::startMode(Mode m) {
  held.on = false;  // a new session replaces one left on hold
  if (!modeAvailable(m)) m = Mode::Normal;
  mode = m; prog.session++;
  if (m != Mode::Focus) focusKana = -1;
  packSession = PackSession{};
  sessionLen = prog.settings.sessionLen;
  if (m == Mode::Review) sessionLen = std::min(sessionLen, prog.dueCount(data, prog.settings.script));
  if (m == Mode::Timed) { sessionLen = 0; timedLeft = TIMED_MS; timedLast = SDL_GetTicks(); timedNewBest = false; flash = 0; flashText.clear(); }
  index = 0; score = 0; results.clear(); missed.clear(); newlyKnown.clear(); lastKey.clear(); goalMet = false;
  go(Screen::Drill); nextQuestion(true); save();
}

bool App::tick(Uint32 now) {  // every timer runs every time; true if anything needs a redraw
  bool redraw = false;
  if (osdUntil && now >= osdUntil) { osdUntil = 0; redraw = true; }  // level card gone
  if (toastUntil && now >= toastUntil) {  // achievement card: next one, or gone
    if (!toastQ.empty()) { toastAch = toastQ.front(); toastQ.erase(toastQ.begin()); toastUntil = now + 3500; }
    else { toastUntil = 0; toastAch = -1; }
    redraw = true;
  }
  if (confStart && now - confLast >= 33) { confLast = now; redraw = true; }  // confetti at ~30 fps
  if (animStart && !paused && now - animLast >= 33) { animLast = now; redraw = true; }  // stroke-order animation
  if (transStart) { if (now - transStart >= TRANS_MS) transStart = 0; redraw = true; }
  if (motion && !paused && (screen == Screen::Home || screen == Screen::Pet || screen == Screen::PetEvolve) && now - petLast >= 70) { petLast = now; redraw = true; }  // Kitsu breathes, blinks and sways
  if (motion && prog.settings.backdrop > 0 && menuScreen() && now - seasonLast >= 90) { seasonLast = now; redraw = true; }  // gentle drift, ~11 fps
  if (countStart) { if (now - countStart >= 900) countStart = 0; redraw = true; }
  if (answerAnim) { if (now - answerAnim >= ANSWER_MS) answerAnim = 0; redraw = true; }
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
  if (!first && !q.intro) index++;  // new-word cards don't use up a question
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
    else if (mode == Mode::Pack) { packSession.left = sessionLen - index; q = makePackQuestion(data, prog, rng, packIdx, packSession); break; }  // it keeps session state: ask once
    else q = makeQuestion(data, prog, rng);
    if (q.key != lastKey) break;
  }
  lastKey = q.key; sel = -1; cursor = -1; /* no cursor until the d-pad/stick is used: a highlight could give the answer away */ checked = false; correct = false; typed.clear(); strokes.clear(); cur.clear();
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
  prog.lastScore = score; prog.lastLen = sessionLen;
  std::vector<int> got = prog.checkAchievements(data);
  if (mode != Mode::Timed && sessionLen >= 10 && score == sessionLen && prog.award("perfect"))
    for (int i = 0; i < int(achievementList().size()); i++) if (achievementList()[i].id == "perfect") got.push_back(i);
  save();
  go(Screen::Result);
  earned(got);
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
      ok = q.kanaInput ? kanaTypedMatches(data, q, typed) : typedMatches(typed, q.answer, q.isWord); said = typed; break;
    default: if (sel < 0) return; ok = (sel == q.correct); said = q.options[sel]; break;
  }
  int before = prog.step(q.key);
  checked = true; correct = ok; results.push_back(ok ? 1 : 0); if (motion) answerAnim = SDL_GetTicks() | 1;  // pop / shake
  int wlvBefore = prog.writeLevel(q.key);
  if (q.type == QType::Write) prog.recordWrite(q.key, ok, q.writeLevel); else prog.record(q.key, ok);
  int after = prog.step(q.key);
  const Word* word = nullptr;
  if (q.isWord) for (const Word& w : data.words) if (Progress::keyWord(w) == q.key) { word = &w; break; }
  bool wordKnown = word && ok && before < KNOWN_STEP && after >= KNOWN_STEP;
  const Kana* k = q.kanaIdx >= 0 ? &data.kana[q.kanaIdx] : nullptr;
  if (ok) {
    score++;
    if (k && before < KNOWN_STEP && after >= KNOWN_STEP) {
      newlyKnown.push_back(k->glyph); feedbackNote = "Known! " + k->glyph + " reached step 4.";
      if (mode != Mode::Timed) celebrate(1800, false);
    }
    else if (k) {
      Time wait = prog.nextGainIn(q.key);
      int h = int((wait + 3599) / 3600);
      feedbackNote = after > before ? "Step " + std::to_string(after) + " of 4"
                   : after >= KNOWN_STEP ? "Known"
                   : wait > 0 ? "Step " + std::to_string(after) + " of 4. Next step in " + std::to_string(h) + (h == 1 ? " hour" : " hours") + ": spacing makes it stick."
                   : "Step " + std::to_string(after) + " of 4 (2 steps a sitting at most)";
    }
    else if (wordKnown) { feedbackNote = "Known word! " + word->kana + " = " + word->english; if (mode != Mode::Timed) celebrate(1800, false); }
    else if (word && mode == Mode::Pack) feedbackNote = word->kana + " (" + word->romaji + ") = " + word->english;
  } else {
    if (k) { auto it = data.hints.find(k->glyph); feedbackNote = it != data.hints.end() ? it->second : ""; }
    else if (word) feedbackNote = word->kana + " (" + word->romaji + ") = " + word->english;
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
  else if (ok && (k || word) && mode == Mode::Review) {
    const Stat& st = prog.stats[q.key];
    if (st.srs > 0 && st.due > prog.now()) {
      int dd = int((st.due - prog.now() + 43200) / 86400);
      feedbackNote = "Next review in " + std::to_string(dd) + (dd == 1 ? " day." : " days.");
    }
  }
  snd::play(wordKnown || (!newlyKnown.empty() && newlyKnown.back() == (k ? k->glyph : "") && ok) ? snd::Sfx::Known : ok ? snd::Sfx::Right : snd::Sfx::Wrong);
  if (prog.goalJustMet) {
    petHopPending = true;  // Kitsu wakes up and hops when you're back on Home
    snd::play(snd::Sfx::Goal);
    if (mode != Mode::Timed) celebrate(2400, true);
    goalMet = true;
    feedbackNote = "Daily goal reached! " + std::to_string(prog.streak) + "-day streak." + (feedbackNote.empty() ? "" : " " + feedbackNote);
  }
  for (auto& u : prog.checkUnlocks(data)) pendingUnlocks.push_back(u);
  earned(prog.checkAchievements(data));
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
  if (id >= B_CHIP0 && id < B_CHIP0 + 7) { chipTap(id - B_CHIP0); return; }
  if (paused) {
    if (id == B_RESUME) paused = false;
    else if (id == B_MENU) { paused = false; holdSession(); go(Screen::Home); }
    else if (id == B_QUIT) running = false;
    return;
  }
  switch (screen) {
    case Screen::Home:
      if (int idx = homeItemIndex(id); idx >= 0) homeSel = idx;  // the focus follows taps
      if (id == B_HOME_GO && resumeSession()) {}
      else if (id == B_CART0 || id == B_HOME_GO) { if (prog.lessonPending(sc)) openLesson(sc, prog.lessons[si(sc)], false); else startSession(); }
      else if (id == B_CART0 + 1) go(Screen::Stages);
      else if (id == B_CART0 + 2) go(Screen::Chart);
      else if (id == B_CART0 + 3) go(Screen::Settings);
      else if (id == B_MODE0) startMode(Mode::Review);
      else if (id == B_MODE0 + 1) startMode(Mode::Mixup);
      else if (id == B_MODE0 + 2) startMode(Mode::Timed);
      else if (id == B_MODE0 + 3) go(Screen::Stats);
      else if (id == B_MODE0 + 4) go(Screen::Packs);
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
      if (q.intro) {  // new-word card: "Got it" marks the word as met and moves on
        if (id == B_CONTINUE) { prog.introduce(q.key); save(); nextQuestion(false); }
        break;
      }
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
      else if (id == B_HINT && !checked && q.writeLevel > 0) showHint = !showHint;  // guided writes: the guide stays on
      else if (id == B_SHOWME && !checked && q.type == QType::Write) { showHint = true; playStrokes(); }
      break;
    case Screen::Result:
      if (id == B_AGAIN) {
        if (!pendingUnlocks.empty()) go(Screen::Unlocked);
        else if (mode == Mode::Focus) startSession(focusKana);
        else startMode(mode);  // same mode again (review falls back to practice once nothing is due)
      }
      else if (id == B_HOME) go(!pendingUnlocks.empty() ? Screen::Unlocked : mode == Mode::Pack ? Screen::Packs : Screen::Home);
      break;
    case Screen::Packs:
      if (id >= B_PACK0 && id < B_PACK0 + int(data.packs.size())) {
        int i = id - B_PACK0;
        if (i == packSel) startPack(i); else packSel = i;  // tap to select, tap again to start
      } else if (id == B_PACK_START) startPack(packSel);
      else if (id == B_PACK_BROWSE) { wordSel = 0; go(Screen::PackWords); }
      else if (id == B_BACK) go(Screen::Home);
      break;
    case Screen::PackWords: {
      int n = int(data.packs[packSel].words.size()), page = wordSel / 15;
      if (id >= B_WORD0 && id < B_WORD0 + 15) wordSel = std::min(n - 1, page * 15 + id - B_WORD0);
      else if (id == B_WPREV && page > 0) wordSel = (page - 1) * 15;
      else if (id == B_WNEXT && (page + 1) * 15 < n) wordSel = (page + 1) * 15;
      else if (id == B_BACK) go(Screen::Packs);
      break; }
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
        if (r == setRow) confirmSetting(setGroup, r); else { setRow = r; resetArmed = resetSetArmed = false; go(Screen::SettingsGroup); }
      } else if (id >= B_SSEG0 && id < B_SSEG0 + n * 8) {
        int r = (id - B_SSEG0) / 8, opt = (id - B_SSEG0) % 8; setRow = r;
        auto rows = settingRows(setGroup);
        for (int guard = 0; guard < 8 && settingRows(setGroup)[r].cur != opt; guard++)
          changeSetting(setGroup, r, settingRows(setGroup)[r].cur < opt ? +1 : -1);
        (void)rows;
      } else if (id >= B_SDEC0 && id < B_SDEC0 + n) { setRow = id - B_SDEC0; changeSetting(setGroup, setRow, -1); }
      else if (id >= B_SINC0 && id < B_SINC0 + n) { setRow = id - B_SINC0; changeSetting(setGroup, setRow, +1); }
      else if (id == B_SUP && setRow > 0) { setRow--; resetArmed = resetSetArmed = false; }
      else if (id == B_SDOWN && setRow < n - 1) { setRow++; resetArmed = resetSetArmed = false; }
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
      if (welcomeStep == 0) {  // pick a look: applies straight away, so the whole app shows it
        if (id == B_WTHEME0 || id == B_WTHEME0 + 1) { prog.settings.light = id == B_WTHEME0; applyTheme(prog.settings.light); save(); }
        else if (id == B_WEL_NEXT) welcomeStep = 1;
        else if (id == B_BACK && welcomeAgain) { welcomeAgain = false; go(Screen::SettingsGroup); }
      } else if (welcomeStep == 1) {  // background: also live, the real backdrop changes behind the tiles
        if (id == B_WBG0 || id == B_WBG0 + 1) { prog.settings.backdrop = id == B_WBG0 ? 1 : 0; save(); }
        else if (id == B_WEL_NEXT) welcomeStep = 2;
      } else if (welcomeStep == 2) {  // vibration: each choice buzzes at its strength so you can feel it
        if (id >= B_WRUM0 && id < B_WRUM0 + 3) {
          prog.settings.rumble = id - B_WRUM0; snd::setRumble(prog.settings.rumble); save();
          if (prog.settings.rumble) snd::rumble(snd::Sfx::Known);
        } else if (id == B_WEL_NEXT) { welcomeStep = 3; welcomeSel = 0; }
      } else if (id >= B_WELCOME0 && id < B_WELCOME0 + 4) welcomeChoice(id - B_WELCOME0);
      break;
    case Screen::Stats:
      if (id >= B_HEAT0 && id < B_HEAT0 + int(data.kana.size())) {  // open that kana in the chart
        int k = id - B_HEAT0, st = data.stageOfKana[k];
        chartTab = st < 10 ? 0 : st < 12 ? 1 : st < 14 ? 2 : 3; go(Screen::Chart); chartSel = k;
        for (auto& c : chartCells(prog.settings.script, chartTab)) if (c.kana == k) chartScroll = std::max(0, c.row - 2);
      } else if (id == B_SCRIPT_H) setScript(Script::Hiragana);
      else if (id == B_SCRIPT_K) setScript(Script::Katakana);
      else if (id == B_AWARDS) go(Screen::Awards);
      else if (id == B_PET) go(Screen::Pet);
      else if (id == B_BACK) go(Screen::Home);
      break;
    case Screen::Pet:
      if (id >= B_ACC0 && id < B_ACC0 + int(petAccessories().size())) {
        int i = id - B_ACC0; petSel = i;
        if (prog.petAccUnlocked(i)) { prog.pet.acc = prog.pet.acc == i ? -1 : i; save(); snd::play(snd::Sfx::Tap); }
      } else if (id == B_PET_NONE) { prog.pet.acc = -1; save(); }
      else if (id == B_PET_RENAME) go(Screen::PetName);
      else if (id == B_BACK) go(Screen::Stats);
      break;
    case Screen::PetEvolve:
      if (id == B_PET_CONT) { prog.pet.seenStage = prog.petStage(); save(); go(Screen::Home); }
      break;
    case Screen::PetName:
      if (id >= B_KANA0 && id < B_KANA0 + 50 && int(Data::partNames(typed).size()) < PET_NAME_MAX) typed += kanaKeyAt(id - B_KANA0, false);
      else if (id >= B_DAKU && id <= B_SMALLK) typed = modifyLast(typed, id - B_DAKU, false);
      else if (id == B_CHOON && !typed.empty() && int(Data::partNames(typed).size()) < PET_NAME_MAX) typed += "ー";
      else if (id == B_BKSP && !typed.empty()) popChar(typed);
      else if (id == B_CHECK && !typed.empty()) { prog.pet.name = typed; save(); go(Screen::Pet); }
      else if (id == B_BACK) go(Screen::Pet);
      break;
    case Screen::Awards:
      if (id >= B_AWARD0 && id < B_AWARD0 + int(achievementList().size())) awardSel = id - B_AWARD0;
      else if (id == B_BACK) go(Screen::Stats);
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
  if (screen == Screen::SettingsGroup && !paused) {  // drag the settings list with the stylus (groups with 6+ rows)
    const int VIS = 5, PITCH = 66, n = int(settingRows(setGroup).size());
    if (type == 0 && y >= 62 && y < 404) { listTouch = true; listDrag = false; listDragY = y; listDragFirst = setFirst; }
    else if (type == 1 && listTouch && n > VIS) {
      int dy = y - listDragY;
      if (!listDrag && std::abs(dy) > 14) { listDrag = true; pressedId = -1; }  // a drag, not a tap: cancel the button
      if (listDrag) {
        int nf = std::clamp(listDragFirst - int(std::lround(dy / float(PITCH))), 0, n - VIS);
        if (nf != setFirst) { setFirst = nf; setRow = std::clamp(setRow, setFirst, setFirst + VIS - 1); resetArmed = resetSetArmed = false; }
        return;
      }
    } else if (type == 2 && listTouch) { listTouch = false; if (listDrag) { listDrag = false; pressedId = -1; return; } }
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
    case Screen::Home: {
      // focus: 0 pill, 1 script switch (top row); 2-5 and 6-9 the two tile rows (4 columns)
      auto f = homeFocus(); int n = int(f.size()); homeSel = std::clamp(homeSel, 0, n - 1);
      int s = homeSel;
      if (s <= 1) {
        if (b == L) s = 0; else if (b == R) s = 1;
        else if (b == D) s = s == 0 ? 2 : 5;
      } else {
        int t = s - 2, row = t / 4, col = t % 4;
        if (b == L && col > 0) s--; else if (b == R && col < 3) s++;
        else if (b == U) s = row == 0 ? (col < 3 ? 0 : 1) : s - 4;
        else if (b == D && row == 0) s += 4;
      }
      if (s != homeSel) { homeSel = s; break; }
      if (b == A) press(f[homeSel]);
      else if (b == ST) press(B_HOME_GO);
      else if (b == Y) setScript(sc == Script::Hiragana ? Script::Katakana : Script::Hiragana);
      break;
    }
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
      if (q.intro) { if (b == A || b == X || b == ST) press(B_CONTINUE); break; }
      bool choice = !q.options.empty(); int n = int(q.options.size());
      if (checked) { if (b == A || b == X) press(B_CONTINUE); break; }
      if (choice) {
        if (cursor < 0) {  // the first d-pad/stick press only shows the cursor, on the first tile
          if (b == L || b == R || b == U || b == D) { cursor = 0; break; }
          if (b == A) break;  // nothing highlighted yet: A does nothing
        }
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
    case Screen::Packs: {
      int n = int(data.packs.size());
      if (b == L) packSel = (packSel + n - 1) % n; else if (b == R) packSel = (packSel + 1) % n;
      else if (b == U) packSel = (packSel + n - 3) % n; else if (b == D) packSel = (packSel + 3) % n;
      else if (b == A || b == ST) press(B_PACK_START);
      else if (b == Y) press(B_PACK_BROWSE);
      else if (b == B) press(B_BACK);
      break; }
    case Screen::PackWords: {
      int n = int(data.packs[packSel].words.size());
      if (b == L) wordSel = std::max(0, wordSel - 1); else if (b == R) wordSel = std::min(n - 1, wordSel + 1);
      else if (b == U) wordSel = std::max(0, wordSel - 3); else if (b == D) wordSel = std::min(n - 1, wordSel + 3);
      else if (b == LS) press(B_WPREV); else if (b == RS) press(B_WNEXT);
      else if (b == B) press(B_BACK);
      break; }
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
      if (welcomeStep == 0) {
        if (b == L) press(B_WTHEME0); else if (b == R) press(B_WTHEME0 + 1);
        else if (b == A || b == ST) press(B_WEL_NEXT);
        else if (b == B && welcomeAgain) press(B_BACK);
      } else if (welcomeStep == 1) {
        if (b == L) press(B_WBG0); else if (b == R) press(B_WBG0 + 1);
        else if (b == A || b == ST) press(B_WEL_NEXT);
        else if (b == B) welcomeStep = 0;
      } else if (welcomeStep == 2) {
        if (b == L) press(B_WRUM0 + std::max(0, prog.settings.rumble - 1)); else if (b == R) press(B_WRUM0 + std::min(2, prog.settings.rumble + 1));
        else if (b == A || b == ST) press(B_WEL_NEXT);
        else if (b == B) welcomeStep = 1;
      } else {
        if (b == U) welcomeSel = (welcomeSel + 3) % 4; else if (b == D) welcomeSel = (welcomeSel + 1) % 4;
        else if (b == A || b == ST) press(B_WELCOME0 + welcomeSel);
        else if (b == B) welcomeStep = 2;  // back to vibration
      }
      break;
    case Screen::Stats:
      if (b == B) press(B_BACK);
      else if (b == Y) press(sc == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H);
      else if (b == X) press(B_AWARDS);
      else if (b == ST) press(B_PET);
      break;
    case Screen::Pet: {
      int n = int(petAccessories().size());
      if (b == L && petSel % 4 > 0) petSel--; else if (b == R && petSel % 4 < 3 && petSel < n - 1) petSel++;
      else if (b == U && petSel >= 4) petSel -= 4; else if (b == D && petSel + 4 < n) petSel += 4;
      else if (b == A) press(B_ACC0 + petSel); else if (b == Y) press(B_PET_RENAME); else if (b == X) press(B_PET_NONE);
      else if (b == B) press(B_BACK);
      break; }
    case Screen::PetEvolve:
      if (b == A || b == B || b == ST || b == X) press(B_PET_CONT);
      break;
    case Screen::PetName:
      if (b == L) kbMove(-1, 0); else if (b == R) kbMove(1, 0); else if (b == U) kbMove(0, -1); else if (b == D) kbMove(0, 1);
      else if (b == A) { int id = kbItemId(keyRow, keyCol); if (id >= 0) press(id); }
      else if (b == X || b == ST) press(B_CHECK);
      else if (b == B) { if (typed.empty()) press(B_BACK); else press(B_BKSP); }
      break;
    case Screen::Awards: {
      int n = int(achievementList().size());
      if (b == L) awardSel = std::max(0, awardSel - 1); else if (b == R) awardSel = std::min(n - 1, awardSel + 1);
      else if (b == U) awardSel = std::max(0, awardSel - 4); else if (b == D) awardSel = std::min(n - 1, awardSel + 4);
      else if (b == B) press(B_BACK);
      break; }
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
    bool pressed = b.id == pressedId;
    // Handheld theme: standard and accent buttons are glossy pills; custom-coloured ones (chart cells, heatmap) stay flat
    bool std0 = sameCol(b.fill, PANEL), acc = sameCol(b.fill, ACCENT);
    SDL_Rect face = b.r;
    if (!b.enabled && (std0 || acc || sameCol(b.fill, BG))) face = pill(bot, b.r, std::min(b.r.h / 2, std::max(b.radius, 18)), 2);
    else if (std0 || acc) {
      int rad = std::min(b.r.h / 2, std::max(b.radius, 18));
      bool ring = !sameCol(b.line, LINE) && !sameCol(b.line, ACCENT) && !sameCol(b.line, PANEL);  // a highlight (cursor, selection)
      if (ring || b.lineW >= 4) {  // drawn behind the pill: a coloured ring around it
        Col rc = ring ? b.line : b.line; SDL_Rect o{b.r.x - 6, b.r.y - 6, b.r.w + 12, b.r.h + 15};
        fillRound(bot, o, rad + 6, rc); fillRound(bot, SDL_Rect{o.x + 3, o.y + 3, o.w - 6, o.h - 6}, rad + 3, BG2);
      }
      face = pill(bot, b.r, rad, acc ? 1 : 0, pressed);
    } else {
      if (pressed) fill = mix(fill, FG, 0.22f);
      if (sameCol(line, fill)) fillRound(bot, b.r, b.radius, fill); else boxRound(bot, b.r, b.radius, fill, line, b.lineW);
    }
    b.r = face;
    SDL_Rect in{b.r.x + 6, b.r.y + 3, b.r.w - 12, b.r.h - 6};
    if (b.icon == 3 || b.icon == 4) {  // toggle: label over a little on/off switch
      bool on = b.icon == 4;
      drawText(bot, b.label, SDL_Rect{in.x, in.y + 2, in.w, in.h - 26}, txt, b.font);
      SDL_Rect sw{b.r.x + b.r.w / 2 - 20, b.r.y + b.r.h - 30, 40, 20};
      fillRound(bot, sw, 10, on ? ACCENT : mix(PANEL, FG, 0.25f));
      fillCircle(bot, on ? sw.x + sw.w - 10 : sw.x + 10, sw.y + 10, 7, PANEL);
    } else if (b.sub.empty()) drawText(bot, b.label, in, txt, b.font);
    else {
      int sh = std::max(16, in.h / 3);
      drawText(bot, b.label, SDL_Rect{in.x, in.y + 2, in.w, in.h - sh - 2}, txt, b.font);
      drawText(bot, b.sub, SDL_Rect{in.x, in.y + in.h - sh - 2, in.w, sh}, sub, b.subFont);
    }
    if (b.icon == 1) lineIcon(bot, "lock", SDL_Rect{b.r.x + b.r.w - 26, b.r.y + 6, 18, 18}, sub, 2.4f);
    if (b.icon == 2) lineIcon(bot, "check", SDL_Rect{b.r.x + b.r.w - 28, b.r.y + 5, 20, 20}, txt, 3.f);
  }
}

void App::save() {
  bool ok = prog.save(statePath);
  if ((!ok || !prog.saveError.empty()) && saveLogs < 10) {
    saveLogs++; fprintf(stderr, "  save %s: %s\n", ok ? "warning" : "FAILED", prog.saveError.c_str()); fflush(stderr);
  }
  backup(false);
}

void App::backup(bool force) {
  if (backupPath.empty()) return;
  unsigned now = SDL_GetTicks();
  if (!force && lastBackup && now - lastBackup < 60000) return;
  if (prog.save(backupPath)) lastBackup = now ? now : 1;
}

void App::appTile(Win& w, int x, int y, int size) {
  ModeCol keep = MC_ORANGE; (void)keep;
  Col a = ACCENT, al = ACC_LINE, as = ACC_SH, ah = ACC_HI, alo = ACC_LO, at = ACCENT_TXT, ao = ON_ACCENT;
  setAccent(MC_ORANGE);
  SDL_Rect f = pill(w, SDL_Rect{x, y, size, size - size / 20}, size / 4, 1);
  drawText(w, "あ", SDL_Rect{f.x, f.y - size / 30, f.w * 9 / 10, f.h * 9 / 10}, hex(0xFFFFFF), size >= 120 ? F_BIG : F_MID);
  int b = size * 3 / 10; SDL_Rect bd{f.x + f.w - b - size / 14, f.y + f.h - b - size / 14, b, b};
  fillRound(w, SDL_Rect{bd.x, bd.y + 3, bd.w, bd.h}, b / 3, ACC_SH); fillRound(w, bd, b / 3, hex(0xFFFFFF));
  drawText(w, "ア", bd, hex(0xE8590C), F_SMALL);
  ACCENT = a; ACC_LINE = al; ACC_SH = as; ACC_HI = ah; ACC_LO = alo; ACCENT_TXT = at; ON_ACCENT = ao;
}

void App::medal(Win& w, SDL_Rect box, bool earned) {
  Col rib = earned ? hex(0xF07A4F) : DIS_LINE, gold = earned ? hex(0xF0C419) : (gLight ? hex(0xE4E8ED) : hex(0x3A414D)),
      rim = earned ? hex(0xB8860B) : DIS_LINE, ink = earned ? hex(0x7A5A00) : DIS_TXT;
  float s = box.w / 24.f;
  auto P = [&](float x, float y) { return Pt{x * 109.f / 24.f, y * 109.f / 24.f}; };
  drawStrokeIn(w, Stroke{P(7, 2.5f), P(10.5f, 9)}, box, rib, 3.2f * s);
  drawStrokeIn(w, Stroke{P(17, 2.5f), P(13.5f, 9)}, box, rib, 3.2f * s);
  int cx = box.x + box.w / 2, cy = box.y + int(14.5f * s), r = int(7 * s);
  drawRing(w, cx, cy, r, r, 1.f, rim, rim);
  drawRing(w, cx, cy, r - int(1.6f * s), r - int(1.6f * s), 1.f, gold, gold);
  lineIcon(w, "star", SDL_Rect{cx - int(5 * s), cy - int(5 * s), int(10 * s), int(10 * s)}, ink, 2.f);
}

// streak colour: this week's goal days on Home warm up as the streak grows (orange, red at 7, blue at 30, purple at 100)
static Col streakColour(int streak) {
  return streak >= 100 ? hex(0x7C4DDB) : streak >= 30 ? hex(0x2F7BEA) : streak >= 7 ? hex(0xE0451B) : hex(0xF07A1A);
}

int App::seasonNow() const {
  int b = prog.settings.backdrop;
  if (b <= 0) return -1;
  if (b >= 2) return b - 2;
  time_t now = prog.now(); tm lt{}; localtime_r(&now, &lt);
  int m = lt.tm_mon + 1;  // spring Mar-May, summer Jun-Aug, autumn Sep-Nov, winter Dec-Feb
  return m >= 3 && m <= 5 ? 0 : m >= 6 && m <= 8 ? 1 : m >= 9 && m <= 11 ? 2 : 3;
}
bool App::menuScreen() const {
  return screen != Screen::Drill && screen != Screen::Lesson && screen != Screen::FreeWrite && !paused;
}

static std::string stageLabel(int st) { return st == STAGE_COUNT - 1 ? "Extras" : "Stage " + std::to_string(st + 1); }

void App::header(const std::string& title, const std::string& right, bool coloured, int indent) {
  Col bar = coloured ? ACCENT : PANEL, ink = coloured ? ON_ACCENT : FG, soft = coloured ? ON_ACCENT : DIM;
  fillRect(top, SDL_Rect{0, 0, LW, 50}, bar);
  if (!coloured) fillRect(top, SDL_Rect{0, 50, LW, 2}, mix(BG, FG, 0.1f));
  int rw = 0;
  if (!right.empty()) {  // context on the right (script, count, stage...); nothing when there's nothing useful to say
    rw = std::min(300, textWidth(top, F_MICRO, right) + 4);
    setBold(true); drawText(top, right, SDL_Rect{LW - 20 - rw, 0, rw, 50}, soft, F_MICRO, A_RIGHT); setBold(false);
  }
  setBold(true); drawText(top, title, SDL_Rect{20 + indent, 0, LW - 60 - rw - indent, 50}, ink, F_TINY, A_LEFT); setBold(false);
}

void App::chips(const std::vector<std::pair<std::string, std::string>>& items) {
  // a tighter layout when the row would overflow (hints used to be dropped silently: the Home quit hint went missing)
  int full = 16; for (auto& [k, label] : items) full += 4 + std::max(28, textWidth(bot, F_MICRO, k) + 14) + 8 + textWidth(bot, F_MICRO, label) + 14 + 8;
  const bool tight = full > LW - 8;
  const int pad = tight ? 8 : 14, gap = tight ? 5 : 8;
  int x = tight ? 10 : 16;
  for (auto& [k, label] : items) {
    int kw = std::max(28, textWidth(bot, F_MICRO, k) + (tight ? 10 : 14)), lw = textWidth(bot, F_MICRO, label);
    int w = 4 + kw + (tight ? 6 : 8) + lw + pad;
    if (x + w > LW - 4) { if (getenv("KT_TEXTLOG")) fprintf(stderr, "chip dropped: %s %s\n", k.c_str(), label.c_str()); break; }
    fillRound(bot, SDL_Rect{x, 438, w, 34}, 17, PANEL);
    fillRound(bot, SDL_Rect{x + 3, 441, kw, 28}, 14, FG);
    if (k == "+") {  // the d-pad, drawn as a cross
      int cx = x + 3 + kw / 2, cy = 455;
      fillRound(bot, SDL_Rect{cx - 9, cy - 3, 18, 6}, 2, BG); fillRound(bot, SDL_Rect{cx - 3, cy - 9, 6, 18}, 2, BG);
    } else drawText(bot, k, SDL_Rect{x + 3, 441, kw, 28}, BG, F_MICRO);
    drawText(bot, label, SDL_Rect{x + 4 + kw + (tight ? 6 : 8), 438, lw + 4, 34}, FG, F_MICRO, A_LEFT);
    // every hint can be tapped with the stylus and does what its button does (user asked 2026-11-17); the d-pad "+"
    // and F+START Quit (too easy to hit by accident) stay hints only. L/R: left half = L, right half = R.
    SDL_Rect hit{x - 4, 432, w + 8, 46};
    if (label == "Awards" && screen == Screen::Stats) { addBtn(hit, B_AWARDS, ""); lastBtn().hidden = true; }
    else if (label == "Your fox" && screen == Screen::Stats) { addBtn(hit, B_PET, ""); lastBtn().hidden = true; }
    else if (k == "F") { if (!paused) { addBtn(hit, B_PAUSE, ""); lastBtn().hidden = true; } }
    else if (k == "L/R") {
      addBtn(SDL_Rect{hit.x, hit.y, hit.w / 2, hit.h}, B_CHIP0 + 5, ""); lastBtn().hidden = true;
      addBtn(SDL_Rect{hit.x + hit.w / 2, hit.y, hit.w - hit.w / 2, hit.h}, B_CHIP0 + 6, ""); lastBtn().hidden = true;
    } else {
      static const char* KEYS[5] = {"A", "B", "X", "Y", "START"};
      int ci = -1; for (int i = 0; i < 5; i++) if (k == KEYS[i]) ci = i;
      if (k == "L") ci = 5; else if (k == "R") ci = 6;
      if (ci >= 0) { addBtn(hit, B_CHIP0 + ci, ""); lastBtn().hidden = true; }
    }
    x += w + gap;
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
  card(w, SDL_Rect{r.x, r.y, r.w, r.h - 5}, 18);
  drawText(w, label, SDL_Rect{r.x + 14, r.y + 8, r.w - 28, 20}, DIM, F_MICRO, A_LEFT);
  if (textWidth(w, font, value) > r.w - 28) drawWrapped(w, value, SDL_Rect{r.x + 14, r.y + 36, r.w - 28, r.h - 46}, FG, font);  // long values wrap
  else drawText(w, value, SDL_Rect{r.x + 14, r.y + 30, r.w - 28, r.h - 36}, FG, font, A_LEFT);
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
  { // a notepad page: paper, faint ruled lines, a margin line and a coloured binding strip
    SDL_Rect pg{c.x - 6, c.y - 6, c.w + 12, c.h + 12};
    fillRound(bot, SDL_Rect{pg.x, pg.y + 5, pg.w, pg.h}, 16, CARD_SH);
    fillRound(bot, pg, 16, CANVAS);
    for (int y = pg.y + 30; y < pg.y + pg.h - 4; y += 33) fillRect(bot, SDL_Rect{pg.x + 4, y, pg.w - 8, 2}, mix(CANVAS, PAPER_LINE, 0.7f));
    fillRect(bot, SDL_Rect{pg.x + 34, pg.y + 14, 2, pg.h - 18}, mix(CANVAS, PAPER_MARGIN, 0.6f));
    fillRound(bot, SDL_Rect{pg.x, pg.y, pg.w, 18}, 16, ACCENT); fillRect(bot, SDL_Rect{pg.x, pg.y + 9, pg.w, 9}, ACCENT);
    for (int k = 0; k < 5; k++) fillCircle(bot, pg.x + pg.w / 2 - 72 + k * 36, pg.y + 9, 5, BG2);
  }
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
  { // each mode has its own colour (Handheld theme); it tints buttons, headers, bars and highlights
    ModeCol mc = MC_ORANGE;
    auto byMode = [&]() { return mode == Mode::Review ? MC_BLUE : mode == Mode::Mixup ? MC_PURPLE : mode == Mode::Timed ? MC_RED
                               : mode == Mode::Pack ? MC_GREEN : MC_ORANGE; };
    switch (screen) {
      case Screen::Drill: case Screen::Result: mc = byMode(); break;
      case Screen::Stats: case Screen::Awards: case Screen::Settings: case Screen::SettingsGroup: mc = MC_TEAL; break;
      case Screen::Packs: case Screen::PackWords: mc = MC_GREEN; break;
      case Screen::Pet: case Screen::PetEvolve: case Screen::PetName: mc = MC_ORANGE; break;
      default: break;
    }
    setAccent(mc);
  }
  setSeason(seasonNow());
  // where you write (lessons, writing drills, free writing) kana always use the handwriting font, so they match the
  // stroke examples; everywhere else the Kana font setting decides (print is what you'll read on signs and in books)
  setKanaHand(prog.settings.kanaHand || (!paused && (screen == Screen::Lesson || screen == Screen::FreeWrite ||
                                                     (screen == Screen::Drill && q.type == QType::Write))));
  drawBackdrop(top, true); drawBackdrop(bot, false); buttons.clear();
  if (season() >= 0 && menuScreen()) {  // petals / fireflies / leaves / snow drift behind the cards and buttons on menus (still when motion is off)
    float t = motion ? SDL_GetTicks() / 1000.f : 0.f;
    drawSeasonParticles(top, true, t); drawSeasonParticles(bot, false, t);
  }
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
    case Screen::Packs: drawPacks(); break;
    case Screen::PackWords: drawPackWords(); break;
    case Screen::Awards: drawAwards(); break;
    case Screen::Pet: drawPet(); break;
    case Screen::PetEvolve: drawPetEvolve(); break;
    case Screen::PetName: drawPetName(); break;
  }
  drawButtons();
  if (transStart) {  // new screen fades in from the backdrop colour
    float t = std::clamp((SDL_GetTicks() - transStart) / float(TRANS_MS), 0.f, 1.f), a = (1.f - t) * (1.f - t);
    for (Win* w : {&top, &bot}) {
      SDL_SetRenderDrawBlendMode(w->r, SDL_BLENDMODE_BLEND);
      fillRect(*w, SDL_Rect{0, 0, LW, LH}, Col{BG.r, BG.g, BG.b, Uint8(220 * a)});
      SDL_SetRenderDrawBlendMode(w->r, SDL_BLENDMODE_NONE);
    }
  }
  drawConfetti();
  drawToast();
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
  Col cols[6];  // the six mode colours, so celebrations match the rest of the app
  for (int m = 0; m < 6; m++) cols[m] = modeCol(ModeCol(m));
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

std::vector<App::HomeItem> App::homeItems() const {
  Script sc = prog.settings.script; int scI = si(sc);
  int due = prog.dueCount(data, sc), mixN = int(mixupSets(data, prog, sc).size()), best = prog.timedBest[scI], streak = prog.currentStreak();
  int packsReady = 0; for (int i = 0; i < int(data.packs.size()); i++) if (int(readyWords(data, prog, i).size()) >= PACK_MIN) packsReady++;
  int curSt = prog.currentStage(sc);
  return {  // modes are solid colour tiles; places (stages, chart, settings) are white with a coloured icon
    {B_MODE0, "Review", "review", due ? std::to_string(due) + " due" : "none due", MC_BLUE, modeAvailable(Mode::Review), true},
    {B_MODE0 + 1, "Mix-ups", "mixups", mixN ? std::to_string(mixN) + (mixN == 1 ? " set" : " sets") : "none yet", MC_PURPLE, modeAvailable(Mode::Mixup), true},
    {B_MODE0 + 4, "Words", "words", packsReady ? std::to_string(packsReady) + (packsReady == 1 ? " pack" : " packs") : "soon", MC_GREEN, !data.packs.empty(), true},
    {B_MODE0 + 2, "Timed", "timed", best ? "best " + std::to_string(best) : "60 seconds", MC_RED, true, true},
    {B_MODE0 + 3, "Stats", "stats", streak ? std::to_string(streak) + "-day streak" : "your progress", MC_TEAL, true, true},
    {B_CART0 + 1, "Stages", "map", std::to_string(curSt) + " of " + std::to_string(STAGE_COUNT), MC_ORANGE, true, false},
    {B_CART0 + 2, "Kana chart", "grid", std::to_string(prog.knownCount(data, sc)) + " known", MC_BLUE, true, false},
    {B_CART0 + 3, "Settings", "gear", "theme, sound", MC_TEAL, true, false},
  };
}
std::vector<int> App::homeFocus() const {
  std::vector<int> f{B_HOME_GO, prog.settings.script == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H};
  for (auto& it : homeItems()) f.push_back(it.id);
  return f;
}
int App::homeItemIndex(int id) const {
  auto f = homeFocus();
  for (int i = 0; i < int(f.size()); i++) if (f[i] == id || (i == 0 && id == B_CART0)) return i;
  return -1;
}

void App::drawHome() {
  Script sc = prog.settings.script; int scI = si(sc);
  const auto& stages = data.stages[scI];
  int curSt = prog.currentStage(sc); const Stage& st = stages[curSt];
  std::time_t tnow = std::time_t(prog.now()); std::tm lt{}; localtime_r(&tnow, &lt);
  { // a greeting in Japanese and English by time of day, with a little hinomaru beside it
    std::string g = lt.tm_hour >= 5 && lt.tm_hour < 11 ? "おはよう · Good morning" : lt.tm_hour >= 11 && lt.tm_hour < 18 ? "こんにちは · Hello"
                  : "こんばんは · Good evening";
    static const char* WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* MO[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char when[40]; snprintf(when, sizeof when, "%s %d %s · %02d:%02d", WD[lt.tm_wday], lt.tm_mday, MO[lt.tm_mon], lt.tm_hour, lt.tm_min);
    header(g, when, false, 46);
    SDL_Rect fl{20, 13, 36, 24};
    boxRound(top, fl, 3, hex(0xFFFFFF), mix(BG, FG, 0.35f), 1);
    drawRing(top, fl.x + fl.w / 2, fl.y + fl.h / 2, 7, 7, 1.f, hex(0xBC002D), hex(0xBC002D));  // smooth disc
  }
  // ---- stage card
  SDL_Rect sc1{22, 64, 392, 232}; card(top, sc1, 20);
  int known = prog.stageKnown(data, sc, curSt), n = int(st.kana.size());
  drawText(top, "れんしゅう · STAGE " + std::to_string(curSt + 1) + " OF " + std::to_string(STAGE_COUNT), SDL_Rect{42, 76, 260, 22}, ACCENT_TXT, F_MICRO, A_LEFT);
  drawText(top, std::to_string(known) + " / " + std::to_string(n) + " known", SDL_Rect{294, 76, 104, 22}, DIM, F_MICRO, A_RIGHT);
  drawText(top, st.name, SDL_Rect{42, 100, 352, 40}, FG, F_SMALL, A_LEFT);
  const int cs = 50, maxChips = 6;  // 6 chips fill the card; with more kana, 5 chips + a "+N" chip
  int show = n > maxChips ? maxChips - 1 : n;
  for (int i = 0; i < show; i++) {
    int ki = st.kana[i]; KanaState ks = prog.state(data, ki);
    SDL_Rect r{42 + i * (cs + 8), 146, cs, cs};
    if (ks == KanaState::Known) { fillRound(top, r, 14, ACCENT); drawText(top, data.kana[ki].glyph, r, hex(0xFFFFFF), F_SMALL); }
    else if (ks == KanaState::Learning || ks == KanaState::Weak) { boxRound(top, r, 14, mix(PANEL, ACCENT, 0.12f), ACCENT, 2); drawText(top, data.kana[ki].glyph, r, FG, F_SMALL); }
    else { fillRound(top, r, 14, PANEL2); drawText(top, data.kana[ki].glyph, r, ks == KanaState::Locked ? DIS_TXT : FG, F_SMALL); }
  }
  if (n > show) {
    SDL_Rect r{42 + show * (cs + 8), 146, cs, cs};
    boxRound(top, r, 14, PANEL, mix(PANEL, FG, 0.25f), 2);
    drawText(top, "+" + std::to_string(n - show), r, DIM, F_TINY);
  }
  std::string msg;
  if (prog.lessonPending(sc)) msg = "New lesson ready: " + stages[std::min(prog.lessons[scI], STAGE_COUNT - 1)].name + ". Press START.";
  else if (known == n && curSt == STAGE_COUNT - 1) msg = "Every stage is unlocked and known. Keep reviewing!";
  else {
    std::vector<std::string> left;
    for (int ki : st.kana) if (prog.step(Progress::keyKana(data.kana[ki])) < KNOWN_STEP && left.size() < 4) left.push_back(data.kana[ki].glyph);
    std::string next = curSt + 1 < STAGE_COUNT ? "the " + stages[curSt + 1].name : "the last stage";
    if (sc == Script::Hiragana && curSt == KATAKANA_AFTER - 1) next = "katakana";
    msg = curSt == STAGE_COUNT - 1 ? "Last stage! Get " + join(left, " ") + " to step 4." : "Get " + join(left, " ") + " to step 4 to unlock " + next + ".";
  }
  drawWrapped(top, msg, SDL_Rect{42, 210, 352, 80}, DIM, F_TINY);
  // ---- today's goal ring
  int goal = prog.settings.dailyGoal, todayN = prog.today == dayNumber(prog.now()) ? prog.todayCount : 0;
  { SDL_Rect r{428, 64, 190, 232}; card(top, r, 20);
    bool met = todayN >= goal; int cx = r.x + r.w / 2, cy = r.y + 92;
    drawRing(top, cx, cy, 56, 14, float(todayN) / std::max(1, goal), met ? GOOD : ACCENT, TRACK);
    if (met) lineIcon(top, "check", SDL_Rect{cx - 22, cy - 22, 44, 44}, GOOD, 3.4f);
    else drawText(top, std::to_string(todayN) + "/" + std::to_string(goal), SDL_Rect{cx - 46, cy - 22, 92, 44}, FG, F_SMALL);
    drawText(top, "Today's goal", SDL_Rect{r.x, r.y + 160, r.w, 28}, FG, F_TINY);
    drawText(top, met ? std::to_string(todayN) + " answers · met!" : std::to_string(goal - todayN) + " to go", SDL_Rect{r.x, r.y + 190, r.w, 24}, met ? GOOD : DIM, F_MICRO); }
  // ---- streak strip: this week, Monday to Sunday
  { SDL_Rect r{22, 310, 596, 100}; card(top, r, 20);
    int streak = prog.currentStreak();
    const bool lit = streak > 0 && prog.lastGoalDay == dayNumber(prog.now());
    // Kitsu sits in the streak card: asleep until today's goal is met, then awake (with a hop just after)
    const int st = prog.petStage(); const Uint32 nowT = SDL_GetTicks();
    const bool hop = petHopStart && nowT - petHopStart < 1500;
    if (!hop) petHopStart = 0;
    FoxMood mood = !lit ? FoxMood::Asleep : hop ? FoxMood::Happy : FoxMood::Awake;
    float hopY = hop ? -std::fabs(std::sin((nowT - petHopStart) / 1000.f * 7.f)) * 10.f : 0.f;
    drawFox(top, r.x + 48, r.y + r.h - 6 + hopY, 112, petLook(mood));
    if (!lit && prog.petStage() > 0) { float zt = motion ? nowT / 1000.f : 0.f;
      for (int k = 0; k < 2; k++) drawText(top, "z", SDL_Rect{r.x + 78 + k * 8, r.y + 6 - k * 10 - int(std::fmod(zt * 6 + k * 5, 10.f)), 20, 20}, DIM, F_MICRO); }
    const std::string& nm = prog.pet.name;
    std::string title, sub; Col subCol = DIM;
    int left = std::max(0, prog.settings.dailyGoal - (prog.today == dayNumber(prog.now()) ? prog.todayCount : 0));
    if (st == 0) { title = "An egg!"; sub = "Hatches at today's goal"; subCol = ACCENT_TXT; }
    else if (!lit) { title = nm + " is asleep"; sub = std::to_string(left) + (left == 1 ? " more answer" : " more answers") + " to wake her"; subCol = ACCENT_TXT; }
    else { title = nm + " · " + std::to_string(PET_TAILS[st]) + (st == 1 ? " tail" : " tails"); sub = std::to_string(streak) + "-day streak · best " + std::to_string(std::max(streak, prog.bestStreak)); }
    // text column stops before the weekday strip (its labels start at r.x + 280); a long line wraps to two
    const int TW = 172, subH = std::min(wrappedHeight(top, sub, TW, F_MICRO), 48), ty = r.y + (r.h - 34 - subH) / 2;
    setBold(true); drawText(top, title, SDL_Rect{r.x + 100, ty, TW, 34}, FG, F_TINY, A_LEFT); setBold(false);
    drawWrapped(top, sub, SDL_Rect{r.x + 100, ty + 34, TW, subH}, subCol, F_MICRO);
    static const char* KJ[7] = {"月", "火", "水", "木", "金", "土", "日"};
    static const char* EN[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    int todayIdx = (lt.tm_wday + 6) % 7;
    for (int i = 0; i < 7; i++) {
      int dayOff = i - todayIdx; int d = dayNumber(prog.now() + Time(dayOff) * 86400);
      auto it = prog.days.find(d); bool met = it != prog.days.end() && it->second.answers >= goal && dayOff <= 0;
      int cx = r.x + 300 + i * 42, cy = r.y + 40;
      const Col dayCol = streakColour(streak);
      if (met) { drawRing(top, cx, cy, 17, 17, 1.f, dayCol, dayCol); drawText(top, KJ[i], SDL_Rect{cx - 17, cy - 17, 34, 34}, hex(0xFFFFFF), F_MICRO); }
      else if (dayOff == 0) { drawRing(top, cx, cy, 17, 3, 1.f, dayCol, dayCol); drawText(top, KJ[i], SDL_Rect{cx - 17, cy - 17, 34, 34}, FG, F_MICRO); }
      else { drawRing(top, cx, cy, 17, 17, 1.f, PANEL2, PANEL2); drawText(top, KJ[i], SDL_Rect{cx - 17, cy - 17, 34, 34}, DIM, F_MICRO); }
      drawText(top, EN[i], SDL_Rect{cx - 20, cy + 20, 40, 20}, DIM, F_MICRO);
    }
  }
  // ---- script progress
  for (int s = 0; s < 2; s++) {
    int x = 22 + s * 306; Script ss = s ? Script::Katakana : Script::Hiragana;
    drawText(top, s ? "カタカナ" : "ひらがな", SDL_Rect{x, 424, 120, 24}, prog.scriptUnlocked(ss) ? FG : DIM, F_MICRO, A_LEFT);
    if (prog.scriptUnlocked(ss)) {
      int k = prog.knownCount(data, ss), total = data.scriptCount(ss);
      drawText(top, std::to_string(k) + " / " + std::to_string(total), SDL_Rect{x + 190, 424, 100, 24}, DIM, F_MICRO, A_RIGHT);
      bar(top, SDL_Rect{x, 452, 290, 12}, float(k) / total);
    } else {
      lineIcon(top, "lock", SDL_Rect{x + 250, 425, 18, 18}, DIM, 2.4f);
      drawText(top, "Locked", SDL_Rect{x + 190, 424, 56, 24}, DIM, F_MICRO, A_RIGHT);
      fillRound(top, SDL_Rect{x, 452, 290, 12}, 6, TRACK);
    }
  }

  // ---- bottom: big Lesson/Practise pill + script switch, then a 4x2 grid of mode tiles
  auto focus = homeFocus(); homeSel = std::clamp(homeSel, 0, int(focus.size()) - 1);
  bool lesson = prog.lessonPending(sc);
  setAccent(MC_ORANGE);
  if (sessionHeld())
    addBtn(SDL_Rect{16, 16, 452, 84}, B_HOME_GO, "Continue · " + std::to_string(held.index + 1 > held.sessionLen ? held.sessionLen : held.index + 1) + " of " + std::to_string(held.sessionLen), F_SMALL);
  else addBtn(SDL_Rect{16, 16, 452, 84}, B_HOME_GO, lesson ? "Lesson: " + stages[std::min(prog.lessons[scI], STAGE_COUNT - 1)].name : "Practise " + (curSt == STAGE_COUNT - 1 ? std::string("Extras") : "stage " + std::to_string(curSt + 1)), F_SMALL);
  accent(lastBtn()); lastBtn().radius = 30;
  if (homeSel == 0) { lastBtn().line = ACC_LINE; lastBtn().lineW = 4; }
  bool kOpen = prog.scriptUnlocked(Script::Katakana);
  addBtn(SDL_Rect{482, 16, 142, 84}, sc == Script::Hiragana ? B_SCRIPT_K : B_SCRIPT_H, sc == Script::Hiragana ? "あ → ア" : "ア → あ", F_SMALL, kOpen || sc == Script::Katakana);
  lastBtn().radius = 30; if (!kOpen) lastBtn().icon = 1;
  if (homeSel == 1) { lastBtn().line = ACC_LINE; lastBtn().lineW = 4; }
  auto items = homeItems();
  const int TW = 143, TH = 150, GAP = 12, X0 = 16, Y0 = 114;
  for (int i = 0; i < int(items.size()); i++) {
    const HomeItem& it = items[i];
    SDL_Rect r{X0 + (i % 4) * (TW + GAP), Y0 + (i / 4) * (TH + GAP), TW, i / 4 == 1 ? TH - 6 : TH};
    setAccent(it.col);
    if (homeSel == i + 2) {  // focus ring in the tile's colour, with a gap like other selections
      fillRound(bot, SDL_Rect{r.x - 7, r.y - 7, r.w + 14, r.h + 14}, 29, ACC_LINE);
      fillRound(bot, SDL_Rect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 25, BG2);
    }
    SDL_Rect f = pill(bot, r, 22, !it.enabled ? 2 : it.solid ? 1 : 0, pressedId == it.id);
    Col ink = !it.enabled ? DIS_TXT : it.solid ? ON_ACCENT : FG;
    lineIcon(bot, it.icon, SDL_Rect{f.x + f.w / 2 - 22, f.y + 16, 44, 44}, !it.enabled ? DIS_TXT : it.solid ? ON_ACCENT : ACCENT_TXT, 2.4f);
    setBold(true); drawText(bot, it.name, SDL_Rect{f.x + 6, f.y + 66, f.w - 12, 32}, ink, F_TINY); setBold(false);
    drawText(bot, it.sub, SDL_Rect{f.x + 6, f.y + 98, f.w - 12, 24}, it.solid && it.enabled ? mix(ON_ACCENT, ACCENT, 0.15f) : DIM, F_MICRO);
    addBtn(r, it.id, it.name, F_TINY, it.enabled); lastBtn().hidden = true; lastBtn().sub = it.sub;
  }
  setAccent(MC_ORANGE);
  chips({{"A", "Open"}, {"+", "Move"}, {"START", sessionHeld() ? "Continue" : lesson ? "Lesson" : "Practise"}, {"Y", "Script"}, {"F+START", "Quit"}});
}

void App::drawStages() {
  int scI = si(mapScript); const auto& stages = data.stages[scI];
  int curSt = prog.currentStage(mapScript);
  const Stage& st = stages[mapSel];
  header("ステージ · Stages", scriptName(mapScript));
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
  drawWrapped(top, "Each kana needs 4 steps. A right answer adds a step (2 a sitting, then a 4-hour break), a miss takes one away. "
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
    if (i == mapSel) { b.line = ACC_LINE; b.lineW = 4; }
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
  header("あたらしい · New kana", stageLabel(lessonStage) + (lessonReplay ? " · replay" : ""));
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
  appTile(top, 270, 6, 96);
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
  std::string where = mode == Mode::Focus ? "れんしゅう · " + fs : mode == Mode::Review ? "ふくしゅう · Review"
                    : mode == Mode::Mixup ? "にている · Mix-ups" : mode == Mode::Pack && packIdx >= 0 ? "ことば · " + data.packs[packIdx].name
                    : "れんしゅう · " + stageLabel(prog.currentStage(prog.settings.script));
  if (timed) {
    header("タイム · Timed challenge", "score " + std::to_string(score), true);
    bar(top, SDL_Rect{24, 56, 592, 12}, float(timedLeft) / TIMED_MS);
    drawText(top, std::to_string((timedLeft + 999) / 1000) + " s", SDL_Rect{24, 76, 160, 38}, timedLeft < 10000 ? BAD : ACCENT_TXT, F_SMALL, A_LEFT);
    drawText(top, q.hint, SDL_Rect{184, 78, 432, 36}, DIM, F_TINY, A_RIGHT);
  } else {
    header(where, std::to_string(std::min(index + 1, sessionLen)) + " / " + std::to_string(sessionLen), true);
    segBar(top, SDL_Rect{24, 56, 592, 12}, sessionLen, index, results);
    drawText(top, q.hint, SDL_Rect{24, 70, 592, 30}, DIM, F_MICRO);
  }
  if (q.intro) {  // new-word card
    drawText(top, q.prompt, SDL_Rect{24, 136, 592, 150}, FG, F_HUGE);
    drawText(top, q.answer, SDL_Rect{24, 286, 592, 50}, DIM, F_MID);
    int w = std::min(560, textWidth(top, F_SMALL, q.meaning) + 60);
    SDL_Rect mr{(LW - w) / 2, 350, w, 52}; fillRound(top, mr, 26, PANEL); drawText(top, q.meaning, mr, ACCENT_TXT, F_SMALL);
    // bottom: the word built from kana you know
    drawText(bot, "MADE OF KANA YOU KNOW", SDL_Rect{16, 14, 608, 24}, DIM, F_MICRO);
    Script ws = isKataText(q.prompt) ? Script::Katakana : Script::Hiragana;
    auto parts = Data::partNames(q.prompt);
    int np = int(parts.size()), tw = std::min(110, (592 - (np - 1) * 10) / std::max(1, np)), x0 = (LW - (np * tw + (np - 1) * 10)) / 2;
    for (int i = 0; i < np; i++) {
      SDL_Rect r{x0 + i * (tw + 10), 46, tw, 150}; fillRound(bot, r, 14, PANEL);
      int ki = data.findKana(ws, parts[i]);
      std::string rom = ki < 0 ? "" : parts[i] == "っ" || parts[i] == "ッ" ? "double" : parts[i] == "ー" ? "long" : data.kana[ki].romaji;
      drawText(bot, parts[i], SDL_Rect{r.x + 4, r.y + 10, r.w - 8, 90}, FG, F_MID);
      drawText(bot, rom, SDL_Rect{r.x + 4, r.y + 104, r.w - 8, 34}, ACCENT_TXT, F_TINY);
    }
    drawWrapped(bot, "Have a good look: it comes back as a question in a moment.", SDL_Rect{16, 214, 608, 60}, DIM, F_TINY);
    addBtn(SDL_Rect{16, 300, 608, 92}, B_CONTINUE, "Got it", F_MID); accent(lastBtn());
    chips({{"A", "Got it"}, {"F", "Pause"}});
    return;
  }
  Col pc = checked ? (correct ? GOOD : BAD) : FG;
  auto meaningLine = [&](int y) {  // a word's English meaning, under the prompt
    if (q.meaning.empty()) return;
    int w = std::min(560, textWidth(top, F_SMALL, q.meaning) + 60);
    SDL_Rect r{(LW - w) / 2, y, w, 46}; card(top, r, 23);
    drawText(top, q.meaning, r, ACCENT_TXT, F_SMALL);
  };
  if (q.type == QType::Type) {
    card(top, SDL_Rect{56, 102, 528, q.meaning.empty() ? 186 : 136}, 26);
    drawText(top, q.prompt, SDL_Rect{64, 104, 512, q.meaning.empty() ? 182 : 132}, pc, F_HUGE);
    meaningLine(248);
    SDL_Rect f{140, 300, 360, 70}; card(top, f, 18); fillRect(top, SDL_Rect{f.x + 14, f.y + f.h - 8, f.w - 28, 4}, ACCENT);
    std::string t = typed + (checked ? "" : "_");
    drawText(top, typed.empty() && checked ? "-" : t, SDL_Rect{f.x + 10, f.y, f.w - 20, f.h - 6}, checked ? pc : ACCENT_TXT, F_MID);
  } else if (q.type == QType::Write) {
    const Glyph* g = activeGlyph(); int m = g ? int(g->strokes.size()) : 0, np = writePartCount();
    auto names = Data::partNames(q.answer);
    card(top, SDL_Rect{24, 100, 300, 250}, 26); card(top, SDL_Rect{336, 100, 280, 250}, 22);
    drawText(top, checked ? q.answer : q.prompt, SDL_Rect{30, 104, 288, 236}, pc, F_HUGE);
    std::string what = q.writeLevel == 0 ? "GUIDED" : q.writeLevel == 1 ? "OUTLINE" : "FROM MEMORY";
    drawText(top, np > 1 ? "PART " + std::to_string(part + 1) + " OF " + std::to_string(np) + " · " + names[std::min(part, np - 1)] + " · " + what
                         : what, SDL_Rect{352, 116, 256, 22}, ACCENT_TXT, F_MICRO, A_LEFT);
    drawText(top, checked ? std::to_string(m) + " strokes" : "Stroke " + std::to_string(std::min(int(strokes.size()) + 1, m)) + " of " + std::to_string(m),
             SDL_Rect{352, 142, 256, 40}, FG, F_SMALL, A_LEFT);
    for (int i = 0; i < m; i++) {
      SDL_Rect r{352 + i * 30, 192, 22, 22};
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
    drawWrapped(top, help, SDL_Rect{352, 226, 250, 120}, DIM, F_MICRO);
  } else {
    card(top, SDL_Rect{56, 104, 528, q.meaning.empty() ? 256 : 192}, 28);
    drawText(top, q.prompt, SDL_Rect{64, 110, 512, q.meaning.empty() ? 244 : 180}, pc, F_HUGE);
    meaningLine(308);
  }
  if (timed && flash) {
    SDL_Rect f{24, 392, 592, 64}; boxRound(top, f, 24, flash == 1 ? GOOD_FILL : BAD_FILL, flash == 1 ? GOOD : BAD, 2);
    if (flash == 1) drawRing(top, 54, 424, 20, 6, 1.f, hex(0xE03131), hex(0xE03131)); else lineIcon(top, "cross", SDL_Rect{34, 404, 40, 40}, DIM, 3.f);
    drawText(top, (flash == 1 ? "Right · " : "Missed · ") + flashText, SDL_Rect{84, 392, 516, 64}, FG, F_TINY, A_LEFT);
  }
  if (checked) {
    // the banner grows upwards to fit its note (long mix-up hints used to spill out of the bottom)
    int noteH = feedbackNote.empty() ? 0 : std::min(wrappedHeight(top, feedbackNote, 500, F_MICRO), 92);
    SDL_Rect f{24, std::min(376, 468 - 52 - noteH), 592, std::max(88, 52 + noteH)};
    boxRound(top, f, 24, correct ? GOOD_FILL : BAD_FILL, correct ? GOOD : BAD, 2);
    const int my = f.y + f.h / 2;
    if (correct) drawRing(top, 58, my, 26, 7, 1.f, hex(0xE03131), hex(0xE03131)); else lineIcon(top, "cross", SDL_Rect{32, my - 26, 52, 52}, DIM, 3.f);  // ○ / × like a Japanese test
    std::string ans = q.answer;
    if (k && (q.type == QType::Write || q.type == QType::RecallKana)) ans = k->glyph + " (" + k->romaji + ")";
    drawText(top, correct ? "せいかい! Correct!" : "ざんねん · it's " + ans, SDL_Rect{96, f.y + 8, 504, 36}, FG, F_SMALL, A_LEFT);
    if (!feedbackNote.empty()) drawWrapped(top, feedbackNote, SDL_Rect{96, f.y + 44, 500, noteH}, DIM, F_MICRO);
  }

  // bottom
  if (!q.options.empty()) {
    int n = int(q.options.size()), cols = n <= 3 ? n : 2, rows = (n + cols - 1) / cols;
    int areaH = timed ? 400 : 314, w = (608 - (cols - 1) * 10) / cols, h = (areaH - (rows - 1) * 10) / rows;
    for (int i = 0; i < n; i++) {
      SDL_Rect r{16 + (i % cols) * (w + 10), 16 + (i / cols) * (h + 10), w, h};
      addBtn(r, B_TILE0 + i, q.options[i], F_MID); Button& b = lastBtn();
      if (checked) {
        float at = answerAnim ? std::clamp((SDL_GetTicks() - answerAnim) / float(ANSWER_MS), 0.f, 1.f) : 1.f;
        if (i == q.correct) { b.fill = GOOD_FILL; b.line = GOOD; b.icon = 2; b.text = FG;
          int g = int(8 * std::sin(3.14159f * at)); b.r = SDL_Rect{r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g}; }  // pop
        else if (i == sel) { b.fill = BAD_FILL; b.line = BAD;
          b.r.x += int(10 * std::sin(at * 38.f) * (1.f - at)); }  // shake
      } else if (i == sel) accent(b);
      else if (i == cursor) { b.line = ACC_LINE; b.lineW = 4; }  // d-pad cursor: the mode colour, like every other selection
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
    // big keys (58x88, the full width) at the bottom like the kana keyboard (Check ends at y 428): easy to hit and reach
    const int KW = 58, KH = 88, PX = 62, PY = 96, AY = 348, KY = AY - 8 - 2 * PY - KH;
    for (int r = 0; r < 3; r++) {
      int len = int(strlen(rows[r])), x0 = (LW - (len * PX - (PX - KW))) / 2;
      for (int i = 0; i < len; i++) {
        char ch = rows[r][i];
        addBtn(SDL_Rect{x0 + i * PX, KY + r * PY, KW, KH}, B_KEY0 + (ch - 'a'), std::string(1, ch), F_MID, !checked);
        lastBtn().radius = 12;
      }
    }
    addBtn(SDL_Rect{12, AY, 190, 80}, B_BKSP, "Delete", F_TINY, !checked && !typed.empty());
    if (checked) { addBtn(SDL_Rect{214, AY, 414, 80}, B_CONTINUE, "Continue", F_SMALL); accent(lastBtn()); }
    else { addBtn(SDL_Rect{214, AY, 414, 80}, B_CHECK, "Check answer", F_SMALL, !typed.empty()); if (!typed.empty()) accent(lastBtn()); }
    if (!checked) markKbCursor();
    chips({{"A", "Type"}, {"X", "Check"}, {"B", "Delete"}, {"F", "Pause"}, {"+", "Move"}});
  } else {
    addBtn(SDL_Rect{16, 16, 120, 74}, B_UNDO, part > 0 && strokes.empty() ? "Back" : "Undo", F_TINY, !checked && (!strokes.empty() || part > 0));
    addBtn(SDL_Rect{16, 98, 120, 74}, B_CLEAR, "Clear", F_TINY, !checked && !strokes.empty());
    // a switch, not a greyed-out button: guided writes always show the guide (on, can't be turned off); later levels toggle a hint
    addBtn(SDL_Rect{16, 180, 120, 74}, B_HINT, q.writeLevel == 0 ? "Guide" : "Hint", F_TINY, !checked);
    lastBtn().icon = q.writeLevel == 0 || showHint ? 4 : 3;
    addBtn(SDL_Rect{16, 262, 120, 74}, B_SHOWME, "Show me", F_TINY, !checked);
    if (checked) { addBtn(SDL_Rect{16, 344, 120, 74}, B_CONTINUE, "Next", F_TINY); accent(lastBtn()); }
    else { addBtn(SDL_Rect{16, 344, 120, 74}, B_CHECK, "Check", F_TINY, !strokes.empty()); if (!strokes.empty()) accent(lastBtn()); }
    drawCanvas(canvasRect(), activeGlyph(), checked || showHint || q.writeLevel == 0 ? 2 : q.writeLevel == 1 ? 1 : 0, checked);
    chips({{"X", "Check"}, {"Y", "Hint"}, {"START", "Show me"}, {"B", "Undo"}, {"F", "Pause"}});
  }
}

void App::drawWelcome() {
  appTile(top, 270, 8, 96);
  drawText(top, welcomeAgain ? "Set up Banana Kana" : "Welcome to Banana Kana", SDL_Rect{20, 112, 600, 60}, FG, F_MID);
  if (welcomeStep == 0) {  // step 1 of 4: light or dark, shown live
    drawWrapped(top, "First, pick a look. Tap one to try it: the whole app changes straight away.",
                SDL_Rect{60, 190, 520, 100}, DIM, F_TINY);
    drawText(top, "Step 1 of 4 · you can change it later in Settings > Display", SDL_Rect{20, 400, 600, 30}, DIM, F_MICRO);
    for (int k = 0; k < 2; k++) {
      bool light = k == 0, on = prog.settings.light == light;
      SDL_Rect r{16 + k * 312, 16, 296, 300};
      if (on) { fillRound(bot, SDL_Rect{r.x - 6, r.y - 6, r.w + 12, r.h + 12}, 30, ACC_LINE); fillRound(bot, SDL_Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 26, BG2); }
      card(bot, r, 24);
      // a little preview of Home in that look
      SDL_Rect m{r.x + 24, r.y + 22, r.w - 48, 190};
      fillRound(bot, m, 16, light ? hex(0xEEF1F4) : hex(0x1A1E25));
      fillRound(bot, SDL_Rect{m.x + 14, m.y + 14, m.w - 28, 70}, 12, light ? hex(0xFFFFFF) : hex(0x262C36));
      fillRound(bot, SDL_Rect{m.x + 28, m.y + 30, 110, 12}, 6, light ? hex(0x2B2F36) : hex(0xE8EAEE));
      fillRound(bot, SDL_Rect{m.x + 28, m.y + 54, 160, 9}, 4, light ? hex(0xA0A7B0) : hex(0x6B7380));
      fillRound(bot, SDL_Rect{m.x + 14, m.y + 98, m.w - 28, 34}, 17, hex(0xF28C28));
      const Col tiles[4] = {hex(0x2F7BEA), hex(0x7C4DDB), hex(0x2FA65A), hex(0xE0453A)};
      for (int t = 0; t < 4; t++) fillRound(bot, SDL_Rect{m.x + 14 + t * ((m.w - 28) / 4), m.y + 142, (m.w - 28) / 4 - 8, 36}, 9, tiles[t]);
      setBold(true); drawText(bot, light ? "Light" : "Dark", SDL_Rect{r.x, r.y + 222, r.w, 40}, FG, F_SMALL); setBold(false);
      drawText(bot, light ? "bright and clear" : "easy on the eyes at night", SDL_Rect{r.x, r.y + 260, r.w, 28}, DIM, F_MICRO);
      addBtn(r, B_WTHEME0 + k, light ? "Light" : "Dark", F_SMALL); lastBtn().hidden = true;
    }
    if (welcomeAgain) addBtn(SDL_Rect{16, 336, 192, 84}, B_BACK, "Back", F_SMALL);
    addBtn(welcomeAgain ? SDL_Rect{224, 336, 400, 84} : SDL_Rect{16, 336, 608, 84}, B_WEL_NEXT, "Next", F_SMALL); accent(lastBtn()); lastBtn().radius = 30;
    if (welcomeAgain) chips({{"+", "Light / Dark"}, {"A", "Next"}, {"B", "Back to Settings"}});
    else chips({{"+", "Light / Dark"}, {"A", "Next"}});
    return;
  }
  if (welcomeStep == 1) {  // step 2 of 4: seasons of Japan or plain, shown live behind the tiles
    drawWrapped(top, "And a background. Seasons of Japan follows the calendar: sakura, summer waves, autumn leaves, snow.",
                SDL_Rect{60, 190, 520, 100}, DIM, F_TINY);
    drawText(top, "Step 2 of 4 · you can change it later in Settings > Display", SDL_Rect{20, 400, 600, 30}, DIM, F_MICRO);
    const Col SEAS[4] = {hex(0xF29BB4), hex(0x3F8FD8), hex(0xE2683A), hex(0x5F8FC6)};
    const char* SK[4] = {"春", "夏", "秋", "冬"};
    for (int k = 0; k < 2; k++) {
      bool seasons = k == 0, on = (prog.settings.backdrop > 0) == seasons;
      SDL_Rect r{16 + k * 312, 16, 296, 300};
      if (on) { fillRound(bot, SDL_Rect{r.x - 6, r.y - 6, r.w + 12, r.h + 12}, 30, ACC_LINE); fillRound(bot, SDL_Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 26, BG2); }
      card(bot, r, 24);
      SDL_Rect m{r.x + 24, r.y + 22, r.w - 48, 190};
      if (seasons) {  // four little season panels
        int pw = (m.w - 12) / 2, ph = (m.h - 12) / 2;
        for (int i = 0; i < 4; i++) {
          SDL_Rect c{m.x + (i % 2) * (pw + 12), m.y + (i / 2) * (ph + 12), pw, ph};
          fillRound(bot, c, 14, mix(PANEL, SEAS[i], 0.22f));
          for (int d = 0; d < 3; d++) fillRound(bot, SDL_Rect{c.x + 12 + d * 34, c.y + 12 + (d % 2) * 50, 12, 12}, 6, mix(PANEL, SEAS[i], 0.6f));
          setBold(true); drawText(bot, SK[i], c, SEAS[i], F_MID); setBold(false);
        }
      } else {  // the calm stripes
        fillRound(bot, m, 16, BG2);
        for (int y = m.y + 14; y < m.y + m.h - 10; y += 22) fillRound(bot, SDL_Rect{m.x + 16, y, m.w - 32, 6}, 3, mix(BG2, FG, 0.08f));
      }
      setBold(true); drawText(bot, seasons ? "Seasons" : "Plain", SDL_Rect{r.x, r.y + 222, r.w, 40}, FG, F_SMALL); setBold(false);
      drawText(bot, seasons ? "petals and snow drift gently" : "clean and still", SDL_Rect{r.x, r.y + 260, r.w, 28}, DIM, F_MICRO);
      addBtn(r, B_WBG0 + k, seasons ? "Seasons" : "Plain", F_SMALL); lastBtn().hidden = true;
    }
    addBtn(SDL_Rect{16, 336, 608, 84}, B_WEL_NEXT, "Next", F_SMALL); accent(lastBtn()); lastBtn().radius = 30;
    chips({{"+", "Seasons / Plain"}, {"A", "Next"}, {"B", "Back"}});
    return;
  }
  if (welcomeStep == 2) {  // step 3 of 4: vibration, felt straight away
    drawWrapped(top, "Vibration: one tick for a right answer, two buzzes for a wrong one, and a long rumble for unlocks. Tap one to feel it.",
                SDL_Rect{60, 190, 520, 100}, DIM, F_TINY);
    drawText(top, "Step 3 of 4 · you can change it later in Settings > Display", SDL_Rect{20, 400, 600, 30}, DIM, F_MICRO);
    const char* RN[3] = {"Off", "Light", "Strong"};
    const char* RS[3] = {"no buzzing", "gentle ticks", "you'll feel it"};
    for (int k = 0; k < 3; k++) {
      bool on = prog.settings.rumble == k;
      SDL_Rect r{16 + k * 208, 16, 192, 300};
      if (on) { fillRound(bot, SDL_Rect{r.x - 6, r.y - 6, r.w + 12, r.h + 12}, 30, ACC_LINE); fillRound(bot, SDL_Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 26, BG2); }
      card(bot, r, 24);
      // a little handheld with buzz marks either side: none, one, two
      SDL_Rect dev{r.x + r.w / 2 - 34, r.y + 46, 68, 120};
      fillRound(bot, dev, 16, k ? ACCENT : PANEL2); fillRound(bot, SDL_Rect{dev.x + 10, dev.y + 12, dev.w - 20, 44}, 8, PANEL);
      fillRound(bot, SDL_Rect{dev.x + 14, dev.y + 76, 14, 14}, 7, PANEL); fillRound(bot, SDL_Rect{dev.x + dev.w - 28, dev.y + 76, 14, 14}, 7, PANEL);
      for (int m = 0; m < k; m++)
        for (int side = -1; side <= 1; side += 2) {
          int x = side < 0 ? dev.x - 18 - m * 14 : dev.x + dev.w + 10 + m * 14;
          fillRound(bot, SDL_Rect{x, dev.y + 34 - m * 8, 6, 52 + m * 16}, 3, ACCENT);
        }
      setBold(true); drawText(bot, RN[k], SDL_Rect{r.x, r.y + 190, r.w, 40}, FG, F_SMALL); setBold(false);
      drawText(bot, RS[k], SDL_Rect{r.x, r.y + 232, r.w, 28}, DIM, F_MICRO);
      addBtn(r, B_WRUM0 + k, RN[k], F_SMALL); lastBtn().hidden = true;
    }
    addBtn(SDL_Rect{16, 336, 608, 84}, B_WEL_NEXT, "Next", F_SMALL); accent(lastBtn()); lastBtn().radius = 30;
    chips({{"+", "Off / Light / Strong"}, {"A", "Next"}, {"B", "Back"}});
    return;
  }
  drawWrapped(top, "Learn hiragana, then katakana, a few kana at a time: a short lesson, then drills. "
                   "Already know some? Skip ahead. Kana you skip count as known, with a quick review soon to check.",
              SDL_Rect{60, 190, 520, 140}, DIM, F_TINY);
  drawText(top, "Step 4 of 4 · you can change this later in Settings > Script", SDL_Rect{20, 400, 600, 30}, DIM, F_MICRO);
  const char* l[4] = {"New to kana", "I know hiragana", "I know both", "I know some"};
  const char* sub[4] = {"start at \u3042 \u3044 \u3046 \u3048 \u304a", "all 105, start on katakana", "straight to reviews", "choose the stages in Settings"};
  for (int i = 0; i < 4; i++) {
    addBtn(SDL_Rect{16, 16 + i * 102, 608, 94}, B_WELCOME0 + i, l[i], F_SMALL); lastBtn().sub = sub[i];
    if (i == welcomeSel) accent(lastBtn());
  }
  chips({{"+", "Choose"}, {"A", "Start"}, {"B", "Back"}});
}

// ================================================================== Kitsu, the streak pet
namespace {
const char* PET_STAGE_NAME[5] = {"Egg", "Kit · 1 tail", "3 tails", "5 tails", "Kyūbi · 9 tails"};
const char* PET_STAGE_TEXT[5] = {
  "Meet today's goal to hatch it.",
  "A tiny kit with one fluffy tail. She grows more tails as you keep practising.",
  "Three tails and a red scarf: a week of practice!",
  "Five tails with glowing tips: 30 days of practice!",
  "Nine tails and fox-fire. A true kyūbi: 100 days of practice!"};
}

FoxLook App::petLook(FoxMood mood) const {
  FoxLook L; L.stage = prog.petStage(); L.mood = mood; L.accessory = prog.pet.acc; L.season = seasonNow();
  L.t = motion ? SDL_GetTicks() / 1000.f : 0.f;
  return L;
}

void App::drawPet() {
  const int st = prog.petStage(), days = prog.pet.days;
  header("きつね · Your fox", std::to_string(days) + (days == 1 ? " day practised" : " days practised"));
  card(top, SDL_Rect{16, 62, 272, 400}, 20);
  drawFox(top, 152, 446, 330, petLook(st == 0 ? FoxMood::Awake : prog.petAwake() ? FoxMood::Awake : FoxMood::Asleep));
  const int X = 306, W = 318;
  setBold(true); drawText(top, prog.pet.name, SDL_Rect{X, 66, W, 56}, FG, F_MID, A_LEFT); setBold(false);
  std::string line = PET_STAGE_NAME[st];
  if (prog.pet.acc >= 0) line += " · wearing " + std::string(petAccessories()[prog.pet.acc].name);
  drawText(top, line, SDL_Rect{X, 124, W, 30}, ACCENT_TXT, F_TINY, A_LEFT);
  if (st == 0) drawWrapped(top, "Meet today's goal to hatch your egg.", SDL_Rect{X, 170, W, 60}, DIM, F_TINY);
  else if (st < 4) {
    int next = PET_STAGE_DAYS[st + 1];
    drawText(top, "Next: " + std::to_string(PET_TAILS[st + 1]) + " tails at " + std::to_string(next) + " days", SDL_Rect{X, 170, W, 28}, DIM, F_MICRO, A_LEFT);
    bar(top, SDL_Rect{X, 204, W - 10, 14}, float(days) / float(next));
    drawText(top, std::to_string(days) + " / " + std::to_string(next) + " days", SDL_Rect{X, 222, W, 26}, DIM, F_MICRO, A_LEFT);
  } else drawText(top, "Fully grown: all nine tails!", SDL_Rect{X, 170, W, 28}, ACCENT_TXT, F_TINY, A_LEFT);
  int streak = prog.currentStreak(), have = 0;
  for (int i = 0; i < int(petAccessories().size()); i++) have += prog.petAccUnlocked(i);
  const std::string facts[3] = {"Streak: " + std::to_string(streak) + (streak == 1 ? " day" : " days") + " (best " + std::to_string(std::max(streak, prog.bestStreak)) + ")",
                                std::string(prog.petAwake() ? "Awake: today's goal is met" : "Asleep until today's goal"),
                                "Accessories: " + std::to_string(have) + " of " + std::to_string(petAccessories().size())};
  for (int i = 0; i < 3; i++) drawText(top, facts[i], SDL_Rect{X, 270 + i * 34, W, 30}, DIM, F_MICRO, A_LEFT);
  drawText(top, "Tails grow with days practised and are never lost.", SDL_Rect{X, 390, W, 30}, DIM, F_MICRO, A_LEFT);

  // bottom: the eight accessories (a little kit wearing each one), Rename / Take it off
  const auto& acc = petAccessories();
  for (int i = 0; i < int(acc.size()); i++) {
    SDL_Rect r{16 + (i % 4) * 155, 14 + (i / 4) * 152, 143, 140};
    bool have1 = prog.petAccUnlocked(i), wearing = prog.pet.acc == i;
    if (i == petSel) { fillRound(bot, SDL_Rect{r.x - 6, r.y - 6, r.w + 12, r.h + 12}, 26, ACC_LINE); fillRound(bot, SDL_Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 22, BG2); }
    if (have1) {
      card(bot, r, 18, wearing ? mix(PANEL, ACCENT, 0.18f) : PANEL);
      FoxLook L; L.stage = 1; L.mood = FoxMood::Awake; L.accessory = i; L.t = 0;
      drawFox(bot, r.x + r.w / 2, r.y + 92, 96, L);
      setBold(true); drawText(bot, acc[i].name, SDL_Rect{r.x + 4, r.y + 98, r.w - 8, 22}, FG, F_MICRO); setBold(false);
      drawText(bot, wearing ? "wearing" : "tap to wear", SDL_Rect{r.x + 4, r.y + 116, r.w - 8, 20}, wearing ? ACCENT_TXT : DIM, F_MICRO);
    } else {
      fillRound(bot, r, 18, mix(BG2, FG, 0.06f));
      lineIcon(bot, "lock", SDL_Rect{r.x + r.w / 2 - 16, r.y + 22, 32, 32}, DIM, 2.4f);
      setBold(true); drawText(bot, acc[i].name, SDL_Rect{r.x + 4, r.y + 64, r.w - 8, 24}, DIM, F_MICRO); setBold(false);
      std::string ach; for (auto& a : achievementList()) if (a.id == acc[i].achievement) ach = a.title;
      drawWrapped(bot, ach, SDL_Rect{r.x + 8, r.y + 90, r.w - 16, 44}, DIM, F_MICRO, A_CENTER);
    }
    addBtn(r, B_ACC0 + i, acc[i].name, F_MICRO); lastBtn().hidden = true;
  }
  addBtn(SDL_Rect{16, 324, 192, 72}, B_BACK, "Back", F_SMALL);
  addBtn(SDL_Rect{224, 324, 192, 72}, B_PET_RENAME, "Rename", F_SMALL);
  addBtn(SDL_Rect{432, 324, 192, 72}, B_PET_NONE, "Take it off", F_SMALL, prog.pet.acc >= 0);
  chips({{"A", "Wear"}, {"Y", "Rename"}, {"X", "Take off"}, {"B", "Back"}});
}

void App::drawPetEvolve() {
  const int st = prog.petStage();
  const bool hatched = prog.pet.seenStage < 1;
  setBold(true); drawText(top, hatched ? "うまれた! " + prog.pet.name + " hatched" : "しんか! " + prog.pet.name + " evolved", SDL_Rect{20, 14, 600, 56}, FG, F_MID); setBold(false);
  drawFox(top, 320, 462, 360, petLook(FoxMood::Happy));
  card(bot, SDL_Rect{24, 24, 592, 270}, 22);
  setBold(true); drawText(bot, PET_STAGE_NAME[st], SDL_Rect{40, 40, 560, 52}, FG, F_MID); setBold(false);
  drawWrapped(bot, PET_STAGE_TEXT[st], SDL_Rect{60, 100, 520, 90}, DIM, F_TINY, A_CENTER);
  if (st < 4) {
    int next = PET_STAGE_DAYS[st + 1], days = prog.pet.days;
    drawText(bot, "Next: " + std::to_string(PET_TAILS[st + 1]) + " tails at " + std::to_string(next) + " days", SDL_Rect{60, 196, 520, 28}, DIM, F_MICRO);
    bar(bot, SDL_Rect{120, 232, 400, 14}, float(days) / float(next));
  }
  addBtn(SDL_Rect{24, 320, 592, 84}, B_PET_CONT, "Continue", F_SMALL); accent(lastBtn()); lastBtn().radius = 30;
  chips({{"A", "Continue"}});
}

void App::drawPetName() {
  header("なまえ · Name your fox");
  setBold(true); drawText(top, "Type a new name", SDL_Rect{20, 70, 600, 40}, DIM, F_TINY); setBold(false);
  SDL_Rect box{100, 120, 440, 110}; card(top, box, 20);
  if (typed.empty()) drawText(top, "type with the kana keys", box, DIM, F_TINY);
  else drawText(top, typed, box, FG, F_BIG);
  drawText(top, "Up to " + std::to_string(PET_NAME_MAX) + " kana · now: " + prog.pet.name, SDL_Rect{20, 244, 600, 30}, DIM, F_MICRO);
  FoxLook L = petLook(FoxMood::Happy); drawFox(top, 320, 466, 180, L);
  drawKanaKeyboard();
}

void App::drawFreeWrite() {
  if (fwList.empty()) {  // draw any kana: it guesses what you drew
    header("かく · Draw any kana", "any kana · no progress");
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
  header("かく · Free writing", std::to_string(fwIdx + 1) + " / " + std::to_string(fwList.size()));
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
// ring the d-pad key and draw it last: the ring sits outside the key and the 4 px gap would let the next key cover it
void App::markKbCursor() {
  int cid = kbItemId(keyRow, keyCol);
  auto it = std::find_if(buttons.begin(), buttons.end(), [&](const Button& b) { return b.id == cid; });
  if (it == buttons.end()) return;
  it->line = ACC_LINE; it->lineW = 4;
  std::rotate(it, it + 1, buttons.end());
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
  else if (screen == Screen::PetName) {
    addBtn(SDL_Rect{16, 356, 192, 72}, B_BACK, "Cancel", F_SMALL);
    addBtn(SDL_Rect{224, 356, 400, 72}, B_CHECK, "Save name", F_SMALL, !typed.empty()); if (!typed.empty()) accent(lastBtn());
  } else { addBtn(SDL_Rect{16, 356, 608, 72}, B_CHECK, "Check answer", F_SMALL, !typed.empty()); if (!typed.empty()) accent(lastBtn()); }
  if (!checked) markKbCursor();
  if (screen == Screen::PetName) chips({{"A", "Type"}, {"X", "Save"}, {"B", "Delete / Back"}, {"+", "Move"}});
  else chips({{"A", "Type"}, {"X", "Check"}, {"B", "Delete"}, {"F", "Pause"}, {"+", "Move"}});
}

void App::drawStats() {
  Script sc = prog.settings.script; int scI = si(sc);
  header("きろく · Stats", scriptName(sc));
  int tot = 0, cor = 0; for (auto& [k, st] : prog.stats) { tot += st.seen; cor += st.correct; }
  int streak = prog.currentStreak(), goal = prog.settings.dailyGoal;
  statBox(top, SDL_Rect{24, 58, 140, 96}, "ANSWERS", std::to_string(tot));
  statBox(top, SDL_Rect{174, 58, 140, 96}, "ACCURACY", tot ? std::to_string(cor * 100 / tot) + "%" : "-");
  statBox(top, SDL_Rect{324, 58, 140, 96}, "STREAK", "", F_TINY);
  { bool lit = streak > 0 && prog.lastGoalDay == dayNumber(prog.now());
    drawFox(top, 356, 143, 50, petLook(prog.petStage() == 0 || lit ? FoxMood::Awake : FoxMood::Asleep));  // Kitsu keeps the streak
    setBold(true); drawText(top, std::to_string(streak), SDL_Rect{380, 92, 76, 28}, FG, F_SMALL, A_LEFT); setBold(false);
    drawText(top, "best " + std::to_string(std::max(streak, prog.bestStreak)), SDL_Rect{380, 120, 80, 20}, DIM, F_MICRO, A_LEFT); }
  statBox(top, SDL_Rect{474, 58, 140, 96}, "TIMED BEST", std::to_string(prog.timedBest[scI]));
  // last 14 days: right answers (yellow) under misses (red), goal line dashed
  card(top, SDL_Rect{14, 162, 612, 300}, 18);
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
    fillRound(top, SDL_Rect{x, y0 - H, bw, H}, 6, mix(PANEL, FG, 0.035f));  // a quiet track: empty days read as empty
    if (ds[i].answers >= goal) fillCircle(top, x + bw / 2, y0 - H - 8, 4, hex(0xF07A1A));  // goal met that day
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
  chips({{"X", "Awards"}, {"START", "Your fox"}, {"Y", "Script"}, {"B", "Back"}});
}

void App::drawResult() {
  int shown = score;  // counts up from 0 over ~0.7 s (after the fade), easing out
  if (countStart) { float t = std::clamp((SDL_GetTicks() - countStart - 150.f) / 700.f, 0.f, 1.f); shown = int(std::lround(score * (1.f - (1.f - t) * (1.f - t) * (1.f - t)))); }
  if (mode == Mode::Timed) {
    header("けっか · Timed challenge", scriptName(prog.settings.script));
    int answered = int(results.size()), best = prog.timedBest[si(prog.settings.script)];
    drawText(top, std::to_string(shown), SDL_Rect{24, 60, 150, 190}, ACCENT_TXT, F_HUGE, A_RIGHT);
    drawText(top, "right", SDL_Rect{184, 150, 130, 70}, DIM, F_MID, A_LEFT);
    drawText(top, timedNewBest ? "New best!" : score == best ? "Equal best!" : "Best: " + std::to_string(best), SDL_Rect{330, 96, 286, 44}, FG, F_SMALL, A_LEFT);
    drawWrapped(top, "In 60 seconds, out of " + std::to_string(answered) + " answered." + (goalMet ? " Daily goal reached!" : ""),
                SDL_Rect{330, 144, 286, 90}, DIM, F_TINY);
    if (answered > 0 && answered <= 40) segBar(top, SDL_Rect{24, 262, 592, 14}, answered, answered, results);
    statBox(top, SDL_Rect{24, 300, 188, 140}, "ANSWERED", std::to_string(answered));
    statBox(top, SDL_Rect{226, 300, 188, 140}, "ACCURACY", answered ? std::to_string(score * 100 / answered) + "%" : "-");
    statBox(top, SDL_Rect{428, 300, 188, 140}, "BEST", std::to_string(best));
  } else {
  header("けっか · Session complete", mode == Mode::Review ? "Review" : mode == Mode::Mixup ? "Mix-ups" : mode == Mode::Focus ? "Practice set"
                                     : mode == Mode::Pack && packIdx >= 0 && packIdx < int(data.packs.size()) ? data.packs[packIdx].name : scriptName(prog.settings.script));
  Script sc = prog.settings.script; int curSt = prog.currentStage(sc); const Stage& st = data.stages[si(sc)][curSt];
  drawText(top, std::to_string(shown), SDL_Rect{24, 60, 150, 190}, ACCENT_TXT, F_HUGE, A_RIGHT);
  if (score == sessionLen && shown == score) medal(top, SDL_Rect{232, 66, 64, 64}, true);  // perfect session: a medal
  drawText(top, "/ " + std::to_string(sessionLen), SDL_Rect{184, 150, 130, 70}, DIM, F_MID, A_LEFT);
  std::string head = score == sessionLen ? "かんぺき! Perfect!" : score * 10 >= sessionLen * 7 ? "よくできました! Nice!" : "がんばって! Keep going!";
  drawText(top, head, SDL_Rect{330, 96, 286, 44}, FG, F_SMALL, A_LEFT);
  std::string body = missed.empty() ? "Every answer was right." : std::to_string(missed.size()) + (mode == Mode::Pack ? (missed.size() == 1 ? " word comes" : " words come") + std::string(" back sooner.")
                                                        : (missed.size() == 1 ? " kana goes" : " answers go") + std::string(" back into review."));
  if (goalMet) body += " Daily goal reached: " + std::to_string(prog.streak) + "-day streak!";
  drawWrapped(top, body, SDL_Rect{330, 144, 286, 110}, DIM, F_TINY);
  segBar(top, SDL_Rect{24, 262, 592, 14}, sessionLen, sessionLen, results);
  if (mode == Mode::Pack && packIdx >= 0) {
    PackInfo pi = packInfo(data, prog, packIdx); const Pack& pk = data.packs[packIdx];
    statBox(top, SDL_Rect{24, 300, 188, 140}, pk.jp, pk.name, F_TINY);
    statBox(top, SDL_Rect{226, 300, 188, 140}, "WORDS KNOWN", std::to_string(pi.known) + " / " + std::to_string(pi.total));
    statBox(top, SDL_Rect{428, 300, 188, 140}, "WAITING ON KANA", pi.total - pi.ready ? std::to_string(pi.total - pi.ready) + " words" : "none", F_TINY);
  } else {
  std::vector<std::string> left;
  for (int ki : st.kana) if (prog.step(Progress::keyKana(data.kana[ki])) < KNOWN_STEP && left.size() < 4) left.push_back(data.kana[ki].glyph);
  if (newlyKnown.empty()) statBox(top, SDL_Rect{24, 300, 188, 140}, "NEWLY KNOWN", "Not this time: steps still count", F_MICRO);
  else statBox(top, SDL_Rect{24, 300, 188, 140}, "NEWLY KNOWN", join(newlyKnown, " "));
  statBox(top, SDL_Rect{226, 300, 188, 140}, "STAGE " + std::to_string(curSt + 1),
          std::to_string(prog.stageKnown(data, sc, curSt)) + " / " + std::to_string(st.kana.size()) + " known", F_TINY);
  statBox(top, SDL_Rect{428, 300, 188, 140}, "STILL TO LEARN", left.empty() ? "all done" : join(left, " "));
  }
  }

  drawText(bot, "TO REVIEW", SDL_Rect{16, 10, 300, 24}, DIM, F_MICRO, A_LEFT);
  if (missed.empty()) {  // a clean sheet gets a little moment instead of an empty box
    card(bot, SDL_Rect{16, 40, 608, 170}, 20);
    lineIcon(bot, "check", SDL_Rect{48, 82, 80, 80}, GOOD, 4.f);
    setBold(true); drawText(bot, "Clean sheet!", SDL_Rect{150, 70, 440, 50}, FG, F_SMALL, A_LEFT); setBold(false);
    drawText(bot, "Nothing to review from this session.", SDL_Rect{150, 120, 440, 40}, DIM, F_TINY, A_LEFT);
  }
  for (size_t i = 0; i < missed.size() && i < 3; i++) {
    SDL_Rect r{16 + int(i) * 206, 40, 196, 170}; boxRound(bot, r, 12, BAD_FILL, BAD, 2);
    drawText(bot, missed[i].prompt, SDL_Rect{r.x + 6, r.y + 8, r.w - 12, 70}, FG, F_MID);
    drawText(bot, missed[i].answer, SDL_Rect{r.x + 6, r.y + 84, r.w - 12, 40}, ACCENT_TXT, F_SMALL);
    drawText(bot, "You said " + missed[i].said, SDL_Rect{r.x + 6, r.y + 128, r.w - 12, 30}, DIM, F_MICRO);
  }
  if (missed.size() > 3) drawText(bot, "+" + std::to_string(missed.size() - 3) + " more", SDL_Rect{16, 214, 608, 24}, DIM, F_MICRO);
  bool unlock = !pendingUnlocks.empty();
  addBtn(SDL_Rect{16, 330, 180, 84}, B_HOME, mode == Mode::Pack && !unlock ? "Packs" : "Home", F_SMALL);
  std::string again = unlock ? "Continue" : mode == Mode::Timed ? "Try again" : mode == Mode::Review ? (modeAvailable(Mode::Review) ? "More reviews" : "Practice")
                   : mode == Mode::Mixup ? "More mix-ups" : mode == Mode::Pack ? "More words" : "Practise again";
  addBtn(SDL_Rect{212, 330, 412, 84}, B_AGAIN, again, F_SMALL); accent(lastBtn());
  chips({{"A", unlock ? "Continue" : "Again"}, {"B", mode == Mode::Pack && !unlock ? "Packs" : "Home"}});
}

void App::drawPacks() {
  { int known = 0, total = 0; for (int i = 0; i < int(data.packs.size()); i++) { PackInfo pi = packInfo(data, prog, i); known += pi.known; total += pi.total; }
    header("ことば · Word packs", std::to_string(known) + " / " + std::to_string(total) + " words known"); }
  int n = int(data.packs.size());
  if (!n) { drawText(top, "No word packs installed", SDL_Rect{24, 100, 592, 60}, DIM, F_SMALL); return; }
  packSel = std::clamp(packSel, 0, n - 1);
  const Pack& pk = data.packs[packSel]; PackInfo pi = packInfo(data, prog, packSel);
  bool open = pi.ready >= PACK_MIN;
  // top: the highlighted pack
  drawText(top, pk.jp, SDL_Rect{24, 54, 380, 76}, FG, F_BIG, A_LEFT);
  drawText(top, pk.name, SDL_Rect{24, 128, 380, 34}, ACCENT_TXT, F_SMALL, A_LEFT);
  drawWrapped(top, pk.desc, SDL_Rect{24, 164, 592, 56}, DIM, F_TINY);
  statBox(top, SDL_Rect{424, 58, 192, 96}, "KNOWN", std::to_string(pi.known) + " / " + std::to_string(pi.total));
  // a few words you can practise now, known ones ticked
  std::vector<int> ready = readyWords(data, prog, packSel);
  drawText(top, open ? "READY TO PRACTISE · " + std::to_string(pi.ready) + " WORDS" : "UNLOCK MORE KANA TO OPEN THIS PACK",
           SDL_Rect{24, 234, 592, 22}, open ? DIM : ACCENT_TXT, F_MICRO, A_LEFT);
  for (int i = 0; i < 8 && i < int(ready.size()); i++) {
    const Word& w = data.words[ready[i]]; bool known = prog.step(Progress::keyWord(w)) >= KNOWN_STEP;
    SDL_Rect r{24 + (i % 4) * 150, 262 + (i / 4) * 90, 142, 82};
    fillRound(top, r, 12, known ? PANEL2 : PANEL);
    drawText(top, w.kana, SDL_Rect{r.x + 6, r.y + 6, r.w - 12, 42}, FG, F_SMALL);
    drawText(top, w.english, SDL_Rect{r.x + 6, r.y + 50, r.w - 12, 26}, known ? ACCENT_TXT : DIM, F_MICRO);
  }
  if (ready.empty()) drawWrapped(top, "Words appear here as soon as every kana in them is unlocked.", SDL_Rect{24, 260, 592, 60}, DIM, F_TINY);
  if (pi.total > pi.ready) drawText(top, std::to_string(pi.total - pi.ready) + " more as you unlock kana", SDL_Rect{24, 440, 592, 24}, DIM, F_MICRO, A_LEFT);
  // bottom: 3 x 4 pack tiles
  for (int i = 0; i < n && i < 12; i++) {
    PackInfo p = packInfo(data, prog, i); bool ok = p.ready >= PACK_MIN;
    SDL_Rect r{16 + (i % 3) * 204, 10 + (i / 3) * 84, 196, 76};
    addBtn(r, B_PACK0 + i, data.packs[i].jp, F_SMALL); Button& b = lastBtn();
    b.sub = ok ? std::to_string(p.known) + " / " + std::to_string(p.total) + " known" : "locked";
    if (!ok) { b.fill = BG; b.line = DIS_LINE; b.text = DIS_TXT; b.subText = DIS_TXT; b.icon = 1; }
    else if (p.known == p.total) { accent(b); b.icon = 2; }
    if (i == packSel) { b.line = ACC_LINE; b.lineW = 4; }
  }
  addBtn(SDL_Rect{16, 350, 150, 64}, B_BACK, "Back", F_SMALL);
  addBtn(SDL_Rect{176, 350, 150, 64}, B_PACK_BROWSE, "Words", F_SMALL);
  addBtn(SDL_Rect{336, 350, 288, 64}, B_PACK_START, open ? "Practise" : "Learn more kana", F_SMALL, open);
  if (open) accent(lastBtn());
  chips({{"+", "Choose"}, {"A", "Practise"}, {"Y", "Words"}, {"B", "Back"}});
}

void App::drawPackWords() {
  const Pack& pk = data.packs[std::clamp(packSel, 0, int(data.packs.size()) - 1)];
  int n = int(pk.words.size()); wordSel = std::clamp(wordSel, 0, std::max(0, n - 1));
  header("ことば · " + pk.name, std::to_string(packInfo(data, prog, packSel).known) + " / " + std::to_string(n) + " known");
  if (!n) return;
  auto missing = [&](const Word& w) {  // kana in the word that aren't unlocked yet
    std::vector<std::string> m; Script ws = w.katakana ? Script::Katakana : Script::Hiragana;
    for (const std::string& c : Data::partNames(w.kana)) {
      int ki = data.findKana(ws, c);
      if ((ki < 0 || !prog.isUnlocked(data, ki)) && !std::count(m.begin(), m.end(), c)) m.push_back(c);
    }
    return m;
  };
  // top: the highlighted word
  const Word& w = data.words[pk.words[wordSel]]; std::string key = Progress::keyWord(w);
  bool ready = wordReady(data, prog, w), met = prog.introduced(key); int st = prog.step(key);
  drawText(top, w.kana, SDL_Rect{24, 64, 592, 130}, ready ? FG : DIM, F_BIG);
  drawText(top, w.romaji, SDL_Rect{24, 196, 592, 44}, DIM, F_SMALL);
  int mw = std::min(560, textWidth(top, F_SMALL, w.english) + 60);
  SDL_Rect mr{(LW - mw) / 2, 248, mw, 50}; fillRound(top, mr, 25, PANEL); drawText(top, w.english, mr, ACCENT_TXT, F_SMALL);
  std::vector<std::string> parts = Data::partNames(w.kana);
  drawText(top, join(parts, " + "), SDL_Rect{24, 310, 592, 36}, DIM, F_TINY);
  std::string status;
  if (!ready) status = "Locked: unlock " + join(missing(w), " ") + " to practise it";
  else if (st >= KNOWN_STEP) status = "Known";
  else if (met) status = "Learning · step " + std::to_string(st) + " of 4";
  else status = "New: you'll meet it on a card in your next session";
  SDL_Rect sr{24, 360, 592, 60}; fillRound(top, sr, 14, PANEL);
  if (st >= KNOWN_STEP) lineIcon(top, "check", SDL_Rect{sr.x + 18, sr.y + 14, 32, 32}, GOOD, 3.f);
  else if (!ready) lineIcon(top, "lock", SDL_Rect{sr.x + 18, sr.y + 14, 32, 32}, DIM, 2.4f);
  else if (met) stepPips(top, sr.x + 14, sr.y + 24, st);
  int tx = (!ready || met || st >= KNOWN_STEP) ? 90 : 18;  // no icon for a new word
  drawText(top, status, SDL_Rect{sr.x + tx, sr.y, sr.w - tx - 14, sr.h}, st >= KNOWN_STEP ? GOOD : FG, F_TINY, A_LEFT);
  // bottom: 15 words a page
  int page = wordSel / 15, pages = (n + 14) / 15;
  for (int i = 0; i < 15 && page * 15 + i < n; i++) {
    int wi = pk.words[page * 15 + i]; const Word& x = data.words[wi]; std::string k = Progress::keyWord(x);
    bool rd = wordReady(data, prog, x), kn = prog.step(k) >= KNOWN_STEP;
    SDL_Rect r{16 + (i % 3) * 204, 10 + (i / 3) * 66, 196, 58};
    addBtn(r, B_WORD0 + i, x.kana, F_TINY); Button& b = lastBtn(); b.sub = x.english; b.subFont = F_MICRO;
    if (!rd) { b.fill = BG; b.line = DIS_LINE; b.text = DIS_TXT; b.subText = DIS_TXT; b.icon = 1; }
    else if (kn) { b.icon = 2; b.fill = PANEL2; }
    else if (!prog.introduced(k)) b.subText = ACCENT_TXT;  // new to you
    if (page * 15 + i == wordSel) { b.line = ACC_LINE; b.lineW = 4; }
  }
  addBtn(SDL_Rect{16, 350, 180, 64}, B_BACK, "Back", F_SMALL);
  // pager across the rest of the row (the known count lives in the header)
  addBtn(SDL_Rect{212, 350, 110, 64}, B_WPREV, "◀", F_SMALL, page > 0);
  drawText(bot, "Page " + std::to_string(page + 1) + " of " + std::to_string(pages), SDL_Rect{322, 350, 176, 64}, DIM, F_TINY);
  addBtn(SDL_Rect{498, 350, 126, 64}, B_WNEXT, "▶", F_SMALL, page + 1 < pages);
  chips({{"+", "Move"}, {"L/R", "Page"}, {"B", "Back"}});
}

void App::drawToast() {  // "Achievement unlocked" card across the bottom of the top screen
  if (!toastUntil || toastAch < 0) return;
  const Achievement& a = achievementList()[toastAch];
  SDL_Rect r{60, 384, 520, 84}; boxRound(top, r, 18, PANEL2, ACCENT, 3);
  medal(top, SDL_Rect{r.x + 14, r.y + 12, 56, 56}, true);
  drawText(top, "ACHIEVEMENT UNLOCKED", SDL_Rect{r.x + 84, r.y + 10, r.w - 100, 22}, ACCENT_TXT, F_MICRO, A_LEFT);
  drawText(top, a.title, SDL_Rect{r.x + 84, r.y + 32, r.w - 100, 30}, FG, F_TINY, A_LEFT);
  drawText(top, a.desc, SDL_Rect{r.x + 84, r.y + 60, r.w - 100, 20}, DIM, F_MICRO, A_LEFT);
}

void App::drawAwards() {
  const auto& A = achievementList(); int n = int(A.size()); awardSel = std::clamp(awardSel, 0, n - 1);
  int got = 0; for (auto& a : A) if (prog.achieved.count(a.id)) got++;
  header("メダル · Achievements", std::to_string(got) + " / " + std::to_string(n));
  const Achievement& a = A[awardSel]; auto it = prog.achieved.find(a.id); bool have = it != prog.achieved.end();
  // top: the highlighted achievement
  SDL_Rect p{24, 60, 592, 400}; fillRound(top, p, 18, PANEL);
  medal(top, SDL_Rect{p.x + 30, p.y + 26, 120, 120}, have);
  drawText(top, a.title, SDL_Rect{p.x + 170, p.y + 40, p.w - 190, 56}, have ? FG : DIM, F_MID, A_LEFT);
  drawWrapped(top, a.desc, SDL_Rect{p.x + 170, p.y + 102, p.w - 190, 60}, DIM, F_TINY);
  if (have) {
    static const char* MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int d = it->second, y = d / 10000, m = (d / 100) % 100, dd = d % 100;
    std::string when = (m >= 1 && m <= 12) ? std::to_string(dd) + " " + MON[m - 1] + " " + std::to_string(y) : "earlier";
    drawText(top, "Earned " + when, SDL_Rect{p.x + 40, p.y + 190, p.w - 80, 40}, GOOD, F_SMALL, A_LEFT);
  } else {
    AchProgress pr = achievementProgress(data, prog, awardSel);
    drawText(top, "Not yet", SDL_Rect{p.x + 40, p.y + 190, 300, 40}, DIM, F_SMALL, A_LEFT);
    if (pr.target > 1) {
      drawText(top, std::to_string(std::min(pr.cur, pr.target)) + " / " + std::to_string(pr.target), SDL_Rect{p.x + 300, p.y + 190, p.w - 340, 40}, ACCENT_TXT, F_SMALL, A_RIGHT);
      bar(top, SDL_Rect{p.x + 40, p.y + 244, p.w - 80, 16}, float(pr.cur) / pr.target);
    }
  }
  drawWrapped(top, "Earn them by learning kana and words, keeping your streak and racing the timed challenge.",
              SDL_Rect{p.x + 40, p.y + 300, p.w - 80, 80}, DIM, F_TINY);
  // bottom: every achievement
  for (int i = 0; i < n; i++) {
    bool h = prog.achieved.count(A[i].id) > 0, on = i == awardSel;
    SDL_Rect r{16 + (i % 4) * 152, 10 + (i / 4) * 78, 146, 72};
    Col fill = h ? PANEL2 : BG; if (pressedId == B_AWARD0 + i) fill = mix(fill, FG, 0.2f);
    boxRound(bot, r, 12, fill, on ? ACCENT : h ? mix(BG, FG, 0.45f) : DIS_LINE, on ? 4 : 2);
    medal(bot, SDL_Rect{r.x + (r.w - 40) / 2, r.y + 4, 40, 40}, h);
    drawText(bot, A[i].title, SDL_Rect{r.x + 4, r.y + 46, r.w - 8, 24}, h ? FG : DIS_TXT, F_MICRO);
    addBtn(r, B_AWARD0 + i, ""); lastBtn().hidden = true;
  }
  addBtn(SDL_Rect{460, 10 + 4 * 78, 164, 72}, B_BACK, "Back", F_SMALL);
  chips({{"+", "Move"}, {"B", "Back"}});
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
  SDL_Rect c{70, 90, 500, 270}; card(top, c, 30);
  lineIcon(top, "pause", SDL_Rect{LW / 2 - 34, c.y + 22, 68, 68}, ACCENT, 2.6f);
  drawRing(top, LW / 2, c.y + 56, 38, 5, 1.f, ACCENT, ACCENT);
  drawText(top, "ひとやすみ · Paused", SDL_Rect{c.x, c.y + 110, c.w, 60}, FG, F_MID);
  drawText(top, screen == Screen::Drill && mode != Mode::Timed
                    ? "Question " + std::to_string(std::min(index + 1, sessionLen)) + " of " + std::to_string(sessionLen) + " · your answers are saved"
                    : std::string("Your progress is saved"), SDL_Rect{c.x, c.y + 180, c.w, 40}, DIM, F_TINY);
  drawText(top, "Up/Down choose · A select · B resume", SDL_Rect{20, 400, 600, 36}, DIM, F_MICRO);
  const char* labels[3] = {"Resume", "Back to menu", "Quit Banana Kana"};
  const int ids[3] = {B_RESUME, B_MENU, B_QUIT};
  for (int i = 0; i < 3; i++) {
    addBtn(SDL_Rect{80, 40 + i * 122, 480, 96}, ids[i], labels[i], F_SMALL); lastBtn().radius = 48;
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

  header("かなひょう · Kana chart", scriptName(sc));
  if (chartSel >= 0) {
    const Kana& k = data.kana[chartSel]; KanaState ks = prog.state(data, chartSel);
    const Glyph* g = data.glyphFor(k.glyph); int stI = data.stageOfKana[chartSel];
    SDL_Rect card{24, 58, 220, 240}; fillRound(top, card, 16, PANEL);
    drawText(top, k.glyph, card, ks == KanaState::Locked ? DIM : FG, F_HUGE);
    drawText(top, k.romaji, SDL_Rect{264, 58, 352, 84}, ACCENT_TXT, F_BIG, A_LEFT);
    drawText(top, (g ? std::to_string(g->strokes.size()) + (g->strokes.size() == 1 ? " stroke" : " strokes") : std::string("Combination")) + " · " + stageLabel(stI),
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
    if (c.kana == chartSel) { b.line = ACC_LINE; b.lineW = 4; }
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
  auto choice = [&](const std::string& label, int cur, int n, const std::string& value, const std::string& help,
                    std::vector<std::string> opts = {}) {
    SRow x; x.label = label; x.value = value; x.help = help; x.kind = 0; x.canDec = cur > 0; x.canInc = cur < n - 1;
    x.opts = std::move(opts); x.cur = cur; r.push_back(x);
  };
  const std::vector<std::string> W3 = {"Off", "Sometimes", "Often"};
  auto info = [&](const std::string& label, const std::string& value, const std::string& help) {
    SRow x; x.label = label; x.value = value; x.help = help; x.kind = 2; r.push_back(x);
  };
  auto action = [&](const std::string& label, const std::string& value, const std::string& help) {
    SRow x; x.label = label; x.value = value; x.help = help; x.kind = 1; r.push_back(x);
  };
  switch (g) {
    case 0:
      choice("Reading", st.reading, 3, WEIGHT_NAMES[st.reading], "See a kana and pick its sound, or see a sound and pick the kana.", W3);
      choice("Word meanings", st.meanings, 3, WEIGHT_NAMES[st.meanings], "Read a word and pick its meaning. Words only use kana you've unlocked.", W3);
      choice("Writing", st.writing, 3, WEIGHT_NAMES[st.writing], "Trace the kana with the stylus, stroke by stroke.", W3);
      choice("Typing", st.typing, 3, WEIGHT_NAMES[st.typing], "Type the answer on the on-screen keyboard.", W3);
      choice("Typing keyboard", st.kanaKeys ? 1 : 0, 2, st.kanaKeys ? "Kana" : "Romaji",
             "Romaji: see the kana, type its romaji. Kana: see the romaji, type the kana on a kana keyboard.", {"Romaji", "Kana"});
      break;
    case 1: {
      int li = 1; for (int i = 0; i < 4; i++) if (SESSION_LENS[i] == st.sessionLen) li = i;
      choice("Session length", li, 4, std::to_string(st.sessionLen) + " questions", "How many questions in each practice session.", {"5", "10", "15", "20"});
      choice("Review older stages", st.review, 3, REVIEW_NAMES[st.review],
             "How often kana from stages you've passed come back. Weak kana and reviews that are due always come back.", {"Off", "Some", "Lots"});
      int gi = 1; for (int i = 0; i < 4; i++) if (GOALS[i] == st.dailyGoal) gi = i;
      choice("Daily goal", gi, 4, std::to_string(st.dailyGoal) + " answers", "Answer this many questions in a day to keep your streak going.", {"5", "10", "20", "30"});
      const char* SN[3] = {"Relaxed", "Normal", "Strict"};
      choice("Writing check", st.strict, 3, SN[st.strict],
             "How closely your strokes must follow the real ones. Strict also wants the right length and position; relaxed forgives wobbly lines.", {"Relaxed", "Normal", "Strict"});
      break; }
    case 2: {
      bool kat = prog.scriptUnlocked(Script::Katakana);
      SRow x; x.label = "Practise"; x.value = scriptName(st.script); x.kind = 0;
      x.help = kat ? "Which kana your practice sessions use." : "Katakana unlocks after hiragana stage 10, or skip ahead below.";
      x.canDec = kat && st.script == Script::Katakana; x.canInc = kat && st.script == Script::Hiragana;
      if (kat) { x.opts = {"ひらがな", "カタカナ"}; x.cur = st.script == Script::Katakana ? 1 : 0; }
      r.push_back(x);
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
      choice("Theme", st.light ? 1 : 0, 2, st.light ? "Light" : "Dark", "Light for the daytime, dark for the evening: the same design either way.", {"Dark", "Light"});
      choice("Kana font", st.kanaHand ? 1 : 0, 2, st.kanaHand ? "Handwriting" : "Rounded",
             "How kana look in drills and menus: rounded print (Zen Maru Gothic) or textbook handwriting (Klee One). Lessons and writing always use handwriting, to match the stroke order.", {"Rounded", "Handwriting"});
      const char* GL[3] = {"Cross", "Grid", "None"}; const char* IN[3] = {"Thin", "Normal", "Thick"};
      choice("Canvas lines", st.guideLines, 3, GL[st.guideLines], "Guide lines on the writing pad: a centre cross, a finer grid, or none.", {GL[0], GL[1], GL[2]});
      choice("Ink", st.ink, 3, IN[st.ink], "How thick your strokes are drawn.", {IN[0], IN[1], IN[2]});
      const char* SO[4] = {"Off", "Quiet", "Normal", "Loud"};
      choice("Sound", st.sound, 4, SO[st.sound], "Sound effects for taps, right and wrong answers, unlocks and your daily goal.", {SO[0], SO[1], SO[2], SO[3]});
      const char* RU[3] = {"Off", "Light", "Strong"};
      choice("Rumble", st.rumble, 3, RU[st.rumble], "One tick for a right answer, two buzzes for a wrong one, three quick ticks when you learn something, and a long rumble for unlocks.", {RU[0], RU[1], RU[2]});
      const char* BD[6] = {"Plain", "Auto", "春", "夏", "秋", "冬"};
      const char* BDN[6] = {"Plain", "Seasons (by the calendar)", "Spring 春", "Summer 夏", "Autumn 秋", "Winter 冬"};
      choice("Background", st.backdrop, 6, BDN[st.backdrop], "Seasons of Japan: sakura in spring, waves and fireflies in summer, maple leaves in autumn, snow in winter. Seasons follows the calendar, or pick one.", {BD[0], BD[1], BD[2], BD[3], BD[4], BD[5]});
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
      action("Reset settings", resetSetArmed ? "Press A again to confirm" : "Reset",
             "Put every setting back to how it started: drills, sessions, theme, fonts, sound and rumble. Your progress stays.");
      action("Run setup again", "Open",
             "Go through the first-launch steps again: light or dark, background, vibration, and what you already know. Your progress and streak stay; skipping ahead only ever moves you forwards.");
      info("Backup", backupPath.empty() ? "Off" : "On the SD card",
           backupPath.empty() ? "No backup copy is kept (only in the installed app)."
                              : "A copy of your progress is kept in roms/ports/BananaKana/progress-backup.txt. It's restored automatically after a reinstall or reflash.");
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
              return on.empty() ? "all off" : on.size() == 4 ? "all four on" : std::to_string(on.size()) + " of 4 on"; }
    case 1: return std::to_string(st.sessionLen) + " a go · goal " + std::to_string(st.dailyGoal);  // short: tile subtitles are one line
    case 2: { std::string s = scriptName(st.script); s[0] = char(std::tolower(s[0])); return s; }
    case 3: return std::string(st.light ? "Light" : "Dark") + (st.sound ? " · sound on" : " · sound off");
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
      else if (r == 4) { st.sound = std::clamp(st.sound + dir, 0, 3); snd::setVolume(st.sound); snd::play(snd::Sfx::Right); }
      else if (r == 5) { st.rumble = std::clamp(st.rumble + dir, 0, 2); snd::setRumble(st.rumble); snd::rumble(snd::Sfx::Wrong); }
      else st.backdrop = std::clamp(st.backdrop + dir, 0, 5);
      break;
    case 4:
      if (r == 4) {
        if (!resetArmed) { resetArmed = true; return; }
        Settings keep = prog.settings; prog = Progress{}; prog.settings = keep; prog.settings.script = Script::Hiragana; held.on = false;
        pendingUnlocks.clear(); resetArmed = false;
      } else if (r == 5) {
        if (!resetSetArmed) { resetSetArmed = true; return; }
        Script keep = prog.settings.script;  // the script you're on isn't a preference: stay on it
        prog.settings = Settings{}; prog.settings.script = keep;
        applyTheme(prog.settings.light); setKanaHand(prog.settings.kanaHand);
        snd::setVolume(prog.settings.sound); snd::setRumble(prog.settings.rumble);
        resetSetArmed = false;
      }
      break;
    case 5: if (r == 1) { go(Screen::Credits); return; } break;
  }
  if (g == 4 && r == 6) { welcomeAgain = true; go(Screen::Welcome); return; }
  save();
}

void App::settingIcon(Win& w, int g, int x, int y, int px, Col a, Col b) {
  (void)b;
  static const char* NAMES[GROUPS] = {"pencil", "clock", "", "display", "chart", "info"};
  int size = 12 * px;
  if (g == 2) { drawText(w, prog.settings.script == Script::Katakana ? "ア" : "あ", SDL_Rect{x, y - px, size, size}, a, px >= 6 ? F_BIG : F_MID); return; }
  lineIcon(w, NAMES[g], SDL_Rect{x, y, size, size}, a, 2.2f);
}

void App::footer(const std::string& lk, const std::string& ll, const std::string& rk, const std::string& rl) {
  auto pill = [&](int x, int w, const std::string& k, const std::string& l, int id) {
    SDL_Rect r{x, 416, w, 46};
    r = kt::pill(bot, r, 23, 0, pressedId == id);
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

void App::settingsPanel(int g, const std::string& label, const std::string& title, const std::string& accentLine, const std::string& body, bool preview) {
  SDL_Rect p{28, 24, 584, 426};
  card(top, p, 26);
  fillRound(top, SDL_Rect{50, 50, 104, 104}, 30, ACCENT);  // the group's icon on a mode-coloured tile
  settingIcon(top, g, 66, 66, 6, ON_ACCENT, ON_ACCENT);
  drawText(top, label, SDL_Rect{170, 54, 420, 24}, DIM, F_MICRO, A_LEFT);
  drawText(top, title, SDL_Rect{170, 78, 420, 56}, FG, F_MID, A_LEFT);
  setBold(true); drawText(top, accentLine, SDL_Rect{170, 136, 420, 34}, ACCENT_TXT, F_TINY, A_LEFT); setBold(false);
  // Display rows have a preview card at y 318: keep the help above it (smaller text when it would run into it)
  const int bodyH = preview ? 318 - 204 - 4 : 230;
  const int bf = wrappedHeight(top, body, 528, F_TINY) <= bodyH ? F_TINY : F_MICRO;
  drawWrapped(top, body, SDL_Rect{56, 204, 528, bodyH}, DIM, bf);
}

// Display settings: a live preview of the highlighted row under its help text (top screen)
void App::settingPreview(int row) {
  const Settings& st = prog.settings;
  SDL_Rect area{56, 318, 528, 118};
  fillRound(top, area, 18, PANEL2);
  drawText(top, "PREVIEW", SDL_Rect{area.x + 16, area.y + 8, 200, 20}, DIM, F_MICRO, A_LEFT);
  if (row == 0) {  // theme: the two looks side by side, the current one ringed
    for (int k = 0; k < 2; k++) {
      bool light = k == 1; SDL_Rect m{area.x + 150 + k * 200, area.y + 14, 160, 92};
      if (light == st.light) fillRound(top, SDL_Rect{m.x - 5, m.y - 5, m.w + 10, m.h + 10}, 20, ACCENT);
      fillRound(top, m, 16, light ? hex(0xEEF1F4) : hex(0x1A1E25));
      fillRound(top, SDL_Rect{m.x + 12, m.y + 10, m.w - 24, 42}, 10, light ? hex(0xFFFFFF) : hex(0x262C36));
      fillRound(top, SDL_Rect{m.x + 22, m.y + 22, 80, 8}, 4, light ? hex(0x2B2F36) : hex(0xE8EAEE));
      fillRound(top, SDL_Rect{m.x + 22, m.y + 38, 110, 6}, 3, light ? hex(0xA0A7B0) : hex(0x6B7380));
      fillRound(top, SDL_Rect{m.x + 12, m.y + 60, m.w - 24, 22}, 11, hex(0xF28C28));
      drawText(top, light ? "Light" : "Dark", SDL_Rect{m.x, m.y + 58, m.w, 26}, hex(0xFFFFFF), F_MICRO);
    }
  } else if (row == 1) {  // kana font: a sample in the font that's on now
    drawText(top, "あいう アイウ", SDL_Rect{area.x + 100, area.y + 4, area.w - 120, area.h - 8}, FG, F_BIG);
  } else if (row == 2 || row == 3) {  // canvas lines / ink: a little writing pad with あ
    SDL_Rect pad{area.x + area.w / 2 - 50, area.y + 9, 100, 100};
    fillRound(top, pad, 12, CANVAS);
    Col ln = mix(CANVAS, FG, 0.25f);
    if (st.guideLines == 1) for (int q4 : {1, 3}) {
      fillRect(top, SDL_Rect{pad.x + pad.w * q4 / 4, pad.y + 6, 1, pad.h - 12}, mix(CANVAS, ln, 0.5f));
      fillRect(top, SDL_Rect{pad.x + 6, pad.y + pad.h * q4 / 4, pad.w - 12, 1}, mix(CANVAS, ln, 0.5f));
    }
    if (st.guideLines < 2) { fillRect(top, SDL_Rect{pad.x + pad.w / 2, pad.y + 6, 1, pad.h - 12}, ln); fillRect(top, SDL_Rect{pad.x + 6, pad.y + pad.h / 2, pad.w - 12, 1}, ln); }
    const float INK[3] = {0.011f, 0.016f, 0.022f};
    // the drill pad is about 400 px wide: scale its ink so the sample looks the same weight
    if (const Glyph* g = data.glyphFor("あ")) for (auto& stroke : g->strokes)
      drawStrokeIn(top, stroke, SDL_Rect{pad.x + 8, pad.y + 8, pad.w - 16, pad.h - 16}, FG, 400 * INK[std::clamp(st.ink, 0, 2)] * 0.42f);
  } else if (row == 6) {  // background: the season's motif, drifting
    int sn = seasonNow();
    if (sn < 0) drawWrapped(top, "The plain striped look. Pick Seasons or a season to see it on both screens.", SDL_Rect{area.x + 24, area.y + 40, area.w - 48, 60}, FG, F_TINY);
    else {
      const char* NAME[4] = {"春 Spring: sakura", "夏 Summer: waves and fireflies", "秋 Autumn: momiji", "冬 Winter: snow"};
      setBold(true); drawText(top, NAME[sn], SDL_Rect{area.x + 24, area.y + 34, area.w - 48, 34}, FG, F_TINY, A_LEFT); setBold(false);
      drawWrapped(top, "Drifts gently on menus; stays still in drills and writing.", SDL_Rect{area.x + 24, area.y + 70, area.w - 48, 40}, DIM, F_MICRO);
    }
  } else {  // sound / rumble: what you get when it plays
    lineIcon(top, row == 4 ? "play" : "timed", SDL_Rect{area.x + 110, area.y + 32, 56, 56}, ACCENT_TXT, 2.6f);
    drawWrapped(top, row == 4 ? (st.sound ? "Changing it plays a sample at the new volume." : "Sound effects are off.")
                              : (st.rumble ? "Changing it buzzes once at the new strength." : "Rumble is off."),
                SDL_Rect{area.x + 180, area.y + 34, area.w - 196, 60}, FG, F_TINY);
  }
}

void App::drawSettings() {
  auto rows = settingRows(setGroup);
  std::vector<std::string> names; for (auto& r : rows) names.push_back(r.label);
  settingsPanel(setGroup, "SETTINGS", groupName(setGroup), std::to_string(rows.size()) + (rows.size() == 1 ? " setting" : " settings"),
                join(names, ", ") + ".");
  card(bot, SDL_Rect{16, 8, 608, 46}, 16);
  setBold(true); drawText(bot, "Settings", SDL_Rect{34, 8, 300, 46}, FG, F_TINY, A_LEFT); setBold(false);
  drawText(bot, "Choose a group", SDL_Rect{300, 10, 306, 48}, DIM, F_MICRO, A_RIGHT);
  for (int g = 0; g < GROUPS; g++) {
    SDL_Rect r{16 + (g % 3) * 206, 70 + (g / 3) * 170, 196, 160};
    bool on = g == setGroup;
    if (on) r = kt::pill(bot, SDL_Rect{r.x, r.y, r.w, r.h - 6}, 22, 1, pressedId == B_GROUP0 + g);
    else card(bot, SDL_Rect{r.x, r.y, r.w, r.h - 6}, 22, pressedId == B_GROUP0 + g ? mix(PANEL, FG, 0.08f) : PANEL);
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
  settingsPanel(setGroup, upper, cur.label, cur.value, cur.help, setGroup == 3);
  if (setGroup == 3) settingPreview(setRow);
  card(bot, SDL_Rect{16, 8, 608, 46}, 16);
  setBold(true); drawText(bot, groupName(setGroup), SDL_Rect{34, 8, 300, 46}, FG, F_TINY, A_LEFT); setBold(false);
  drawText(bot, "L / R: other groups", SDL_Rect{300, 8, 306, 46}, DIM, F_MICRO, A_RIGHT);
  // rows keep a comfortable height; groups with more than 5 rows scroll (the selected row stays in view)
  const int VIS = 5, PITCH = 66, nRows = int(rows.size());
  // the window follows the selected row (d-pad) but a stylus drag can move it freely
  if (setRow < setFirst) setFirst = setRow;
  if (setRow >= setFirst + VIS) setFirst = setRow - VIS + 1;
  setFirst = nRows <= VIS ? 0 : std::clamp(setFirst, 0, nRows - VIS);
  const int first = setFirst;
  for (int i = first; i < std::min(nRows, first + VIS); i++) {
    const SRow& row = rows[i];
    SDL_Rect r{16, 68 + (i - first) * PITCH, 608, PITCH - 12};
    bool on = i == setRow;
    if (on) fillRound(bot, SDL_Rect{r.x - 4, r.y - 4, r.w + 8, r.h + 13}, 20, ACCENT);
    card(bot, r, 16, pressedId == B_SROW0 + i ? mix(PANEL, FG, 0.08f) : PANEL);
    Col t = FG, v = row.kind == 2 ? DIM : FG;
    setBold(true); drawText(bot, row.label, SDL_Rect{r.x + 18, r.y, 190, r.h}, t, F_TINY, A_LEFT); setBold(false);
    if (row.kind == 0 && !row.opts.empty()) {  // segmented pill: every option one tap away
      SDL_Rect tr{r.x + 214, r.y + 6, r.w - 222, r.h - 12}; fillRound(bot, tr, tr.h / 2, PANEL2);
      int no = int(row.opts.size()), sw = (tr.w - 8) / no;
      for (int j = 0; j < no; j++) {
        SDL_Rect sg{tr.x + 4 + j * sw, tr.y + 4, sw, tr.h - 8};
        if (j == row.cur) { fillRound(bot, sg, sg.h / 2, ACCENT); setBold(true); drawText(bot, row.opts[j], sg, ON_ACCENT, F_TINY); setBold(false); }
        else drawText(bot, row.opts[j], sg, DIM, F_TINY);
        addBtn(sg, B_SSEG0 + i * 8 + j, ""); lastBtn().hidden = true;
      }
    } else if (row.kind == 0 || row.kind == 3) {
      Col ad = row.canDec ? t : DIS_TXT, ai = row.canInc ? t : DIS_TXT;
      drawText(bot, "◀", SDL_Rect{r.x + r.w - 300, r.y, 36, r.h}, ad, F_MICRO);
      drawText(bot, row.value, SDL_Rect{r.x + r.w - 264, r.y, 220, r.h}, row.kind == 3 && row.canDec ? ACCENT_TXT : v, F_TINY);
      drawText(bot, "▶", SDL_Rect{r.x + r.w - 44, r.y, 36, r.h}, ai, F_MICRO);
      addBtn(SDL_Rect{r.x + r.w - 310, r.y, 70, r.h}, B_SDEC0 + i, ""); lastBtn().hidden = true;
      addBtn(SDL_Rect{r.x + r.w - 70, r.y, 70, r.h}, B_SINC0 + i, ""); lastBtn().hidden = true;
    } else if (row.kind == 1) {
      drawText(bot, row.value, SDL_Rect{r.x + r.w - 330, r.y, 280, r.h}, (row.label == "Reset progress" && resetArmed) || (row.label == "Reset settings" && resetSetArmed) ? BAD : v, F_TINY, A_RIGHT);
      drawText(bot, "▶", SDL_Rect{r.x + r.w - 44, r.y, 36, r.h}, t, F_MICRO);
    } else {
      drawText(bot, row.value, SDL_Rect{r.x + r.w - 350, r.y, 332, r.h}, v, F_TINY, A_RIGHT);
    }
    addBtn(r, B_SROW0 + i, ""); lastBtn().hidden = true;
  }
  std::string act = cur.kind == 0 ? "Change" : cur.kind == 1 ? (cur.label.rfind("Reset", 0) == 0 ? "Reset" : "Open")
                  : cur.kind == 3 ? (cur.canDec ? "Confirm" : "") : "";
  footer("B", "Back", "A", act);
  if (nRows > VIS) {  // more rows than fit: up/down buttons between the footer pills, plus "3 of 7"
    addBtn(SDL_Rect{234, 410, 64, 56}, B_SUP, "▲", F_TINY, setRow > 0); lastBtn().radius = 28;
    addBtn(SDL_Rect{342, 410, 64, 56}, B_SDOWN, "▼", F_TINY, setRow < nRows - 1); lastBtn().radius = 28;
    drawText(bot, std::to_string(setRow + 1) + "/" + std::to_string(nRows), SDL_Rect{298, 410, 44, 56}, DIM, F_MICRO);
  }
}

void App::drawCredits() {
  const float t = SDL_GetTicks() / 1000.f;
  // top: twinkling confetti, a wavy thank-you and a banana parade
  Col conf[MC_COUNT];
  for (int m = 0; m < MC_COUNT; m++) conf[m] = modeCol(ModeCol(m));
  for (int i = 0; i < 26; i++) {
    unsigned h = 2654435761u * unsigned(i + 7);
    int x = int(h % 600) + 12, y = int((h >> 10) % 300) + 46, sz = 4 + int((h >> 20) % 7);
    float tw = 0.5f + 0.5f * std::sin(t * 2.2f + i);
    if (tw > 0.25f) fillRect(top, SDL_Rect{x, y, sz, sz}, mix(BG, conf[i % MC_COUNT], tw));
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
    appTile(top, bx, by, 52);
  }
  drawText(top, "For the Anbernic RG DS Plus · code MIT · licences in data/licenses",
           SDL_Rect{20, 420, 600, 34}, DIM, F_MICRO);

  // bottom: sticker cards
  struct Card { const char* title; const char* role; const char* who; const char* icon; ModeCol col; };
  const Card cards[6] = {
    {"BananaOS", "the OS it all runs on", "JorreFog · BananaLinux on GitHub", "play", MC_ORANGE},
    {"KanjiVG", "stroke order data", "Ulrich Apel · CC BY-SA 3.0", "pencil", MC_BLUE},
    {"Zen Maru Gothic · Klee One", "rounded and handwriting fonts", "SIL Open Font License 1.1", "book", MC_PURPLE},
    {"Noto Sans CJK JP", "fallback font", "Google and Adobe · OFL 1.1", "grid", MC_TEAL},
    {"Banana Player", "showed how BananaOS apps work", "tonywei92 · banana-player on GitHub", "map", MC_GREEN},
    {"Banana Kana", "hints, words and code", "Parzival3971 and Claude · MIT", "star", MC_RED},
  };
  for (int i = 0; i < 6; i++) {
    SDL_Rect r{16 + (i % 2) * 310, 10 + (i / 2) * 112, 298, 102};
    const Col c = modeCol(cards[i].col);
    card(bot, r, 16);
    fillRound(bot, SDL_Rect{r.x, r.y, 10, r.h}, 5, c);
    SDL_Rect badge{r.x + r.w - 46, r.y + 10, 36, 36};
    fillCircle(bot, badge.x + 18, badge.y + 18, 18, mix(PANEL, c, 0.18f));
    lineIcon(bot, cards[i].icon, SDL_Rect{badge.x + 7, badge.y + 7, 22, 22}, c, 2.4f);
    drawText(bot, cards[i].title, SDL_Rect{r.x + 24, r.y + 8, r.w - 76, 32}, FG, F_TINY, A_LEFT);
    drawText(bot, cards[i].role, SDL_Rect{r.x + 24, r.y + 40, r.w - 76, 24}, ACCENT_TXT, F_MICRO, A_LEFT);
    drawText(bot, cards[i].who, SDL_Rect{r.x + 24, r.y + 66, r.w - 32, 24}, DIM, F_MICRO, A_LEFT);
  }
  addBtn(SDL_Rect{16, 350, 608, 74}, B_BACK, "Back", F_SMALL); accent(lastBtn());
  chips({{"B", "Back"}});
}

}  // namespace kt
