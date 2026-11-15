#include "core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <ctime>

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
struct MixDef { const char* kana; const char* note; };
const MixDef MIX_DEFS[] = {
  {"さ き", "き has two bars across, さ has one."},
  {"さ ち", "Mirror images: さ ends in a small c-curve, ち bulges out like the number 5."},
  {"は ほ け", "は is a post plus a bar and loop; ほ adds a second bar on top; け has no loop."},
  {"ね れ わ", "Same post, different ending: ね loops, れ kicks out, わ curves round like a wave."},
  {"め ぬ", "ぬ is め with a little loop at the end."},
  {"る ろ", "る ends in a loop, ろ stays open."},
  {"い り", "い has two short, even strokes; り's right stroke is much longer."},
  {"こ に", "に is こ with a post on the left."},
  {"あ お", "あ has a cross through its loop; お has one bar and an open loop with a dot."},
  {"う つ", "う has a dash on top; つ is just the wave."},
  {"ま も", "ま loops at the bottom of its post; も is a hook with two bars."},
  {"た な", "た ends in こ; な ends in a loop."},
  {"ば ぱ", "Two marks make b; a little circle makes p."},
  {"シ ツ", "シ's strokes rise from the bottom-left; ツ's fall from the top, like rain."},
  {"ソ ン", "ソ's long stroke falls from the top; ン's rises from the bottom."},
  {"ク ケ タ", "ク is the bare hook; ケ's bar crosses out to the right; タ adds a line inside ク."},
  {"ウ ワ フ", "ウ has a tick on top, ワ is the box without it, フ has no left side."},
  {"ス ヌ", "ヌ has a short slash through it; ス's leg kicks out on its own."},
  {"チ テ", "チ's top stroke slants; テ has two flat bars."},
  {"ノ メ", "メ is ノ crossed by a second stroke."},
  {"コ ユ", "ユ's bottom bar sticks out to the left; コ is a closed corner."},
  {"フ ヲ", "ヲ is フ with an extra bar across the middle."},
  {"マ ム", "マ points down; ム points up with an arm out."},
  {"ア マ", "ア has a tail falling through the middle; マ ends with a short tick."},
};
}  // namespace

const int SRS_DAYS[SRS_LEVELS] = {1, 3, 7, 14, 30, 90};

