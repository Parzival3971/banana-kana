#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <fstream>
#include <map>
#include <set>
#include <vector>
#include "../src/core.h"
#include "../src/stickpad.h"
#include "../src/sound.h"
using namespace kt;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void stepAll(const Data& d, Progress& p, int step) {  // every kana at least this far along
  for (auto& k : d.kana) { int& st = p.stats[Progress::keyKana(k)].step; st = std::max(st, step); }
}
static void knowStage(const Data& d, Progress& p, Script s, int stage) {  // make every kana in a stage known
  for (int i : d.stages[si(s)][stage].kana) p.stats[Progress::keyKana(d.kana[i])].step = KNOWN_STEP;
}

int main(int argc, char** argv) {
  int fails = 0; std::string dir = argc > 1 ? argv[1] : "app/data", err;
  Data d; CHECK(d.load(dir, &err)); if (!err.empty()) printf("err: %s\n", err.c_str());
  int h = 0, k = 0; for (auto& x : d.kana) (x.script == Script::Hiragana ? h : k)++;
  CHECK(h == 105 && k == 119); CHECK(d.scriptCount(Script::Hiragana) == 105 && d.scriptCount(Script::Katakana) == 119); CHECK(d.words.size() >= 120);
  CHECK(d.hints.size() == 224);
  for (auto& x : d.kana) CHECK(d.hints.count(x.glyph) == 1);

  // ---- stages: 14 per script, every kana in exactly one, sizes as designed
  for (Script s : {Script::Hiragana, Script::Katakana}) {
    CHECK(d.stages[si(s)].size() == STAGE_COUNT);
    size_t total = 0; for (auto& st : d.stages[si(s)]) total += st.kana.size();
    CHECK(int(total) == d.scriptCount(s));
    const int sizes[STAGE_COUNT] = {5, 5, 5, 5, 5, 5, 5, 3, 5, 3, 15, 10, 15, 18, s == Script::Hiragana ? 1 : 15};
    for (int i = 0; i < STAGE_COUNT; i++) CHECK(int(d.stages[si(s)][i].kana.size()) == sizes[i]);
  }
  CHECK(d.kana[d.stages[0][0].kana[0]].glyph == "あ"); CHECK(d.kana[d.stages[1][1].kana[0]].glyph == "カ");
  CHECK(d.stages[1][0].name == "ア row");

  // ---- stroke data and scoring (unchanged)
  CHECK(d.glyphFor("あ") && d.glyphFor("あ")->strokes.size() == 3);
  CHECK(d.glyphFor("きゃ") == nullptr);
  for (auto& x : d.kana) if (utf8Codepoints(x.glyph).size() == 1) CHECK(d.glyphFor(x.glyph) != nullptr);
  const Stroke& ref = d.glyphFor("あ")->strokes[0];
  CHECK(scoreStroke(ref, ref).ok);
  Stroke rev(ref.rbegin(), ref.rend()); CHECK(!scoreStroke(rev, ref).ok);
  Stroke near = ref; for (auto& p : near) { p.x += 7; p.y += 5; } CHECK(scoreStroke(near, ref).ok);
  Stroke far = ref; for (auto& p : far) { p.x += 45; } CHECK(!scoreStroke(far, ref).ok);
  CHECK(typedMatches("kohi", "koohii")); CHECK(typedMatches("gyunyu", "gyuunyuu")); CHECK(!typedMatches("ka", "ki"));
  // Kunrei / IME spellings, を said "o", ん typed "nn"; long vowels folded for words only
  for (auto [t, e] : std::vector<std::pair<const char*, const char*>>{{"si", "shi"}, {"tu", "tsu"}, {"hu", "fu"}, {"ti", "chi"},
         {"zi", "ji"}, {"di", "ji"}, {"du", "zu"}, {"sya", "sha"}, {"tyo", "cho"}, {"zyu", "ju"}, {"jya", "ja"}, {"o", "wo"},
         {"nn", "n"}, {"shi", "shi"}, {"chu", "chu"}, {"tsu", "tsu"}})
    CHECK(typedMatches(t, e, false));
  CHECK(typedMatches("konnnitiha", "konnichiha") && typedMatches("konnichiha", "konnichiha") && typedMatches("onna", "onna"));
  CHECK(typedMatches("kanzi", "kanji") && typedMatches("honn", "hon") && typedMatches("sinbun", "shinbun"));
  CHECK(!typedMatches("chi", "ti", false) && !typedMatches("sei", "se", false) && !typedMatches("kyou", "kyo", false));
  CHECK(typedMatches("sensei", "sensee") && typedMatches("kohi", "koohii"));  // words keep the long-vowel leniency

  // ---- pass rule: +1 per right answer, max 2 per sitting (a later session >= GAIN_GAP on), -1 when wrong, known at 4
  {
    Progress p; p.session = 1; p.clock = 1700000000;
    p.record("h:あ", true); p.record("h:あ", true); p.record("h:あ", true);
    CHECK(p.step("h:あ") == 2);                     // capped at 2 this session
    p.session = 2; p.clock += 60; p.record("h:あ", true); CHECK(p.step("h:あ") == 2);  // quit and restart: no new steps
    CHECK(p.nextGainIn("h:あ") > GAIN_GAP - 120 && p.nextGainIn("h:あ") <= GAIN_GAP);
    p.clock += GAIN_GAP; CHECK(p.nextGainIn("h:あ") == 0);
    p.session = 3; p.record("h:あ", true); CHECK(p.step("h:あ") == 3);  // hours later: a new sitting
    p.record("h:あ", false); CHECK(p.step("h:あ") == 2);  // wrong: back one step, not a reset
    p.record("h:あ", true); CHECK(p.step("h:あ") == 3);   // second gain this session
    p.record("h:あ", true); CHECK(p.step("h:あ") == 3);   // cap
    p.session = 4; p.clock += GAIN_GAP; p.record("h:あ", true); p.record("h:あ", true); CHECK(p.step("h:あ") == KNOWN_STEP);
    CHECK(p.save("/tmp/kt_gain.txt")); Progress b; CHECK(b.load("/tmp/kt_gain.txt") && b.stats["h:あ"].gainAt == p.stats["h:あ"].gainAt);
  }
  {  // the daily goal: lowering it mid-day still meets it; "yesterday" survives the clocks going forward
    Progress p; p.clock = 1790000000; p.settings.dailyGoal = 20;
    for (int i = 0; i < 15; i++) p.countAnswer(true, p.clock);
    p.settings.dailyGoal = 10; p.countAnswer(true, p.clock); CHECK(p.goalJustMet && p.lastGoalDay == dayNumber(p.clock));
    p.countAnswer(true, p.clock); CHECK(!p.goalJustMet && p.streak == 1);  // once a day
    const char* tz = getenv("TZ"); std::string keepTz = tz ? tz : ""; setenv("TZ", "Europe/London", 1); tzset();
    std::tm t{}; t.tm_year = 126; t.tm_mon = 2; t.tm_mday = 30; t.tm_min = 30; t.tm_isdst = -1;  // 00:30, day after BST starts
    Progress q; q.lastGoalDay = 20260329; q.streak = 5; q.settings.dailyGoal = 1; q.clock = std::mktime(&t);
    CHECK(q.currentStreak() == 5); q.countAnswer(true, q.clock); CHECK(q.streak == 6);
    if (tz) setenv("TZ", keepTz.c_str(), 1); else unsetenv("TZ"); tzset();
  }
  {  // reviews never ask a guided write (it can't clear the review): skipped-ahead kana have writing level 0
    Progress p; p.clock = 1790000000; p.skipTo(d, Script::Hiragana, 3); p.clock += 3 * 86400; p.settings.writing = 2;
    std::mt19937 rng(3); int asked = 0;
    for (int n = 0; n < 300; n++) { Question q = makeReviewQuestion(d, p, rng); if (q.type == QType::Write) CHECK(q.writeLevel == WRITE_MEMORY); asked++; }
    std::string ka = "h:あ"; p.stats[ka].wlv = WRITE_MEMORY; int writes = 0;
    for (int n = 0; n < 300; n++) { Question q = makeReviewQuestion(d, p, rng); if (q.type == QType::Write) { writes++; CHECK(q.key == ka && q.writeLevel == WRITE_MEMORY); } }
    CHECK(asked == 300 && writes > 0);  // from memory it's fine
  }
  {  // mix-ups with っ / ッ ask "which one is small tsu?", never "what sound is this?"
    Progress p; p.skipTo(d, Script::Hiragana, SKIP_ALL); std::mt19937 rng(9); int seen = 0;
    for (int n = 0; n < 2000; n++) {
      Question q = makeMixupQuestion(d, p, rng); bool special = false;
      for (auto& o : q.options) if (o == "っ" || o == "small tsu") special = true;
      if (special) { seen++; CHECK(q.type == QType::RecallKana && q.optionsAreKana); }
    }
    CHECK(seen > 0);
  }

  // ---- unlocking: stage by stage, permanent, katakana after hiragana stage 10
  {
    Progress p; CHECK(p.unlocked[0] == 1 && p.unlocked[1] == 0);
    CHECK(p.checkUnlocks(d).empty());
    knowStage(d, p, Script::Hiragana, 0);
    auto u = p.checkUnlocks(d); CHECK(u.size() == 1 && u[0].second == 1 && p.unlocked[0] == 2);
    p.record("h:あ", false);                       // slipping later does not re-lock
    CHECK(p.checkUnlocks(d).empty() && p.unlocked[0] == 2);
    for (int s = 1; s < 9; s++) knowStage(d, p, Script::Hiragana, s);
    p.checkUnlocks(d); CHECK(p.unlocked[0] == 10 && p.unlocked[1] == 0);
    knowStage(d, p, Script::Hiragana, 9);
    u = p.checkUnlocks(d); CHECK(p.unlocked[0] == 11 && p.unlocked[1] == 1);
    bool kat = false; for (auto& [s, st] : u) if (s == Script::Katakana && st == 0) kat = true; CHECK(kat);
    // states
    int a = d.findKana(Script::Hiragana, "あ"), ga = d.findKana(Script::Hiragana, "が"), kya = d.findKana(Script::Hiragana, "きゃ");
    CHECK(p.state(d, a) == KanaState::Weak);
    CHECK(p.state(d, ga) == KanaState::New);
    CHECK(p.state(d, kya) == KanaState::Locked);
    CHECK(p.state(d, d.findKana(Script::Hiragana, "か")) == KanaState::Known);
  }

  // ---- questions only use unlocked kana; small pools get 4 options; words appear only when eligible
  {
    std::mt19937 rng(7); Progress p; p.settings.meanings = 2; p.settings.writing = 2; p.settings.typing = 2;
    for (auto& w : d.words) p.introduce(Progress::keyWord(w));  // words already met, so meaning questions can come up
    std::set<std::string> stage1;
    for (int i : d.stages[0][0].kana) { stage1.insert(d.kana[i].glyph); stage1.insert(d.kana[i].romaji); }
    std::set<int> types;
    for (int n = 0; n < 1500; n++) {
      Question q = makeQuestion(d, p, rng); types.insert(int(q.type));
      CHECK(!q.prompt.empty() && !q.key.empty());
      if (!q.isWord) {
        CHECK(stage1.count(q.answer) == 1);
        for (auto& o : q.options) CHECK(stage1.count(o) == 1);
        if (!q.options.empty()) CHECK(q.options.size() == 4);
      }
      if (!q.options.empty()) { CHECK(q.correct >= 0 && q.options[q.correct] == q.answer);
        std::set<std::string> u(q.options.begin(), q.options.end()); CHECK(u.size() == q.options.size()); }
    }
    auto w1 = eligibleWords(d, p, Script::Hiragana);
    for (auto* w : w1) for (uint32_t cp : utf8Codepoints(w->kana)) CHECK(cp >= 0x3041 && cp <= 0x304A);  // あ-お only
    printf("stage 1 words: %zu, question types seen: %zu\n", w1.size(), types.size());
    CHECK(w1.size() >= 6); CHECK(types.size() == 3);  // brand-new kana: reading and meanings only
    CHECK(!types.count(int(QType::Write)) && !types.count(int(QType::Type)));
    // everything unlocked: 6 options, all five types
    for (int s = 0; s < STAGE_COUNT - 1; s++) knowStage(d, p, Script::Hiragana, s);
    p.checkUnlocks(d); CHECK(p.unlocked[0] == STAGE_COUNT);
    types.clear();
    for (int n = 0; n < 2000; n++) {
      Question q = makeQuestion(d, p, rng); types.insert(int(q.type));
      if (!q.options.empty() && !(q.kanaIdx >= 0 && d.kana[q.kanaIdx].special)) CHECK(q.options.size() == 6);
      if (q.type == QType::Write) CHECK(!d.writeParts(q.answer).empty());
    }
    CHECK(types.size() == 5);
    // katakana words stay katakana
    p.settings.script = Script::Katakana;
    for (auto* w : eligibleWords(d, p, Script::Katakana)) CHECK(w->katakana);
  }

  // ---- current stage is drilled most
  {
    std::mt19937 rng(3); Progress p;
    for (int s = 0; s < 3; s++) knowStage(d, p, Script::Hiragana, s);
    p.checkUnlocks(d); CHECK(p.unlocked[0] == 4);
    p.settings.reading = 2; p.settings.meanings = 0; p.settings.writing = 0; p.settings.typing = 0;
    int cur = 0, tot = 0;
    for (int n = 0; n < 2000; n++) { Question q = makeQuestion(d, p, rng); if (q.isWord) continue; tot++; if (d.stageOfKana[q.kanaIdx] == 3) cur++; }
    printf("current stage share: %d/%d\n", cur, tot); CHECK(cur * 10 > tot * 6);
    p.settings.review = 0; cur = tot = 0;
    for (int n = 0; n < 500; n++) { Question q = makeQuestion(d, p, rng); if (q.isWord) continue; tot++; if (d.stageOfKana[q.kanaIdx] == 3) cur++; }
    CHECK(cur == tot);  // review off and nothing weak: only the current stage
  }

  // ---- save/load roundtrip (v2)
  {
    Progress a; a.settings.sessionLen = 15; a.settings.script = Script::Katakana; a.settings.light = true; a.settings.review = 2;
    a.unlocked = {11, 2}; a.lessons = {11, 1}; a.session = 9; a.record("h:あ", true); a.record("h:あ", false);
    CHECK(a.save("/tmp/kt_prog.txt")); Progress b; CHECK(b.load("/tmp/kt_prog.txt", &d));
    CHECK(!b.migrated && b.settings.light && b.settings.sessionLen == 15 && b.settings.review == 2);
    CHECK(b.settings.script == Script::Katakana && b.unlocked[0] == 11 && b.unlocked[1] == 2 && b.lessons[1] == 1 && b.session == 9);
    CHECK(b.stats["h:あ"].seen == 2 && b.stats["h:あ"].wrong == 1 && b.stats["h:あ"].step == 0);
  }
  // ---- migration from a kanji-trainer v1 file
  {
    std::ofstream f("/tmp/kt_v1.txt");
    f << "# kanji-trainer progress v1\nset kana_pct 60\nset reading 2\nset script 1\nset theme 1\n";
    for (auto g : {"あ", "い", "う", "え", "お"}) f << "stat h:" << g << " 6 5 1 4\n";
    f << "stat h:か 3 2 1 2\nstat h:け 1 0 1 0\n"; f.close();
    Progress p; CHECK(p.load("/tmp/kt_v1.txt", &d));
    CHECK(p.migrated); CHECK(p.step("h:あ") == 4 && p.step("h:か") == 2 && p.step("h:け") == 0);
    CHECK(p.unlocked[0] == 2 && p.lessons[0] == 1);           // あ row known -> か row unlocked, its lesson pending
    CHECK(p.settings.script == Script::Hiragana);              // katakana still locked, so back to hiragana
    CHECK(p.settings.light);
  }
  // ---- spaced review: known -> due after 1, 3, 7... days; a miss drops a level
  {
    Progress p; const Time day = 86400; p.clock = 1700000000;
    std::string k = "h:あ";
    for (int sess = 0; sess < 2; sess++) { p.session = sess; p.clock += GAIN_GAP; p.record(k, true); p.record(k, true); }
    CHECK(p.step(k) == KNOWN_STEP && p.stats[k].srs == 1 && p.stats[k].due == p.clock + day);
    CHECK(!p.isDue(k)); p.clock += day; CHECK(p.isDue(k));
    CHECK(p.dueKana(d, Script::Hiragana).size() == 1);
    p.record(k, true); CHECK(p.stats[k].srs == 2 && p.stats[k].due == p.clock + 3 * day && !p.isDue(k));
    p.record(k, true); CHECK(p.stats[k].srs == 2);              // answering early doesn't advance it
    p.clock += 3 * day; p.record(k, true); CHECK(p.stats[k].srs == 3);
    p.clock += 7 * day; p.record(k, false); CHECK(p.stats[k].srs == 2 && p.step(k) == KNOWN_STEP - 1 && !p.isDue(k));
    p.session = 5; p.record(k, true); CHECK(p.step(k) == KNOWN_STEP && p.stats[k].due == p.clock + 3 * day);
    for (int i = 0; i < 20; i++) { p.clock += 100 * day; p.record(k, true); }
    CHECK(p.stats[k].srs == SRS_LEVELS);
    // review questions only ask due kana
    std::mt19937 rng(5); p.clock += 100 * day; Progress& q = p;
    for (int n = 0; n < 50; n++) CHECK(makeReviewQuestion(d, q, rng).key == k);
    // saved and loaded; kana known before SRS get scheduled 1-3 days out
    CHECK(p.save("/tmp/kt_srs.txt")); Progress b; b.clock = p.clock; CHECK(b.load("/tmp/kt_srs.txt", &d));
    CHECK(b.stats[k].srs == SRS_LEVELS && b.stats[k].due == p.stats[k].due);
    std::ofstream f("/tmp/kt_old.txt"); f << "# banana-kana progress v2\nset version 2\nstat h:い 9 9 0 9 4 1 2\n"; f.close();
    Progress o; o.clock = p.clock; CHECK(o.load("/tmp/kt_old.txt", &d));
    CHECK(o.stats["h:い"].srs == 1 && o.stats["h:い"].due > o.clock && !o.isDue("h:い"));
  }
  // ---- daily goal and streak
  {
    Progress p; p.settings.dailyGoal = 5; p.clock = 1700000000; const Time day = 86400;
    for (int i = 0; i < 4; i++) p.record("h:あ", true);
    CHECK(p.todayCount == 4 && p.streak == 0 && !p.goalJustMet);
    p.record("h:い", false); CHECK(p.goalJustMet && p.streak == 1 && p.currentStreak() == 1);
    p.record("h:い", true); CHECK(!p.goalJustMet && p.streak == 1 && p.todayCount == 6);
    p.clock += day; CHECK(p.currentStreak() == 1);
    for (int i = 0; i < 5; i++) p.record("h:あ", true);
    CHECK(p.streak == 2 && p.bestStreak == 2 && p.todayCount == 5);
    p.clock += 3 * day; CHECK(p.currentStreak() == 0);        // missed days break it
    for (int i = 0; i < 5; i++) p.record("h:あ", true);
    CHECK(p.streak == 1 && p.bestStreak == 2);
    CHECK(p.save("/tmp/kt_goal.txt")); Progress b; CHECK(b.load("/tmp/kt_goal.txt", &d));
    CHECK(b.streak == 1 && b.bestStreak == 2 && b.settings.dailyGoal == 5 && b.lastGoalDay == p.lastGoalDay);
  }
  // ---- mix-ups: built-in sets for unlocked kana, plus your own confusions
  {
    Progress p; std::mt19937 rng(9);
    CHECK(d.mixSets.size() >= 20);
    for (auto& m : mixupSets(d, p, Script::Hiragana)) CHECK(m.kana.size() >= 2);
    auto sets = mixupSets(d, p, Script::Hiragana);
    CHECK(sets.size() == 1 && sets[0].kana.size() == 2);         // あ row only: just あ/お
    p.unlocked[0] = 2; CHECK(mixupSets(d, p, Script::Hiragana).size() == 1);  // こ/に waits for に
    p.addConfusion("h:か", "h:え"); p.addConfusion("h:え", "h:か");
    CHECK(p.confusions["h:え|h:か"] == 2); CHECK(mixupSets(d, p, Script::Hiragana).size() == 2);
    for (int n = 0; n < 100; n++) {
      Question q = makeMixupQuestion(d, p, rng);
      CHECK(q.mixup && !q.note.empty() && q.options.size() == 2 && q.options[q.correct] == q.answer);
    }
    p.unlocked[0] = STAGE_COUNT; p.unlocked[1] = STAGE_COUNT;
    CHECK(mixupSets(d, p, Script::Katakana).size() >= 10);
    CHECK(p.save("/tmp/kt_mix.txt")); Progress b; CHECK(b.load("/tmp/kt_mix.txt", &d)); CHECK(b.confusions["h:え|h:か"] == 2);
  }
  // ---- writing parts: every kana can be traced; combos in two parts, the small kana full size
  {
    for (auto& x : d.kana) CHECK(!d.writeParts(x.glyph).empty());
    auto kya = d.writeParts("きゃ"); CHECK(kya.size() == 2 && kya[0] == d.glyphFor("き") && kya[1] == d.glyphFor("や"));
    CHECK(d.writeParts("ジョ").size() == 2 && d.writeParts("ジョ")[1] == d.glyphFor("ヨ"));
    CHECK(Data::partNames("きゃ").size() == 2 && Data::partNames("きゃ")[1] == "ゃ");
    Progress p; p.unlocked[0] = STAGE_COUNT; std::mt19937 rng(4); int combos = 0; stepAll(d, p, PRODUCE_STEP);
    for (int n = 0; n < 400; n++) { Question q = makeQuestionOfType(d, p, rng, QType::Write); CHECK(q.type == QType::Write);
      if (Data::partNames(q.answer).size() == 2) combos++; }
    CHECK(combos > 0);
  }
  // ---- kana keyboard typing
  {
    Progress p; p.settings.kanaKeys = true; p.unlocked[0] = STAGE_COUNT; std::mt19937 rng(6); stepAll(d, p, PRODUCE_STEP);
    int ji = d.findKana(Script::Hiragana, "ぢ");
    Question q = makeKanaQuestion(d, p, rng, ji, QType::Type);
    CHECK(q.kanaInput && q.answer == "ぢ" && q.prompt == d.kana[ji].romaji);
    CHECK(kanaTypedMatches(d, q, "ぢ") && kanaTypedMatches(d, q, "じ") && !kanaTypedMatches(d, q, "し"));
    for (int n = 0; n < 200; n++) { Question w = makeQuestionOfType(d, p, rng, QType::Type); CHECK(w.kanaInput); }
    p.settings.kanaKeys = false; CHECK(!makeKanaQuestion(d, p, rng, ji, QType::Type).kanaInput);
  }
  // ---- timed questions are always multiple choice; history and best are saved
  {
    Progress p; p.unlocked[0] = 5; std::mt19937 rng(8);
    for (int n = 0; n < 300; n++) { Question q = makeTimedQuestion(d, p, rng); CHECK(!q.options.empty()); }
    p.clock = 1700000000; p.record("h:あ", true); p.record("h:い", false); p.clock += 86400; p.record("h:あ", true);
    CHECK(p.days.size() == 2 && p.days.begin()->second.answers == 2 && p.days.begin()->second.correct == 1);
    p.timedBest[1] = 23; CHECK(p.save("/tmp/kt_days.txt")); Progress b; CHECK(b.load("/tmp/kt_days.txt", &d));
    CHECK(b.days.size() == 2 && b.timedBest[1] == 23 && b.days.rbegin()->second.correct == 1);
    CHECK(dayDiff(20261130, 20261201) == 1 && dayDiff(20261231, 20270101) == 1 && dayDiff(20261115, 20261101) == -14);
  }
  // ---- staging: choice only at step 0, romaji typing from step 1, kana keyboard and writing from step 2
  {
    Progress p; std::mt19937 rng(11); int a = d.findKana(Script::Hiragana, "あ"); std::string ka = "h:あ";
    CHECK(!typeAllowed(d, p, a, QType::Write) && !typeAllowed(d, p, a, QType::Type) && typeAllowed(d, p, a, QType::ReadKana));
    CHECK(makeKanaQuestion(d, p, rng, a, QType::Write).type == QType::ReadKana);
    p.stats[ka].step = 1; CHECK(typeAllowed(d, p, a, QType::Type) && !typeAllowed(d, p, a, QType::Write));
    p.settings.kanaKeys = true; CHECK(!typeAllowed(d, p, a, QType::Type));
    p.stats[ka].step = 2; CHECK(typeAllowed(d, p, a, QType::Type) && typeAllowed(d, p, a, QType::Write));
    // writing levels: full guide -> outline -> memory, one level per session; guided writes don't move steps
    Question q = makeKanaQuestion(d, p, rng, a, QType::Write); CHECK(q.type == QType::Write && q.writeLevel == 0);
    p.session = 1; p.recordWrite(ka, false, 0); CHECK(p.step(ka) == 2 && p.writeLevel(ka) == 0);
    p.recordWrite(ka, true, 0); CHECK(p.step(ka) == 2 && p.writeLevel(ka) == 1);
    p.recordWrite(ka, true, 1); CHECK(p.writeLevel(ka) == 1);   // one level per session
    p.session = 2; p.recordWrite(ka, true, 1); CHECK(p.writeLevel(ka) == 2 && p.step(ka) == 2);
    p.recordWrite(ka, true, 2); CHECK(p.step(ka) == 3);         // from memory: counts like any answer
    p.recordWrite(ka, false, 2); CHECK(p.step(ka) == 2 && p.writeLevel(ka) == 1);
    CHECK(p.save("/tmp/kt_wlv.txt")); Progress b; CHECK(b.load("/tmp/kt_wlv.txt", &d)); CHECK(b.writeLevel(ka) == 1 && b.stats[ka].wlvSession == 2);
  }
  // ---- skipping ahead: earlier stages known with an early review, katakana straight away
  {
    Progress p; p.clock = 1700000000;
    p.skipTo(d, Script::Hiragana, 5);
    CHECK(p.unlocked[0] == 5 && p.lessons[0] == 4 && p.lessonPending(Script::Hiragana));
    for (int st = 0; st < 4; st++) CHECK(p.stageKnown(d, Script::Hiragana, st) == int(d.stages[0][st].kana.size()));
    CHECK(p.stageKnown(d, Script::Hiragana, 4) == 0 && p.dueKana(d, Script::Hiragana).empty());
    p.clock += 2 * 86400; CHECK(p.dueKana(d, Script::Hiragana).size() == 20);   // all checked within two days
    p.skipTo(d, Script::Hiragana, 3); CHECK(p.unlocked[0] == 5);                   // never moves back
    CHECK(!p.scriptUnlocked(Script::Katakana)); p.skipTo(d, Script::Katakana, 1);
    CHECK(p.unlocked[1] == 1 && p.lessons[1] == 0 && p.lessonPending(Script::Katakana));
    Progress all; all.skipTo(d, Script::Hiragana, SKIP_ALL);
    CHECK(all.knownCount(d, Script::Hiragana) == 105 && all.unlocked[0] == STAGE_COUNT && !all.lessonPending(Script::Hiragana));
    CHECK(all.scriptUnlocked(Script::Katakana) && all.unlocked[1] == 1);          // katakana opens as usual after stage 10
  }
  // ---- stage 15 extras: っ ッ ー are read through words; extended katakana write in two parts
  {
    Progress p; p.unlocked = {STAGE_COUNT, STAGE_COUNT}; std::mt19937 rng(21);
    int tsu = d.findKana(Script::Hiragana, "っ"), cho = d.findKana(Script::Katakana, "ー"), fa = d.findKana(Script::Katakana, "ファ");
    CHECK(tsu >= 0 && cho >= 0 && fa >= 0 && d.kana[tsu].special && d.kana[cho].special && !d.kana[fa].special);
    CHECK(d.stageOfKana[tsu] == STAGE_COUNT - 1 && d.stageOfKana[fa] == STAGE_COUNT - 1);
    for (int n = 0; n < 50; n++) {
      Question q = makeKanaQuestion(d, p, rng, n % 2 ? tsu : cho, QType::ReadKana);
      CHECK(q.prompt.size() > 3 && q.options.size() >= 2 && q.options[q.correct] == q.answer && q.key == Progress::keyKana(d.kana[n % 2 ? tsu : cho]));
      std::set<std::string> u(q.options.begin(), q.options.end()); CHECK(u.size() == q.options.size());
    }
    { Question r = makeKanaQuestion(d, p, rng, tsu, QType::RecallKana); CHECK(r.options.size() >= 4 && r.options[r.correct] == "っ"); }
    p.stats["h:っ"].step = 3; CHECK(!typeAllowed(d, p, tsu, QType::Type) && typeAllowed(d, p, tsu, QType::Write));
    CHECK(d.writeParts("ファ").size() == 2 && d.writeParts("ファ")[1] == d.glyphFor("ア"));
    CHECK(d.writeParts("っ").size() == 1 && d.writeParts("っ")[0] == d.glyphFor("つ"));
    for (int n = 0; n < 300; n++) { Question q = makeKanaQuestion(d, p, rng, fa, QType::ReadKana);
      for (auto& o : q.options) CHECK(o != "small tsu" && o != "long vowel"); }   // っ/ー never stand in as sounds
  }
  // ---- stroke checking strictness
  {
    const Glyph* g = d.glyphFor("し"); Stroke s0 = g->strokes[0], off = s0;
    for (auto& pt : off) { pt.x += 15; pt.y += 4; }
    CHECK(scoreStrokeAt(s0, s0, 2).ok && scoreStrokeAt(off, s0, 0).ok && !scoreStrokeAt(off, s0, 2).ok);
    Stroke shortS(s0.begin(), s0.begin() + s0.size() * 3 / 5);
    CHECK(!scoreStrokeAt(shortS, s0, 2).ok);
    Progress p; p.settings.strict = 2; p.settings.ink = 0; p.settings.guideLines = 1; p.settings.kanaHand = true;
    CHECK(p.save("/tmp/kt_set.txt")); Progress b; CHECK(b.load("/tmp/kt_set.txt", &d));
    CHECK(b.settings.strict == 2 && b.settings.ink == 0 && b.settings.guideLines == 1 && b.settings.kanaHand);
  }
  // ---- recognising a freely drawn kana: the real strokes, moved and resized, come back as themselves
  {
    int right = 0, total = 0;
    for (auto& k : d.kana) {
      const Glyph* g = d.glyphFor(k.glyph); if (!g || k.special) continue;
      std::vector<Stroke> drawn = g->strokes;
      for (auto& st : drawn) for (auto& pt : st) { pt.x = pt.x * 0.7f + 20; pt.y = pt.y * 0.75f + 10; }
      auto gs = recognizeKana(d, drawn); total++;
      if (!gs.empty() && d.kana[gs[0].kana].glyph == k.glyph) right++;
      else if (!gs.empty()) { // identical shapes across scripts (へ/ヘ, り/リ...) are fine
        std::string a = d.kana[gs[0].kana].romaji; if (a == k.romaji) right++; }
    }
    printf("recogniser: %d/%d exact or same-sound\n", right, total);
    CHECK(right * 100 >= total * 95);
    CHECK(recognizeKana(d, {}).empty());
    // wobbly hand: smooth wave plus jitter on every point, stroke ends trimmed a little
    std::mt19937 jr(42); std::normal_distribution<float> nd(0.f, 2.0f); int r2 = 0, t2 = 0, top3 = 0;
    for (auto& k : d.kana) {
      const Glyph* g = d.glyphFor(k.glyph); if (!g || k.special) continue;
      std::vector<Stroke> drawn;
      for (size_t si2 = 0; si2 < g->strokes.size(); si2++) {
        Stroke rs = resample(g->strokes[si2], 30), o;
        for (size_t j = 2; j + 2 < rs.size(); j++)
          o.push_back({rs[j].x + 3.f * std::sin(j * 0.5f + si2) + nd(jr), rs[j].y + 3.f * std::cos(j * 0.4f) + nd(jr)});
        drawn.push_back(o);
      }
      auto gs = recognizeKana(d, drawn); t2++;
      bool ok = !gs.empty() && (d.kana[gs[0].kana].glyph == k.glyph || d.kana[gs[0].kana].romaji == k.romaji);
      if (ok) r2++;
      for (auto& g3 : gs) if (d.kana[g3.kana].glyph == k.glyph || d.kana[g3.kana].romaji == k.romaji) { top3++; break; }
    }
    { const Glyph* g = d.glyphFor("シ"); std::vector<Stroke> rev(g->strokes.rbegin(), g->strokes.rend());
      for (auto& st : rev) std::reverse(st.begin(), st.end());
      auto gs = recognizeKana(d, rev); CHECK(!gs.empty() && d.kana[gs[0].kana].glyph == "シ"); }   // wrong order and direction: still シ
    printf("recogniser, wobbly: %d/%d first, %d/%d in top 3\n", r2, t2, top3, t2);
    CHECK(r2 * 100 >= t2 * 85 && top3 * 100 >= t2 * 95);
  }
  // ---- word prompts carry their English meaning (except when the meaning is the answer)
  {
    Progress p; p.unlocked[0] = STAGE_COUNT; stepAll(d, p, PRODUCE_STEP); std::mt19937 rng(31); int typedWords = 0;
    for (int n = 0; n < 300; n++) {
      Question q = makeQuestion(d, p, rng);
      if ((q.type == QType::Type || q.type == QType::ReadKana) && q.isWord) { typedWords++; CHECK(!q.meaning.empty()); }
      if (q.type == QType::Meaning) CHECK(q.meaning.empty());
    }
    CHECK(typedWords > 0);
    p.settings.meanings = 0; int reads = 0;  // shown even with the Word meanings drill turned off
    for (int n = 0; n < 300; n++) { Question q = makeQuestion(d, p, rng); if (q.type == QType::ReadKana && q.isWord) { reads++; CHECK(!q.meaning.empty()); } }
    CHECK(reads > 0);
  }
  {  // analogue stick -> d-pad without jitter
    kt::StickPad sp; std::vector<int> got; auto f = [&](int d) { got.push_back(d); };
    unsigned t = 0;
    auto feed = [&](int x, int y) { t += 8; sp.axis(0, true, x, t, f); sp.axis(0, false, y, t, f); sp.repeat(t, f); };
    feed(1500, -900);                                   // rests a little off centre
    for (int v = 0; v <= 30000; v += 5000) feed(v, -900);   // push right
    CHECK(got.size() == 1 && got[0] == kt::STICK_RIGHT);
    for (int w = 0; w < 10; w++) feed(26000, w % 2 ? 24000 : -22000);  // wobble towards both diagonals
    CHECK(got.size() == 1);
    for (int v = 30000; v >= 0; v -= 6000) feed(v, 0);  // let go...
    feed(-14000, 0); feed(-21000, 0); feed(-9000, 0); feed(2000, 0);  // ...spring-back overshoot
    CHECK(got.size() == 1);
    for (int k = 0; k < 20; k++) feed(1500, -900);      // settle at rest
    feed(1500, 25000);                                  // a real down press
    CHECK(got.size() == 2 && got[1] == kt::STICK_DOWN);
    for (int k = 0; k < 20; k++) feed(1500, 25000);     // under the repeat delay
    CHECK(got.size() == 2);
    for (int k = 0; k < 60; k++) feed(1500, 25000);     // held: repeats
    CHECK(got.size() >= 4 && got.back() == kt::STICK_DOWN);
    size_t n = got.size(); for (int k = 0; k < 20; k++) feed(1500, -900);
    feed(21000, 20000);                                 // dead diagonal: no press
    CHECK(got.size() == n);
    printf("stick: %zu presses, no jitter\n", got.size());
  }
  {  // hiragana <-> katakana matching
    Progress p; p.skipTo(d, Script::Hiragana, 5);
    int a = d.findKana(Script::Hiragana, "あ");
    CHECK(twinKana(d, p, a) < 0);                       // katakana still locked: no matching yet
    p.skipTo(d, Script::Katakana, SKIP_ALL); p.skipTo(d, Script::Hiragana, SKIP_ALL);
    CHECK(twinKana(d, p, a) == d.findKana(Script::Katakana, "ア"));
    CHECK(twinKana(d, p, d.findKana(Script::Katakana, "ー")) < 0);
    CHECK(twinKana(d, p, d.findKana(Script::Katakana, "ティ")) < 0);   // no hiragana twin
    CHECK(twinKana(d, p, d.findKana(Script::Katakana, "ッ")) == d.findKana(Script::Hiragana, "っ"));
    std::mt19937 rng(7); int matches = 0;
    for (Script sc : {Script::Hiragana, Script::Katakana}) {
      p.settings.script = sc;
      for (int n = 0; n < 600; n++) {
        Question q = makeQuestion(d, p, rng); if (!q.match) continue;
        matches++;
        const Kana& k = d.kana[q.kanaIdx];
        CHECK(k.script == sc && q.optionsAreKana && q.options[q.correct] == k.glyph);
        int tp = -1; for (size_t i = 0; i < d.kana.size(); i++) if (d.kana[i].glyph == q.prompt) tp = int(i);
        CHECK(tp >= 0 && d.kana[tp].script != sc && d.kana[tp].romaji == k.romaji);
        for (auto& o : q.options) { int oi = d.findKana(sc, o); CHECK(oi >= 0); }   // every option is in the script you're learning
      }
    }
    CHECK(matches > 20);
    printf("match: %d hiragana/katakana questions\n", matches);
  }
  {  // word packs
    CHECK(d.packs.size() == 12);
    std::set<std::string> ids; int words = 0;
    for (auto& pk : d.packs) { ids.insert(pk.id); words += int(pk.words.size()); CHECK(pk.words.size() >= 20 && !pk.jp.empty() && !pk.name.empty()); }
    CHECK(ids.size() == d.packs.size() && words > 300);
    Progress p;                                          // brand new: only words made of あいうえお
    for (size_t i = 0; i < d.packs.size(); i++)
      for (int wi : readyWords(d, p, int(i))) for (uint32_t cp : utf8Codepoints(d.words[wi].kana)) CHECK(cp >= 0x3041 && cp <= 0x304A);
    p.skipTo(d, Script::Hiragana, 6);                    // あ-は rows known
    int food = -1; for (size_t i = 0; i < d.packs.size(); i++) if (d.packs[i].id == "food") food = int(i);
    CHECK(food >= 0);
    for (int wi : readyWords(d, p, food)) {              // only hiragana words made of unlocked kana
      const Word& w = d.words[wi]; CHECK(!w.katakana && wordReady(d, p, w));
      CHECK(w.kana.find("ら") == std::string::npos && w.kana.find("ま") == std::string::npos);
    }
    p.skipTo(d, Script::Hiragana, SKIP_ALL); p.skipTo(d, Script::Katakana, SKIP_ALL);
    PackInfo fi = packInfo(d, p, food); CHECK(fi.ready == fi.total && fi.known == 0);
    std::mt19937 rng(11); int kinds[4] = {0, 0, 0, 0}, intros = 0;
    for (int sess = 0; sess < 12; sess++) {                // sessions of 10 questions, like the app
      PackSession ps; std::vector<std::string> introduced; int asked = 0, cards = 0;
      std::map<std::string, int> introAt, quizAt;
      while (asked < 10) {
        ps.left = 10 - asked;
        Question q = makePackQuestion(d, p, rng, food, ps);
        CHECK(q.isWord && q.key.rfind("w:", 0) == 0);
        const Word* w = nullptr; for (int wi : d.packs[food].words) if (Progress::keyWord(d.words[wi]) == q.key) w = &d.words[wi];
        CHECK(w != nullptr); if (!w) break;
        if (q.intro) {                                    // a card: shows kana, romaji and meaning; never a word met before
          CHECK(!p.introduced(q.key) && q.prompt == w->kana && q.answer == w->romaji && q.meaning == w->english && q.options.empty());
          p.introduce(q.key); introduced.push_back(q.key); introAt[q.key] = asked; cards++; intros++;
          CHECK(cards <= PACK_NEW_PER_SESSION);
          continue;
        }
        asked++;
        if (!quizAt.count(q.key)) quizAt[q.key] = asked;
        CHECK(p.introduced(q.key));                       // never tested on a word you haven't met
        if (q.type == QType::Meaning) { kinds[0]++; CHECK(q.options[q.correct] == w->english && q.prompt == w->kana); }
        else if (q.type == QType::RecallKana) {
          kinds[1]++; CHECK(q.prompt == w->english && q.options[q.correct] == w->kana && q.optionsAreKana);
          for (size_t o = 0; o < q.options.size(); o++)     // no other option means the same thing
            if (int(o) != q.correct) for (auto& x : d.words) if (x.kana == q.options[o]) CHECK(x.english != w->english);
        }
        else if (q.type == QType::ReadKana) { kinds[2]++; CHECK(q.options[q.correct] == w->romaji && !q.meaning.empty()); }
        else if (q.type == QType::Type) { kinds[3]++; CHECK(q.answer == w->romaji || q.answer == w->kana); }
        if (!q.options.empty()) { std::set<std::string> u(q.options.begin(), q.options.end()); CHECK(u.size() == q.options.size() && q.options.size() >= 4); }
        p.session++; p.clock = p.now() + GAIN_GAP; p.record(q.key, (asked % 4) != 0);
      }
      if (sess == 0) CHECK(cards >= 2);                   // a first session starts by meeting words
      for (auto& k : introduced) CHECK(quizAt.count(k) && quizAt[k] > introAt[k]);  // every new word is asked again the same session
    }
    CHECK(intros >= 15 && kinds[0] > 0 && kinds[1] > 0 && kinds[2] > 0 && kinds[3] > 0);
    {  // normal practice: "what does it mean?" only for words you've met
      Progress np; np.skipTo(d, Script::Hiragana, SKIP_ALL); np.settings.reading = 0; np.settings.typing = 0; np.settings.writing = 0;
      int meanings = 0;
      for (int n = 0; n < 200; n++) { Question q = makeQuestionOfType(d, np, rng, QType::Meaning); if (q.type == QType::Meaning) meanings++; }
      CHECK(meanings == 0);                               // nothing met yet: meaning questions turn into reading (which shows the meaning)
      int a = -1; for (size_t i = 0; i < d.words.size(); i++) if (d.words[i].kana == "ねこ") a = int(i);
      np.introduce(Progress::keyWord(d.words[a]));
      for (int n = 0; n < 200; n++) { Question q = makeQuestionOfType(d, np, rng, QType::Meaning); if (q.type == QType::Meaning) { meanings++; CHECK(q.prompt == "ねこ"); } }
      CHECK(meanings > 0);
    }
    Question q; for (int n = 0; n < 50 && (q.key.empty() || q.intro); n++) { PackSession ps2; q = makePackQuestion(d, p, rng, food, ps2); }
    for (int r = 0; r < 4; r++) { p.session++; p.clock = p.now() + GAIN_GAP; p.record(q.key, true); p.record(q.key, true); }  // knowing words shows in the count
    CHECK(packInfo(d, p, food).known >= 1);
    printf("packs: %zu packs, %d words, food %d/%d ready, %d new-word cards\n", d.packs.size(), words, fi.ready, fi.total, intros);
  }
  {  // rumble patterns: right, wrong, learned and unlock feel different on one motor
    using snd::Sfx; using snd::rumblePattern;
    for (int lv : {1, 2}) {
      auto r = rumblePattern(Sfx::Right, lv), w = rumblePattern(Sfx::Wrong, lv), k = rumblePattern(Sfx::Known, lv), u = rumblePattern(Sfx::Unlock, lv);
      CHECK(r.size() == 1 && w.size() == 2 && k.size() == 3 && u.size() == 1);
      for (auto* pat : {&r, &w, &k, &u}) {
        int end = -1;
        for (auto& p : *pat) {
          CHECK(p.strength >= snd::RUMBLE_FLOOR && p.strength <= 0xFFFF && p.ms >= snd::RUMBLE_MIN_MS);
          CHECK(p.at >= end + 60 || end < 0);           // a clear gap between pulses
          end = p.at + p.ms;
        }
      }
      CHECK(u[0].ms > 2 * r[0].ms && w[0].strength > r[0].strength);
    }
    CHECK(rumblePattern(Sfx::Right, 0).empty() && rumblePattern(Sfx::Tap, 2).empty());
    CHECK(rumblePattern(Sfx::Unlock, 2)[0].ms > rumblePattern(Sfx::Unlock, 1)[0].ms);
    printf("rumble: patterns ok\n");
  }
  {  // spaced review for words, like kana
    Progress p; p.clock = 1800000000; p.skipTo(d, Script::Hiragana, SKIP_ALL);
    int wi = -1; for (size_t i = 0; i < d.words.size(); i++) if (d.words[i].kana == "ねこ") wi = int(i);
    std::string key = Progress::keyWord(d.words[wi]);
    for (int s2 = 0; s2 < 2; s2++) { p.session++; p.clock += GAIN_GAP; p.record(key, true); p.record(key, true); }
    CHECK(p.step(key) == KNOWN_STEP && p.stats[key].srs == 1);
    CHECK(p.dueWords(d).empty());
    p.clock += 86400 * 2; CHECK(p.isDue(key));
    int dueW = int(p.dueWords(d).size()); CHECK(dueW >= 1 && p.dueCount(d, Script::Hiragana) >= dueW);
    for (auto& [k, st] : p.stats) if (k[0] != 'w' && st.srs) st.due = p.clock + 86400 * 30;  // only the word is due
    std::mt19937 rng(5); bool wordAsked = false;
    for (int n = 0; n < 20; n++) { Question q = makeReviewQuestion(d, p, rng); if (q.key == key) { wordAsked = true; CHECK(q.isWord && q.hint.rfind("Review", 0) == 0); } }
    CHECK(wordAsked);
    p.session++; p.record(key, true); CHECK(p.stats[key].srs == 2 && !p.isDue(key));
    p.clock += 86400 * 4; p.session++; p.record(key, false); CHECK(p.stats[key].srs == 1 && p.step(key) == KNOWN_STEP - 1);
    printf("word reviews: ok\n");
  }
  {  // achievements
    Progress p; p.clock = 1800000000;
    CHECK(p.checkAchievements(d).empty() && achievementList().size() >= 15);
    std::set<std::string> ids; for (auto& a : achievementList()) ids.insert(a.id); CHECK(ids.size() == achievementList().size());
    p.skipTo(d, Script::Hiragana, SKIP_ALL);
    auto got = p.checkAchievements(d); std::set<std::string> g; for (int i : got) g.insert(achievementList()[i].id);
    CHECK(g.count("first_kana") && g.count("hira_half") && g.count("hira_all") && g.count("kata_open") && !g.count("kata_all"));
    CHECK(p.checkAchievements(d).empty());                 // each one only once
    CHECK(!g.count("perfect") && p.award("perfect") && !p.award("perfect"));
    int ki = -1; for (int i = 0; i < int(achievementList().size()); i++) if (achievementList()[i].id == "words10") ki = i;
    AchProgress ap = achievementProgress(d, p, ki); CHECK(ap.cur == 0 && ap.target == 10);
    p.timedBest[1] = 25; got = p.checkAchievements(d); CHECK(got.size() == 1 && achievementList()[got[0]].id == "timed20");
    std::string path = "/tmp/kt_ach_test.txt"; CHECK(p.save(path));
    Progress q; CHECK(q.load(path, &d)); CHECK(q.achieved == p.achieved && q.checkAchievements(d).empty());
    printf("achievements: %zu earned in the test\n", p.achieved.size());
  }
  {  // Kitsu: hatches on the first goal-met day, grows with days practised (never shrinks), mood from today's goal
    Progress p; p.settings.dailyGoal = 5; Time t0 = 1790000000; p.clock = t0;
    CHECK(p.petStage() == 0 && p.pet.name == "Kitsu" && !p.petAwake());
    for (int i = 0; i < 4; i++) p.countAnswer(true, t0);
    CHECK(p.petStage() == 0 && p.pet.days == 1);  // practised, egg still
    p.countAnswer(true, t0); CHECK(p.petHatched() && p.petStage() == 1 && p.petAwake());
    for (int day = 1; day < 7; day++) p.countAnswer(true, t0 + day * 86400);  // one answer a day: goal not met, still counts
    CHECK(p.pet.days == 7 && p.petStage() == 2);
    p.clock = t0 + 7 * 86400; CHECK(!p.petAwake());                            // asleep until today's goal
    for (int day = 7; day < 100; day++) p.countAnswer(true, t0 + day * 86400);
    CHECK(p.pet.days == 100 && p.petStage() == 4 && PET_TAILS[p.petStage()] == 9);
    p.streak = 0; CHECK(p.petStage() == 4);                                    // a broken streak never costs a tail
    CHECK(!p.petAccUnlocked(1)); p.award("perfect"); CHECK(p.petAccUnlocked(1) && petAccessories().size() == 8);
    for (auto& a : petAccessories()) { bool found = false; for (auto& x : achievementList()) found |= x.id == a.achievement; CHECK(found); }
    p.pet.name = "こはく"; p.pet.acc = 1; p.pet.seenStage = 4;
    std::string path = "/tmp/kt_pet_test.txt"; CHECK(p.save(path));
    Progress q; CHECK(q.load(path, &d)); CHECK(q.pet.name == "こはく" && q.pet.acc == 1 && q.pet.days == 100 && q.pet.seenStage == 4 && q.petLoaded);
    // an older file (no pet line): Kitsu arrives grown for the days in the history, already "seen" (no celebration)
    std::ofstream(path) << "set version 2\nset last_goal_day 20260101\nday 20260101 12 10\nday 20260102 3 3\nday 20260103 0 0\n";
    Progress r; CHECK(r.load(path, &d)); CHECK(!r.petLoaded && r.pet.days == 2 && r.petStage() == 1 && r.pet.seenStage == 1);
    // history keeps only 90 days: a 120-day best streak still proves 120 days of practice (nine tails)
    std::ofstream(path) << "set version 2\nset best_streak 120\nset last_goal_day 20260101\nday 20260101 12 10\n";
    Progress m; CHECK(m.load(path, &d)); CHECK(m.pet.days == 120 && m.petStage() == 4 && m.pet.seenStage == 4);
  }
  printf(fails ? "%d FAILURES\n" : "all core tests passed\n", fails); return fails != 0;
}
