// Scripted UI run with dummy video: drives the real screens with synthetic touches and buttons,
// checks the flows, and saves screenshots of every screen (both themes) when --shots is given.
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
    } else if (q.type == QType::Type) {
      for (char ch : (right ? q.answer : std::string("zzq"))) click(B_KEY0 + (ch - 'a'));
      click(B_CHECK);
    } else {
      const Glyph* g = activeGlyph(); CHECK(g != nullptr, "write glyph");
      if (right) trace(*g, 1.5f, -1.0f); else { SDL_Rect c = canvasRect(); pointer(0, c.x + 10, c.y + 10); pointer(1, c.x + 60, c.y + 12); pointer(2, c.x + 60, c.y + 12); }
      click(B_CHECK);
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
  CHECK(click(B_CELL0 + a), "tap again"); CHECK(screen == Screen::Drill && focusKana == a && sessionLen == 5, "focused drill");
  for (int i = 0; i < 10 && screen == Screen::Drill; i++) { CHECK(q.kanaIdx == a, "focus kana only"); answer(true, false); }
  CHECK(screen == Screen::Result, "focus session result");
  CHECK(click(B_HOME), "home"); CHECK(click(B_CART0 + 2), "chart again");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 1, "voiced tab"); shot("chart_voiced");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 2, "combos tab"); shot("chart_combos");
  padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); CHECK(chartTab == 0, "basic tab");
  CHECK(click(B_SCROLL_DOWN), "scroll"); CHECK(chartScroll == 1, "scrolled");
  padButton(SDL_CONTROLLER_BUTTON_B);

  // ---- 6. settings
  CHECK(click(B_CART0 + 3), "settings cart"); CHECK(screen == Screen::Settings, "settings");
  shot("settings");
  CHECK(click(B_ROW0 + 2), "reading row"); CHECK(click(B_OPT0 + 0), "reading off"); CHECK(prog.settings.reading == 0, "reading off saved");
  CHECK(click(B_OPT0 + 2), "reading often");
  CHECK(click(B_ROW0 + 6), "length row"); CHECK(click(B_OPT0 + 0), "length 5"); CHECK(prog.settings.sessionLen == 5, "length 5");
  padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); CHECK(prog.settings.sessionLen == 10, "length via d-pad");
  CHECK(click(B_ROW0 + 0), "script row"); { const Button* k = find(B_OPT0 + 1); CHECK(k && !k->enabled, "katakana option locked"); }
  CHECK(click(B_ROW0 + 8), "credits row"); CHECK(click(B_CREDITS), "open credits"); CHECK(screen == Screen::Credits, "credits");
  shot("credits"); CHECK(click(B_BACK), "credits back");
  { Progress re; re.load(statePath, &data); CHECK(re.settings.sessionLen == 10 && re.settings.reading == 2, "settings persisted"); }

  // ---- 7. light theme: every screen once more
  CHECK(click(B_ROW0 + 7), "theme row"); CHECK(click(B_OPT0 + 1), "light"); CHECK(prog.settings.light && sameCol(BG, LIGHT_THEME.bg), "light applied");
  shot("light_settings");
  go(Screen::Home); shot("light_home");
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
  printf(fails ? "SELFTEST: %d failure(s)\n" : "SELFTEST: all passed (sessions %d, question types %zu)\n", fails ? fails : prog.session, typesSeen.size());
  return fails ? 1 : 0;
}

}  // namespace kt
