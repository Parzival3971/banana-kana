#include "core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace kt {

std::vector<uint32_t> utf8Codepoints(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    uint32_t cp; int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3f);
    out.push_back(cp); i += n;
  }
  return out;
}

static std::vector<std::string> split(const std::string& s, char d) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == d) { out.push_back(cur); cur.clear(); } else cur += c; }
  out.push_back(cur); return out;
}

bool Data::load(const std::string& dir, std::string* err) {
  auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
  {
    std::ifstream f(dir + "/kana.tsv"); if (!f) return fail("missing kana.tsv");
    for (std::string line; std::getline(f, line);) {
      auto c = split(line, '\t'); if (c.size() < 4) continue;
      kana.push_back({c[0] == "hiragana" ? Script::Hiragana : Script::Katakana, c[1], c[2], c[3]});
    }
  }
  {
    std::ifstream f(dir + "/words.tsv"); if (!f) return fail("missing words.tsv");
    for (std::string line; std::getline(f, line);) {
      auto c = split(line, '\t'); if (c.size() < 3) continue;
      uint32_t first = utf8Codepoints(c[0])[0];
      words.push_back({c[0], c[1], c[2], first >= 0x30A0});
    }
  }
  {
    std::ifstream f(dir + "/strokes.txt"); if (!f) return fail("missing strokes.txt");
    Glyph* cur = nullptr; int remaining = 0;
    for (std::string line; std::getline(f, line);) {
      if (line.empty() || line[0] == '#') continue;
      if (remaining == 0 && line[0] == 'U') {
        unsigned cp; int n; if (sscanf(line.c_str(), "U %x %d", &cp, &n) == 2) { cur = &glyphs[cp]; remaining = n; }
        continue;
      }
      if (cur && remaining > 0) {
        Stroke s; std::istringstream is(line); std::string tok;
        while (is >> tok) { float x, y; if (sscanf(tok.c_str(), "%f,%f", &x, &y) == 2) s.push_back({x, y}); }
        cur->strokes.push_back(std::move(s)); remaining--;
      }
    }
  }
  if (kana.empty() || words.empty()) return fail("empty data");
  return true;
}

