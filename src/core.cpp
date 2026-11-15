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


// ---------------------------------------------------------------- data
namespace {
std::string toKatakana(const std::string& s) {
  std::string out;
  for (uint32_t cp : utf8Codepoints(s)) {
    if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
  }
  return out;
}
std::vector<std::string> chars(const std::string& s) {  // split UTF-8 into characters, skipping spaces
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i]; int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    std::string ch = s.substr(i, n); i += n;
    if (ch != " ") out.push_back(ch);
  }
  return out;
}
struct StageDef { const char* name; const char* kana; bool yoon; };
const StageDef STAGE_DEFS[STAGE_COUNT] = {
  {"あ row", "あいうえお", false}, {"か row", "かきくけこ", false}, {"さ row", "さしすせそ", false},
  {"た row", "たちつてと", false}, {"な row", "なにぬねの", false}, {"は row", "はひふへほ", false},
  {"ま row", "まみむめも", false}, {"や row", "やゆよ", false}, {"ら row", "らりるれろ", false},
  {"わ を ん", "わをん", false}, {"が ざ だ", "がぎぐげござじずぜぞだぢづでど", false},
  {"ば ぱ", "ばびぶべぼぱぴぷぺぽ", false}, {"きゃ … ひゃ", "きしちにひ", true}, {"みゃ … ぴゃ", "みりぎじびぴ", true},
};
}  // namespace

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
  {
    std::ifstream f(dir + "/hints.tsv");  // optional
    for (std::string line; std::getline(f, line);) {
      if (line.empty() || line[0] == '#') continue;
      auto c = split(line, '\t'); if (c.size() >= 2) hints[c[0]] = c[1];
    }
  }
  if (kana.empty() || words.empty()) return fail("empty data");
  // stages
  stageOfKana.assign(kana.size(), -1);
  for (Script s : {Script::Hiragana, Script::Katakana}) {
    auto& list = stages[si(s)]; list.clear();
    for (int i = 0; i < STAGE_COUNT; i++) {
      const StageDef& def = STAGE_DEFS[i];
      Stage st; st.name = s == Script::Hiragana ? def.name : toKatakana(def.name);
      for (const std::string& base : chars(def.kana)) {
        std::vector<std::string> glyphsHere;
        if (def.yoon) for (const char* small : {"ゃ", "ゅ", "ょ"}) glyphsHere.push_back(base + small);
        else glyphsHere.push_back(base);
        for (std::string g : glyphsHere) {
          if (s == Script::Katakana) g = toKatakana(g);
          int idx = findKana(s, g);
          if (idx < 0) return fail("stage kana missing from kana.tsv: " + g);
          st.kana.push_back(idx); stageOfKana[idx] = i;
        }
      }
      list.push_back(std::move(st));
    }
  }
  for (size_t i = 0; i < kana.size(); i++) if (stageOfKana[i] < 0) return fail("kana in no stage: " + kana[i].glyph);
  return true;
}

int Data::findKana(Script s, const std::string& g) const {
  for (size_t i = 0; i < kana.size(); i++) if (kana[i].script == s && kana[i].glyph == g) return int(i);
  return -1;
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
  if (ok) {
    s.correct++; s.streak++;
    if (s.gainSession != session) { s.gainSession = session; s.gain = 0; }
    if (s.gain < MAX_GAIN_PER_SESSION && s.step < KNOWN_STEP) { s.step++; s.gain++; }
  } else {
    s.wrong++; s.streak = 0; s.step = std::max(0, s.step - 1);
  }
}

int Progress::step(const std::string& key) const { auto it = stats.find(key); return it == stats.end() ? 0 : it->second.step; }

bool Progress::isUnlocked(const Data& d, int i) const { return d.stageOfKana[i] < unlocked[si(d.kana[i].script)]; }

KanaState Progress::state(const Data& d, int i) const {
  if (!isUnlocked(d, i)) return KanaState::Locked;
  auto it = stats.find(keyKana(d.kana[i]));
  if (it == stats.end()) return KanaState::New;
  const Stat& s = it->second;
  if (s.streak == 0 && s.wrong > 0) return KanaState::Weak;
  if (s.step >= KNOWN_STEP) return KanaState::Known;
  return s.seen == 0 ? KanaState::New : KanaState::Learning;
}

int Progress::stageKnown(const Data& d, Script s, int stage) const {
  int n = 0; for (int i : d.stages[si(s)][stage].kana) if (step(keyKana(d.kana[i])) >= KNOWN_STEP) n++; return n;
}

int Progress::knownCount(const Data& d, Script s) const {
  int n = 0; for (size_t i = 0; i < d.kana.size(); i++) if (d.kana[i].script == s && step(keyKana(d.kana[i])) >= KNOWN_STEP) n++; return n;
}

