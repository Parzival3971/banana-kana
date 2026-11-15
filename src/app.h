// Banana Kana screens, navigation and input handling.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "core.h"
#include "gfx.h"

namespace kt {

enum class Screen { Home, Stages, Lesson, Unlocked, Drill, Result, Chart, Settings, SettingsGroup, Credits, Stats, Welcome, FreeWrite, Packs, PackWords, Awards };

// What a practice session draws from.
enum class Mode { Normal, Focus, Review, Mixup, Timed, Pack };
constexpr int TIMED_MS = 60000;

enum BtnId {
  B_NONE = 0,
  // home
  B_CART0 = 1, B_SCRIPT_H = 10, B_SCRIPT_K, B_MODE0 = 120,  // B_MODE0..+4: Review, Mix-ups, Timed, Stats, Words (drawn Review, Mix-ups, Words, Timed, Stats)
  // shared
  B_BACK = 20, B_HOME, B_QUIT,
  // drill
  B_TILE0 = 30, B_CHECK = 40, B_CONTINUE, B_UNDO, B_CLEAR, B_HINT, B_BKSP,
  // pause
  B_RESUME = 50, B_MENU, B_PAUSE = 58,
  // results / unlock
  B_AGAIN = 60, B_LATER, B_START_LESSON,
  // stages
  B_STAGE0 = 70, B_STAGE_ACTION = 90, B_HOME_GO = 91,
  // lesson
  B_LESSON_NEXT = 95, B_LESSON_PREV, B_LESSON_CLEAR,
  // chart
  B_TAB0 = 100, B_SCROLL_UP = 110, B_SCROLL_DOWN, B_PICK_DRILL, B_PICK_WRITE, B_PICK_CLEAR, B_PICK_ANY, B_CELL0 = 1000,
  // free writing, welcome
  B_FW_GUIDE = 130, B_FW_NEXT, B_FW_PREV, B_SHOWME, B_WELCOME0 = 140,
  // settings (BananaOS style: group tiles -> rows with < value >)
  B_GROUP0 = 200, B_SROW0 = 220, B_SDEC0 = 240, B_SINC0 = 260, B_FOOT_L = 280, B_FOOT_R, B_SSEG0 = 3000,  // B_SSEG0 + row * 8 + option
  // keyboard
  B_KEY0 = 300, B_KANA0 = 400, B_DAKU = 460, B_HANDAKU, B_SMALLK, B_CHOON,
  // stats heatmap
  B_HEAT0 = 2000, B_PACK0 = 2600, B_PACK_START = 2650, B_PACK_BROWSE, B_WORD0 = 2700, B_WPREV = 2720, B_WNEXT, B_AWARDS = 2730, B_AWARD0 = 2740,
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
  // achievements: earned ones pop up one at a time on the top screen
  std::vector<int> toastQ; Uint32 toastUntil = 0; int toastAch = -1, awardSel = 0;
  void earned(const std::vector<int>& newly);
  // Home: a scrolling strip of mode tiles (homeSel = the centred one) over a big "Lesson / Practise" pill
  struct HomeItem { int id; std::string name, icon, sub; ModeCol col; bool enabled; };
  std::vector<HomeItem> homeItems() const;
  int homeItemIndex(int id) const;  // -1 if id isn't a Home tile
  int packSel = 0, packIdx = -1; PackSession packSession; int wordSel = 0;  // wordSel: highlighted word in the pack word list  // word packs: highlighted tile, pack being practised
  void startPack(int pack);
  int homeSel = 0, mapSel = -1, chartTab = 0, chartSel = -1, chartScroll = 0;
  int setGroup = 0, setRow = 0; bool resetArmed = false, resetSetArmed = false; std::string version = "dev";
  Script mapScript = Script::Hiragana;
  // lesson
  Script lessonScript = Script::Hiragana; int lessonStage = 0, lessonIdx = 0; bool lessonReplay = false;
  // session
  Question q; int index = 0, score = 0, sel = -1, cursor = 0, sessionLen = 10, focusKana = -1;
  bool checked = false, correct = false, showHint = false, drawing = false;
  std::string typed, lastKey; std::vector<Stroke> strokes; Stroke cur; std::vector<int> strokeOk, results;
  std::vector<Missed> missed; std::vector<std::string> newlyKnown; std::string feedbackNote;
  std::vector<std::pair<Script, int>> pendingUnlocks;
  Mode mode = Mode::Normal; bool goalMet = false;
  std::vector<int> chartPick;  // practice set picked in the chart (drill it, or write it freely)
  std::vector<int> focusSet;   // what a Focus session draws from
  // free writing
  std::vector<int> fwList; int fwIdx = 0, fwGuide = 0;  // fwGuide 0 full, 1 outline, 2 none; empty list = draw any kana
  std::vector<Guess> guesses;  // draw-any: what it looks like, best first
  // first launch, settings skip-ahead
  Uint32 flameLast = 0;  // Home streak flame flicker
  // motion (0.5.0 polish): screen fade-in, Home strip glide, answer pop/shake. KT_MOTION=0 or the self-test turn it off.
  bool motion = true; Uint32 countStart = 0; Uint32 transStart = 0, answerAnim = 0, homeAnimLast = 0; float homePos = 0.f;
  static constexpr Uint32 TRANS_MS = 180, ANSWER_MS = 420;
  Uint32 creditsLast = 0, animStart = 0, animLast = 0;  // stroke-order animation (0 = not playing)
  void playStrokes() { animStart = SDL_GetTicks(); if (!animStart) animStart = 1; }
  std::string osdWhat; int osdPct = 0; Uint32 osdUntil = 0;  // volume / brightness level card (from BananaOS)
  void showOsd(const std::string& what, int pct) { osdWhat = what; osdPct = pct; osdUntil = SDL_GetTicks() + 1800; if (!osdUntil) osdUntil = 1; }
  void drawOsd();
  Uint32 confStart = 0, confDur = 0, confLast = 0; bool confBoth = false;  // confetti celebration
  void celebrate(Uint32 ms, bool both); void drawConfetti();
  int keyRow = 0, keyCol = 0;  // d-pad cursor on the on-screen keyboards
  int kbRows() const; int kbRowLen(int r) const; int kbItemId(int r, int c) const; void kbMove(int dc, int dr);
  bool inPractice() const { return screen == Screen::Drill || screen == Screen::Lesson || screen == Screen::FreeWrite; }
  bool firstRun = false; int welcomeSel = 0; int skipPending[2] = {0, 0};
  // timed challenge
  int timedLeft = 0; Uint32 timedLast = 0; bool timedNewBest = false; int flash = 0; std::string flashText;  // flash 1 right, 2 wrong
  // writing in parts (combination kana): finished parts and their per-stroke results
  int part = 0; std::vector<std::vector<Stroke>> partStrokes; std::vector<std::vector<int>> partOk;

