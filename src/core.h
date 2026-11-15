// banana-kana core: data, progress, question generation, stroke scoring. No SDL here.
#pragma once
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace kt {

enum class Script { Hiragana, Katakana };
inline const char* scriptName(Script s) { return s == Script::Hiragana ? "Hiragana" : "Katakana"; }

struct Kana { Script script; std::string glyph, romaji, group; };
struct Word { std::string kana, romaji, english; bool katakana; };
struct Pt { float x, y; };
using Stroke = std::vector<Pt>;
struct Glyph { std::vector<Stroke> strokes; };  // coordinates in KanjiVG 109x109 space

// UTF-8 helpers
std::vector<uint32_t> utf8Codepoints(const std::string& s);

struct Data {
  std::vector<Kana> kana;
  std::vector<Word> words;
  std::map<uint32_t, Glyph> glyphs;
  bool load(const std::string& dir, std::string* err = nullptr);
  const Glyph* glyphFor(const std::string& kanaGlyph) const;  // single-codepoint kana only
};

// ---------------------------------------------------------------- progress
struct Stat { int seen = 0, correct = 0, wrong = 0, streak = 0; };

struct Settings {
  int kanaPct = 60;  // % kana questions vs word questions
  int reading = 2, meanings = 1, writing = 1, typing = 1;  // weight 0..2
  Script script = Script::Hiragana;
  bool light = false;  // UI theme: false = dark, true = light
};

struct Progress {
  std::map<std::string, Stat> stats;
  Settings settings;
  static std::string keyKana(const Kana& k);
  static std::string keyWord(const Word& w);
  void record(const std::string& key, bool correct);
  int learned(const Data& d, Script s) const;  // kana with streak >= 3
  bool load(const std::string& path);
  bool save(const std::string& path) const;
};

// ---------------------------------------------------------------- questions
enum class QType { ReadKana, RecallKana, Meaning, Write, Type };

struct Question {
  QType type = QType::ReadKana;
  std::string key;                 // progress key
  std::string prompt;              // big text on top screen
  std::string hint;                // small text under it
  std::vector<std::string> options;  // choice tiles (empty for Write/Type)
  int correct = -1;                // index into options
  std::string answer;              // expected romaji (Type) or kana glyph (Write)
  bool isWord = false;
  bool optionsAreKana = false;     // render tiles with big kana font
};

std::string normalizeRomaji(std::string s);   // lowercase, strip spaces, shorten long vowels
bool typedMatches(const std::string& typed, const std::string& expectedRomaji);

Question makeQuestion(const Data& d, const Progress& p, std::mt19937& rng);
Question makeQuestionOfType(const Data& d, const Progress& p, std::mt19937& rng, QType t);

// ---------------------------------------------------------------- strokes
Stroke resample(const Stroke& s, int n);
float strokeLength(const Stroke& s);
struct StrokeResult { bool ok; float meanDist; };
StrokeResult scoreStroke(const Stroke& user, const Stroke& ref, float tol = 17.f);

}  // namespace kt
