#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include "../src/core.h"
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

  // ---- pass rule: +1 per right answer, max 2 per session, -1 when wrong, known at 4
  {
    Progress p; p.session = 1;
    p.record("h:あ", true); p.record("h:あ", true); p.record("h:あ", true);
    CHECK(p.step("h:あ") == 2);                     // capped at 2 this session
    p.session = 2; p.record("h:あ", true); CHECK(p.step("h:あ") == 3);
    p.record("h:あ", false); CHECK(p.step("h:あ") == 2);  // wrong: back one step, not a reset
    p.record("h:あ", true); CHECK(p.step("h:あ") == 3);   // second gain this session
    p.record("h:あ", true); CHECK(p.step("h:あ") == 3);   // cap
    p.session = 3; p.record("h:あ", true); p.record("h:あ", true); CHECK(p.step("h:あ") == KNOWN_STEP);
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
    for (int sess = 0; sess < 2; sess++) { p.session = sess; p.record(k, true); p.record(k, true); }
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
  printf(fails ? "%d FAILURES\n" : "all core tests passed\n", fails); return fails != 0;
}
