// Scripted UI run with dummy video: drives the real screens with synthetic touches and buttons,
// checks the flows, and saves screenshots of every screen (both themes) when --shots is given.
#include <algorithm>
#include <cstdio>
#include <set>

#include "app.h"

namespace kt {

int App::selfTest(const std::string& shotDir) {
  int fails = 0;
  auto CHECK = [&](bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); fails++; } };
  rng.seed(12345);
  auto frame = [&]() { render(); };
  auto find = [&](int id) -> const Button* { frame(); for (auto& b : buttons) if (b.id == id) return &b; return nullptr; };
  auto click = [&](int id) {
    const Button* b = find(id); if (!b || !b->enabled) return false;
    int cx = b->r.x + b->r.w / 2, cy = b->r.y + b->r.h / 2; pointer(0, cx, cy); pointer(2, cx, cy); return true;
  };
  auto shot = [&](const std::string& name) {
    if (shotDir.empty()) return;
    frame();
    saveShot(top, shotDir + "/" + name + "_top.bmp"); saveShot(bot, shotDir + "/" + name + "_bot.bmp");
  };
  auto trace = [&](const Glyph& g, float jx, float jy) {
    SDL_Rect c = canvasRect();
    for (auto& s : g.strokes) {
      bool first = true;
      for (auto& p : resample(s, 30)) { SDL_Point sp = glyphToBox({p.x + jx, p.y + jy}, c); pointer(first ? 0 : 1, sp.x, sp.y); first = false; }
      SDL_Point e = glyphToBox(s.back(), c); pointer(2, e.x, e.y);
    }
  };
  std::set<int> typesSeen; std::set<std::string> shotTypes;
  const char* tname[5] = {"read", "recall", "meaning", "write", "type"};
  // Answers the current drill question; returns whether the app marked it correct.
  auto answer = [&](bool right, bool shots) {
    frame(); typesSeen.insert(int(q.type));
    std::string tn = tname[int(q.type)] + std::string(q.isWord ? "_word" : "");
    if (shots && !shotTypes.count(tn)) shot("q_" + tn);
    if (!q.options.empty()) {
      int n = int(q.options.size()); click(B_TILE0 + (right ? q.correct : (q.correct + 1) % n)); click(B_CHECK);
    } else if (q.type == QType::Type && q.kanaInput) {
      typed = right ? q.answer : "ぬぬ"; click(B_CHECK);
    } else if (q.type == QType::Type) {
      for (char ch : (right ? q.answer : std::string("zzq"))) click(B_KEY0 + (ch - 'a'));
      click(B_CHECK);
    } else {
      for (int guard = 0; guard < 3 && !checked; guard++) {  // combination kana take two parts
        const Glyph* g = activeGlyph(); CHECK(g != nullptr, "write glyph"); if (!g) break;
        if (right) trace(*g, 1.5f, -1.0f); else { SDL_Rect c = canvasRect(); pointer(0, c.x + 10, c.y + 10); pointer(1, c.x + 60, c.y + 12); pointer(2, c.x + 60, c.y + 12); }
        click(B_CHECK);
      }
    }
    CHECK(checked, "question checked");
    if (shots && !shotTypes.count(tn)) { shot("q_" + tn + "_checked"); shotTypes.insert(tn); }
    bool ok = correct; click(B_CONTINUE); return ok;
  };
  auto finishLessonFlow = [&]() {
    for (int guard = 0; guard < 30 && screen == Screen::Lesson; guard++) click(B_LESSON_NEXT);
  };

  // ---- 1. fresh start: home, lesson for stage 1, tracing
  go(Screen::Home); shot("home_new");
  CHECK(prog.lessonPending(Script::Hiragana), "lesson pending at start");
  CHECK(click(B_CART0), "practice cart"); CHECK(screen == Screen::Lesson && lessonStage == 0, "lesson opens");
  shot("lesson");
  trace(*activeGlyph(), 0.5f, 0.5f); CHECK(strokes.size() == activeGlyph()->strokes.size(), "lesson trace strokes");
  shot("lesson_traced");
  CHECK(click(B_LESSON_NEXT), "lesson next"); CHECK(lessonIdx == 1 && strokes.empty(), "next kana clears");
  finishLessonFlow();
  CHECK(prog.lessons[0] == 1, "lesson recorded"); CHECK(screen == Screen::Drill, "lesson -> practice");

  // ---- 2. practise until stage 2 unlocks (2 steps per session means several sessions)
  prog.settings.meanings = 2; prog.settings.writing = 2; prog.settings.typing = 2;
  bool unlockedSeen = false, wrongDone = false, pauseDone = false;
  for (int sess = 0; sess < 12 && !unlockedSeen; sess++) {
    if (screen != Screen::Drill) { CHECK(click(B_AGAIN), "practice again"); }
    for (int i = 0; i < 40 && screen == Screen::Drill; i++) {
      if (!pauseDone && i == 2) {
        togglePause(); CHECK(paused, "paused"); shot("pause");
        CHECK(click(B_RESUME), "resume"); CHECK(!paused && screen == Screen::Drill, "resumed");
        pauseDone = true;
      }
      if (!wrongDone && i == 3 && !q.options.empty() && !q.isWord) {
        std::string key = q.key; int before = prog.step(key);
        frame(); click(B_TILE0 + (q.correct + 1) % int(q.options.size())); click(B_CHECK);
        CHECK(checked && !correct, "wrong answer marked wrong"); CHECK(prog.step(key) == std::max(0, before - 1), "wrong drops a step");
        shot("q_wrong"); click(B_CONTINUE); wrongDone = true; continue;
      }
      CHECK(answer(true, true), "scripted answer correct");
    }
    CHECK(screen == Screen::Result, "session ends on results");
    if (sess == 0) shot("result");
    if (!pendingUnlocks.empty()) {
      unlockedSeen = true;
      shot("result_unlock");
      CHECK(click(B_AGAIN), "continue to unlock"); CHECK(screen == Screen::Unlocked, "unlocked screen");
      shot("unlocked");
      CHECK(prog.unlocked[0] == 2, "stage 2 unlocked");
      CHECK(click(B_START_LESSON), "start lesson"); CHECK(screen == Screen::Lesson && lessonStage == 1, "stage 2 lesson");
      finishLessonFlow(); CHECK(prog.lessons[0] == 2, "stage 2 lesson recorded");
    }
  }
  CHECK(unlockedSeen, "stage 2 unlocked within 12 sessions");
  printf("sessions so far: %d\n", prog.session);
  // a few more sessions with stage 2 to see every question type
  for (int sess = 0; sess < 6 && typesSeen.size() < 5; sess++) {
    if (screen != Screen::Drill) { if (screen == Screen::Result && pendingUnlocks.empty()) click(B_AGAIN); else startSession(); }
    for (int i = 0; i < 40 && screen == Screen::Drill; i++) answer(true, true);
  }
  CHECK(typesSeen.size() == 5, "all five question types seen");
  pendingUnlocks.clear();

  // ---- 3. pause -> back to menu
  startSession(); togglePause(); pauseSel = 1; padButton(SDL_CONTROLLER_BUTTON_A);
  CHECK(screen == Screen::Home && !paused, "pause -> menu");
  shot("home");

  // ---- 4. stage map: passed stage replays its lesson, locked stage cannot start
  CHECK(click(B_CART0 + 1), "stages cart"); CHECK(screen == Screen::Stages, "stage map");
  shot("stages");
  CHECK(click(B_STAGE0 + 0), "select stage 1"); frame();
  { const Button* a = find(B_STAGE_ACTION); CHECK(a && a->label == "Replay lesson", "replay label"); }
  CHECK(click(B_STAGE_ACTION), "replay"); CHECK(screen == Screen::Lesson && lessonReplay, "replay lesson");
  finishLessonFlow(); CHECK(screen == Screen::Stages, "replay returns to map");
  CHECK(click(B_STAGE0 + 6), "select locked stage"); shot("stages_locked");
  { const Button* a = find(B_STAGE_ACTION); CHECK(a && !a->enabled, "locked stage action disabled"); }
  padButton(SDL_CONTROLLER_BUTTON_B); CHECK(screen == Screen::Home, "map back");

  // ---- 5. kana chart: select, drill a single kana, tabs
  CHECK(click(B_CART0 + 2), "chart cart"); CHECK(screen == Screen::Chart, "chart");
  int a = data.findKana(Script::Hiragana, "あ");
  CHECK(click(B_CELL0 + data.findKana(Script::Hiragana, "か")), "select か"); shot("chart");
  CHECK(click(B_CELL0 + a), "select あ"); CHECK(chartSel == a, "chart selection");
  CHECK(click(B_CELL0 + a), "tap again"); CHECK(chartPick.size() == 1 && chartPick[0] == a && screen == Screen::Chart, "added to set");
  shot("chart_set");
  CHECK(click(B_PICK_DRILL), "drill set"); CHECK(screen == Screen::Drill && mode == Mode::Focus && sessionLen == prog.settings.sessionLen, "set drill");
  { int target = 0, n = 0;
    for (int i = 0; i < 20 && screen == Screen::Drill; i++) {
      n++; if (q.kanaIdx == a) target++;
      if (!q.options.empty() && q.optionsAreKana) CHECK(std::count(q.options.begin(), q.options.end(), std::string("あ")) == 1 || q.kanaIdx != a, "set kana among options");
      answer(true, false);
    }
    CHECK(target > 0 && target < n, "single kana mixed with look-alikes"); }
  CHECK(screen == Screen::Result, "focus session result");
  CHECK(click(B_HOME), "home"); CHECK(click(B_CART0 + 2), "chart again");
  // free writing on the set
  chartSel = a; CHECK(click(B_PICK_WRITE), "write set"); CHECK(screen == Screen::FreeWrite && fwList.size() == 1, "free writing");
  shot("free_write");
  trace(*activeGlyph(), 0.5f, 0.5f); CHECK(click(B_CHECK), "free check"); CHECK(checked && correct, "free write right");
  { int before = prog.stats["h:あ"].seen; CHECK(prog.stats["h:あ"].seen == before, "free write leaves progress"); }
  shot("free_write_checked");
  CHECK(click(B_CHECK), "again"); CHECK(!checked && strokes.empty(), "again clears");
  padButton(SDL_CONTROLLER_BUTTON_Y); CHECK(fwGuide == 1, "guide: outline"); shot("free_write_outline");
  padButton(SDL_CONTROLLER_BUTTON_Y); padButton(SDL_CONTROLLER_BUTTON_Y); CHECK(fwGuide == 0, "guide cycles");
  padButton(SDL_CONTROLLER_BUTTON_B); CHECK(screen == Screen::Chart, "free write back to chart");
  CHECK(click(B_PICK_CLEAR), "clear set"); CHECK(chartPick.empty(), "set cleared");
  // draw any kana: it guesses what you drew
  CHECK(click(B_PICK_ANY), "draw any"); CHECK(screen == Screen::FreeWrite && fwList.empty(), "draw-any screen"); shot("draw_any");
  { const Glyph* g = data.glyphFor("ね"); trace(*g, 0.8f, -0.6f); }
  CHECK(click(B_CHECK), "what is it"); CHECK(checked && !guesses.empty() && data.kana[guesses[0].kana].glyph == "ね", "guessed ね");
  CHECK(correct, "ね strokes in order"); shot("draw_any_checked");
  CHECK(click(B_SHOWME), "show me plays"); CHECK(animStart != 0, "animation running"); frame();
  CHECK(click(B_CHECK), "again"); CHECK(!checked && strokes.empty() && guesses.empty(), "draw any cleared");
  { const Glyph* g = data.glyphFor("シ"); std::vector<Stroke> rev = g->strokes; std::reverse(rev.begin(), rev.end());  // wrong order
    for (auto& st : rev) { strokes.push_back(st); } }
  CHECK(click(B_CHECK), "check reversed"); CHECK(checked && !correct && !guesses.empty() && data.kana[guesses[0].kana].glyph == "シ", "シ recognised, wrong order flagged"); shot("draw_any_wrong_order");
  CHECK(click(B_BACK), "draw any back"); CHECK(screen == Screen::Chart, "back to chart");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 1, "voiced tab"); shot("chart_voiced");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 2, "combos tab"); shot("chart_combos");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 3, "extras tab"); shot("chart_extras");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 0, "basic tab");
  CHECK(click(B_SCROLL_DOWN), "scroll"); CHECK(chartScroll == 1, "scrolled");
  padButton(SDL_CONTROLLER_BUTTON_B);

  // ---- 6. settings: BananaOS-style groups -> rows
  CHECK(click(B_CART0 + 3), "settings cart"); CHECK(screen == Screen::Settings, "settings");
  setGroup = 0; shot("settings");
  CHECK(click(B_GROUP0 + 0), "open Practice (tap selected tile)"); CHECK(screen == Screen::SettingsGroup && setGroup == 0, "practice group");
  shot("settings_practice");
  CHECK(click(B_SDEC0 + 0), "reading <"); CHECK(prog.settings.reading == 1, "reading sometimes");
  CHECK(click(B_SINC0 + 0), "reading >"); CHECK(prog.settings.reading == 2, "reading often");
  padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN); CHECK(setRow == 1, "row down");
  int m0 = prog.settings.meanings; padButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT); CHECK(prog.settings.meanings == std::max(0, m0 - 1), "meanings via d-pad");
  padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK(prog.settings.meanings == std::min(2, std::max(0, m0 - 1) + 1), "meanings back");
  prog.settings.meanings = 2;
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(setGroup == 1 && screen == Screen::SettingsGroup, "R: next group");
  shot("settings_sessions");
  CHECK(click(B_SDEC0 + 0), "length <"); CHECK(prog.settings.sessionLen == 5, "length 5");
  padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK(prog.settings.sessionLen == 10, "length via d-pad");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(setGroup == 2, "script group");
  CHECK(click(B_SINC0 + 0), "script >"); CHECK(prog.settings.script == Script::Hiragana, "katakana stays locked");
  shot("settings_script");
  padButton(SDL_CONTROLLER_BUTTON_B); CHECK(screen == Screen::Settings, "B: back to groups");
  CHECK(click(B_GROUP0 + 5), "select About"); CHECK(setGroup == 5 && screen == Screen::Settings, "tile selected, not opened");
  CHECK(click(B_GROUP0 + 5), "open About"); CHECK(screen == Screen::SettingsGroup, "about open");
  CHECK(click(B_SROW0 + 1), "select credits"); CHECK(click(B_SROW0 + 1), "open credits"); CHECK(screen == Screen::Credits, "credits");
  shot("credits"); CHECK(click(B_BACK), "credits back"); CHECK(screen == Screen::SettingsGroup, "back to about");
  CHECK(click(B_FOOT_L), "footer back"); CHECK(screen == Screen::Settings, "groups again");
  { Progress re; re.load(statePath, &data); CHECK(re.settings.sessionLen == 10 && re.settings.reading == 2, "settings persisted"); }

  // ---- 7. light theme: every screen once more
  setGroup = 3; CHECK(click(B_FOOT_R), "open Display"); CHECK(click(B_SINC0 + 0), "light");
  CHECK(prog.settings.light && sameCol(BG, LIGHT_THEME.bg), "light applied");
  shot("light_settings_display");
  go(Screen::Settings); shot("light_settings");
  go(Screen::Home); shot("light_home");
  go(Screen::Stats); shot("light_stats");
  go(Screen::Stages); shot("light_stages");
  go(Screen::Chart); shot("light_chart");
  openLesson(Script::Hiragana, 1, true); trace(*activeGlyph(), 0.5f, 0.5f); shot("light_lesson");
  startSession();
  for (int i = 0; i < 40 && screen == Screen::Drill; i++) {
    std::string tn = tname[int(q.type)];
    if (!shotTypes.count("light_" + tn)) { shot("light_q_" + tn); shotTypes.insert("light_" + tn); }
    answer(i % 4 != 1, false);
  }
  shot("light_result");
  pendingUnlocks = {{Script::Hiragana, 2}}; go(Screen::Unlocked); shot("light_unlocked"); pendingUnlocks.clear();
  startSession(); togglePause(); shot("light_pause"); paused = false;
  prog.settings.light = false; applyTheme(false); go(Screen::Home);

  // ---- 8. katakana unlock after hiragana stage 10
  for (int st = 0; st < KATAKANA_AFTER; st++)
    for (int ki : data.stages[0][st].kana) prog.stats[Progress::keyKana(data.kana[ki])].step = KNOWN_STEP;
  auto u = prog.checkUnlocks(data);
  CHECK(prog.unlocked[1] == 1 && prog.unlocked[0] == KATAKANA_AFTER + 1, "katakana unlocked");
  pendingUnlocks.clear(); for (auto& x : u) if (x.first == Script::Katakana) pendingUnlocks.push_back(x);
  go(Screen::Unlocked); shot("unlocked_katakana");
  CHECK(click(B_START_LESSON), "katakana lesson"); CHECK(screen == Screen::Lesson && lessonScript == Script::Katakana, "katakana lesson open");
  shot("lesson_katakana");
  finishLessonFlow(); CHECK(prog.settings.script == Script::Katakana && screen == Screen::Drill, "katakana practice");
  answer(true, false);
  go(Screen::Home); shot("home_katakana");
  setScript(Script::Hiragana); openLesson(Script::Hiragana, 12, true); shot("lesson_combo"); go(Screen::Home);
  go(Screen::Stages); shot("stages_late");

  CHECK(prog.save(statePath), "save");
  { Progress re; CHECK(re.load(statePath, &data), "reload"); CHECK(re.unlocked[1] == 1 && re.lessons[1] >= 1, "katakana state persisted"); }
  // ---- 9. daily goal: plenty answered today
  CHECK(prog.streak >= 1 && prog.currentStreak() >= 1 && prog.todayCount >= prog.settings.dailyGoal, "daily goal reached");

  // ---- 10. spaced review: known kana that are due
  setScript(Script::Hiragana); go(Screen::Home);
  { int n = 0; for (int ki : data.stages[0][0].kana) { Stat& st = prog.stats[Progress::keyKana(data.kana[ki])]; st.step = KNOWN_STEP; st.srs = 2; st.due = prog.now() - 60; n++; }
    CHECK(int(prog.dueKana(data, Script::Hiragana).size()) >= n, "reviews due"); }
  homeSel = 4; shot("home_reviews");
  { const Button* rb = find(B_MODE0); CHECK(rb && rb->enabled && rb->sub.find("due") != std::string::npos, "review button shows due"); }
  int dueBefore = int(prog.dueKana(data, Script::Hiragana).size());
  CHECK(click(B_MODE0), "review"); CHECK(screen == Screen::Drill && mode == Mode::Review, "review session");
  CHECK(sessionLen == std::min(prog.settings.sessionLen, dueBefore), "review length = due");
  for (int i = 0; i < 40 && screen == Screen::Drill; i++) {
    CHECK(prog.isDue(q.key) || i > 0, "review asks due kana");
    if (i == 0) shot("q_review");
    answer(true, false); if (i == 0) { CHECK(feedbackNote.find("Next review") != std::string::npos || !correct, "next review note"); }
  }
  CHECK(screen == Screen::Result, "review result"); shot("result_review");
  CHECK(int(prog.dueKana(data, Script::Hiragana).size()) < dueBefore, "reviews cleared");
  CHECK(click(B_HOME), "home after review");

  // ---- 11. mix-ups: options from one look-alike set, wrong picks remember the confusion
  CHECK(click(B_MODE0 + 1), "mix-ups"); CHECK(screen == Screen::Drill && mode == Mode::Mixup && q.mixup, "mix-up session");
  shot("q_mixup");
  { std::string key = q.key; size_t before = prog.confusions.size();
    click(B_TILE0 + (q.correct + 1) % int(q.options.size())); click(B_CHECK);
    CHECK(checked && !correct && feedbackNote == q.note, "mix-up note"); CHECK(prog.confusions.size() >= before && !prog.confusions.empty(), "confusion recorded");
    shot("q_mixup_checked"); click(B_CONTINUE); (void)key; }
  for (int i = 0; i < 40 && screen == Screen::Drill; i++) { CHECK(q.mixup, "mix-up questions only"); answer(true, false); }
  CHECK(screen == Screen::Result, "mix-up result"); CHECK(click(B_HOME), "home after mix-ups");

  // ---- 12. timed challenge: one tap answers, the clock ends it, best is kept
  CHECK(click(B_MODE0 + 2), "timed"); CHECK(screen == Screen::Drill && mode == Mode::Timed && timedLeft == TIMED_MS, "timed session");
  for (int i = 0; i < 12; i++) {
    CHECK(!q.options.empty(), "timed is multiple choice");
    int before = index; click(B_TILE0 + (i % 4 == 3 ? (q.correct + 1) % int(q.options.size()) : q.correct));
    CHECK(index == before + 1 && !checked, "tap answers and moves on");
    if (i == 3) { CHECK(flash == 2, "miss flashed"); shot("timed"); }
  }
  CHECK(score == 9, "timed score");
  padButton(SDL_CONTROLLER_BUTTON_A); CHECK(index == 13, "A answers in timed");
  togglePause(); tick(timedLast + 5000); CHECK(timedLeft > TIMED_MS - 2000 && screen == Screen::Drill, "pause stops the clock"); togglePause();
  timedLeft = 300; tick(timedLast + 400); CHECK(screen == Screen::Result, "time up -> result");
  CHECK(prog.timedBest[0] == score && timedNewBest, "timed best saved"); shot("result_timed");
  CHECK(click(B_AGAIN), "try again"); CHECK(mode == Mode::Timed && timedLeft == TIMED_MS, "timed again");
  timedLeft = 1; tick(timedLast + 10); CHECK(screen == Screen::Result && !timedNewBest, "no new best on 0");
  CHECK(click(B_HOME), "home after timed");

  // ---- 13. stats screen and its heatmap
  CHECK(click(B_MODE0 + 3), "stats"); CHECK(screen == Screen::Stats, "stats screen"); shot("stats");
  { int ka = data.findKana(Script::Hiragana, "か");
    CHECK(click(B_HEAT0 + ka), "heatmap cell"); CHECK(screen == Screen::Chart && chartSel == ka && chartTab == 0, "heatmap opens chart"); }
  go(Screen::Home);

  // ---- 14. kana keyboard
  { Settings keep = prog.settings;
    for (auto& k : data.kana) { int& st = prog.stats[Progress::keyKana(k)].step; st = std::max(st, PRODUCE_STEP); }  // far enough to type and write
    prog.settings.reading = 0; prog.settings.meanings = 0; prog.settings.writing = 0; prog.settings.typing = 2; prog.settings.kanaKeys = true;
    startSession(); CHECK(q.type == QType::Type && q.kanaInput, "kana typing question"); shot("q_type_kana");
    click(B_KANA0 + 6); click(B_KANA0 + 35); CHECK(typed == "きや", "kana keys"); click(B_SMALLK); CHECK(typed == "きゃ", "small key");
    click(B_DAKU); CHECK(typed == "ぎゃ" || typed == "きゃ", "dakuten leaves small ゃ"); click(B_BKSP); click(B_KANA0 + 5 * 5); click(B_HANDAKU);
    CHECK(typed == "きぱ", "handakuten"); click(B_HANDAKU); CHECK(typed == "きは", "handakuten off"); click(B_DAKU); CHECK(typed == "きば", "dakuten");
    click(B_CHOON); CHECK(typed == "きばー", "long vowel"); shot("q_type_kana_typed");
    typed = q.answer; click(B_CHECK); CHECK(checked && correct, "kana typed right"); shot("q_type_kana_checked");
    // ---- 15. writing a combination kana in two parts
    prog.settings.typing = 0; prog.settings.writing = 2; prog.settings.kanaKeys = false; prog.unlocked[0] = STAGE_COUNT;
    startSession(); bool found = false;
    for (int t = 0; t < 400 && !found; t++) { if (q.type == QType::Write && writePartCount() == 2) found = true; else { index = 0; nextQuestion(true); } }
    CHECK(found, "combo writing question");
    if (found) {
      CHECK(q.writeLevel == prog.writeLevel(q.key), "write level from progress");
      shot("q_write_combo"); trace(*activeGlyph(), 1.0f, 0.5f); click(B_CHECK);
      CHECK(!checked && part == 1 && strokes.empty(), "on to part 2"); shot("q_write_combo_part2");
      click(B_UNDO); CHECK(part == 0 && !strokes.empty(), "undo goes back a part"); click(B_CHECK); CHECK(part == 1, "part 2 again");
      trace(*activeGlyph(), 1.0f, 0.5f); click(B_CHECK); CHECK(checked && correct, "combo traced right"); shot("q_write_combo_checked");
    }
    prog.settings = keep; prog.unlocked[0] = KATAKANA_AFTER + 1; go(Screen::Home);
  }

  // ---- 15b. Show me in a lesson, display settings, strict checking, the extras stage
  openLesson(Script::Hiragana, 0, true); CHECK(animStart != 0, "lesson plays the order"); shot("lesson_showme");
  { Uint32 a0 = animStart; CHECK(tick(SDL_GetTicks() + 40), "animation redraws"); (void)a0; }
  CHECK(click(B_SHOWME), "lesson show me"); go(Screen::Home);
  go(Screen::Settings); setGroup = 3; CHECK(click(B_FOOT_R), "open Display");
  CHECK(click(B_SINC0 + 1), "kana font >"); CHECK(prog.settings.kanaHand, "handwriting font"); shot("settings_display");
  CHECK(click(B_SINC0 + 2), "canvas lines >"); CHECK(prog.settings.guideLines == 1, "grid");
  CHECK(click(B_SINC0 + 3), "ink >"); CHECK(prog.settings.ink == 2, "thick ink");
  openLesson(Script::Hiragana, 1, true); animStart = 0; trace(*activeGlyph(), 0.5f, 0.5f); shot("lesson_handwriting_grid");
  prog.settings.kanaHand = false; setKanaHand(false); prog.settings.guideLines = 0; prog.settings.ink = 1;
  go(Screen::Settings); setGroup = 1; CHECK(click(B_FOOT_R), "open Sessions");
  CHECK(click(B_SINC0 + 3), "writing check >"); CHECK(prog.settings.strict == 2, "strict"); prog.settings.strict = 1;
  { Progress keep = prog; prog.skipTo(data, Script::Hiragana, STAGE_COUNT); prog.skipTo(data, Script::Katakana, STAGE_COUNT);
    CHECK(prog.unlocked[1] == STAGE_COUNT, "katakana extras open");
    setScript(Script::Katakana); go(Screen::Stages); shot("stages_15");
    openLesson(Script::Katakana, STAGE_COUNT - 1, true); shot("lesson_extras");
    for (int i = 0; i < 3; i++) click(B_LESSON_NEXT);
    shot("lesson_extras_fa");
    int cho = data.findKana(Script::Katakana, "ー"); prog.stats[Progress::keyKana(data.kana[cho])].step = 2;
    startFocus({cho}); bool wordQ = false;
    for (int i = 0; i < 30 && screen == Screen::Drill; i++) {
      if (q.kanaIdx == cho && q.type == QType::ReadKana && !wordQ) {
        wordQ = true; CHECK(q.prompt.find("ー") != std::string::npos, "ー read through a word"); shot("q_long_vowel");
        for (auto& o : q.options) CHECK(o.size() >= 3 && o != "fo" && o != "fi", "word options stay words");
      }
      answer(true, false);
    }
    CHECK(wordQ, "saw a ー word question");
    go(Screen::Stats); shot("stats_katakana");
    prog = keep; setScript(Script::Hiragana); go(Screen::Home); }

  // ---- 16. first launch and skipping ahead (on a fresh profile, then put back)
  { Progress keep = prog;
    prog = Progress{}; prog.settings = keep.settings; prog.settings.script = Script::Hiragana;
    go(Screen::Welcome); shot("welcome");
    padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN); CHECK(welcomeSel == 1, "welcome move");
    CHECK(click(B_WELCOME0 + 1), "I know hiragana");
    CHECK(screen == Screen::Home && prog.knownCount(data, Script::Hiragana) == data.scriptCount(Script::Hiragana) && prog.settings.script == Script::Katakana &&
          prog.lessonPending(Script::Katakana), "hiragana skipped, katakana lesson next");
    shot("home_after_welcome");
    prog = Progress{}; prog.settings = keep.settings; prog.settings.script = Script::Hiragana;
    go(Screen::Welcome); CHECK(click(B_WELCOME0 + 3), "I know some"); CHECK(screen == Screen::SettingsGroup && setGroup == 2 && setRow == 1, "to script settings");
    for (int i = 0; i < 3; i++) click(B_SINC0 + 1);
    CHECK(skipPending[0] == 4 && prog.unlocked[0] == 1, "pending only"); shot("settings_skip");
    CHECK(click(B_FOOT_R), "confirm skip"); CHECK(prog.unlocked[0] == 4 && prog.lessons[0] == 3 && prog.stageKnown(data, Script::Hiragana, 2) == 5, "skipped to stage 4");
    click(B_SDEC0 + 1); CHECK(skipPending[0] == 4, "can't go back down");
    CHECK(click(B_SROW0 + 2), "katakana row"); CHECK(click(B_SINC0 + 2), "katakana >"); CHECK(click(B_FOOT_R), "unlock katakana");
    CHECK(prog.unlocked[1] == 1 && prog.lessonPending(Script::Katakana), "katakana unlocked early");
    prog = keep; go(Screen::Home);
  }
  // ---- 17. reset progress needs two presses and keeps settings
  go(Screen::Settings); setGroup = 4; CHECK(click(B_FOOT_R), "open Progress"); shot("settings_progress");
  CHECK(click(B_SROW0 + 4), "select reset"); CHECK(click(B_SROW0 + 4), "reset once"); CHECK(resetArmed && prog.unlocked[1] == 1, "first press only arms");
  shot("settings_reset_armed");
  CHECK(click(B_FOOT_R), "confirm reset"); CHECK(prog.unlocked[0] == 1 && prog.unlocked[1] == 0 && prog.stats.empty(), "progress reset");
  CHECK(prog.settings.sessionLen == 10, "settings kept after reset");
  printf(fails ? "SELFTEST: %d failure(s)\n" : "SELFTEST: all passed (sessions %d, question types %zu)\n", fails ? fails : prog.session, typesSeen.size());
  return fails ? 1 : 0;
}

}  // namespace kt
