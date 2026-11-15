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

struct Kana { Script script; std::string glyph, romaji, group; };
struct Word { std::string kana, romaji, english; bool katakana; };
struct Pt { float x, y; };
using Stroke = std::vector<Pt>;
struct Glyph { std::vector<Stroke> strokes; };  // coordinates in KanjiVG 109x109 space

// ---------------------------------------------------------------- stages
// 14 stages per script: 10 basic rows, が-ざ-だ, ば-ぱ, then two groups of combination kana (yōon).
constexpr int STAGE_COUNT = 14;
constexpr int KATAKANA_AFTER = 10;     // katakana unlocks once hiragana stage 10 is passed
constexpr int KNOWN_STEP = 4;          // a kana is "known" at step 4
constexpr int MAX_GAIN_PER_SESSION = 2;  // right answers count for at most 2 steps per session

struct Stage { std::string name; std::vector<int> kana; };  // indices into Data::kana

std::vector<uint32_t> utf8Codepoints(const std::string& s);

struct Data {
  std::vector<Kana> kana;
  std::vector<Word> words;
  std::map<uint32_t, Glyph> glyphs;
  std::map<std::string, std::string> hints;  // kana glyph -> lesson hint
  std::array<std::vector<Stage>, 2> stages;  // [script]
  std::vector<int> stageOfKana;              // kana index -> stage index
  bool load(const std::string& dir, std::string* err = nullptr);
  const Glyph* glyphFor(const std::string& kanaGlyph) const;  // single-codepoint kana only
  int findKana(Script s, const std::string& glyph) const;
};

// ---------------------------------------------------------------- progress
struct Stat { int seen = 0, correct = 0, wrong = 0, streak = 0, step = 0, gainSession = -1, gain = 0; };

struct Settings {
  int review = 1;  // older stages in practice: 0 off (weak ones only), 1 some, 2 lots
  int reading = 2, meanings = 1, writing = 1, typing = 1;  // drill weights 0..2
  int sessionLen = 10;
  Script script = Script::Hiragana;
  bool light = false;  // UI theme
};

enum class KanaState { Locked, New, Learning, Known, Weak };

struct Progress {
  std::map<std::string, Stat> stats;
  Settings settings;
  int session = 0;                       // incremented at the start of every practice session
  std::array<int, 2> unlocked{{1, 0}};   // stages unlocked per script (katakana starts at 0 = locked)
  std::array<int, 2> lessons{{0, 0}};    // stage lessons completed per script
  bool migrated = false;                 // set when an older progress file was upgraded on load

  static std::string keyKana(const Kana& k);
  static std::string keyWord(const Word& w);
  void record(const std::string& key, bool correct);
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
  int kanaIdx = -1;                // for kana questions
};

std::string normalizeRomaji(std::string s);
bool typedMatches(const std::string& typed, const std::string& expectedRomaji);

std::vector<int> unlockedKana(const Data& d, const Progress& p, Script s);
std::vector<const Word*> eligibleWords(const Data& d, const Progress& p, Script s);
Question makeQuestion(const Data& d, const Progress& p, std::mt19937& rng);
Question makeKanaQuestion(const Data& d, const Progress& p, std::mt19937& rng, int kanaIdx, QType t);
Question makeQuestionOfType(const Data& d, const Progress& p, std::mt19937& rng, QType t);

// ---------------------------------------------------------------- strokes
Stroke resample(const Stroke& s, int n);
float strokeLength(const Stroke& s);
struct StrokeResult { bool ok; float meanDist; };
StrokeResult scoreStroke(const Stroke& user, const Stroke& ref, float tol = 17.f);

}  // namespace kt
