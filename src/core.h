// Banana Kana core: data, stages, progress, question generation, stroke scoring. No SDL here.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace kt {

enum class Script { Hiragana, Katakana };
inline const char* scriptName(Script s) { return s == Script::Hiragana ? "Hiragana" : "Katakana"; }
inline int si(Script s) { return s == Script::Hiragana ? 0 : 1; }

struct Kana { Script script; std::string glyph, romaji, group; bool special = false; };  // special: っ ッ ー (read through words)
struct Word { std::string kana, romaji, english; bool katakana; std::string pack; };
struct Pack { std::string id, jp, name, desc; std::vector<int> words; };  // topic word pack (packs.tsv)
struct Pt { float x, y; };
using Stroke = std::vector<Pt>;
struct Glyph { std::vector<Stroke> strokes; };  // coordinates in KanjiVG 109x109 space

// ---------------------------------------------------------------- stages
// 14 stages per script: 10 basic rows, が-ざ-だ, ば-ぱ, then two groups of combination kana (yōon).
constexpr int STAGE_COUNT = 15;  // 14 + Extras (small tsu, long vowel, katakana for foreign sounds)
constexpr int KATAKANA_AFTER = 10;     // katakana unlocks once hiragana stage 10 is passed
constexpr int KNOWN_STEP = 4;          // a kana is "known" at step 4
constexpr int SKIP_ALL = STAGE_COUNT + 1;  // skipTo(): every stage known
// Staging: a brand-new kana is only asked as multiple choice. Romaji typing from step 1,
// kana keyboard and writing (guided first) from step 2.
constexpr int TYPE_STEP = 1, PRODUCE_STEP = 2, WRITE_MEMORY = 2;
constexpr int MAX_GAIN_PER_SESSION = 2;  // right answers count for at most 2 steps per session

struct Stage { std::string name; std::vector<int> kana; };  // indices into Data::kana
struct MixSet { std::vector<int> kana; std::string note; };  // look-alike kana and how to tell them apart

// ---------------------------------------------------------------- time
using Time = long long;  // unix seconds
constexpr int SRS_LEVELS = 6;
extern const int SRS_DAYS[SRS_LEVELS];  // review intervals for known kana: 1, 3, 7, 14, 30, 90 days
int dayNumber(Time t);                  // local calendar day as YYYYMMDD

std::vector<uint32_t> utf8Codepoints(const std::string& s);

struct Data {
  std::vector<Kana> kana;
  std::vector<Word> words;
  std::map<uint32_t, Glyph> glyphs;
  std::map<std::string, std::string> hints;  // kana glyph -> lesson hint
  std::array<std::vector<Stage>, 2> stages;  // [script]
  std::vector<int> stageOfKana;              // kana index -> stage index
  std::vector<MixSet> mixSets;               // built-in look-alike sets (both scripts)
  std::vector<Pack> packs;                   // topic word packs, in packs.tsv order
  bool load(const std::string& dir, std::string* err = nullptr);
  const Glyph* glyphFor(const std::string& kanaGlyph) const;  // single-codepoint kana only
  int scriptCount(Script s) const;  // kana in a script (105 hiragana, 119 katakana)
  // Glyphs to trace for a kana, one per character (combos: き + ゃ, small kana traced full size). Empty if any is missing.
  std::vector<const Glyph*> writeParts(const std::string& kanaGlyph) const;
  static std::vector<std::string> partNames(const std::string& kanaGlyph);  // the characters, small ones as written
  int findKana(Script s, const std::string& glyph) const;
};

// ---------------------------------------------------------------- progress
struct Stat {
  int seen = 0, correct = 0, wrong = 0, streak = 0, step = 0, gainSession = -1, gain = 0;
  int srs = 0; Time due = 0;  // spaced review once known: level 1..SRS_LEVELS and when it is next due
  int wlv = 0, wlvSession = -1;  // writing level: 0 full guide, 1 faint outline, 2 from memory (one level per session)
};

struct Settings {
  int review = 1;  // older stages in practice: 0 off (weak ones only), 1 some, 2 lots
  int reading = 2, meanings = 1, writing = 1, typing = 1;  // drill weights 0..2
  int sessionLen = 10;
  int dailyGoal = 10;  // answers per day for the streak
  bool kanaKeys = false;  // Typing answers with a kana keyboard (see romaji, type kana) instead of romaji
  bool kanaHand = false;  // kana drawn in a handwriting (textbook) font instead of rounded print
  int guideLines = 0;     // writing canvas: 0 cross, 1 grid, 2 none
  int ink = 1;            // 0 thin, 1 normal, 2 thick
  int strict = 1;         // stroke checking: 0 relaxed, 1 normal, 2 strict
  int sound = 2;          // sound effects: 0 off, 1 quiet, 2 normal, 3 loud
  int rumble = 1;         // gamepad rumble on answers: 0 off, 1 light, 2 strong
  int backdrop = 1;       // background: 0 plain, 1 seasons (follows the calendar; the default since 0.6.0), 2-5 spring/summer/autumn/winter
  Script script = Script::Hiragana;
  bool light = true;   // UI theme: the Handheld design in light (default) or dark
};

