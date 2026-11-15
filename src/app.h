// Banana Kana screens, navigation and input handling.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "core.h"
#include "gfx.h"

namespace kt {

enum class Screen { Home, Stages, Lesson, Unlocked, Drill, Result, Chart, Settings, Credits };

enum BtnId {
  B_NONE = 0,
  // home
  B_CART0 = 1, B_SCRIPT_H = 10, B_SCRIPT_K,
  // shared
  B_BACK = 20, B_HOME, B_QUIT,
  // drill
  B_TILE0 = 30, B_CHECK = 40, B_CONTINUE, B_UNDO, B_CLEAR, B_HINT, B_BKSP,
  // pause
  B_RESUME = 50, B_MENU,
  // results / unlock
  B_AGAIN = 60, B_LATER, B_START_LESSON,
  // stages
  B_STAGE0 = 70, B_STAGE_ACTION = 90,
  // lesson
  B_LESSON_NEXT = 95, B_LESSON_PREV, B_LESSON_CLEAR,
  // chart
  B_TAB0 = 100, B_SCROLL_UP = 110, B_SCROLL_DOWN, B_CELL0 = 1000,
  // settings
  B_ROW0 = 200, B_OPT0 = 220, B_CREDITS = 240,
  // keyboard
  B_KEY0 = 300,
};

struct Button {
  SDL_Rect r{}; int id = 0; std::string label, sub;
  int font = F_SMALL, subFont = F_MICRO;
  Col fill, line, text, subText;
  bool enabled = true; int radius = 12; int lineW = 3;
  int icon = 0;  // 1 lock, 2 check
  bool hidden = false;  // hit area only; the screen draws it itself
};

struct Missed { std::string prompt, answer, said; };

struct App {
  Win top, bot; Data data; Progress prog; std::string statePath, dataDir;
  std::mt19937 rng{std::random_device{}()};
  Screen screen = Screen::Home; bool running = true, paused = false; int pauseSel = 0;
  std::vector<Button> buttons; int pressedId = -1;

  // home / stage map / settings / chart selections
  int homeSel = 0, mapSel = -1, setSel = 0, chartTab = 0, chartSel = -1, chartScroll = 0;
  Script mapScript = Script::Hiragana;
  // lesson
  Script lessonScript = Script::Hiragana; int lessonStage = 0, lessonIdx = 0; bool lessonReplay = false;
  // session
  Question q; int index = 0, score = 0, sel = -1, cursor = 0, sessionLen = 10, focusKana = -1;
  bool checked = false, correct = false, showHint = false, drawing = false;
  std::string typed, lastKey; std::vector<Stroke> strokes; Stroke cur; std::vector<int> strokeOk, results;
  std::vector<Missed> missed; std::vector<std::string> newlyKnown; std::string feedbackNote;
  std::vector<std::pair<Script, int>> pendingUnlocks;

  // flow
  void go(Screen s);
  void startSession(int focus = -1);
  void nextQuestion(bool first = false);
  void finishSession();
  void check();
  void openLesson(Script s, int stage, bool replay);
  void finishLesson();
  void togglePause();
  void setScript(Script s);
  void save() { prog.save(statePath); }

  // input
  void press(int id);
  void pointer(int type, int x, int y);  // 0 down, 1 move, 2 up (bottom-screen logical coords)
  void padButton(SDL_GameControllerButton b);
  void key(SDL_Keycode k);

  // drawing
  void render();
  void addBtn(SDL_Rect r, int id, const std::string& label, int font = F_SMALL, bool enabled = true);
  Button& lastBtn() { return buttons.back(); }
  void drawButtons();
  void header(const std::string& right);
  void chips(const std::vector<std::pair<std::string, std::string>>& items);
  void segBar(Win& w, SDL_Rect r, int total, int done, const std::vector<int>& res);
  void statBox(Win& w, SDL_Rect r, const std::string& label, const std::string& value, int font = F_SMALL);
  void bar(Win& w, SDL_Rect r, float frac);
  void stepPips(Win& w, int x, int y, int step, int size = 12);
  SDL_Rect canvasRect() const;
  const Glyph* activeGlyph() const;
  int currentLessonKana() const;

  void drawHome(); void drawStages(); void drawLesson(); void drawUnlocked(); void drawDrill(); void drawResult();
  void drawChart(); void drawSettings(); void drawCredits(); void drawPause();
  void drawCanvas(SDL_Rect c, const Glyph* g, bool guide, bool checkedState);

  // chart helpers
  struct Cell { int kana, row, col; };
  std::vector<Cell> chartCells(Script s, int tab) const;
  int chartCols(int tab) const { return tab == 2 ? 3 : 5; }

  // self-test (scripted UI run with dummy video)
  int selfTest(const std::string& shotDir);
};

}  // namespace kt