std::vector<std::pair<Script, int>> Progress::checkUnlocks(const Data& d) {
  std::vector<std::pair<Script, int>> out;
  for (int pass = 0; pass < 2; pass++) {
    for (Script s : {Script::Hiragana, Script::Katakana}) {
      int& u = unlocked[si(s)];
      if (s == Script::Katakana && u == 0 && unlocked[0] > KATAKANA_AFTER) { u = 1; out.push_back({s, 0}); }
      while (u > 0 && u < STAGE_COUNT && stageKnown(d, s, u - 1) == int(d.stages[si(s)][u - 1].kana.size())) {
        out.push_back({s, u}); u++;
        if (s == Script::Hiragana && unlocked[1] == 0 && u > KATAKANA_AFTER) { unlocked[1] = 1; out.push_back({Script::Katakana, 0}); }
      }
    }
  }
  return out;
}

std::vector<int> Progress::weakest(const Data& d, Script s, int n) const {
  std::vector<std::pair<double, int>> c;
  for (size_t i = 0; i < d.kana.size(); i++) {
    if (d.kana[i].script != s || !isUnlocked(d, int(i))) continue;
    auto it = stats.find(keyKana(d.kana[i])); if (it == stats.end() || it->second.wrong == 0) continue;
    const Stat& st = it->second;
    c.push_back({double(st.wrong) / st.seen + (st.streak == 0 ? 1.0 : 0.0) - 0.1 * st.step, int(i)});
  }
  std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<int> out; for (auto& [w, i] : c) { if (int(out.size()) >= n) break; out.push_back(i); }
  return out;
}

bool Progress::load(const std::string& path, const Data* d) {
  std::ifstream f(path); if (!f) return false;
  bool v2 = false, anyStat = false;
  for (std::string line; std::getline(f, line);) {
    std::istringstream is(line); std::string tag; is >> tag;
    if (tag == "set") {
      std::string k; int v; is >> k >> v;
      if (k == "version") v2 = v >= 2;
      else if (k == "review") settings.review = std::clamp(v, 0, 2);
      else if (k == "reading") settings.reading = std::clamp(v, 0, 2);
      else if (k == "meanings") settings.meanings = std::clamp(v, 0, 2);
      else if (k == "writing") settings.writing = std::clamp(v, 0, 2);
      else if (k == "typing") settings.typing = std::clamp(v, 0, 2);
      else if (k == "session_len") settings.sessionLen = std::clamp(v, 5, 30);
      else if (k == "script") settings.script = v ? Script::Katakana : Script::Hiragana;
      else if (k == "theme") settings.light = v != 0;
      else if (k == "session") session = std::max(0, v);
      else if (k == "last_score") lastScore = v;
      else if (k == "last_len") lastLen = std::max(0, v);
      else if (k == "unlocked_h") unlocked[0] = std::clamp(v, 1, STAGE_COUNT);
      else if (k == "unlocked_k") unlocked[1] = std::clamp(v, 0, STAGE_COUNT);
      else if (k == "lessons_h") lessons[0] = std::clamp(v, 0, STAGE_COUNT);
      else if (k == "lessons_k") lessons[1] = std::clamp(v, 0, STAGE_COUNT);
    } else if (tag == "stat") {
      std::string key; Stat s; is >> key >> s.seen >> s.correct >> s.wrong >> s.streak;
      if (!(is >> s.step >> s.gainSession >> s.gain)) { s.step = 0; s.gainSession = -1; s.gain = 0; }
      stats[key] = s; anyStat = true;
    }
  }
  if (!v2 && anyStat) {  // v1 file (kanji-trainer): derive steps from streaks, then stages from what is known
    migrated = true;
    for (auto& [k, s] : stats) { s.step = std::min(KNOWN_STEP, s.streak); s.gainSession = -1; s.gain = 0; }
    if (d) {
      unlocked = {1, 0}; checkUnlocks(*d);
      for (int i = 0; i < 2; i++) lessons[i] = std::max(0, unlocked[i] - 1);
    }
  }
  if (!scriptUnlocked(settings.script)) settings.script = Script::Hiragana;
  return true;
}