  // flow
  void go(Screen s);
  void startSession(int focus = -1);  // focus >= 0: practise that kana (with its look-alikes)
  void startFocus(const std::vector<int>& set);
  void openFreeWrite(const std::vector<int>& list);
  bool scoreWriting(const std::string& glyph, bool* countsMatch, size_t* total);  // partStrokes + strokes vs the glyph
  void welcomeChoice(int i);
  int skipCurrent(Script s) const;
  void confirmSetting(int g, int r);
  void startMode(Mode m);
  bool modeAvailable(Mode m) const;
  bool tick(Uint32 nowMs);  // timers; true if the screen should be redrawn
  void nextQuestion(bool first = false);
  void finishSession();
  void check();
  void openLesson(Script s, int stage, bool replay);
  void finishLesson();
  void togglePause();
  void setScript(Script s);
  // Progress lives in ~/.config/banana-kana (hidden). A copy goes next to the app on the SD card
  // (roms/ports/BananaKana/progress-backup.txt) at most once a minute, after sessions, unlocks and on exit.
  std::string backupPath; unsigned lastBackup = 0;
  void backup(bool force);
  void save() { prog.save(statePath); backup(false); }

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
  // Header bar across the top screen: bold title on the left, small text (or the wordmark) on the right;
  // coloured = filled with the mode colour (drills).
  void appTile(Win& w, int x, int y, int size);            // the app icon: glossy orange tile with あ and a little ア
  void medal(Win& w, SDL_Rect box, bool earned);              // achievement medal: ribbon + gold disc (grey when not earned)
  void header(const std::string& title, const std::string& right = "", bool coloured = false, int indent = 0);
  void chips(const std::vector<std::pair<std::string, std::string>>& items);
  void segBar(Win& w, SDL_Rect r, int total, int done, const std::vector<int>& res);
  void settingPreview(int row);
  void statBox(Win& w, SDL_Rect r, const std::string& label, const std::string& value, int font = F_SMALL);
  void bar(Win& w, SDL_Rect r, float frac);
  void stepPips(Win& w, int x, int y, int step, int size = 12);
  SDL_Rect canvasRect() const;
  const Glyph* activeGlyph() const;
  int writePartCount() const;
  int currentLessonKana() const;

  void drawHome(); void drawStages(); void drawLesson(); void drawUnlocked(); void drawDrill(); void drawResult();
  void drawChart(); void drawSettings(); void drawSettingsGroup(); void drawCredits(); void drawPause(); void drawStats(); void drawWelcome(); void drawFreeWrite(); void drawPacks(); void drawPackWords(); void drawAwards(); void drawToast();
  void drawKanaKeyboard();
  // settings model
  struct SRow { std::string label, value, help; int kind = 0; bool canDec = false, canInc = false;
                std::vector<std::string> opts; int cur = 0; };  // opts: shown as a segmented pill (tap one)  // kind 0 choice, 1 action, 2 info, 3 skip-ahead (pick then confirm)
  static const int GROUPS = 6;
  std::vector<SRow> settingRows(int g) const;
  std::string groupName(int g) const; std::string groupSummary(int g) const;
  void changeSetting(int g, int r, int dir);  // dir +1/-1 for choices; any for actions
  void settingIcon(Win& w, int g, int x, int y, int px, Col a, Col b);
  void settingsPanel(int g, const std::string& label, const std::string& title, const std::string& accentLine, const std::string& body);
  void footer(const std::string& lk, const std::string& ll, const std::string& rk, const std::string& rl);
  void drawCanvas(SDL_Rect c, const Glyph* g, int guide, bool checkedState);  // guide 0 none, 1 faint outline, 2 full

  // chart helpers
  struct Cell { int kana, row, col; };
  std::vector<Cell> chartCells(Script s, int tab) const;
  int chartCols(int tab) const { return tab == 2 ? 3 : 5; }
  static const int CHART_TABS = 4;

  // self-test (scripted UI run with dummy video)
  int selfTest(const std::string& shotDir);
};

}  // namespace kt