int dayNumber(Time t) {
  std::time_t tt = std::time_t(t); std::tm tmv{};
  localtime_r(&tt, &tmv);
  return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

Time Progress::now() const { return clock ? clock : Time(std::time(nullptr)); }

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
  for (const MixDef& m : MIX_DEFS) {
    MixSet set; set.note = m.note;
    for (const std::string& g : chars(m.kana)) {
      uint32_t cp = utf8Codepoints(g)[0];
      int idx = findKana(cp >= 0x30A0 ? Script::Katakana : Script::Hiragana, g);
      if (idx < 0) return fail("mix-up kana missing: " + g);
      set.kana.push_back(idx);
    }
    mixSets.push_back(set);
  }
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

std::vector<std::string> Data::partNames(const std::string& g) {
  std::vector<std::string> out; size_t i = 0;
  while (i < g.size()) {
    unsigned char c = g[i]; size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    out.push_back(g.substr(i, n)); i += n;
  }
  return out;
}

std::vector<const Glyph*> Data::writeParts(const std::string& g) const {
  static const std::map<std::string, std::string> BIG = {{"ゃ", "や"}, {"ゅ", "ゆ"}, {"ょ", "よ"}, {"っ", "つ"},
                                                         {"ャ", "ヤ"}, {"ュ", "ユ"}, {"ョ", "ヨ"}, {"ッ", "ツ"}};
  std::vector<const Glyph*> out;
  for (const std::string& c : partNames(g)) {
    auto it = BIG.find(c);
    const Glyph* gl = glyphFor(it != BIG.end() && partNames(g).size() > 1 ? it->second : c);
    if (!gl) return {};
    out.push_back(gl);
  }
  return out;
}

// ---------------------------------------------------------------- progress
std::string Progress::keyKana(const Kana& k) { return std::string(k.script == Script::Hiragana ? "h:" : "k:") + k.glyph; }
std::string Progress::keyWord(const Word& w) { return "w:" + w.kana; }

void Progress::record(const std::string& key, bool ok) {
  const Time t = now();
  Stat& s = stats[key]; s.seen++;
  const bool kana = key.size() > 2 && (key[0] == 'h' || key[0] == 'k') && key[1] == ':';
  const bool wasKnown = s.step >= KNOWN_STEP;
  const bool wasDue = kana && wasKnown && s.srs > 0 && s.due <= t;
  if (ok) {
    s.correct++; s.streak++;
    if (s.gainSession != session) { s.gainSession = session; s.gain = 0; }
    if (s.gain < MAX_GAIN_PER_SESSION && s.step < KNOWN_STEP) { s.step++; s.gain++; }
    if (kana && s.step >= KNOWN_STEP && (!wasKnown || s.srs == 0)) {            // newly known: first review in a day (or at its old level)
      s.srs = std::max(1, s.srs); s.due = t + Time(SRS_DAYS[s.srs - 1]) * 86400;
    } else if (wasDue) {                                         // a review answered right: wait longer next time
      s.srs = std::min(SRS_LEVELS, std::max(1, s.srs) + 1); s.due = t + Time(SRS_DAYS[s.srs - 1]) * 86400;
    }
  } else {
    s.wrong++; s.streak = 0; s.step = std::max(0, s.step - 1);
    if (kana && wasKnown) s.srs = std::max(1, s.srs - 1);       // relearn it; it comes back sooner once known again
  }
  // daily goal and streak
  goalJustMet = false;
  int d = dayNumber(t);
  if (d != today) { today = d; todayCount = 0; }
  todayCount++;
  Day& dh = days[d]; dh.answers++; if (ok) dh.correct++;
  while (days.size() > 90) days.erase(days.begin());
  if (todayCount == settings.dailyGoal && lastGoalDay != d) {
    streak = lastGoalDay == dayNumber(t - 86400) ? streak + 1 : 1;
    lastGoalDay = d; bestStreak = std::max(bestStreak, streak); goalJustMet = true;
  }
}

bool Progress::isDue(const std::string& key) const {
  auto it = stats.find(key);
  return it != stats.end() && it->second.step >= KNOWN_STEP && it->second.srs > 0 && it->second.due <= now();
}

std::vector<int> Progress::dueKana(const Data& d, Script s) const {
  std::vector<int> out;
  for (size_t i = 0; i < d.kana.size(); i++)
    if (d.kana[i].script == s && isUnlocked(d, int(i)) && isDue(keyKana(d.kana[i]))) out.push_back(int(i));
  return out;
}

void Progress::addConfusion(const std::string& a, const std::string& b) {
  if (a == b) return;
  confusions[a < b ? a + "|" + b : b + "|" + a]++;
}

int Progress::currentStreak() const {
  int d = dayNumber(now());
  return (lastGoalDay == d || lastGoalDay == dayNumber(now() - 86400)) ? streak : 0;
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
      else if (k == "daily_goal") settings.dailyGoal = std::clamp(v, 5, 50);
      else if (k == "today") today = v;
      else if (k == "today_count") todayCount = std::max(0, v);
      else if (k == "streak") streak = std::max(0, v);
      else if (k == "best_streak") bestStreak = std::max(0, v);
      else if (k == "last_goal_day") lastGoalDay = v;
      else if (k == "kana_keys") settings.kanaKeys = v != 0;
      else if (k == "timed_best_h") timedBest[0] = std::max(0, v);
      else if (k == "timed_best_k") timedBest[1] = std::max(0, v);
      else if (k == "script") settings.script = v ? Script::Katakana : Script::Hiragana;
      else if (k == "theme") settings.light = v != 0;
      else if (k == "session") session = std::max(0, v);
      else if (k == "last_score") lastScore = v;
      else if (k == "last_len") lastLen = std::max(0, v);
      else if (k == "unlocked_h") unlocked[0] = std::clamp(v, 1, STAGE_COUNT);
      else if (k == "unlocked_k") unlocked[1] = std::clamp(v, 0, STAGE_COUNT);
      else if (k == "lessons_h") lessons[0] = std::clamp(v, 0, STAGE_COUNT);
      else if (k == "lessons_k") lessons[1] = std::clamp(v, 0, STAGE_COUNT);
    } else if (tag == "day") {
      int dd = 0; Day x; if (is >> dd >> x.answers >> x.correct) days[dd] = x;
    } else if (tag == "mix") {
      std::string a, b; int n = 0; if (is >> a >> b >> n && n > 0) confusions[a < b ? a + "|" + b : b + "|" + a] = n;
    } else if (tag == "stat") {
      std::string key; Stat s; is >> key >> s.seen >> s.correct >> s.wrong >> s.streak;
      if (!(is >> s.step >> s.gainSession >> s.gain)) { s.step = 0; s.gainSession = -1; s.gain = 0; }
      else if (!(is >> s.srs >> s.due)) { s.srs = 0; s.due = 0; }
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
  // kana known before spaced review existed: first review in 1-3 days, spread out so they don't all land at once
  int spread = 0;
  for (auto& [k, s] : stats)
    if (s.step >= KNOWN_STEP && s.srs == 0 && k.size() > 2 && k[1] == ':') { s.srs = 1; s.due = now() + Time(86400) * (1 + spread++ % 3); }
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
      << "\nset daily_goal " << settings.dailyGoal << "\nset today " << today << "\nset today_count " << todayCount
      << "\nset kana_keys " << int(settings.kanaKeys) << "\nset timed_best_h " << timedBest[0] << "\nset timed_best_k " << timedBest[1]
      << "\nset streak " << streak << "\nset best_streak " << bestStreak << "\nset last_goal_day " << lastGoalDay
      << "\nset session " << session << "\nset last_score " << lastScore << "\nset last_len " << lastLen << "\nset unlocked_h " << unlocked[0] << "\nset unlocked_k " << unlocked[1]
      << "\nset lessons_h " << lessons[0] << "\nset lessons_k " << lessons[1] << "\n";
    for (const auto& [k, s] : stats)
      f << "stat " << k << ' ' << s.seen << ' ' << s.correct << ' ' << s.wrong << ' ' << s.streak << ' ' << s.step << ' '
        << s.gainSession << ' ' << s.gain << ' ' << s.srs << ' ' << s.due << '\n';
    for (const auto& [dd, x] : days) f << "day " << dd << ' ' << x.answers << ' ' << x.correct << '\n';
    for (const auto& [k, n] : confusions) { auto bar = k.find('|'); f << "mix " << k.substr(0, bar) << ' ' << k.substr(bar + 1) << ' ' << n << '\n'; }
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
  if (p.settings.review > 0 && p.isDue(key)) return std::max(w, 1.0) * 5;  // reviews due come first
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
    if (needGlyph && d.writeParts(d.kana[i].glyph).empty()) continue;
    double x = kanaWeight(d, p, i); if (x <= 0) continue;
    pool.push_back(i); w.push_back(x);
  }
  if (pool.empty()) {  // everything reviewed away: fall back to any unlocked kana
    for (int i : unlockedKana(d, p, s)) if (!needGlyph || !d.writeParts(d.kana[i].glyph).empty()) { pool.push_back(i); w.push_back(1); }
  }
  if (pool.empty()) return -1;
  std::discrete_distribution<int> dist(w.begin(), w.end());
  return pool[dist(rng)];
}
}  // namespace

