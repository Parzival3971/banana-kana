#include "core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

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
  {"Small っ", "っ", false},  // katakana uses EXTRA_K instead
};
const char* EXTRA_K = "ッ ー ティ ディ ファ フィ フェ フォ ウィ ウェ ウォ ヴ チェ シェ ジェ";
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
  {"つ っ", "っ is つ written small: it makes no sound, it doubles the next consonant (きって = kitte)."},
  {"ツ ッ", "ッ is ツ written small: it doubles the next consonant (カップ = kappu)."},
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
      auto c = split(line, '\t'); if (c.size() < 3 || c[0].empty()) continue;
      uint32_t first = utf8Codepoints(c[0])[0];
      words.push_back({c[0], c[1], c[2], first >= 0x30A0, c.size() > 3 ? c[3] : ""});
    }
  }
  {
    std::ifstream f(dir + "/packs.tsv");  // optional: no packs, no Words screen
    for (std::string line; f && std::getline(f, line);) {
      auto c = split(line, '\t'); if (c.size() < 4) continue;
      Pack pk{c[0], c[1], c[2], c[3], {}};
      for (size_t i = 0; i < words.size(); i++) if (words[i].pack == pk.id) pk.words.push_back(int(i));
      packs.push_back(pk);
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
      if (i == STAGE_COUNT - 1 && s == Script::Katakana) {  // ッ, ー and new sounds: their own list
        st.name = "ッ ー and new sounds";
        for (const std::string& g : split(EXTRA_K, ' ')) {
          int idx = findKana(s, g);
          if (idx < 0) return fail("stage kana missing from kana.tsv: " + g);
          st.kana.push_back(idx); stageOfKana[idx] = i;
        }
        list.push_back(std::move(st)); continue;
      }
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
  for (auto& k : kana) k.special = k.glyph == "っ" || k.glyph == "ッ" || k.glyph == "ー";
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

int Data::scriptCount(Script s) const {
  int n = 0; for (auto& k : kana) if (k.script == s) n++; return n;
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
  static const std::map<std::string, std::string> BIG = {
    {"ゃ", "や"}, {"ゅ", "ゆ"}, {"ょ", "よ"}, {"っ", "つ"}, {"ャ", "ヤ"}, {"ュ", "ユ"}, {"ョ", "ヨ"}, {"ッ", "ツ"},
    {"ァ", "ア"}, {"ィ", "イ"}, {"ゥ", "ウ"}, {"ェ", "エ"}, {"ォ", "オ"}};  // small kana are traced full size
  std::vector<const Glyph*> out;
  for (const std::string& c : partNames(g)) {
    auto it = BIG.find(c);
    const Glyph* gl = glyphFor(it != BIG.end() ? it->second : c);
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
  const bool kana = key.size() > 2 && (key[0] == 'h' || key[0] == 'k' || key[0] == 'w') && key[1] == ':';  // kana and words: spaced review
  const bool wasKnown = s.step >= KNOWN_STEP;
  const bool wasDue = kana && wasKnown && s.srs > 0 && s.due <= t;
  if (ok) {
    s.correct++; s.streak++;
    // a new sitting: a later session AND at least GAIN_GAP since the last step won (restarting at once doesn't count)
    if (s.gainSession != session && (s.gainAt == 0 || t - s.gainAt >= GAIN_GAP)) { s.gainSession = session; s.gain = 0; }
    if (s.gain < MAX_GAIN_PER_SESSION && s.step < KNOWN_STEP) { s.step++; s.gain++; s.gainAt = t; }
    if (kana && s.step >= KNOWN_STEP && (!wasKnown || s.srs == 0)) {            // newly known: first review in a day (or at its old level)
      s.srs = std::max(1, s.srs); s.due = t + Time(SRS_DAYS[s.srs - 1]) * 86400;
    } else if (wasDue) {                                         // a review answered right: wait longer next time
      s.srs = std::min(SRS_LEVELS, std::max(1, s.srs) + 1); s.due = t + Time(SRS_DAYS[s.srs - 1]) * 86400;
    }
  } else {
    s.wrong++; s.streak = 0; s.step = std::max(0, s.step - 1);
    if (kana && wasKnown) s.srs = std::max(1, s.srs - 1);       // relearn it; it comes back sooner once known again
  }
  countAnswer(ok, t);
}

void Progress::countAnswer(bool ok, Time t) {  // daily goal, streak and history
  goalJustMet = false;
  int d = dayNumber(t);
  if (d != pet.lastDay) { pet.days++; pet.lastDay = d; }  // Kitsu grows with days practised
  if (d != today) { today = d; todayCount = 0; }
  todayCount++;
  Day& dh = days[d]; dh.answers++; if (ok) dh.correct++;
  while (days.size() > 90) days.erase(days.begin());
  if (todayCount >= settings.dailyGoal && lastGoalDay != d) {  // >=: lowering the goal mid-day still meets it
    streak = lastGoalDay > 0 && dayDiff(lastGoalDay, d) == 1 ? streak + 1 : 1;
    lastGoalDay = d; bestStreak = std::max(bestStreak, streak); goalJustMet = true;
  }
}

const std::vector<PetAccessory>& petAccessories() {
  static const std::vector<PetAccessory> A = {
    {"bell", "Suzu bell", "answers10"}, {"hachimaki", "Hachimaki", "perfect"}, {"omamori", "Omamori", "streak7"},
    {"lantern", "Lantern", "timed20"}, {"mask", "Fox mask", "hira_all"}, {"scroll", "Scroll", "words50"},
    {"kanzashi", "Kanzashi", "kata_all"}, {"daruma", "Daruma", "answers1000"},
  };
  return A;
}
int Progress::petStage() const {
  if (!petHatched()) return 0;
  int s = 1; for (int i = 2; i < 5; i++) if (pet.days >= PET_STAGE_DAYS[i]) s = i;
  return s;
}
bool Progress::petAwake() const { return lastGoalDay == dayNumber(now()); }
bool Progress::petAccUnlocked(int i) const {
  return i >= 0 && i < int(petAccessories().size()) && achieved.count(petAccessories()[i].achievement) > 0;
}

void Progress::recordWrite(const std::string& key, bool ok, int level) {
  Stat& s = stats[key];
  if (level >= WRITE_MEMORY) {
    record(key, ok);
    if (!ok && s.wlv >= WRITE_MEMORY) s.wlv = WRITE_MEMORY - 1;  // back to the outline for a while
    return;
  }
  s.seen++; if (ok) s.correct++; else s.wrong++;  // guided practice: counted, but no step won or lost
  if (ok && s.wlvSession != session) { s.wlv = std::min(WRITE_MEMORY, s.wlv + 1); s.wlvSession = session; }
  countAnswer(ok, now());
}

int Progress::writeLevel(const std::string& key) const {
  auto it = stats.find(key); return it == stats.end() ? 0 : std::clamp(it->second.wlv, 0, WRITE_MEMORY);
}

void Progress::skipTo(const Data& d, Script s, int stage) {
  stage = std::clamp(stage, 1, SKIP_ALL);
  int n = std::min(stage - 1, STAGE_COUNT), spread = 0;
  for (int st = 0; st < n; st++)
    for (int ki : d.stages[si(s)][st].kana) {
      Stat& x = stats[keyKana(d.kana[ki])];
      if (x.step >= KNOWN_STEP) continue;
      x.step = KNOWN_STEP; x.gainSession = -1; x.gain = 0;
      x.srs = 1; x.due = now() + Time(86400) * (1 + spread++ % 2);  // a quick check in a day or two
    }
  int& u = unlocked[si(s)]; int& l = lessons[si(s)];
  u = std::max(u, std::min(stage, STAGE_COUNT));
  l = std::max(l, stage == SKIP_ALL ? STAGE_COUNT : stage - 1);
  checkUnlocks(d);
}

bool Progress::isDue(const std::string& key) const {
  auto it = stats.find(key);
  return it != stats.end() && it->second.step >= KNOWN_STEP && it->second.srs > 0 && it->second.due <= now();
}

std::vector<int> Progress::dueWords(const Data& d) const {
  std::vector<int> out;
  for (size_t i = 0; i < d.words.size(); i++) if (isDue(keyWord(d.words[i]))) out.push_back(int(i));
  return out;
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
  return (lastGoalDay == d || (lastGoalDay > 0 && dayDiff(lastGoalDay, d) == 1)) ? streak : 0;  // calendar days (DST-safe)
}

int Progress::step(const std::string& key) const { auto it = stats.find(key); return it == stats.end() ? 0 : it->second.step; }

Time Progress::nextGainIn(const std::string& key) const {
  auto it = stats.find(key); if (it == stats.end()) return 0;
  const Stat& s = it->second;
  if (s.step >= KNOWN_STEP || s.gainAt == 0) return 0;
  if (s.gainSession == session && s.gain < MAX_GAIN_PER_SESSION) return 0;  // this sitting still has a step to give
  return std::max<Time>(0, s.gainAt + GAIN_GAP - now());
}

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
      else if (k == "kana_hand") settings.kanaHand = v != 0;
      else if (k == "guide_lines") settings.guideLines = std::clamp(v, 0, 2);
      else if (k == "ink") settings.ink = std::clamp(v, 0, 2);
      else if (k == "strict") settings.strict = std::clamp(v, 0, 2);
      else if (k == "sound") settings.sound = std::clamp(v, 0, 3);
      else if (k == "rumble") settings.rumble = std::clamp(v, 0, 2);
      else if (k == "backdrop") settings.backdrop = std::clamp(v, 0, 5);
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
    } else if (tag == "pet") {  // pet <seenStage> <acc> <days> <lastDay> <name...>
      Pet p; if (is >> p.seenStage >> p.acc >> p.days >> p.lastDay) {
        std::string nm; std::getline(is, nm); while (!nm.empty() && nm[0] == ' ') nm.erase(0, 1);
        if (!nm.empty()) p.name = nm;
        p.days = std::max(0, p.days); p.acc = std::clamp(p.acc, -1, int(petAccessories().size()) - 1);
        pet = p; petLoaded = true;
      }
    } else if (tag == "ach") {
      std::string id; int day = 0; if (is >> id >> day) achieved[id] = day;
    } else if (tag == "mix") {
      std::string a, b; int n = 0; if (is >> a >> b >> n && n > 0) confusions[a < b ? a + "|" + b : b + "|" + a] = n;
    } else if (tag == "stat") {
      std::string key; Stat s; is >> key >> s.seen >> s.correct >> s.wrong >> s.streak;
      if (!(is >> s.step >> s.gainSession >> s.gain)) { s.step = 0; s.gainSession = -1; s.gain = 0; }
      else if (!(is >> s.srs >> s.due)) { s.srs = 0; s.due = 0; }
      else if (!(is >> s.wlv >> s.wlvSession)) { s.wlv = 0; s.wlvSession = -1; }
      else if (!(is >> s.gainAt)) s.gainAt = 0;
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
  if (!petLoaded) {  // an older file: Kitsu arrives already grown for the days in the history, without a celebration
    for (auto& [dd, x] : days) if (x.answers > 0) { pet.days++; pet.lastDay = std::max(pet.lastDay, dd); }
    pet.days = std::max(pet.days, bestStreak);  // history keeps 90 days; a longer best streak proves more practice
    pet.seenStage = petStage();
  }
  return true;
}

bool Progress::save(const std::string& path) const {
  std::string tmp = path + ".tmp";
  { std::ostringstream f;
    f << "# banana-kana progress v2\nset version 2\n";
    f << "set review " << settings.review << "\nset reading " << settings.reading << "\nset meanings " << settings.meanings
      << "\nset writing " << settings.writing << "\nset typing " << settings.typing << "\nset session_len " << settings.sessionLen
      << "\nset script " << (settings.script == Script::Katakana) << "\nset theme " << settings.light
      << "\nset daily_goal " << settings.dailyGoal << "\nset today " << today << "\nset today_count " << todayCount
      << "\nset kana_keys " << int(settings.kanaKeys) << "\nset kana_hand " << int(settings.kanaHand)
      << "\nset guide_lines " << settings.guideLines << "\nset ink " << settings.ink << "\nset strict " << settings.strict << "\nset sound " << settings.sound << "\nset rumble " << settings.rumble << "\nset backdrop " << settings.backdrop << "\nset timed_best_h " << timedBest[0] << "\nset timed_best_k " << timedBest[1]
      << "\nset streak " << streak << "\nset best_streak " << bestStreak << "\nset last_goal_day " << lastGoalDay
      << "\nset session " << session << "\nset last_score " << lastScore << "\nset last_len " << lastLen << "\nset unlocked_h " << unlocked[0] << "\nset unlocked_k " << unlocked[1]
      << "\nset lessons_h " << lessons[0] << "\nset lessons_k " << lessons[1] << "\n";
    for (const auto& [k, s] : stats)
      f << "stat " << k << ' ' << s.seen << ' ' << s.correct << ' ' << s.wrong << ' ' << s.streak << ' ' << s.step << ' '
        << s.gainSession << ' ' << s.gain << ' ' << s.srs << ' ' << s.due << ' ' << s.wlv << ' ' << s.wlvSession << ' ' << s.gainAt << '\n';
    for (const auto& [dd, x] : days) f << "day " << dd << ' ' << x.answers << ' ' << x.correct << '\n';
    for (const auto& [k, n] : confusions) { auto bar = k.find('|'); f << "mix " << k.substr(0, bar) << ' ' << k.substr(bar + 1) << ' ' << n << '\n'; }
    for (const auto& [id, day] : achieved) f << "ach " << id << ' ' << day << '\n';
    f << "pet " << pet.seenStage << ' ' << pet.acc << ' ' << pet.days << ' ' << pet.lastDay << ' ' << pet.name << '\n';
    // write + fsync before the rename: a flat battery mid-save must not leave an empty progress file behind
    const std::string data = f.str();
    auto fail = [&](const char* what) { saveError = std::string(what) + " " + tmp + ": " + std::strerror(errno); return false; };
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) return fail("open");
    size_t off = 0;
    while (off < data.size()) {
      ssize_t n = ::write(fd, data.data() + off, data.size() - off);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { int e = errno; ::close(fd); errno = e; return fail("write"); }
      off += size_t(n);
    }
    // fsync is best effort: some storage refuses it (EINVAL/EROFS...). The data is written either way, as before 0.8.
    if (::fsync(fd) != 0 && errno != EINTR) saveError = std::string("fsync (ignored) ") + tmp + ": " + std::strerror(errno);
    else saveError.clear();
    if (::close(fd) != 0) return fail("close"); }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) { saveError = "rename " + tmp + " -> " + path + ": " + std::strerror(errno); return false; }
  std::string dir = path.substr(0, path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/'));
  int dfd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY); if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }  // the rename itself
  return true;
}

