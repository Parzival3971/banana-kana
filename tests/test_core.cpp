#include <cstdio>
#include <fstream>
#include <set>
#include "../src/core.h"
using namespace kt;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void knowStage(const Data& d, Progress& p, Script s, int stage) {  // make every kana in a stage known
  for (int i : d.stages[si(s)][stage].kana) p.stats[Progress::keyKana(d.kana[i])].step = KNOWN_STEP;
}

int main(int argc, char** argv) {
  int fails = 0; std::string dir = argc > 1 ? argv[1] : "app/data", err;
  Data d; CHECK(d.load(dir, &err)); if (!err.empty()) printf("err: %s\n", err.c_str());
  int h = 0, k = 0; for (auto& x : d.kana) (x.script == Script::Hiragana ? h : k)++;
  CHECK(h == 104 && k == 104); CHECK(d.words.size() >= 120);
  CHECK(d.hints.size() == 208);
  for (auto& x : d.kana) CHECK(d.hints.count(x.glyph) == 1);

  // ---- stages: 14 per script, every kana in exactly one, sizes as designed
  for (Script s : {Script::Hiragana, Script::Katakana}) {
    CHECK(d.stages[si(s)].size() == STAGE_COUNT);
    size_t total = 0; for (auto& st : d.stages[si(s)]) total += st.kana.size();
    CHECK(total == 104);
    const int sizes[STAGE_COUNT] = {5, 5, 5, 5, 5, 5, 5, 3, 5, 3, 15, 10, 15, 18};
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
    CHECK(w1.size() >= 6); CHECK(types.size() >= 4);
    // everything unlocked: 6 options, all five types
    for (int s = 0; s < 13; s++) knowStage(d, p, Script::Hiragana, s);
    p.checkUnlocks(d); CHECK(p.unlocked[0] == STAGE_COUNT);
    types.clear();
    for (int n = 0; n < 2000; n++) {
      Question q = makeQuestion(d, p, rng); types.insert(int(q.type));
      if (!q.options.empty()) CHECK(q.options.size() == 6);
      if (q.type == QType::Write) CHECK(d.glyphFor(q.answer) != nullptr);
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
  printf(fails ? "%d FAILURES\n" : "all core tests passed\n", fails); return fails != 0;
}