enum class KanaState { Locked, New, Learning, Known, Weak };

struct Progress {
  std::map<std::string, Stat> stats;
  Settings settings;
  int session = 0;                       // incremented at the start of every practice session
  int lastScore = -1, lastLen = 0;       // most recent finished session
  // daily goal and streak
  int today = 0, todayCount = 0, streak = 0, bestStreak = 0, lastGoalDay = 0;
  bool goalJustMet = false;              // set by record() when today's goal is reached (not saved)
  // mix-ups: how often two kana were confused ("h:さ|h:き" -> count)
  std::map<std::string, int> confusions;
  struct Day { int answers = 0, correct = 0; };
  std::map<int, Day> days;               // answers per calendar day (YYYYMMDD), last 90 days kept
  int timedBest[2] = {0, 0};             // best 60-second challenge score per script
  std::map<std::string, int> achieved;   // achievement id -> day earned (YYYYMMDD)
  Time clock = 0;                        // 0 = real time (tests set a fake clock)
  Time now() const;
  std::array<int, 2> unlocked{{1, 0}};   // stages unlocked per script (katakana starts at 0 = locked)
  std::array<int, 2> lessons{{0, 0}};    // stage lessons completed per script
  bool migrated = false;                 // set when an older progress file was upgraded on load

  static std::string keyKana(const Kana& k);
  static std::string keyWord(const Word& w);
  void record(const std::string& key, bool correct);  // steps, spaced review, daily goal
  // Words are met before they're tested: a word is "introduced" once it has a stat entry (a new-word card, or any
  // question that showed its meaning). Meaning questions only ask about introduced words.
  void introduce(const std::string& key) { stats[key]; }
  bool introduced(const std::string& key) const { return stats.count(key) > 0; }
  void countAnswer(bool correct, Time t);
  void recordWrite(const std::string& key, bool correct, int level);  // guided writes (level < 2) don't move steps
  int writeLevel(const std::string& key) const;
  void skipTo(const Data& d, Script s, int stage);  // stages before `stage` (1-14, SKIP_ALL = all) become known, review soon
  bool isDue(const std::string& key) const;
  std::vector<int> dueKana(const Data& d, Script s) const;
  std::vector<int> dueWords(const Data& d) const;           // word indices whose spaced review is due (either script)
  // Achievements: checkAchievements() earns every one whose goal is met and returns the newly earned indices;
  // award() earns an event achievement (a perfect session). Both save the day it was earned.
  std::vector<int> checkAchievements(const Data& d);
  bool award(const std::string& id);
  int dueCount(const Data& d, Script s) const { return int(dueKana(d, s).size() + dueWords(d).size()); }
  void addConfusion(const std::string& a, const std::string& b);
  int currentStreak() const;  // 0 once a day without reaching the goal has passed
  int step(const std::string& key) const;
  KanaState state(const Data& d, int kanaIdx) const;
  bool isUnlocked(const Data& d, int kanaIdx) const;
  bool scriptUnlocked(Script s) const { return unlocked[si(s)] > 0; }
  int currentStage(Script s) const { return std::max(0, unlocked[si(s)] - 1); }
  bool lessonPending(Script s) const { return scriptUnlocked(s) && lessons[si(s)] < unlocked[si(s)]; }
  int knownCount(const Data& d, Script s) const;
  int stageKnown(const Data& d, Script s, int stage) const;
  // Unlocks every stage whose predecessor is fully known; returns the newly unlocked stages as (script, stage).
  std::vector<std::pair<Script, int>> checkUnlocks(const Data& d);
  std::vector<int> weakest(const Data& d, Script s, int n) const;  // unlocked kana with the worst record
  bool load(const std::string& path, const Data* d = nullptr);
  bool save(const std::string& path) const;
};

// ---------------------------------------------------------------- questions
enum class QType { ReadKana, RecallKana, Meaning, Write, Type };