Question makeKanaQuestion(const Data& d, const Progress& p, std::mt19937& rng, int ki, QType type) {
  const Kana& k = d.kana[ki];
  if (type == QType::Write && d.writeParts(k.glyph).empty()) type = QType::ReadKana;
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
      if (p.settings.kanaKeys) { q.prompt = k.romaji; q.hint = "Type it in kana"; q.answer = k.glyph; q.kanaInput = true; }
      else { q.prompt = k.glyph; q.hint = "Type the romaji"; q.answer = k.romaji; }
      break;
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
      if (p.settings.kanaKeys) { q.prompt = w.romaji; q.hint = "Type it in kana · " + w.english; q.answer = w.kana; q.kanaInput = true; }
      else { q.prompt = w.kana; q.hint = "Type the romaji · " + w.english; q.answer = w.romaji; }
      break;
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

// ---------------------------------------------------------------- reviews and mix-ups
Question makeReviewQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  Script s = p.settings.script;
  auto due = p.dueKana(d, s);
  if (due.empty()) return makeQuestion(d, p, rng);
  std::vector<double> w; for (int i : due) w.push_back(baseWeight(p, Progress::keyKana(d.kana[i])));
  std::discrete_distribution<int> pick(w.begin(), w.end());
  int ki = due[pick(rng)];
  const Settings& st = p.settings;
  std::vector<std::pair<QType, int>> ts = {{QType::ReadKana, st.reading}, {QType::RecallKana, st.reading},
                                           {QType::Write, d.writeParts(d.kana[ki].glyph).empty() ? 0 : st.writing}, {QType::Type, st.typing}};
  std::vector<double> tw; for (auto& t : ts) tw.push_back(t.second);
  QType t = QType::ReadKana;
  double tot = 0; for (double x : tw) tot += x;
  if (tot > 0) { std::discrete_distribution<int> dt(tw.begin(), tw.end()); t = ts[dt(rng)].first; }
  Question q = makeKanaQuestion(d, p, rng, ki, t);
  q.hint = "Review · " + q.hint;
  return q;
}

