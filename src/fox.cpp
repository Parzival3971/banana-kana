// Kitsu, the streak pet. See fox.h. Coordinates below are "fox units": the fox stands on (0, 0) and its ears reach
// about y = -196; drawFox() scales them to the requested height. Shapes follow the approved mockup (2026-11-16).
#include "fox.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace kt {
namespace {

struct P { float x, y; };
const float PI = 3.14159265f;

Col rgb(unsigned h, Uint8 a = 255) { return Col{Uint8(h >> 16), Uint8(h >> 8), Uint8(h), a}; }

struct Shade { Col light, mid, dark; };
const Shade FUR{rgb(0xFFC27A), rgb(0xF59231), rgb(0xC45A12)};
const Shade TAIL{rgb(0xFFB867), rgb(0xEE8222), rgb(0xB24F0E)};
const Shade CREAM{rgb(0xFFFFFF), rgb(0xFFF5E8), rgb(0xF0D2AE)};
const Shade EYE{rgb(0x7A5340), rgb(0x3A2418), rgb(0x140B07)};
const Shade EAR{rgb(0xFFD8D4), rgb(0xF6AFAB), rgb(0xE07F80)};
const Shade GLOWTIP{rgb(0xFFFDF0), rgb(0xFFF1B8), rgb(0xFFD97A)};
const Shade RED{rgb(0xFF8A7A), rgb(0xE0453A), rgb(0xA8221A)};
const Shade BLUE{rgb(0x9CCBFF), rgb(0x3F86E8), rgb(0x1F55B0)};
const Shade GOLD{rgb(0xFFF1A8), rgb(0xF2C230), rgb(0xB8860B)};
const Shade PINK{rgb(0xFFE3EC), rgb(0xF7B3C8), rgb(0xE07FA0)};
const Shade WHITE{rgb(0xFFFFFF), rgb(0xF4F6F8), rgb(0xC9D2DC)};

struct Geo {
  std::vector<SDL_Vertex> v;
  float cx, by, s;
  SDL_FPoint at(P p) const { return {cx + p.x * s, by + p.y * s}; }
  void tri(P a, Col ca, P b, Col cb, P c, Col cc) {
    v.push_back({at(a), {ca.r, ca.g, ca.b, ca.a}, {0, 0}});
    v.push_back({at(b), {cb.r, cb.g, cb.b, cb.a}, {0, 0}});
    v.push_back({at(c), {cc.r, cc.g, cc.b, cc.a}, {0, 0}});
  }
  // A shaded shape: light at a point nudged towards the top-left, the base colour two thirds of the way out and a
  // darker rim. The outline must be star-shaped around its centre (all of ours are).
  void shaded(const std::vector<P>& ring, const Shade& sh, float lightX = -0.25f, float lightY = -0.3f) {
    if (ring.size() < 3) return;
    P c{0, 0}; for (auto& p : ring) { c.x += p.x; c.y += p.y; } c.x /= ring.size(); c.y /= ring.size();
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (auto& p : ring) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    P l{c.x + lightX * (maxx - minx) * 0.5f, c.y + lightY * (maxy - miny) * 0.5f};
    size_t n = ring.size();
    for (size_t i = 0; i < n; i++) {
      P a = ring[i], b = ring[(i + 1) % n];
      P ma{l.x + (a.x - l.x) * 0.62f, l.y + (a.y - l.y) * 0.62f}, mb{l.x + (b.x - l.x) * 0.62f, l.y + (b.y - l.y) * 0.62f};
      tri(l, sh.light, ma, sh.mid, mb, sh.mid);
      tri(ma, sh.mid, a, sh.dark, b, sh.dark);
      tri(ma, sh.mid, b, sh.dark, mb, sh.mid);
    }
  }
  void flat(const std::vector<P>& ring, Col c) {
    if (ring.size() < 3) return;
    P m{0, 0}; for (auto& p : ring) { m.x += p.x; m.y += p.y; } m.x /= ring.size(); m.y /= ring.size();
    for (size_t i = 0; i < ring.size(); i++) tri(m, c, ring[i], c, ring[(i + 1) % ring.size()], c);
  }
  void glow(P c, float r, Col inner) {  // a soft disc fading to transparent
    Col out = inner; out.a = 0;
    for (int i = 0; i < 20; i++) {
      float a0 = i * 2 * PI / 20, a1 = (i + 1) * 2 * PI / 20;
      tri(c, inner, {c.x + r * std::cos(a0), c.y + r * std::sin(a0)}, out, {c.x + r * std::cos(a1), c.y + r * std::sin(a1)}, out);
    }
  }
  void stroke(const std::vector<P>& pts, float wdt, Col c) {  // a round-ish polyline: quads plus small joints
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      P a = pts[i], b = pts[i + 1]; float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-3f) continue;
      float nx = -dy / len * wdt / 2, ny = dx / len * wdt / 2;
      tri({a.x + nx, a.y + ny}, c, {b.x + nx, b.y + ny}, c, {b.x - nx, b.y - ny}, c);
      tri({a.x + nx, a.y + ny}, c, {b.x - nx, b.y - ny}, c, {a.x - nx, a.y - ny}, c);
    }
  }
};