// ---------------------------------------------------------------- typing
std::string normalizeRomaji(const std::string& s, bool word) {
  std::string t;
  for (char c : s) if (c != ' ' && c != '-' && c != '\'') t += (char)std::tolower((unsigned char)c);
  if (!word) return t;
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
namespace {
// Kunrei-shiki / IME spellings -> Hepburn, applied to what was typed only (so "chi" never passes for ティ "ti").
std::string toHepburn(const std::string& t) {
  static const std::pair<const char*, const char*> R[] = {
    {"sya", "sha"}, {"syu", "shu"}, {"syo", "sho"}, {"sye", "she"}, {"tya", "cha"}, {"tyu", "chu"}, {"tyo", "cho"},
    {"cya", "cha"}, {"cyu", "chu"}, {"cyo", "cho"}, {"zya", "ja"}, {"zyu", "ju"}, {"zyo", "jo"}, {"jya", "ja"},
    {"jyu", "ju"}, {"jyo", "jo"}, {"dya", "ja"}, {"dyu", "ju"}, {"dyo", "jo"}, {"si", "shi"}, {"ti", "chi"},
    {"tu", "tsu"}, {"hu", "fu"}, {"zi", "ji"}, {"di", "ji"}, {"du", "zu"}};
  std::string o;
  for (size_t i = 0; i < t.size();) {
    if (t.compare(i, 2, "nn") == 0 && (i + 2 >= t.size() || !std::strchr("aiueo", t[i + 2]))) { o += 'n'; i += 2; continue; }  // IME ん (also before y: kinnyoubi)
    bool hit = false;
    for (auto& [k, v] : R) {
      size_t n = std::strlen(k);
      if (k[0] == 'h' && i > 0 && (t[i - 1] == 's' || t[i - 1] == 'c')) continue;  // "shu"/"chu" are Hepburn already
      if (t.compare(i, n, k) == 0) { o += v; i += n; hit = true; break; }
    }
    if (!hit) o += t[i++];
  }
  return o;
}
}  // namespace

bool typedMatches(const std::string& typed, const std::string& expected, bool word) {
  if (typed.empty()) return false;
  std::string e = normalizeRomaji(expected, word), a = normalizeRomaji(typed, word);
  if (a == e) return true;
  if (normalizeRomaji(toHepburn(normalizeRomaji(typed, false)), word) == e) return true;
  return e == "wo" && a == "o";  // を / ヲ are said "o"
}


// ---------------------------------------------------------------- question generation
std::vector<int> unlockedKana(const Data& d, const Progress& p, Script s) {
  std::vector<int> out;
  for (size_t i = 0; i < d.kana.size(); i++) if (d.kana[i].script == s && p.isUnlocked(d, int(i))) out.push_back(int(i));
  return out;
}

static std::set<uint32_t> okCodepoints(const Data& d, const Progress& p, Script s) {  // characters a word may use
  std::set<uint32_t> ok;
  for (int i : unlockedKana(d, p, s)) for (uint32_t cp : utf8Codepoints(d.kana[i].glyph)) ok.insert(cp);
  if (p.unlocked[si(s)] >= 10) for (uint32_t cp : {0x3063u, 0x30C3u, 0x30FCu}) ok.insert(cp);  // small tsu, long mark
  return ok;
}

std::vector<const Word*> eligibleWords(const Data& d, const Progress& p, Script s) {
  std::set<uint32_t> ok = okCodepoints(d, p, s);
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

bool allowedType(const Data& d, const Progress& p, int ki, QType t) {
  if (ki < 0) return false;
  int st = p.step(Progress::keyKana(d.kana[ki]));
  if (t == QType::Write) return st >= PRODUCE_STEP && !d.writeParts(d.kana[ki].glyph).empty();
  if (t == QType::Type) return !d.kana[ki].special && st >= (p.settings.kanaKeys ? PRODUCE_STEP : TYPE_STEP);
  return true;
}

int pickKana(const Data& d, const Progress& p, std::mt19937& rng, Script s, QType t) {
  std::vector<int> pool; std::vector<double> w;
  for (int i : unlockedKana(d, p, s)) {
    if (!allowedType(d, p, i, t)) continue;
    double x = kanaWeight(d, p, i); if (x <= 0) continue;
    pool.push_back(i); w.push_back(x);
  }
  if (pool.empty()) {  // everything reviewed away: fall back to any unlocked kana
    for (int i : unlockedKana(d, p, s)) if (allowedType(d, p, i, t)) { pool.push_back(i); w.push_back(1); }
  }
  if (pool.empty()) return -1;
  std::discrete_distribution<int> dist(w.begin(), w.end());
  return pool[dist(rng)];
}
}  // namespace

Question makeKanaQuestion(const Data& d, const Progress& p, std::mt19937& rng, int ki, QType type) {
  const Kana& k = d.kana[ki];
  if (!allowedType(d, p, ki, type)) type = QType::ReadKana;  // too new for writing or typing
  if (type == QType::Meaning) type = QType::ReadKana;
  Question q; q.type = type; q.key = Progress::keyKana(k); q.kanaIdx = ki;
  std::vector<int> pool = unlockedKana(d, p, k.script);
  if (pool.size() < 4) for (size_t i = 0; i < d.kana.size(); i++) if (d.kana[i].script == k.script) pool.push_back(int(i));
  std::vector<std::string> kanaPool, romajiPool;
  for (int i : pool) {
    const Kana& o = d.kana[i];
    if (o.glyph == k.glyph || o.romaji == k.romaji || (o.special && !k.special)) continue;  // っ/ー are never a wrong "sound"
    int reps = (d.stageOfKana[i] == d.stageOfKana[ki]) ? 3 : 1;  // prefer look-alikes from the same stage
    for (int r = 0; r < reps; r++) { kanaPool.push_back(o.glyph); romajiPool.push_back(o.romaji); }
  }
  std::set<std::string> distinct(romajiPool.begin(), romajiPool.end());
  size_t n = distinct.size() >= 5 ? 6 : 4;
  if (k.special && type == QType::ReadKana) {  // っ / ー have no sound of their own: read a word that uses them
    std::vector<const Word*> ws;
    for (auto& w : d.words) if (w.katakana == (k.script == Script::Katakana) && w.kana.find(k.glyph) != std::string::npos) ws.push_back(&w);
    if (ws.empty()) type = QType::RecallKana;
    else {
      const Word& w = *ws[std::uniform_int_distribution<int>(0, int(ws.size()) - 1)(rng)];
      auto variants = [&](const std::string& r) {  // near misses: the mark ignored, or put in the wrong place
        std::vector<std::string> v;
        for (size_t i = 0; i + 1 < r.size(); i++)
          if (r[i] == r[i + 1] && !std::strchr("n", r[i])) { std::string x = r; x.erase(i, 1); v.push_back(x); break; }
        for (size_t i = 0; i + 1 < r.size(); i++)
          if (std::strchr("aiueo", r[i]) && !std::strchr("aiueo", r[i + 1])) { std::string x = r; x.insert(i, 1, r[i]); v.push_back(x); break; }
        for (size_t i = 1; i + 1 < r.size(); i++)
          if (!std::strchr("aiueo", r[i]) && std::strchr("aiueo", r[i + 1]) && r[i - 1] != r[i]) { std::string x = r; x.insert(i, 1, r[i]); v.push_back(x); break; }
        return v;
      };
      std::vector<std::string> opts{w.romaji};
      for (auto& x : variants(w.romaji)) if (!std::count(opts.begin(), opts.end(), x)) opts.push_back(x);
      std::vector<const Word*> others = ws; std::shuffle(others.begin(), others.end(), rng);
      for (auto* o : others) if (opts.size() < 4 && !std::count(opts.begin(), opts.end(), o->romaji)) opts.push_back(o->romaji);
      while (opts.size() > 4) opts.pop_back();
      std::shuffle(opts.begin(), opts.end(), rng);
      q.type = QType::ReadKana; q.prompt = w.kana; q.hint = "How do you read this word?"; q.meaning = w.english;
      q.options = opts; q.answer = w.romaji; q.correct = int(std::find(opts.begin(), opts.end(), w.romaji) - opts.begin());
      return q;
    }
  }
  switch (type) {
    case QType::ReadKana:
      q.prompt = k.glyph; q.hint = "What sound is this?";
      q.options = fillOptions(romajiPool, k.romaji, rng, n, &q.correct); q.answer = k.romaji; break;
    case QType::RecallKana:
      q.prompt = k.romaji; q.hint = "Pick the matching kana";
      q.options = fillOptions(kanaPool, k.glyph, rng, n, &q.correct); q.optionsAreKana = true; q.answer = k.glyph; break;
    case QType::Write:
      q.writeLevel = p.writeLevel(q.key); q.prompt = k.romaji; q.answer = k.glyph;
      q.hint = q.writeLevel == 0 ? "Trace the kana over the guide" : q.writeLevel == 1 ? "Write it over the outline" : "Write it from memory";
      break;
    case QType::Type:
      if (p.settings.kanaKeys) { q.prompt = k.romaji; q.hint = "Type it in kana"; q.answer = k.glyph; q.kanaInput = true; }
      else { q.prompt = k.glyph; q.hint = "Type the romaji"; q.answer = k.romaji; }
      break;
    default: break;
  }
  return q;
}

static Question makeWordQuestion(const Data&, const Progress& p, std::mt19937& rng, QType type, const std::vector<const Word*>& pool) {
  std::vector<const Word*> targets = pool;  // "what does it mean?" only for words you've met (reading shows the meaning)
  if (type == QType::Meaning) { targets.clear(); for (auto* w : pool) if (p.introduced(Progress::keyWord(*w))) targets.push_back(w); }
  if (targets.empty()) { targets = pool; type = QType::ReadKana; }
  std::vector<double> wts; for (auto* w : targets) wts.push_back(baseWeight(p, Progress::keyWord(*w)));
  std::discrete_distribution<int> dist(wts.begin(), wts.end());
  const Word& w = *targets[dist(rng)];
  Question q; q.type = type; q.key = Progress::keyWord(w); q.isWord = true;
  std::vector<std::string> romajiPool, engPool;
  for (auto* o : pool) if (o->kana != w.kana) { romajiPool.push_back(o->romaji); engPool.push_back(o->english); }
  switch (type) {
    case QType::Meaning:
      q.prompt = w.kana; q.hint = "What does it mean?";
      q.options = fillOptions(engPool, w.english, rng, 6, &q.correct); q.answer = w.english; break;
    case QType::Type:
      q.meaning = w.english;
      if (p.settings.kanaKeys) { q.prompt = w.romaji; q.hint = "Spell it in kana"; q.answer = w.kana; q.kanaInput = true; }
      else { q.prompt = w.kana; q.hint = "Type the romaji"; q.answer = w.romaji; }
      break;
    default:
      q.type = QType::ReadKana; q.prompt = w.kana; q.hint = "How do you read this word?"; q.meaning = w.english;
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
  int ki = pickKana(d, p, rng, s, t);
  if (ki < 0) { ki = pickKana(d, p, rng, s, QType::ReadKana); t = QType::ReadKana; }
  return makeKanaQuestion(d, p, rng, ki, t);
}

Question makeQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  const Settings& st = p.settings; Script s = st.script;
  auto words = eligibleWords(d, p, s);
  bool wordsOk = words.size() >= 6;
  std::vector<const Word*> typeWords;  // typing a word needs every kana in it far enough along
  for (auto* w : words) {
    bool ok = true;
    for (const std::string& c : Data::partNames(w->kana)) {
      int ki = d.findKana(s, c);
      if (ki >= 0 && !allowedType(d, p, ki, QType::Type)) ok = false;
    }
    if (ok) typeWords.push_back(w);
  }
  struct Opt { QType t; bool word; double w; };
  std::vector<Opt> opts = {{QType::ReadKana, false, double(st.reading)}, {QType::RecallKana, false, double(st.reading)},
                           {QType::Write, false, double(st.writing)}, {QType::Type, false, double(st.typing)}};
  if (wordsOk) {
    opts.push_back({QType::Meaning, true, double(st.meanings)});
    opts.push_back({QType::ReadKana, true, st.reading * 0.5});
    if (!typeWords.empty()) opts.push_back({QType::Type, true, st.typing * 0.5});
  }
  std::vector<double> w; for (auto& o : opts) w.push_back(o.w);
  double total = 0; for (double x : w) total += x;
  if (total <= 0) return makeKanaQuestion(d, p, rng, pickKana(d, p, rng, s, QType::ReadKana), QType::ReadKana);
  std::discrete_distribution<int> dist(w.begin(), w.end());
  const Opt& o = opts[dist(rng)];
  if (o.word) return makeWordQuestion(d, p, rng, o.t, o.t == QType::Type ? typeWords : words);
  int ki = pickKana(d, p, rng, s, o.t);
  if (ki < 0) return makeKanaQuestion(d, p, rng, pickKana(d, p, rng, s, QType::ReadKana), QType::ReadKana);  // nothing far enough along yet
  if (o.t == QType::RecallKana && twinKana(d, p, ki) >= 0 && std::uniform_int_distribution<int>(0, 2)(rng) == 0)
    return makeMatchQuestion(d, p, rng, ki);  // a third of recall questions link the two scripts once both are unlocked
  return makeKanaQuestion(d, p, rng, ki, o.t);
}

bool typeAllowed(const Data& d, const Progress& p, int ki, QType t) { return allowedType(d, p, ki, t); }

// ---------------------------------------------------------------- hiragana <-> katakana matching
int twinKana(const Data& d, const Progress& p, int ki) {
  if (ki < 0) return -1;
  const Kana& k = d.kana[ki];
  if (k.glyph == "ー") return -1;  // no hiragana twin
  Script other = k.script == Script::Hiragana ? Script::Katakana : Script::Hiragana;
  for (size_t i = 0; i < d.kana.size(); i++) {
    const Kana& o = d.kana[i];
    if (o.script != other || o.romaji != k.romaji || o.special != k.special) continue;
    if (d.stageOfKana[i] != d.stageOfKana[ki]) continue;  // same chart cell only (no ヴ/ゔ or extras look-alikes)
    return p.isUnlocked(d, int(i)) && p.isUnlocked(d, ki) ? int(i) : -1;
  }
  return -1;
}

Question makeMatchQuestion(const Data& d, const Progress& p, std::mt19937& rng, int ki) {
  int tw = twinKana(d, p, ki);
  Question q = makeKanaQuestion(d, p, rng, ki, QType::RecallKana);
  if (tw < 0 || q.type != QType::RecallKana || q.isWord) return q;
  q.match = true; q.prompt = d.kana[tw].glyph;
  q.hint = d.kana[ki].script == Script::Katakana ? "Same sound in katakana?" : "Same sound in hiragana?";
  return q;
}

// ---------------------------------------------------------------- reviews and mix-ups

// ---------------------------------------------------------------- achievements
const std::vector<Achievement>& achievementList() {
  static const std::vector<Achievement> A = {
    {"answers10", "First steps", "Answer 10 questions"},
    {"first_kana", "Got one!", "Learn your first kana"},
    {"hira_half", "Halfway there", "Know 50 hiragana"},
    {"hira_all", "ひらがな master", "Know every hiragana"},
    {"kata_open", "A whole new script", "Unlock katakana"},
    {"kata_all", "カタカナ master", "Know every katakana"},
    {"streak3", "On fire", "Reach a 3-day streak"},
    {"streak7", "Week warrior", "Reach a 7-day streak"},
    {"streak30", "Unstoppable", "Reach a 30-day streak"},
    {"words10", "First words", "Know 10 words"},
    {"words50", "Word collector", "Know 50 words"},
    {"words150", "Wordsmith", "Know 150 words"},
    {"pack_done", "Pack complete", "Know every word in a pack"},
    {"timed20", "Speedy", "Score 20 in the timed challenge"},
    {"timed40", "Lightning", "Score 40 in the timed challenge"},
    {"perfect", "Perfect session", "Get every answer right in a session of 10 or more"},
    {"answers1000", "Dedicated", "Answer 1,000 questions"},
  };
  return A;
}

AchProgress achievementProgress(const Data& d, const Progress& p, int idx) {
  const std::string& id = achievementList()[idx].id;
  int answers = 0; for (auto& [k, s] : p.stats) answers += s.seen;
  int words = 0; for (auto& w : d.words) if (p.step(Progress::keyWord(w)) >= KNOWN_STEP) words++;
  int streak = std::max(p.bestStreak, p.currentStreak()), timed = std::max(p.timedBest[0], p.timedBest[1]);
  int hk = p.knownCount(d, Script::Hiragana), kk = p.knownCount(d, Script::Katakana);
  if (id == "answers10") return {answers, 10};
  if (id == "answers1000") return {answers, 1000};
  if (id == "first_kana") return {hk + kk, 1};
  if (id == "hira_half") return {hk, 50};
  if (id == "hira_all") return {hk, d.scriptCount(Script::Hiragana)};
  if (id == "kata_open") return {p.scriptUnlocked(Script::Katakana) ? 1 : 0, 1};
  if (id == "kata_all") return {kk, d.scriptCount(Script::Katakana)};
  if (id == "streak3") return {streak, 3};
  if (id == "streak7") return {streak, 7};
  if (id == "streak30") return {streak, 30};
  if (id == "words10") return {words, 10};
  if (id == "words50") return {words, 50};
  if (id == "words150") return {words, 150};
  if (id == "pack_done") { int best = 0, tot = 1;  // the pack you're furthest through
    for (size_t i = 0; i < d.packs.size(); i++) { PackInfo pi = packInfo(d, p, int(i)); if (pi.total && pi.known * tot >= best * pi.total) { best = pi.known; tot = pi.total; } }
    return {best, tot}; }
  if (id == "timed20") return {timed, 20};
  if (id == "timed40") return {timed, 40};
  return {p.achieved.count(id) ? 1 : 0, 1};  // event achievements (perfect session)
}

std::vector<int> Progress::checkAchievements(const Data& d) {
  std::vector<int> out;
  const auto& A = achievementList();
  for (int i = 0; i < int(A.size()); i++) {
    if (achieved.count(A[i].id) || A[i].id == "perfect") continue;
    AchProgress a = achievementProgress(d, *this, i);
    if (a.cur >= a.target) { achieved[A[i].id] = dayNumber(now()); out.push_back(i); }
  }
  return out;
}

bool Progress::award(const std::string& id) {
  if (achieved.count(id)) return false;
  achieved[id] = dayNumber(now()); return true;
}

// ---------------------------------------------------------------- word packs
static bool readyWith(const std::set<uint32_t>& ok, const Word& w) {
  for (uint32_t cp : utf8Codepoints(w.kana)) if (!ok.count(cp)) return false;
  return true;
}
bool wordReady(const Data& d, const Progress& p, const Word& w) {
  return readyWith(okCodepoints(d, p, w.katakana ? Script::Katakana : Script::Hiragana), w);
}
std::vector<int> readyWords(const Data& d, const Progress& p, int pack) {
  std::vector<int> out;
  if (pack < 0 || pack >= int(d.packs.size())) return out;
  auto okH = okCodepoints(d, p, Script::Hiragana), okK = okCodepoints(d, p, Script::Katakana);
  for (int wi : d.packs[pack].words) if (readyWith(d.words[wi].katakana ? okK : okH, d.words[wi])) out.push_back(wi);
  return out;
}
PackInfo packInfo(const Data& d, const Progress& p, int pack) {
  PackInfo pi; if (pack < 0 || pack >= int(d.packs.size())) return pi;
  pi.total = int(d.packs[pack].words.size()); pi.ready = int(readyWords(d, p, pack).size());
  for (int wi : d.packs[pack].words) if (p.step(Progress::keyWord(d.words[wi])) >= KNOWN_STEP) pi.known++;
  return pi;
}

// A question about one word (meaning, meaning -> kana, reading or typing). Wrong options come from `ready` (its pack)
// first, and never share the answer's meaning (ミルク / ぎゅうにゅう are both "milk").
static Question wordQuiz(const Data& d, const Progress& p, std::mt19937& rng, const Word& w, const std::vector<int>& ready,
                         const std::string& title) {
  Script ws = w.katakana ? Script::Katakana : Script::Hiragana;
  bool typeOk = true;
  for (const std::string& c : Data::partNames(w.kana)) { int ki = d.findKana(ws, c); if (ki < 0 || !allowedType(d, p, ki, QType::Type)) typeOk = false; }
  // 0 meaning, 1 meaning -> kana, 2 reading, 3 typing
  std::vector<double> tw = {3, 2, 2, typeOk ? 1.5 : 0};
  std::discrete_distribution<int> dt(tw.begin(), tw.end());
  int kind = dt(rng);
  // distractors: same pack (ready words first), then any ready word of the same script
  std::vector<const Word*> same, other;
  for (int wi : ready) if (&d.words[wi] != &w && d.words[wi].english != w.english && d.words[wi].kana != w.kana) same.push_back(&d.words[wi]);
  for (const Word* o : eligibleWords(d, p, ws)) if (o != &w && o->english != w.english && o->pack != w.pack) other.push_back(o);
  std::shuffle(same.begin(), same.end(), rng); std::shuffle(other.begin(), other.end(), rng);
  std::vector<const Word*> pool = same; for (auto* o : other) pool.push_back(o);
  auto opts = [&](auto field, size_t n, int* correctIdx) {
    std::vector<std::string> v{field(w)};
    for (auto* o : pool) { if (v.size() >= n) break; std::string x = field(*o); if (!std::count(v.begin(), v.end(), x)) v.push_back(x); }
    std::shuffle(v.begin(), v.end(), rng);
    *correctIdx = int(std::find(v.begin(), v.end(), field(w)) - v.begin());
    return v;
  };
  Question q; q.key = Progress::keyWord(w); q.isWord = true;
  switch (kind) {
    case 0:
      q.type = QType::Meaning; q.prompt = w.kana; q.hint = title + " · What does it mean?";
      q.options = opts([](const Word& x) { return x.english; }, 6, &q.correct); q.answer = w.english; break;
    case 1:
      q.type = QType::RecallKana; q.prompt = w.english; q.hint = title + " · Which word is it?"; q.optionsAreKana = true;
      q.options = opts([](const Word& x) { return x.kana; }, 4, &q.correct); q.answer = w.kana; break;
    case 3:
      q.type = QType::Type; q.meaning = w.english;
      if (p.settings.kanaKeys) { q.prompt = w.romaji; q.hint = title + " · Spell it in kana"; q.answer = w.kana; q.kanaInput = true; }
      else { q.prompt = w.kana; q.hint = title + " · Type the romaji"; q.answer = w.romaji; }
      break;
    default:
      q.type = QType::ReadKana; q.prompt = w.kana; q.hint = title + " · How do you read it?"; q.meaning = w.english;
      q.options = opts([](const Word& x) { return x.romaji; }, 6, &q.correct); q.answer = w.romaji; break;
  }
  return q;
}

// Pack questions: kana -> meaning, meaning -> kana, reading (romaji), typing. Wrong options come from the same pack first,
// and never share the answer's meaning (ミルク / ぎゅうにゅう are both "milk").
Question makePackQuestion(const Data& d, const Progress& p, std::mt19937& rng, int pack, PackSession& ps) {
  std::vector<int> ready = readyWords(d, p, pack);
  if (int(ready.size()) < PACK_MIN) return makeQuestion(d, p, rng);
  std::vector<int> met, unmet;
  for (int wi : ready) {
    std::string k = Progress::keyWord(d.words[wi]);
    bool fresh = std::count(ps.fresh.begin(), ps.fresh.end(), k) > 0;
    (p.introduced(k) || fresh ? met : unmet).push_back(wi);
  }
  const Word* target = nullptr;
  // 1. a word met on a card this session comes back after a couple of other questions
  if (!ps.fresh.empty() && (ps.sinceIntro >= 2 || met.size() <= ps.fresh.size() || ps.left <= int(ps.fresh.size()))) {  // (or nothing else to ask / time's up)
    for (int wi : ready) if (Progress::keyWord(d.words[wi]) == ps.fresh.front()) target = &d.words[wi];
    ps.fresh.erase(ps.fresh.begin());
  }
  // 2. a new word on a card: always while fewer than 2 are met, otherwise now and then (up to the session limit)
  if (!target && !unmet.empty() && ps.intros < PACK_NEW_PER_SESSION && ps.left > int(ps.fresh.size()) + 2 &&
      (met.size() < 2 || std::uniform_int_distribution<int>(0, 99)(rng) < 40)) {
    const Word& w = d.words[unmet[0]];  // pack order: simpler words first
    ps.intros++; ps.sinceIntro = 0; ps.fresh.push_back(Progress::keyWord(w));
    Question q; q.intro = true; q.isWord = true; q.key = Progress::keyWord(w); q.type = QType::ReadKana;
    q.prompt = w.kana; q.answer = w.romaji; q.meaning = w.english; q.hint = d.packs[pack].jp + " · New word";
    return q;
  }
  ps.sinceIntro++;
  if (!target) {  // 3. any word you've met, weakest first
    std::vector<double> wts;
    for (int wi : met) { std::string k = Progress::keyWord(d.words[wi]); wts.push_back(baseWeight(p, k) * (p.step(k) >= KNOWN_STEP ? 0.4 : 1.0)); }
    if (wts.empty()) return makeQuestion(d, p, rng);
    std::discrete_distribution<int> pick(wts.begin(), wts.end());
    target = &d.words[met[pick(rng)]];
  }
  return wordQuiz(d, p, rng, *target, ready, d.packs[pack].jp);
}

Question makeReviewQuestion(const Data& d, const Progress& p, std::mt19937& rng) {
  Script s = p.settings.script;
  auto due = p.dueKana(d, s); auto dueW = p.dueWords(d);
  if (due.empty() && dueW.empty()) return makeQuestion(d, p, rng);
  std::vector<double> w;
  for (int i : due) w.push_back(baseWeight(p, Progress::keyKana(d.kana[i])));
  for (int i : dueW) w.push_back(baseWeight(p, Progress::keyWord(d.words[i])));
  std::discrete_distribution<int> pick(w.begin(), w.end());
  int choice = pick(rng);
  if (choice >= int(due.size())) {  // a word: quizzed like in its pack
    const Word& wd = d.words[dueW[choice - due.size()]];
    std::vector<int> mates;
    for (size_t pi = 0; pi < d.packs.size(); pi++) if (d.packs[pi].id == wd.pack) mates = readyWords(d, p, int(pi));
    return wordQuiz(d, p, rng, wd, mates, "Review");
  }
  int ki = due[choice];
  const Settings& st = p.settings;
  std::vector<std::pair<QType, int>> ts = {{QType::ReadKana, st.reading}, {QType::RecallKana, st.reading},
                                           {QType::Write, d.writeParts(d.kana[ki].glyph).empty() || p.writeLevel(Progress::keyKana(d.kana[ki])) < WRITE_MEMORY ? 0 : st.writing},
                                           {QType::Type, st.typing}};  // a guided write doesn't count as a review, so only from memory
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
  bool special = false; for (int i : m.kana) if (d.kana[i].special) special = true;
  bool recall = special || std::uniform_int_distribution<int>(0, 1)(rng) == 1;  // っ has no sound: ask "which one is small tsu?"
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
  int ki = pickKana(d, p, rng, s, QType::ReadKana);
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
  auto toT = [](int x) { std::tm t{}; t.tm_year = x / 10000 - 1900; t.tm_mon = x / 100 % 100 - 1; t.tm_mday = x % 100; t.tm_hour = 12; t.tm_isdst = -1; return std::mktime(&t); };
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

std::vector<Guess> recognizeKana(const Data& d, const std::vector<Stroke>& strokes, int topN) {
  std::vector<Guess> out;
  if (strokes.empty()) return out;
  auto bbox = [](const std::vector<Stroke>& ss, float& x0, float& y0, float& x1, float& y1) {
    x0 = y0 = 1e9f; x1 = y1 = -1e9f;
    for (auto& s : ss) for (auto& p : s) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
  };
  float ux0, uy0, ux1, uy1; bbox(strokes, ux0, uy0, ux1, uy1);
  float uw = std::max(ux1 - ux0, 1.f), uh = std::max(uy1 - uy0, 1.f), ucx = (ux0 + ux1) / 2, ucy = (uy0 + uy1) / 2;
  const int N = 16; const int n = int(strokes.size());
  std::vector<Stroke> ur; for (auto& s : strokes) ur.push_back(resample(s, N));
  std::set<std::string> seen;
  for (size_t ki = 0; ki < d.kana.size(); ki++) {
    const Kana& k = d.kana[ki];
    if (seen.count(k.glyph)) continue;
    seen.insert(k.glyph);
    const Glyph* g = d.glyphFor(k.glyph);  // single characters only (small kana are drawn small: their own data)
    if (!g) continue;
    int m = int(g->strokes.size());
    if (std::abs(m - n) > 1) continue;
    float gx0, gy0, gx1, gy1; bbox(g->strokes, gx0, gy0, gx1, gy1);
    float gw = std::max(gx1 - gx0, 1.f), gh = std::max(gy1 - gy0, 1.f), gcx = (gx0 + gx1) / 2, gcy = (gy0 + gy1) / 2;
    float sc = std::max(gw, gh) / std::max(uw, uh);  // same box, keeping the drawing's proportions
    int cnt = std::min(n, m);
    std::vector<Stroke> gr; for (auto& gs : g->strokes) gr.push_back(resample(gs, N));
    auto dist = [&](int ui, int gi, bool rev) {
      float dsum = 0;
      for (int j = 0; j < N; j++) {
        const Pt& u = ur[ui][rev ? N - 1 - j : j];
        dsum += std::hypot((u.x - ucx) * sc + gcx - gr[gi][j].x, (u.y - ucy) * sc + gcy - gr[gi][j].y);
      }
      return dsum / N;
    };
    float inOrder = 0; for (int i = 0; i < cnt; i++) inOrder += dist(i, i, false);
    inOrder /= cnt;
    float shape = inOrder;
    if (n == m) {  // the right kana in the wrong order or direction should still be recognised (the order is checked after)
      std::vector<std::vector<float>> c(n, std::vector<float>(m));
      for (int i = 0; i < n; i++) for (int j = 0; j < m; j++) c[i][j] = std::min(dist(i, j, false), dist(i, j, true));
      std::vector<bool> uu(n, false), gu(m, false); float free = 0;
      for (int k2 = 0; k2 < n; k2++) {
        int bi = -1, bj = -1; float best = 1e9f;
        for (int i = 0; i < n; i++) if (!uu[i]) for (int j = 0; j < m; j++) if (!gu[j] && c[i][j] < best) { best = c[i][j]; bi = i; bj = j; }
        if (bi < 0) break;  // only with NaN costs
        uu[bi] = gu[bj] = true; free += best;
      }
      shape = std::min(inOrder, free / n + 4.f);
    }
    float aspect = std::abs(std::log((uw / uh) / (gw / gh)));  // a tall drawing isn't a wide kana
    out.push_back({int(ki), shape + 14.f * std::abs(m - n) + 6.f * aspect});
  }
  std::sort(out.begin(), out.end(), [](const Guess& a, const Guess& b) { return a.score < b.score; });
  if (int(out.size()) > topN) out.resize(topN);
  return out;
}

std::vector<Stroke> fitStrokes(const std::vector<Stroke>& user, const Glyph& g) {
  auto bbox = [](const std::vector<Stroke>& ss, float& x0, float& y0, float& x1, float& y1) {
    x0 = y0 = 1e9f; x1 = y1 = -1e9f;
    for (auto& s : ss) for (auto& p : s) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
  };
  if (user.empty() || g.strokes.empty()) return user;
  float ux0, uy0, ux1, uy1, gx0, gy0, gx1, gy1; bbox(user, ux0, uy0, ux1, uy1); bbox(g.strokes, gx0, gy0, gx1, gy1);
  float sc = std::max(std::max(gx1 - gx0, gy1 - gy0), 1.f) / std::max(std::max(ux1 - ux0, uy1 - uy0), 1.f);
  std::vector<Stroke> out = user;
  for (auto& s : out) for (auto& p : s) { p.x = (p.x - (ux0 + ux1) / 2) * sc + (gx0 + gx1) / 2; p.y = (p.y - (uy0 + uy1) / 2) * sc + (gy0 + gy1) / 2; }
  return out;
}

StrokeResult scoreStrokeAt(const Stroke& user, const Stroke& ref, int strictness) {
  const float tol[3] = {22.f, 17.f, 12.5f};
  return scoreStroke(user, ref, tol[std::clamp(strictness, 0, 2)], strictness >= 2);
}

StrokeResult scoreStroke(const Stroke& user, const Stroke& ref, float tol, bool strictLen) {
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
  bool lenOk = strictLen ? (lr > 0.65f && lr < 1.5f) : (lr > 0.45f && lr < 2.2f);
  return {mean <= tol && dirOk && lenOk, mean};
}

}  // namespace kt