std::vector<MixSet> mixupSets(const Data& d, const Progress& p, Script s) {
  std::vector<MixSet> out;
  for (const MixSet& m : d.mixSets) {
    MixSet u; u.note = m.note;
    for (int i : m.kana) if (d.kana[i].script == s && p.isUnlocked(d, i)) u.kana.push_back(i);
    if (u.kana.size() >= 2) out.push_back(u);
  }
  for (const auto& [k, n] : p.confusions) {  // pairs you've mixed up yourself
    auto bar = k.find('|'); std::string a = k.substr(0, bar), b = k.substr(bar + 1);
    int ia = -1, ib = -1;
    for (size_t i = 0; i < d.kana.size(); i++) { std::string kk = Progress::keyKana(d.kana[i]); if (kk == a) ia = int(i); if (kk == b) ib = int(i); }
    if (ia < 0 || ib < 0 || d.kana[ia].script != s || !p.isUnlocked(d, ia) || !p.isUnlocked(d, ib)) continue;
    bool covered = false;
    for (auto& m : out) if (std::count(m.kana.begin(), m.kana.end(), ia) && std::count(m.kana.begin(), m.kana.end(), ib)) covered = true;
    if (!covered) out.push_back({{ia, ib}, "You've mixed these up before: " + d.kana[ia].glyph + " is " + d.kana[ia].romaji + ", " +
                                              d.kana[ib].glyph + " is " + d.kana[ib].romaji + "."});
  }
  return out;
}

Question makeMixupQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  Script s = p.settings.script;
  auto sets = mixupSets(d, p, s);
  if (sets.empty()) return makeQuestion(d, p, rng);
  std::vector<double> w;
  for (auto& m : sets) {
    int n = 0;
    for (size_t a = 0; a < m.kana.size(); a++)
      for (size_t b = a + 1; b < m.kana.size(); b++) {
        std::string ka = Progress::keyKana(d.kana[m.kana[a]]), kb = Progress::keyKana(d.kana[m.kana[b]]);
        auto it = p.confusions.find(ka < kb ? ka + "|" + kb : kb + "|" + ka); if (it != p.confusions.end()) n += it->second;
      }
    w.push_back(1.0 + 3.0 * n);
  }
  std::discrete_distribution<int> pick(w.begin(), w.end());
  const MixSet& m = sets[pick(rng)];
  int ki = m.kana[std::uniform_int_distribution<int>(0, int(m.kana.size()) - 1)(rng)];
  const Kana& k = d.kana[ki];
  bool recall = std::uniform_int_distribution<int>(0, 1)(rng) == 1;
  Question q; q.type = recall ? QType::RecallKana : QType::ReadKana; q.key = Progress::keyKana(k); q.kanaIdx = ki;
  q.mixup = true; q.note = m.note; q.optionsAreKana = recall;
  for (int i : m.kana) q.options.push_back(recall ? d.kana[i].glyph : d.kana[i].romaji);
  std::shuffle(q.options.begin(), q.options.end(), rng);
  q.answer = recall ? k.glyph : k.romaji;
  q.correct = int(std::find(q.options.begin(), q.options.end(), q.answer) - q.options.begin());
  q.prompt = recall ? k.romaji : k.glyph;
  q.hint = recall ? "Mix-up · which one is " + k.romaji + "?" : "Mix-up · what sound is this?";
  return q;
}

Question makeTimedQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  Script s = p.settings.script;
  std::vector<QType> ts = {QType::ReadKana, QType::RecallKana, QType::ReadKana};
  if (p.settings.meanings > 0 && eligibleWords(d, p, s).size() >= 6) ts.push_back(QType::Meaning);
  QType t = ts[std::uniform_int_distribution<int>(0, int(ts.size()) - 1)(rng)];
  if (t == QType::Meaning) return makeQuestionOfType(d, p, rng, t);
  int ki = pickKana(d, p, rng, s, false);
  return makeKanaQuestion(d, p, rng, ki, t);
}

bool kanaTypedMatches(const Data& d, const Question& q, const std::string& typed) {
  if (typed == q.answer) return true;
  if (q.isWord || q.kanaIdx < 0) return false;
  const Kana& k = d.kana[q.kanaIdx];
  for (const Kana& o : d.kana) if (o.script == k.script && o.romaji == k.romaji && o.glyph == typed) return true;
  return false;
}

int dayDiff(int a, int b) {
  auto toT = [](int x) { std::tm t{}; t.tm_year = x / 10000 - 1900; t.tm_mon = x / 100 % 100 - 1; t.tm_mday = x % 100; t.tm_hour = 12; return std::mktime(&t); };
  return int(std::lround(std::difftime(toT(b), toT(a)) / 86400.0));
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