const Glyph* Data::glyphFor(const std::string& g) const {
  auto cps = utf8Codepoints(g);
  if (cps.size() != 1) return nullptr;
  auto it = glyphs.find(cps[0]);
  return it == glyphs.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------- progress
std::string Progress::keyKana(const Kana& k) { return std::string(k.script == Script::Hiragana ? "h:" : "k:") + k.glyph; }
std::string Progress::keyWord(const Word& w) { return "w:" + w.kana; }

void Progress::record(const std::string& key, bool ok) {
  Stat& s = stats[key]; s.seen++;
  if (ok) { s.correct++; s.streak++; } else { s.wrong++; s.streak = 0; }
}

int Progress::learned(const Data& d, Script sc) const {
  int n = 0;
  for (const auto& k : d.kana) if (k.script == sc) { auto it = stats.find(keyKana(k)); if (it != stats.end() && it->second.streak >= 3) n++; }
  return n;
}

bool Progress::load(const std::string& path) {
  std::ifstream f(path); if (!f) return false;
  for (std::string line; std::getline(f, line);) {
    std::istringstream is(line); std::string tag; is >> tag;
    if (tag == "set") {
      std::string k; int v; is >> k >> v;
      if (k == "kana_pct") settings.kanaPct = std::clamp(v, 0, 100);
      else if (k == "reading") settings.reading = std::clamp(v, 0, 2);
      else if (k == "meanings") settings.meanings = std::clamp(v, 0, 2);
      else if (k == "writing") settings.writing = std::clamp(v, 0, 2);
      else if (k == "typing") settings.typing = std::clamp(v, 0, 2);
      else if (k == "script") settings.script = v ? Script::Katakana : Script::Hiragana;
      else if (k == "theme") settings.light = v != 0;
    } else if (tag == "stat") {
      std::string key; Stat s; is >> key >> s.seen >> s.correct >> s.wrong >> s.streak; stats[key] = s;
    }
  }
  return true;
}

bool Progress::save(const std::string& path) const {
  std::string tmp = path + ".tmp";
  { std::ofstream f(tmp); if (!f) return false;
    f << "# banana-kana progress v1\n";
    f << "set kana_pct " << settings.kanaPct << "\nset reading " << settings.reading << "\nset meanings " << settings.meanings << "\nset writing " << settings.writing << "\nset typing " << settings.typing
      << "\nset script " << (settings.script == Script::Katakana) << "\nset theme " << settings.light << "\n";
    for (const auto& [k, s] : stats) f << "stat " << k << ' ' << s.seen << ' ' << s.correct << ' ' << s.wrong << ' ' << s.streak << '\n';
    if (!f) return false; }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// ---------------------------------------------------------------- typing
std::string normalizeRomaji(std::string s) {
  std::string t;
  for (char c : s) if (c != ' ' && c != '-' && c != '\'') t += (char)std::tolower((unsigned char)c);
  // shorten long vowels so "koohii", "kohi" and "koohi" all compare equal
  std::string o;
  for (size_t i = 0; i < t.size(); i++) {
    if (!o.empty()) {
      char p = o.back(), c = t[i];
      bool vowel = std::string("aiueo").find(c) != std::string::npos;
      if (vowel && (p == c)) continue;                // aa ii uu ee oo -> single
      if (p == 'o' && c == 'u') continue;             // ou -> o
      if (p == 'e' && c == 'i') continue;             // ei -> e
    }
    o += t[i];
  }
  return o;
}
bool typedMatches(const std::string& typed, const std::string& expected) {
  return !typed.empty() && normalizeRomaji(typed) == normalizeRomaji(expected);
}

// ---------------------------------------------------------------- question generation
namespace {
double weightFor(const Progress& p, const std::string& key) {
  auto it = p.stats.find(key);
  if (it == p.stats.end()) return 2.0;  // unseen: moderate priority
  const Stat& s = it->second;
  double wrongRatio = s.seen ? double(s.wrong) / s.seen : 0.0;
  double w = 0.6 + 4.0 * wrongRatio - 0.15 * std::min(s.streak, 6);
  return std::max(0.15, w);
}

template <class T, class KeyFn>
const T& pickWeighted(const std::vector<const T*>& pool, const Progress& p, std::mt19937& rng, KeyFn key) {
  std::vector<double> w; w.reserve(pool.size());
  for (auto* x : pool) w.push_back(weightFor(p, key(*x)));
  std::discrete_distribution<int> dist(w.begin(), w.end());
  return *pool[dist(rng)];
}

std::vector<std::string> fillOptions(std::vector<std::string> pool, const std::string& correct, std::mt19937& rng, size_t n, int* correctIdx) {
  std::shuffle(pool.begin(), pool.end(), rng);
  std::vector<std::string> opts{correct};
  for (auto& x : pool) { if (opts.size() >= n) break; if (std::find(opts.begin(), opts.end(), x) == opts.end()) opts.push_back(x); }
  std::shuffle(opts.begin(), opts.end(), rng);
  *correctIdx = int(std::find(opts.begin(), opts.end(), correct) - opts.begin());
  return opts;
}

QType chooseType(const Settings& st, bool wordMode, bool canWrite, std::mt19937& rng) {
  std::vector<QType> t; std::vector<double> w;
  auto add = [&](QType q, int weight) { if (weight > 0) { t.push_back(q); w.push_back(weight); } };
  if (wordMode) {
    add(QType::Meaning, st.meanings); add(QType::ReadKana, st.reading); add(QType::Type, st.typing);
  } else {
    add(QType::ReadKana, st.reading); add(QType::RecallKana, st.reading);
    if (canWrite) add(QType::Write, st.writing);
    add(QType::Type, st.typing);
  }
  if (t.empty()) return QType::ReadKana;
  std::discrete_distribution<int> dist(w.begin(), w.end());
  return t[dist(rng)];
}
}  // namespace

Question makeQuestionOfType(const Data& d, const Progress& p, std::mt19937& rng, QType type) {
  const Script sc = p.settings.script;
  bool wordMode = (type == QType::Meaning);
  if (type != QType::Meaning && type != QType::Write) {
    // word flavour of Read/Type is handled in makeQuestion
  }
  (void)wordMode;
  std::vector<const Kana*> kpool;
  for (auto& k : d.kana) if (k.script == sc) kpool.push_back(&k);
  if (type == QType::Write) {
    std::vector<const Kana*> w; for (auto* k : kpool) if (d.glyphFor(k->glyph)) w.push_back(k);
    if (!w.empty()) kpool = w;
  }
  const Kana& k = pickWeighted<Kana>(kpool, p, rng, Progress::keyKana);
  Question q; q.type = type; q.key = Progress::keyKana(k);
  std::vector<std::string> kanaPool, romajiPool;
  // prefer distractors from the same group, then anything in script
  for (auto* o : kpool) if (o->glyph != k.glyph && o->romaji != k.romaji) {
    kanaPool.push_back(o->glyph); romajiPool.push_back(o->romaji);
    if (o->group == k.group) { kanaPool.push_back(o->glyph); romajiPool.push_back(o->romaji); }
  }
  switch (type) {
    case QType::ReadKana:
      q.prompt = k.glyph; q.hint = "What sound is this?";
      q.options = fillOptions(romajiPool, k.romaji, rng, 6, &q.correct); q.answer = k.romaji; break;
    case QType::RecallKana:
      q.prompt = k.romaji; q.hint = "Pick the matching kana";
      q.options = fillOptions(kanaPool, k.glyph, rng, 6, &q.correct); q.optionsAreKana = true; q.answer = k.glyph; break;
    case QType::Write:
      q.prompt = k.romaji; q.hint = "Trace the kana"; q.answer = k.glyph; break;
    case QType::Type:
      q.prompt = k.glyph; q.hint = "Type the romaji"; q.answer = k.romaji; break;
    default: break;
  }
  return q;
}

static Question makeWordQuestion(const Data& d, const Progress& p, std::mt19937& rng, QType type) {
  const bool kata = p.settings.script == Script::Katakana;
  std::vector<const Word*> pool; for (auto& w : d.words) if (w.katakana == kata) pool.push_back(&w);
  const Word& w = pickWeighted<Word>(pool, p, rng, Progress::keyWord);
  Question q; q.type = type; q.key = Progress::keyWord(w); q.isWord = true;
  std::vector<std::string> kanaPool, romajiPool, engPool;
  for (auto* o : pool) if (o->kana != w.kana) { kanaPool.push_back(o->kana); romajiPool.push_back(o->romaji); engPool.push_back(o->english); }
  switch (type) {
    case QType::Meaning:
      q.prompt = w.kana; q.hint = "What does it mean?";
      q.options = fillOptions(engPool, w.english, rng, 6, &q.correct); q.answer = w.english; break;
    case QType::Type:
      q.prompt = w.kana; q.hint = "Type the romaji (" + w.english + ")"; q.answer = w.romaji; break;
    default:  // ReadKana: word -> romaji
      q.type = QType::ReadKana; q.prompt = w.kana; q.hint = "How do you read this word?";
      q.options = fillOptions(romajiPool, w.romaji, rng, 6, &q.correct); q.answer = w.romaji; break;
  }
  return q;
}

Question makeQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  const Settings& st = p.settings;
  bool wordMode = std::uniform_int_distribution<int>(0, 99)(rng) >= st.kanaPct;
  bool anyWords = false; for (auto& w : d.words) if (w.katakana == (st.script == Script::Katakana)) { anyWords = true; break; }
  if (!anyWords) wordMode = false;
  QType t = chooseType(st, wordMode, true, rng);
  return wordMode ? makeWordQuestion(d, p, rng, t) : makeQuestionOfType(d, p, rng, t);
}

// ---------------------------------------------------------------- strokes
float strokeLength(const Stroke& s) {
  float L = 0; for (size_t i = 1; i < s.size(); i++) L += std::hypot(s[i].x - s[i - 1].x, s[i].y - s[i - 1].y); return L;
}

Stroke resample(const Stroke& s, int n) {
  Stroke out; if (s.empty() || n < 2) return out;
  float total = strokeLength(s);
  if (total < 1e-4f) { out.assign(n, s[0]); return out; }
  float step = total / (n - 1), acc = 0; out.push_back(s[0]); size_t i = 1; Pt prev = s[0];
  while (i < s.size() && (int)out.size() < n) {
    float seg = std::hypot(s[i].x - prev.x, s[i].y - prev.y);
    if (acc + seg >= step && seg > 0) {
      float t = (step - acc) / seg; Pt np{prev.x + t * (s[i].x - prev.x), prev.y + t * (s[i].y - prev.y)};
      out.push_back(np); prev = np; acc = 0;
    } else { acc += seg; prev = s[i]; i++; }
  }
  while ((int)out.size() < n) out.push_back(s.back());
  return out;
}

StrokeResult scoreStroke(const Stroke& user, const Stroke& ref, float tol) {
  if (user.size() < 2 || ref.size() < 2) return {false, 999.f};
  if (strokeLength(user) < 6.f) return {false, 999.f};  // a tap, not a stroke
  const int N = 24; Stroke a = resample(user, N), b = resample(ref, N);
  float sum = 0; for (int i = 0; i < N; i++) sum += std::hypot(a[i].x - b[i].x, a[i].y - b[i].y);
  float mean = sum / N;
  // direction: start->end vectors must roughly agree (rejects strokes drawn backwards)
  float ux = a[N - 1].x - a[0].x, uy = a[N - 1].y - a[0].y, rx = b[N - 1].x - b[0].x, ry = b[N - 1].y - b[0].y;
  bool dirOk = true;
  if (std::hypot(rx, ry) > 15.f) dirOk = (ux * rx + uy * ry) > 0;
  // length ratio sanity
  float lr = strokeLength(user) / std::max(1.f, strokeLength(ref));
  bool lenOk = lr > 0.45f && lr < 2.2f;
  return {mean <= tol && dirOk && lenOk, mean};
}

}  // namespace kt