std::vector<P> ellipse(float cx, float cy, float rx, float ry, int n = 22, float rotDeg = 0) {
  std::vector<P> r; float ca = std::cos(rotDeg * PI / 180), sa = std::sin(rotDeg * PI / 180);
  for (int i = 0; i < n; i++) { float a = i * 2 * PI / n, x = rx * std::cos(a), y = ry * std::sin(a); r.push_back({cx + x * ca - y * sa, cy + x * sa + y * ca}); }
  return r;
}
void cubic(std::vector<P>& out, P a, P b, P c, P d, int n) {  // appends n points (excluding a)
  for (int i = 1; i <= n; i++) {
    float t = float(i) / n, u = 1 - t;
    out.push_back({u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                   u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
  }
}
void quad(std::vector<P>& out, P a, P b, P c, int n) {
  for (int i = 1; i <= n; i++) { float t = float(i) / n, u = 1 - t; out.push_back({u * u * a.x + 2 * u * t * b.x + t * t * c.x, u * u * a.y + 2 * u * t * b.y + t * t * c.y}); }
}
std::vector<P> moved(std::vector<P> pts, float rotDeg, P off, float sc = 1.f) {
  float ca = std::cos(rotDeg * PI / 180), sa = std::sin(rotDeg * PI / 180);
  for (auto& p : pts) { float x = p.x * sc, y = p.y * sc; p = {off.x + x * ca - y * sa, off.y + x * sa + y * ca}; }
  return pts;
}

// tail: a teardrop from the base (0,0) up to the tip (4,-98), with a cream tip
std::vector<P> tailShape() {
  std::vector<P> p{{0, 0}};
  cubic(p, {0, 0}, {-28, -20}, {-28, -72}, {4, -98}, 9);
  cubic(p, {4, -98}, {36, -74}, {32, -22}, {0, 0}, 9);
  p.pop_back();
  return p;
}
std::vector<P> tailTip() {
  std::vector<P> p{{4, -98}};
  cubic(p, {4, -98}, {-7, -88}, {-11, -76}, {-7, -66}, 5);
  cubic(p, {-7, -66}, {6, -68}, {17, -76}, {21, -83}, 5);
  cubic(p, {21, -83}, {17, -90}, {11, -95}, {4, -98}, 4);
  p.pop_back();
  return p;
}

}  // namespace

int drawFox(Win& w, float cx, float baseY, float height, const FoxLook& L) {
  static const float GROWTH[5] = {0.8f, 0.8f, 0.9f, 0.96f, 1.f};  // the kit is small and grows up
  Geo g{{}, cx, baseY, height / 200.f * GROWTH[std::clamp(L.stage, 0, 4)]};
  const float t = L.t;
  const bool asleep = L.mood == FoxMood::Asleep, happy = L.mood == FoxMood::Happy;

  if (L.stage >= 4) g.glow({0, -110}, 150, rgb(0xFFE7A0, 120));  // the kyubi's golden aura
  { // soft ground shadow
    Col in{0, 0, 0, 70}, out{0, 0, 0, 0}; auto e = ellipse(0, 2, 62, 12, 20);
    for (size_t i = 0; i < e.size(); i++) g.tri({0, 2}, in, e[i], out, e[(i + 1) % e.size()], out);
  }

  if (L.stage <= 0) {  // the egg: cream with orange spots, a zigzag band, and a little wobble
    float wob = t > 0 ? std::sin(t * 3.f) * 4.f * (std::fmod(t, 5.f) < 1.2f ? 1.f : 0.15f) : 0.f;
    auto R = [&](std::vector<P> s) { return moved(s, wob, {0, 0}); };
    g.shaded(R(ellipse(0, -64, 48, 62, 26)), CREAM);
    g.shaded(R(ellipse(-16, -84, 11, 10, 14)), FUR); g.shaded(R(ellipse(18, -56, 14, 12, 14)), FUR); g.shaded(R(ellipse(-10, -30, 8, 7, 12)), FUR);
    std::vector<P> zig; for (int i = 0; i <= 8; i++) zig.push_back({-46.f + i * 11.5f, -66.f + (i % 2 ? -7.f : 0.f)});
    g.stroke(R(zig), 3.f, rgb(0xC45A12));
    g.shaded(R(ellipse(-18, -102, 12, 6, 12, -25)), WHITE);  // shine
    SDL_SetRenderDrawBlendMode(w.r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(w.r, nullptr, g.v.data(), int(g.v.size()), nullptr, 0);
    SDL_SetRenderDrawBlendMode(w.r, SDL_BLENDMODE_NONE);
    return int(g.v.size() / 3);
  }

  // ---- tails (behind everything): fanned out awake, curled round the front when asleep
  static const float FAN[5][9] = {{0}, {58}, {-42, 4, 48}, {-70, -35, 0, 35, 70}, {-88, -66, -44, -22, 0, 22, 44, 66, 88}};
  const int nT = PET_TAILS[std::clamp(L.stage, 0, 4)];
  const auto tail = tailShape(), tip = tailTip();
  auto drawTails = [&](bool front) {
    for (int i = 0; i < nT; i++) {
      float a = FAN[L.stage][i] + L.tailTurn, sc = L.tailScale;
      P base{std::clamp(a / 6.f, -16.f, 16.f), -18.f};
      if (asleep) {  // curled round the front like a sleeping cat: low, smaller, overlapping the feet
        int side = (i % 2) ? -1 : 1;
        a = side * (76.f - (i / 2) * 9.f); sc = 0.7f - (i / 2) * 0.05f; base = {-side * 22.f, -2.f - (i / 2) * 4.f};
      }
      a += t > 0 ? std::sin(t * 1.6f + i * 0.7f) * (asleep ? 1.5f : 4.f) : 0.f;
      if (asleep != front) continue;
      g.shaded(moved(tail, a, base, sc), TAIL, -0.35f, -0.2f);
      g.shaded(moved(tip, a, base, sc), L.stage >= 3 ? GLOWTIP : CREAM);
      if (L.stage >= 3) { auto tp = moved({{6, -84}}, a, base, sc)[0]; g.glow(tp, 18 * sc, rgb(0xFFF1B8, 150)); }
    }
  };
  drawTails(false);

  // ---- body, chest, feet (the body breathes)
  const float breathe = t > 0 ? std::sin(t * 2.f) : 0.f;
  g.shaded(ellipse(0, -42, 34, 38 + breathe * 0.8f, 24), FUR);
  { std::vector<P> chest{{-17, -70}}; quad(chest, {-17, -70}, {-6, -60}, {0, -70}, 4); quad(chest, {0, -70}, {6, -60}, {17, -70}, 4);
    quad(chest, {17, -70}, {22, -44}, {0, -22}, 5); quad(chest, {0, -22}, {-22, -44}, {-17, -70}, 5); chest.pop_back();
    g.shaded(chest, CREAM); }
  for (int sx : {-1, 1}) { g.shaded(ellipse(16.f * sx, -6, 15, 9, 16), FUR); g.shaded(ellipse(16.f * sx, -4, 9, 4, 12), CREAM); }
  drawTails(true);  // asleep: tails wrapped in front of the feet

  // ---- head (bobs gently): ears, cheek tufts, head, muzzle, face
  const float hb = breathe * 1.5f;
  auto H = [&](std::vector<P> s) { for (auto& p : s) p.y += hb; return s; };
  for (int sx : {-1, 1}) {
    float f = float(sx);
    // ears are rooted inside the head outline (they used to float above it)
    std::vector<P> ear{{44 * f, -128}}; quad(ear, {44 * f, -128}, {58 * f, -192}, {8 * f, -150}, 10);
    g.shaded(H(ear), FUR);
    std::vector<P> inner{{38 * f, -140}}; quad(inner, {38 * f, -140}, {50 * f, -182}, {16 * f, -154}, 8);
    g.shaded(H(inner), EAR);
    g.shaded(H({{46 * f, -104}, {57 * f, -92}, {44 * f, -90}, {53 * f, -77}, {37 * f, -84}}), FUR);  // cheek tuft
  }
  g.shaded(H(ellipse(0, -114, 48, 42, 28)), FUR);
  g.shaded(H(ellipse(0, -94, 28, 19, 20)), CREAM);
  const bool blink = t > 0 && std::fmod(t, 4.2f) < 0.14f;
  for (int sx : {-1, 1}) {
    float f = float(sx);
    if (asleep || blink) {        // closed: a gentle downward curve
      std::vector<P> c; quad(c, {f * 19 - 9, -118}, {f * 19, -112}, {f * 19 + 9, -118}, 6); c.insert(c.begin(), {f * 19 - 9, -118});
      g.stroke(H(c), 2.6f, rgb(0x3A2418));
    } else if (happy) {           // happy: upturned arcs ^ ^
      std::vector<P> c; quad(c, {f * 19 - 9, -114}, {f * 19, -126}, {f * 19 + 9, -114}, 6); c.insert(c.begin(), {f * 19 - 9, -114});
      g.stroke(H(c), 3.f, rgb(0x3A2418));
    } else {                      // big glossy eyes with two highlights
      g.shaded(H(ellipse(f * 19, -118, 10, 13, 18)), EYE, 0.f, 0.45f);
      g.flat(H(ellipse(f * 19 - 3, -123, 4.5f, 4.5f, 10)), rgb(0xFFFFFF));
      g.flat(H(ellipse(f * 19 + 4, -111, 2, 2, 8)), rgb(0xFFFFFF, 215));
    }
    g.flat(H(ellipse(f * 32, -100, 8, 4.5f, 12)), rgb(0xFF8A8A, 140));  // blush
  }
  g.shaded(H(ellipse(0, -103, 5.5f, 3.8f, 12)), Shade{rgb(0x6A4636), rgb(0x2A1B14), rgb(0x150D09)});
  g.flat(H(ellipse(-1.5f, -104.5f, 2, 1, 8)), rgb(0xFFFFFF, 180));
  if (happy) g.shaded(H(ellipse(0, -92, 5, 4, 12)), EAR);
  else { std::vector<P> m{{-7, -95}}; quad(m, {-7, -95}, {-3.5f, -90}, {0, -95}, 4); quad(m, {0, -95}, {3.5f, -90}, {7, -95}, 4); g.stroke(H(m), 2.f, rgb(0x5A3626)); }
  g.flat(H(ellipse(-16, -140, 18, 7, 14, -18)), rgb(0xFFFFFF, 80));    // head shine
  g.flat(ellipse(-20, -58, 7, 12, 12), rgb(0xFFFFFF, 45));             // body shine

  // ---- scarf from 3 tails on (blue knit in winter)
  if (L.stage >= 2 || L.season == 3) {
    const Shade& sc = L.season == 3 ? BLUE : RED;
    std::vector<P> band{{-34, -74}}; quad(band, {-34, -74}, {0, -56}, {34, -74}, 10);
    band.push_back({33, -60}); quad(band, {33, -60}, {0, -42}, {-33, -60}, 10);
    g.shaded(H(band), sc);
    g.shaded(H({{14, -56}, {28, -24}, {20, -22}, {14, -26}}), sc);
    if (L.season == 3) { std::vector<P> st; quad(st, {-28, -68}, {0, -54}, {28, -68}, 10); for (size_t i = 0; i + 1 < st.size(); i += 2) g.stroke(H({st[i], st[i + 1]}), 2.f, rgb(0xFFFFFF, 200)); }
  }

  // ---- seasonal touch (follows the Background season)
  if (L.season == 0) {  // a sakura blossom behind the right ear
    for (int k = 0; k < 5; k++) { float a = k * 72 * PI / 180 - PI / 2; g.shaded(H(ellipse(26 + 7 * std::cos(a), -150 + 7 * std::sin(a), 6, 6, 10)), PINK); }
    g.flat(H(ellipse(26, -150, 3, 3, 8)), rgb(0xE0453A));
  } else if (L.season == 1) {  // an uchiwa fan held at the side
    g.stroke({{40, -30}, {48, -64}}, 4.f, rgb(0x8A5A2A));
    g.shaded(ellipse(52, -82, 20, 20, 20), Shade{rgb(0xE6F5FF), rgb(0x8FCBF5), rgb(0x3C8EEA)});
    std::vector<P> wv; quad(wv, {38, -82}, {52, -94}, {66, -82}, 8); wv.insert(wv.begin(), {38, -82}); g.stroke(wv, 2.f, rgb(0xFFFFFF));
  } else if (L.season == 2) {  // a momiji leaf on the head
    std::vector<P> leaf; const float ra = -15 * PI / 180, ca = std::cos(ra), sa = std::sin(ra);
    for (auto q : mapleLeaf()) leaf.push_back({-20 + 18 * (q.x * ca - q.y * sa), -156 + 18 * (q.x * sa + q.y * ca)});
    g.stroke(H({{-20 - 18 * 0.75f * sa, -156 + 18 * 0.75f * ca}, {-20, -156}}), 2.f, rgb(0xA8221A));
    g.shaded(H(leaf), RED);
  }

  // ---- accessory (achievement rewards)
  switch (L.accessory) {
    case 0:  // suzu bell on a red cord
      g.stroke(H({{-30, -72}, {0, -64}, {30, -72}}), 4.f, rgb(0xC8322A));
      g.shaded(H(ellipse(0, -60, 8, 8, 14)), GOLD); g.stroke(H({{-5, -58}, {5, -58}}), 1.6f, rgb(0x8A5A0A)); break;
    case 1: {  // hachimaki: a white headband with a red sun, tails flying
      std::vector<P> b{{-47, -134}}; quad(b, {-47, -134}, {0, -150}, {47, -134}, 10); b.push_back({47, -124}); quad(b, {47, -124}, {0, -140}, {-47, -124}, 10);
      g.shaded(H(b), WHITE); g.shaded(H(ellipse(0, -137, 6, 6, 12)), RED);
      g.shaded(H({{44, -132}, {66, -126}, {62, -136}}), WHITE); g.shaded(H({{44, -128}, {62, -114}, {64, -124}}), WHITE); break; }
    case 2:  // omamori charm hanging at the side
      g.stroke({{24, -64}, {30, -50}}, 2.f, rgb(0xF2C230));
      g.shaded({{24, -50}, {38, -50}, {38, -26}, {31, -22}, {24, -26}}, RED); g.flat({{27, -46}, {35, -46}, {35, -40}, {27, -40}}, rgb(0xF2C230)); break;
    case 3:  // chochin lantern beside the fox, glowing
      g.glow({-62, -52}, 30, rgb(0xFFD86B, 120));
      g.flat({{-72, -76}, {-52, -76}, {-52, -71}, {-72, -71}}, rgb(0x2A1B14)); g.flat({{-72, -33}, {-52, -33}, {-52, -28}, {-72, -28}}, rgb(0x2A1B14));
      g.shaded(ellipse(-62, -52, 15, 20, 18), RED);
      for (int k = -1; k <= 1; k++) g.stroke({{-74.f, -52.f + k * 8}, {-50.f, -52.f + k * 8}}, 1.2f, rgb(0xA8221A));
      break;
    case 4:  // a little fox mask worn at the side of the head
      g.shaded(H(ellipse(34, -146, 13, 15, 16, 20)), WHITE);
      g.flat(H({{26, -158}, {30, -168}, {34, -158}}), rgb(0xFFFFFF)); g.flat(H({{38, -156}, {44, -165}, {44, -154}}), rgb(0xFFFFFF));
      g.stroke(H({{28, -146}, {33, -149}}), 1.6f, rgb(0xE0453A)); g.stroke(H({{37, -142}, {42, -144}}), 1.6f, rgb(0xE0453A)); break;
    case 5:  // a scroll tucked under the arm
      g.shaded({{-50, -40}, {-12, -54}, {-8, -44}, {-46, -30}}, CREAM);
      g.shaded(ellipse(-50, -35, 5, 6, 10, -20), Shade{rgb(0xC08A50), rgb(0x8A5A2A), rgb(0x5A3A1A)});
      g.shaded(ellipse(-10, -49, 5, 6, 10, -20), Shade{rgb(0xC08A50), rgb(0x8A5A2A), rgb(0x5A3A1A)}); break;
    case 6:  // kanzashi hairpin with a flower by the left ear
      g.stroke(H({{-40, -128}, {-18, -152}}), 2.2f, rgb(0x8A5A2A));
      for (int k = 0; k < 5; k++) { float a = k * 72 * PI / 180; g.shaded(H(ellipse(-38 + 6 * std::cos(a), -132 + 6 * std::sin(a), 5, 5, 10)), PINK); }
      g.flat(H(ellipse(-38, -132, 2.5f, 2.5f, 8)), rgb(0xF2C230));
      g.stroke(H({{-36, -126}, {-34, -114}}), 1.4f, rgb(0xF2C230)); g.flat(H(ellipse(-34, -112, 2.5f, 2.5f, 8)), rgb(0xF2C230)); break;
    case 7:  // a daruma doll sitting beside the fox
      g.shaded(ellipse(52, -18, 15, 18, 18), RED);
      g.shaded(ellipse(52, -22, 9, 8, 14), WHITE);
      g.flat(ellipse(48, -23, 2, 2, 8), rgb(0x1A1A1A)); g.flat(ellipse(56, -23, 2, 2, 8), rgb(0x1A1A1A)); break;
    default: break;
  }

  // ---- kyubi fox-fire: three small blue flames circling slowly
  if (L.stage >= 4) {
    for (int k = 0; k < 3; k++) {
      float a = t * 0.6f + k * 2.094f, fx = std::cos(a) * 112, fy = -110 + std::sin(a) * 70, sz = 1.f + 0.12f * std::sin(t * 4 + k);
      g.glow({fx, fy - 10}, 22, rgb(0x8FD3FF, 110));
      std::vector<P> fl{{fx, fy}}; cubic(fl, {fx, fy}, {fx - 10 * sz, fy - 8 * sz}, {fx - 9 * sz, fy - 24 * sz}, {fx, fy - 34 * sz}, 6);
      cubic(fl, {fx, fy - 34 * sz}, {fx + 9 * sz, fy - 24 * sz}, {fx + 10 * sz, fy - 8 * sz}, {fx, fy}, 6); fl.pop_back();
      g.shaded(fl, Shade{rgb(0xF2FBFF), rgb(0x8FD3FF), rgb(0x3C8EEA)}, 0.f, 0.4f);
    }
  }

  SDL_SetRenderDrawBlendMode(w.r, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(w.r, nullptr, g.v.data(), int(g.v.size()), nullptr, 0);
  SDL_SetRenderDrawBlendMode(w.r, SDL_BLENDMODE_NONE);
  return int(g.v.size() / 3);
}

}  // namespace kt