bool Progress::save(const std::string& path) const {
  std::string tmp = path + ".tmp";
  { std::ofstream f(tmp); if (!f) return false;
    f << "# banana-kana progress v2\nset version 2\n";
    f << "set review " << settings.review << "\nset reading " << settings.reading << "\nset meanings " << settings.meanings
      << "\nset writing " << settings.writing << "\nset typing " << settings.typing << "\nset session_len " << settings.sessionLen
      << "\nset script " << (settings.script == Script::Katakana) << "\nset theme " << settings.light
      << "\nset session " << session << "\nset last_score " << lastScore << "\nset last_len " << lastLen << "\nset unlocked_h " << unlocked[0] << "\nset unlocked_k " << unlocked[1]
      << "\nset lessons_h " << lessons[0] << "\nset lessons_k " << lessons[1] << "\n";
    for (const auto& [k, s] : stats)
      f << "stat " << k << ' ' << s.seen << ' ' << s.correct << ' ' << s.wrong << ' ' << s.streak << ' ' << s.step << ' '
        << s.gainSession << ' ' << s.gain << '\n';
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
std::vector<int> unlockedKana(const Data& d, const Progress& p, Script s) {
  std::vector<int> out;
  for (size_t i = 0; i < d.kana.size(); i++) if (d.kana[i].script == s && p.isUnlocked(d, int(i))) out.push_back(int(i));
  return out;
}

std::vector<const Word*> eligibleWords(const Data& d, const Progress& p, Script s) {
  std::set<uint32_t> ok;
  for (int i : unlockedKana(d, p, s)) for (uint32_t cp : utf8Codepoints(d.kana[i].glyph)) ok.insert(cp);
  if (p.unlocked[si(s)] >= 10) for (uint32_t cp : {0x3063u, 0x30C3u, 0x30FCu}) ok.insert(cp);  // small tsu, long mark
  std::vector<const Word*> out;
  for (auto& w : d.words) {
    if (w.katakana != (s == Script::Katakana)) continue;
    bool all = true; for (uint32_t cp : utf8Codepoints(w.kana)) if (!ok.count(cp)) { all = false; break; }
    if (all) out.push_back(&w);
  }
  return out;
}

namespace {
double baseWeight(const Progress& p, const std::string& key) {
  auto it = p.stats.find(key);
  if (it == p.stats.end()) return 2.0;
  const Stat& s = it->second;
  double wrongRatio = s.seen ? double(s.wrong) / s.seen : 0.0;
  return std::max(0.15, 0.6 + 4.0 * wrongRatio - 0.15 * std::min(s.streak, 6));
}

double kanaWeight(const Data& d, const Progress& p, int i) {
  const std::string key = Progress::keyKana(d.kana[i]);
  double w = baseWeight(p, key);
  int st = p.step(key);
  if (d.stageOfKana[i] == p.currentStage(d.kana[i].script)) return w * (2 + (KNOWN_STEP - std::min(st, KNOWN_STEP)));
  KanaState ks = p.state(d, i);
  bool needs = ks == KanaState::Weak || st < KNOWN_STEP;
  if (needs) return w * 1.5;
  const double review[3] = {0.0, 0.35, 1.0};
  return w * review[p.settings.review];
}

std::vector<std::string> fillOptions(std::vector<std::string> pool, const std::string& correct, std::mt19937& rng, size_t n, int* correctIdx) {
  std::shuffle(pool.begin(), pool.end(), rng);
  std::vector<std::string> opts{correct};
  for (auto& x : pool) { if (opts.size() >= n) break; if (std::find(opts.begin(), opts.end(), x) == opts.end()) opts.push_back(x); }
  std::shuffle(opts.begin(), opts.end(), rng);
  *correctIdx = int(std::find(opts.begin(), opts.end(), correct) - opts.begin());
  return opts;
}

int pickKana(const Data& d, const Progress& p, std::mt19937& rng, Script s, bool needGlyph) {
  std::vector<int> pool; std::vector<double> w;
  for (int i : unlockedKana(d, p, s)) {
    if (needGlyph && !d.glyphFor(d.kana[i].glyph)) continue;
    double x = kanaWeight(d, p, i); if (x <= 0) continue;
    pool.push_back(i); w.push_back(x);
  }
  if (pool.empty()) {  // everything reviewed away: fall back to any unlocked kana
    for (int i : unlockedKana(d, p, s)) if (!needGlyph || d.glyphFor(d.kana[i].glyph)) { pool.push_back(i); w.push_back(1); }
  }
  if (pool.empty()) return -1;
  std::discrete_distribution<int> dist(w.begin(), w.end());
  return pool[dist(rng)];
}
}  // namespace

Question makeKanaQuestion(const Data& d, const Progress& p, std::mt19937& rng, int ki, QType type) {
  const Kana& k = d.kana[ki];
  if (type == QType::Write && !d.glyphFor(k.glyph)) type = QType::ReadKana;
  if (type == QType::Meaning) type = QType::ReadKana;
  Question q; q.type = type; q.key = Progress::keyKana(k); q.kanaIdx = ki;
  std::vector<int> pool = unlockedKana(d, p, k.script);
  if (pool.size() < 4) for (size_t i = 0; i < d.kana.size(); i++) if (d.kana[i].script == k.script) pool.push_back(int(i));
  std::vector<std::string> kanaPool, romajiPool;
  for (int i : pool) {
    const Kana& o = d.kana[i];
    if (o.glyph == k.glyph || o.romaji == k.romaji) continue;
    int reps = (d.stageOfKana[i] == d.stageOfKana[ki]) ? 3 : 1;  // prefer look-alikes from the same stage
    for (int r = 0; r < reps; r++) { kanaPool.push_back(o.glyph); romajiPool.push_back(o.romaji); }
  }
  std::set<std::string> distinct(romajiPool.begin(), romajiPool.end());
  size_t n = distinct.size() >= 5 ? 6 : 4;
  switch (type) {
    case QType::ReadKana:
      q.prompt = k.glyph; q.hint = "What sound is this?";
      q.options = fillOptions(romajiPool, k.romaji, rng, n, &q.correct); q.answer = k.romaji; break;
    case QType::RecallKana:
      q.prompt = k.romaji; q.hint = "Pick the matching kana";
      q.options = fillOptions(kanaPool, k.glyph, rng, n, &q.correct); q.optionsAreKana = true; q.answer = k.glyph; break;
    case QType::Write:
      q.prompt = k.romaji; q.hint = "Trace the kana"; q.answer = k.glyph; break;
    case QType::Type:
      q.prompt = k.glyph; q.hint = "Type the romaji"; q.answer = k.romaji; break;
    default: break;
  }
  return q;
}

static Question makeWordQuestion(const Data&, const Progress& p, std::mt19937& rng, QType type, const std::vector<const Word*>& pool) {
  std::vector<double> wts; for (auto* w : pool) wts.push_back(baseWeight(p, Progress::keyWord(*w)));
  std::discrete_distribution<int> dist(wts.begin(), wts.end());
  const Word& w = *pool[dist(rng)];
  Question q; q.type = type; q.key = Progress::keyWord(w); q.isWord = true;
  std::vector<std::string> romajiPool, engPool;
  for (auto* o : pool) if (o->kana != w.kana) { romajiPool.push_back(o->romaji); engPool.push_back(o->english); }
  switch (type) {
    case QType::Meaning:
      q.prompt = w.kana; q.hint = "What does it mean?";
      q.options = fillOptions(engPool, w.english, rng, 6, &q.correct); q.answer = w.english; break;
    case QType::Type:
      q.prompt = w.kana; q.hint = "Type the romaji · " + w.english; q.answer = w.romaji; break;
    default:
      q.type = QType::ReadKana; q.prompt = w.kana; q.hint = "How do you read this word?";
      q.options = fillOptions(romajiPool, w.romaji, rng, 6, &q.correct); q.answer = w.romaji; break;
  }
  return q;
}

Question makeQuestionOfType(const Data& d, const Progress& p, std::mt19937& rng, QType t) {
  Script s = p.settings.script;
  if (t == QType::Meaning) {
    auto words = eligibleWords(d, p, s);
    if (words.size() >= 6) return makeWordQuestion(d, p, rng, t, words);
    t = QType::ReadKana;
  }
  int ki = pickKana(d, p, rng, s, t == QType::Write);
  return makeKanaQuestion(d, p, rng, ki, t);
}

Question makeQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  const Settings& st = p.settings; Script s = st.script;
  auto words = eligibleWords(d, p, s);
  bool wordsOk = words.size() >= 6;
  struct Opt { QType t; bool word; double w; };
  std::vector<Opt> opts = {{QType::ReadKana, false, double(st.reading)}, {QType::RecallKana, false, double(st.reading)},
                           {QType::Write, false, double(st.writing)}, {QType::Type, false, double(st.typing)}};
  if (wordsOk) {
    opts.push_back({QType::Meaning, true, double(st.meanings)});
    opts.push_back({QType::ReadKana, true, st.reading * 0.5});
    opts.push_back({QType::Type, true, st.typing * 0.5});
  }
  std::vector<double> w; for (auto& o : opts) w.push_back(o.w);
  double total = 0; for (double x : w) total += x;
  if (total <= 0) return makeKanaQuestion(d, p, rng, pickKana(d, p, rng, s, false), QType::ReadKana);
  std::discrete_distribution<int> dist(w.begin(), w.end());
  const Opt& o = opts[dist(rng)];
  if (o.word) return makeWordQuestion(d, p, rng, o.t, words);
  return makeKanaQuestion(d, p, rng, pickKana(d, p, rng, s, o.t == QType::Write), o.t);
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
