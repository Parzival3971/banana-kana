#include <cassert>
#include <cstdio>
#include <set>
#include "../src/core.h"
using namespace kt;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(int argc, char** argv) {
  int fails = 0; std::string dir = argc > 1 ? argv[1] : "app/data", err;
  Data d; CHECK(d.load(dir, &err)); if (!err.empty()) printf("err: %s\n", err.c_str());
  int h = 0, k = 0; for (auto& x : d.kana) (x.script == Script::Hiragana ? h : k)++;
  CHECK(h == 104 && k == 104); CHECK(d.words.size() == 87);
  // stroke data: あ has 3 strokes, し has 1
  CHECK(d.glyphFor("あ") && d.glyphFor("あ")->strokes.size() == 3);
  CHECK(d.glyphFor("し") && d.glyphFor("し")->strokes.size() == 1);
  CHECK(d.glyphFor("きゃ") == nullptr);  // yoon pairs have no single glyph
  // every single-codepoint kana has stroke data
  for (auto& x : d.kana) if (utf8Codepoints(x.glyph).size() == 1) CHECK(d.glyphFor(x.glyph) != nullptr);
  // scoring
  const Stroke& ref = d.glyphFor("あ")->strokes[0];
  CHECK(scoreStroke(ref, ref).ok);
  Stroke rev(ref.rbegin(), ref.rend()); CHECK(!scoreStroke(rev, ref).ok);
  Stroke near = ref; for (auto& p : near) { p.x += 7; p.y += 5; } CHECK(scoreStroke(near, ref).ok);
  Stroke far = ref; for (auto& p : far) { p.x += 45; } CHECK(!scoreStroke(far, ref).ok);
  CHECK(!scoreStroke({{10, 10}, {11, 10}}, ref).ok);  // tap
  // typing normalisation
  CHECK(typedMatches("koohii", "koohii")); CHECK(typedMatches("kohi", "koohii")); CHECK(typedMatches("Gyuunyuu", "gyuunyuu"));
  CHECK(typedMatches("gyunyu", "gyuunyuu")); CHECK(!typedMatches("", "a")); CHECK(!typedMatches("ka", "ki"));
  // question generation across settings
  std::mt19937 rng(42); Progress p;
  for (int sc = 0; sc < 2; sc++) {
    p.settings.script = sc ? Script::Katakana : Script::Hiragana;
    p.settings.meanings = 2; p.settings.writing = 2; p.settings.typing = 2; p.settings.reading = 2;
    std::set<int> types;
    for (int i = 0; i < 2000; i++) {
      Question q = makeQuestion(d, p, rng); types.insert(int(q.type));
      CHECK(!q.key.empty()); CHECK(!q.prompt.empty());
      if (q.type == QType::Write || q.type == QType::Type) CHECK(q.options.empty() && !q.answer.empty());
      else {
        CHECK(q.options.size() == 6); CHECK(q.correct >= 0 && q.correct < 6);
        std::set<std::string> u(q.options.begin(), q.options.end()); CHECK(u.size() == 6);
        CHECK(q.options[q.correct] == q.answer);
      }
      if (q.type == QType::Write) CHECK(d.glyphFor(q.answer) != nullptr);
      p.record(q.key, i % 3 != 0);
    }
    CHECK(types.size() == 5);
  }
  // weak spots get asked more
  Progress w; w.settings.reading = 2; w.settings.writing = 0; w.settings.typing = 0; w.settings.kanaPct = 100;
  for (auto& x : d.kana) if (x.script == Script::Hiragana) { for (int i = 0; i < 5; i++) w.record(Progress::keyKana(x), true); }
  for (int i = 0; i < 6; i++) w.record("h:あ", false);
  int hits = 0; for (int i = 0; i < 1000; i++) { Question q = makeQuestionOfType(d, w, rng, QType::ReadKana); if (q.key == "h:あ") hits++; }
  printf("weak-spot hits for あ: %d/1000 (uniform would be ~10)\n", hits); CHECK(hits > 60);
  // save/load roundtrip
  Progress a; a.settings.kanaPct = 30; a.settings.script = Script::Katakana; a.record("h:あ", true); a.record("h:あ", false);
  CHECK(a.save("/tmp/kt_prog.txt")); Progress b; CHECK(b.load("/tmp/kt_prog.txt"));
  CHECK(b.settings.kanaPct == 30 && b.settings.script == Script::Katakana && b.stats["h:あ"].seen == 2 && b.stats["h:あ"].wrong == 1);
  printf(fails ? "%d FAILURES\n" : "all core tests passed\n", fails); return fails != 0;
}