struct Question {
  QType type = QType::ReadKana;
  std::string key;
  std::string prompt, hint;
  std::vector<std::string> options;
  int correct = -1;
  std::string answer;              // expected romaji (Type), kana glyph (Write), option text otherwise
  bool isWord = false;
  bool optionsAreKana = false;
  bool kanaInput = false;  // Type question answered on the kana keyboard
  int writeLevel = WRITE_MEMORY;  // Write: 0 full guide, 1 faint outline, 2 from memory
  std::string meaning;  // the English meaning, shown under word prompts (empty when it would give the answer away)
  bool mixup = false; std::string note;  // mix-up questions carry the set's difference note
  bool intro = false;              // a new-word card: shown, not answered (prompt = kana, answer = romaji, meaning)
  bool match = false;              // RecallKana shown with the twin from the other script (あ -> ア)
  int kanaIdx = -1;                // for kana questions
};

std::string normalizeRomaji(std::string s);
bool typedMatches(const std::string& typed, const std::string& expectedRomaji);

std::vector<int> unlockedKana(const Data& d, const Progress& p, Script s);
std::vector<const Word*> eligibleWords(const Data& d, const Progress& p, Script s);
Question makeQuestion(const Data& d, const Progress& p, std::mt19937& rng);
Question makeKanaQuestion(const Data& d, const Progress& p, std::mt19937& rng, int kanaIdx, QType t);
Question makeQuestionOfType(const Data& d, const Progress& p, std::mt19937& rng, QType t);
bool typeAllowed(const Data& d, const Progress& p, int kanaIdx, QType t);  // the staging above
int twinKana(const Data& d, const Progress& p, int kanaIdx);  // same sound in the other script, unlocked; -1 if none
Question makeMatchQuestion(const Data& d, const Progress& p, std::mt19937& rng, int kanaIdx);
// ---- achievements
struct Achievement { std::string id, title, desc; };
const std::vector<Achievement>& achievementList();
struct AchProgress { int cur = 0, target = 1; };
AchProgress achievementProgress(const Data& d, const Progress& p, int idx);

// ---- word packs: a word is ready once every kana in it is unlocked (in its own script)
constexpr int PACK_MIN = 4;  // ready words a pack needs before it can be practised
bool wordReady(const Data& d, const Progress& p, const Word& w);
std::vector<int> readyWords(const Data& d, const Progress& p, int pack);
struct PackInfo { int total = 0, ready = 0, known = 0; };
PackInfo packInfo(const Data& d, const Progress& p, int pack);
// A pack session introduces at most PACK_NEW_PER_SESSION new words, each on a card, and quizzes every new word again
// a couple of questions later (while it's fresh). Meaning questions only ever ask about introduced words.
constexpr int PACK_NEW_PER_SESSION = 5;
struct PackSession { int intros = 0, sinceIntro = 0, left = 99; std::vector<std::string> fresh; };  // left: questions left (set by the caller)
Question makePackQuestion(const Data& d, const Progress& p, std::mt19937& rng, int pack, PackSession& ps);
Question makeReviewQuestion(const Data& d, const Progress& p, std::mt19937& rng);  // due kana only
std::vector<MixSet> mixupSets(const Data& d, const Progress& p, Script s);       // built-in + learned, unlocked only
Question makeMixupQuestion(const Data& d, const Progress& p, std::mt19937& rng);
Question makeTimedQuestion(const Data& d, const Progress& p, std::mt19937& rng);  // multiple choice only
bool kanaTypedMatches(const Data& d, const Question& q, const std::string& typed);  // じ/ぢ etc. both accepted for "ji"
int dayDiff(int dayA, int dayB);  // calendar days from A to B (YYYYMMDD)

// ---------------------------------------------------------------- strokes
Stroke resample(const Stroke& s, int n);
float strokeLength(const Stroke& s);
struct StrokeResult { bool ok; float meanDist; };
StrokeResult scoreStroke(const Stroke& user, const Stroke& ref, float tol = 17.f, bool strictLen = false);
struct Guess { int kana; float score; };  // lower score = closer
// What a freely drawn kana most likely is: compares the strokes (in order) with every single-part kana's stroke data,
// after fitting both to the same box. Best first.
std::vector<Guess> recognizeKana(const Data& d, const std::vector<Stroke>& strokes, int topN = 3);
std::vector<Stroke> fitStrokes(const std::vector<Stroke>& user, const Glyph& g);  // user strokes moved/scaled onto the glyph's box
StrokeResult scoreStrokeAt(const Stroke& user, const Stroke& ref, int strictness);  // 0 relaxed, 1 normal, 2 strict

}  // namespace kt
