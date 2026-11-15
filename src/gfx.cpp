#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kt {

Col mix(Col a, Col b, float t) {
  auto m = [t](Uint8 x, Uint8 y) { return Uint8(std::lround(x + (y - x) * t)); };
  return Col{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
}

// fields: bg panel panel2 line fg dim accent accentTxt onAccent good goodFill bad badFill canvas ghost ghostOk disLine disTxt track frame
const Theme LIGHT_THEME{hex(0xEEF1F4), hex(0xFFFFFF), hex(0xF1F3F6), hex(0xC3CAD2), hex(0x23262B), hex(0x5B6470),
  hex(0xF28C28), hex(0xB8560B), hex(0x3A1F05), hex(0x1E8E4E), hex(0xE7F6EC), hex(0xC43A3F), hex(0xFDE8E8),
  hex(0xFFFDF6), hex(0xE7E2D4), hex(0xCDEEDB), hex(0xC9CFD6), hex(0x8A929C), hex(0xE4E8ED), hex(0xDDE2E8)};
const Theme DARK_THEME{hex(0x1A1E25), hex(0x262C36), hex(0x323843), hex(0x4A5260), hex(0xEEF1F5), hex(0xA8B1BE),
  hex(0xF28C28), hex(0xFFA94D), hex(0x3A1F05), hex(0x4CD38A), hex(0x173524), hex(0xF07A7E), hex(0x3A1F22),
  hex(0x23272E), hex(0x3A414D), hex(0x2C4A3A), hex(0x3D4450), hex(0x737C89), hex(0x3A414D), hex(0x323843)};
Col BG, PANEL, PANEL2, LINE, FG, DIM, ACCENT, ACCENT_TXT, ON_ACCENT, GOOD, GOOD_FILL, BAD, BAD_FILL, CANVAS, GHOST, GHOST_OK,
    DIS_LINE, DIS_TXT, TRACK;
Col BG2, CARD_SH, PILL_LINE, PILL_SH, PILL_HI, PILL_LO, ACC_LINE, ACC_SH, ACC_HI, ACC_LO, PAPER_LINE, PAPER_MARGIN;
bool gLight = true;
namespace {
// per mode: fill, outline, shadow, gloss top, gloss bottom, text on light, text on dark, text on the fill
struct Acc { unsigned fill, line, sh, hi, lo, txtL, txtD, on; };
const Acc ACCS[MC_COUNT] = {
  {0xF28C28, 0xD9741C, 0xC96A14, 0xFFB15E, 0xE98221, 0xB8560B, 0xFFA94D, 0x3A1F05},  // orange: lessons, practice, home
  {0x2F80ED, 0x1F6FD6, 0x1B5FB8, 0x5AA2F5, 0x2A74D8, 0x1F6FD6, 0x7AB3FF, 0xFFFFFF},  // blue: review
  {0x34C26F, 0x1E8E4E, 0x1A7A42, 0x6FDC9C, 0x2BB062, 0x1E8E4E, 0x6FDC9C, 0x0D3B21},  // green: words
  {0x8B5CF6, 0x7A4BD1, 0x643BB3, 0xA985F0, 0x7F52E6, 0x6D3FD1, 0xB49CFF, 0xFFFFFF},  // purple: mix-ups
  {0xEB5257, 0xD64045, 0xB3363A, 0xF37A7E, 0xDE474C, 0xC4373C, 0xFF8A8E, 0xFFFFFF},  // red: timed
  {0x20B2AA, 0x0F8F8A, 0x127F79, 0x4FD1C5, 0x1AA39B, 0x0F7F7A, 0x4FD1C5, 0x062E2B},  // teal: stats, settings
};
}
Col modeCol(ModeCol m) { return hex(ACCS[m].fill); }

void setAccent(ModeCol m) {
  const Acc& a = ACCS[m];
  ACCENT = hex(a.fill); ACC_LINE = hex(a.line); ACC_SH = hex(a.sh); ACC_HI = hex(a.hi); ACC_LO = hex(a.lo);
  ACCENT_TXT = hex(gLight ? a.txtL : a.txtD); ON_ACCENT = hex(a.on);
}

void applyTheme(bool light) {
  const Theme& t = light ? LIGHT_THEME : DARK_THEME;
  BG = t.bg; PANEL = t.panel; PANEL2 = t.panel2; LINE = t.line; FG = t.fg; DIM = t.dim; ACCENT = t.accent; ACCENT_TXT = t.accentTxt;
  ON_ACCENT = t.onAccent; GOOD = t.good; GOOD_FILL = t.goodFill; BAD = t.bad; BAD_FILL = t.badFill; CANVAS = t.canvas;
  GHOST = t.ghost; GHOST_OK = t.ghostOk; DIS_LINE = t.disLine; DIS_TXT = t.disTxt; TRACK = t.track;
  gLight = light;
  BG2 = light ? hex(0xE8ECF0) : hex(0x14181E); CARD_SH = light ? hex(0xD3D9E0) : hex(0x0D1015);
  PILL_LINE = light ? hex(0xC3CAD2) : hex(0x4A5260); PILL_SH = light ? hex(0xB7BEC7) : hex(0x0D1015);
  PILL_HI = light ? hex(0xFFFFFF) : hex(0x3D4451); PILL_LO = light ? hex(0xECEFF3) : hex(0x2E343F);
  PAPER_LINE = light ? hex(0xCFE0F5) : hex(0x34404F); PAPER_MARGIN = light ? hex(0xF3B0B0) : hex(0x6A3B42);
  setAccent(MC_ORANGE);
}

// ------------------------------------------------------------------ fonts
namespace {
const int FONT_PX[F_COUNT] = {170, 100, 52, 30, 22, 17};
TTF_Font* kanaF[F_COUNT];  // Zen Maru Gothic: kana
TTF_Font* kanaHF[F_COUNT]; // Klee One: kana in a handwriting (textbook) style, optional
bool gKanaHand = false;
bool gBold = false;  // setBold(): Latin text in the bold UI font at any size
TTF_Font* uiF[F_COUNT];    // Zen Maru Gothic Medium (Latin): English / UI
TTF_Font* uiB[F_COUNT];    // Zen Maru Gothic Bold (Latin): headings, buttons, big text (F_HUGE..F_SMALL)
TTF_Font* fbF[F_COUNT];    // Noto subset: anything the others lack
float gFontScale = 1.f;

std::string utf8Encode(uint32_t cp) {
  std::string o;
  if (cp < 0x80) o += char(cp);
  else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  return o;
}
TTF_Font* pickFont(int size, uint32_t cp) {
  bool jp = (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
  if (jp && gKanaHand && kanaHF[size] && TTF_GlyphIsProvided32(kanaHF[size], cp)) return kanaHF[size];
  if (jp && TTF_GlyphIsProvided32(kanaF[size], cp)) return kanaF[size];
  if (!jp && (size <= F_SMALL || gBold) && uiB[size] && TTF_GlyphIsProvided32(uiB[size], cp)) return uiB[size];
  if (!jp && TTF_GlyphIsProvided32(uiF[size], cp)) return uiF[size];
  if (TTF_GlyphIsProvided32(kanaF[size], cp)) return kanaF[size];
  return fbF[size];
}
// Renders mixed kana/English text: each run uses the font that has its glyphs, aligned on a shared baseline.
SDL_Surface* renderMixed(int size, const std::string& s, SDL_Color col) {
  std::vector<std::pair<TTF_Font*, std::string>> runs;
  for (uint32_t cp : utf8Codepoints(s)) {
    TTF_Font* f = cp == ' ' ? ((size <= F_SMALL || gBold) && uiB[size] ? uiB[size] : uiF[size]) : pickFont(size, cp);  // spaces always from the UI font (kana spaces are full-width)
    if (runs.empty() || runs.back().first != f) runs.push_back({f, ""});
    runs.back().second += utf8Encode(cp);
  }
  std::vector<SDL_Surface*> parts; int w = 0, asc = 0, desc = 0;
  for (auto& [f, txt] : runs) {
    SDL_Surface* p = TTF_RenderUTF8_Blended(f, txt.c_str(), col); parts.push_back(p);
    if (!p) continue;
    w += p->w; asc = std::max(asc, TTF_FontAscent(f)); desc = std::max(desc, p->h - TTF_FontAscent(f));
  }
  if (w <= 0) { for (auto* p : parts) if (p) SDL_FreeSurface(p); return nullptr; }
  SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, w, asc + desc, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_FillRect(out, nullptr, 0);
  int x = 0;
  for (size_t i = 0; i < parts.size(); i++) {
    SDL_Surface* p = parts[i]; if (!p) continue;
    SDL_SetSurfaceBlendMode(p, SDL_BLENDMODE_NONE);
    SDL_Rect d{x, asc - TTF_FontAscent(runs[i].first), p->w, p->h}; SDL_BlitSurface(p, nullptr, out, &d);
    x += p->w; SDL_FreeSurface(p);
  }
  return out;
}

SDL_Texture* textTex(Win& w, int font, const std::string& s, Col c, int* tw, int* th) {
  char key[48]; snprintf(key, sizeof key, "%d|%d%d|%02x%02x%02x|", font, int(gKanaHand), int(gBold), c.r, c.g, c.b);
  std::string k = std::string(key) + s;
  auto it = w.cache.find(k);
  if (it == w.cache.end()) {
    if (w.cache.size() > 600) clearTextCache(w);
    SDL_Surface* sf = renderMixed(font, s, SDL_Color{c.r, c.g, c.b, 255});
    if (!sf) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(w.r, sf); SDL_FreeSurface(sf);
    it = w.cache.emplace(k, t).first;
  }
  int pw, ph; SDL_QueryTexture(it->second, nullptr, nullptr, &pw, &ph);
  *tw = int(std::lround(pw / gFontScale)); *th = int(std::lround(ph / gFontScale));
  return it->second;
}
}  // namespace

bool loadFonts(const std::string& dir, float scale, std::string* err) {
  gFontScale = std::max(0.5f, scale);
  for (int i = 0; i < F_COUNT; i++) {
    int px = std::max(6, int(std::lround(FONT_PX[i] * gFontScale)));
    kanaF[i] = TTF_OpenFont((dir + "/kana.ttf").c_str(), px);
    uiF[i] = TTF_OpenFont((dir + "/ui.ttf").c_str(), px);
    fbF[i] = TTF_OpenFont((dir + "/fallback.ttf").c_str(), px);
    if (!kanaF[i] || !uiF[i] || !fbF[i]) { if (err) *err = TTF_GetError(); return false; }
    kanaHF[i] = TTF_OpenFont((dir + "/kana_hand.ttf").c_str(), px);  // optional
    uiB[i] = TTF_OpenFont((dir + "/ui_bold.ttf").c_str(), px);       // optional: falls back to ui.ttf
    if (uiB[i]) TTF_SetFontHinting(uiB[i], TTF_HINTING_LIGHT);
    if (kanaHF[i]) TTF_SetFontHinting(kanaHF[i], TTF_HINTING_LIGHT);
    for (TTF_Font* f : {kanaF[i], uiF[i], fbF[i]}) TTF_SetFontHinting(f, TTF_HINTING_LIGHT);  // keeps thin strokes (H crossbar) at small sizes
  }
  return true;
}

void setKanaHand(bool on) { gKanaHand = on; }
void setBold(bool on) { gBold = on; }

void clearTextCache(Win& w) { for (auto& [_, t] : w.cache) SDL_DestroyTexture(t); w.cache.clear(); }

// ------------------------------------------------------------------ shapes
void clearWin(Win& w, Col c) { SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, 255); SDL_RenderClear(w.r); }
void fillRect(Win& w, SDL_Rect r, Col c) { SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, c.a); SDL_RenderFillRect(w.r, &r); }

void fillRound(Win& w, SDL_Rect r, int rad, Col c) {
  if (r.w <= 0 || r.h <= 0) return;
  rad = std::max(0, std::min({rad, r.w / 2, r.h / 2}));
  SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, c.a);
  // Rows as 1-px rectangles (not lines): they tile exactly under non-integer logical scaling, so no seams.
  // Each piece overlaps its neighbour by a pixel so scaled edges never leave a gap.
  SDL_Rect mid{r.x, r.y + rad - 1, r.w, r.h - 2 * rad + 2};
  if (rad == 0) mid = r;
  SDL_RenderFillRect(w.r, &mid);
  for (int y = 0; y < rad; y++) {
    float dy = rad - y - 0.5f;
    int inset = rad - int(std::sqrt(std::max(0.f, rad * rad - dy * dy)) + 0.5f);
    SDL_Rect a{r.x + inset, r.y + y, r.w - 2 * inset, 2}, b{r.x + inset, r.y + r.h - 2 - y, r.w - 2 * inset, 2};
    SDL_RenderFillRect(w.r, &a); SDL_RenderFillRect(w.r, &b);
  }
}

void boxRound(Win& w, SDL_Rect r, int rad, Col fill, Col line, int t) {
  fillRound(w, r, rad, line); fillRound(w, SDL_Rect{r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t}, std::max(0, rad - t), fill);
}

void fillCircle(Win& w, int cx, int cy, int rad, Col c) { fillRound(w, SDL_Rect{cx - rad, cy - rad, 2 * rad, 2 * rad}, rad, c); }

// Vertical gradient in a rounded rect: one quad per pixel row of the corners plus one for the middle, one call.
void gradRound(Win& w, SDL_Rect r, int rad, Col top, Col bottom) {
  if (r.w <= 0 || r.h <= 0) return;
  rad = std::max(0, std::min({rad, r.w / 2, r.h / 2}));
  std::vector<SDL_Vertex> v; v.reserve(size_t(rad * 2 + 1) * 6);
  auto colAt = [&](float y) { Col c = mix(top, bottom, std::clamp((y - r.y) / float(r.h), 0.f, 1.f)); return SDL_Color{c.r, c.g, c.b, 255}; };
  auto quad = [&](float x0, float x1, float y0, float y1) {
    SDL_Color c0 = colAt(y0), c1 = colAt(y1);
    SDL_Vertex a{{x0, y0}, c0, {0, 0}}, b{{x1, y0}, c0, {0, 0}}, c{{x1, y1}, c1, {0, 0}}, d{{x0, y1}, c1, {0, 0}};
    v.push_back(a); v.push_back(b); v.push_back(c); v.push_back(a); v.push_back(c); v.push_back(d);
  };
  for (int y = 0; y < rad; y++) {
    float dy = rad - y - 0.5f, ins = rad - std::sqrt(std::max(0.f, rad * float(rad) - dy * dy));
    quad(r.x + ins, r.x + r.w - ins, float(r.y + y), float(r.y + y + 1) + 0.6f);            // rows overlap a little:
    quad(r.x + ins, r.x + r.w - ins, float(r.y + r.h - y - 1) - 0.6f, float(r.y + r.h - y));  // no seams when scaled
  }
  quad(float(r.x), float(r.x + r.w), float(r.y + rad) - 0.6f, float(r.y + r.h - rad) + 0.6f);
  SDL_RenderGeometry(w.r, nullptr, v.data(), int(v.size()), nullptr, 0);
}

void card(Win& w, SDL_Rect r, int rad, Col fill) {
  if (fill.a == 0) fill = PANEL;
  fillRound(w, SDL_Rect{r.x, r.y + 5, r.w, r.h}, rad, CARD_SH);
  fillRound(w, r, rad, fill);
}

SDL_Rect pill(Win& w, SDL_Rect r, int rad, int kind, bool pressed) {
  int drop = pressed ? 2 : 5, sink = pressed ? 3 : 0;
  SDL_Rect f{r.x, r.y + sink, r.w, r.h};
  if (kind == 2) {  // disabled: flat, no shadow
    fillRound(w, f, rad, DIS_LINE); fillRound(w, SDL_Rect{f.x + 3, f.y + 3, f.w - 6, f.h - 6}, std::max(0, rad - 3), BG2);
    return f;
  }
  Col line = kind == 1 ? ACC_LINE : PILL_LINE, sh = kind == 1 ? ACC_SH : PILL_SH;
  Col hi = kind == 1 ? ACC_HI : PILL_HI, mid = kind == 1 ? ACCENT : mix(PILL_HI, PILL_LO, 0.55f), lo = kind == 1 ? ACC_LO : PILL_LO;
  fillRound(w, SDL_Rect{f.x, f.y + drop, f.w, f.h}, rad, sh);
  fillRound(w, f, rad, line);
  SDL_Rect in{f.x + 3, f.y + 3, f.w - 6, f.h - 6}; int ir = std::max(0, rad - 3);
  // gloss: the main colour from 60% down, lighter above, a touch darker at the bottom (one shape, three stops)
  int split = in.h * 6 / 10;
  gradRound(w, in, ir, mid, lo);
  SDL_Rect topPart{in.x, in.y, in.w, split + ir};
  gradRound(w, topPart, ir, hi, mid);
  if (split + ir < in.h) fillRect(w, SDL_Rect{in.x + ir, in.y + split, in.w - 2 * ir, 1}, mid);
  return f;
}

// Backdrops: faint diagonal stripes (top screen) or a dot grid (bottom), drawn once per window/theme into a texture.
void drawBackdrop(Win& w, bool topScreen) {
  std::string key = std::string("#backdrop") + (topScreen ? "T" : "B") + (gLight ? "L" : "D");
  SDL_Texture*& t = w.cache[key];
  Col base = topScreen ? BG : BG2, mark = mix(base, FG, gLight ? 0.045f : 0.05f);
  if (!t) {
    int W = int(LW * w.scale + 0.5f), Hh = int(LH * w.scale + 0.5f);
    t = SDL_CreateTexture(w.r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, Hh);
    if (!t) { clearWin(w, base); return; }
    SDL_Texture* prev = SDL_GetRenderTarget(w.r);
    float sx, sy; SDL_RenderGetScale(w.r, &sx, &sy);
    SDL_SetRenderTarget(w.r, t); SDL_RenderSetScale(w.r, 1.f, 1.f);
    SDL_SetRenderDrawColor(w.r, base.r, base.g, base.b, 255); SDL_RenderClear(w.r);
    SDL_SetRenderDrawColor(w.r, mark.r, mark.g, mark.b, 255);
    float sc = w.scale;
    if (topScreen) {  // 135-degree stripes, 2 px wide every 12 px
      for (int k = -Hh; k < W + Hh; k += int(12 * sc)) for (int d = 0; d < int(2 * sc + 0.5f); d++) SDL_RenderDrawLine(w.r, k + d, 0, k + d - Hh, Hh);
    } else {          // dots every 16 px
      int step = int(16 * sc), rr = std::max(1, int(1.3f * sc));
      for (int y = step / 2; y < Hh; y += step) for (int x = step / 2; x < W; x += step) { SDL_Rect d{x - rr, y - rr, 2 * rr, 2 * rr}; SDL_RenderFillRect(w.r, &d); }
    }
    SDL_SetRenderTarget(w.r, prev); SDL_RenderSetScale(w.r, sx, sy);
  }
  SDL_Rect dst{0, 0, LW, LH}; SDL_RenderCopy(w.r, t, nullptr, &dst);
}

// ------------------------------------------------------------------ line icons
namespace {
using Poly = std::vector<Pt>;
Poly arc(float cx, float cy, float r, float a0, float a1, int n = 20) {  // degrees, 0 = right, clockwise (y down)
  Poly p; for (int i = 0; i <= n; i++) { float a = (a0 + (a1 - a0) * i / n) * 3.14159265f / 180.f; p.push_back({cx + r * std::cos(a), cy + r * std::sin(a)}); }
  return p;
}
Poly L(std::initializer_list<float> xy) { Poly p; auto it = xy.begin(); while (it != xy.end()) { float x = *it++; float y = *it++; p.push_back({x, y}); } return p; }
const std::map<std::string, std::vector<Poly>>& icons() {
  static const std::map<std::string, std::vector<Poly>> I = {
    {"review", {arc(12, 12, 8, -40, 290), L({20, 4, 20, 8, 16, 8})}},
    {"mixups", {L({3, 6, 7, 6, 11, 11, 13, 13, 17, 18, 21, 18}), L({18, 15, 21, 18, 18, 21}), L({3, 18, 7, 18, 9.5f, 15.5f}), L({14.5f, 8.5f, 17, 6, 21, 6}), L({18, 3, 21, 6, 18, 9})}},
    {"words", {L({6, 5, 18, 5}), arc(18, 8, 3, -90, 0, 6), L({21, 8, 21, 16}), arc(18, 16, 3, 0, 90, 6), L({18, 19, 6, 19}), arc(6, 16, 3, 90, 180, 6), L({3, 16, 3, 8}), arc(6, 8, 3, 180, 270, 6),
               L({10, 5, 10, 19}), L({10, 12, 21, 12}), L({6.5f, 9, 6.6f, 9}), L({6.5f, 15, 6.6f, 15}), L({15.5f, 8.5f, 15.6f, 8.5f}), L({15.5f, 15.5f, 15.6f, 15.5f})}},
    {"timed", {arc(12, 13.5f, 7.5f, 0, 360, 28), L({12, 13.5f, 12, 10}), L({10, 2.5f, 14, 2.5f}), L({18.5f, 6.5f, 20, 5})}},
    {"stats", {L({5, 20, 5, 14}), L({10, 20, 10, 9}), L({15, 20, 15, 12}), L({20, 20, 20, 4})}},
    {"chart", {L({5, 20, 5, 14}), L({10, 20, 10, 9}), L({15, 20, 15, 12}), L({20, 20, 20, 4})}},
    {"pencil", {L({4, 20, 8, 19, 19, 8, 16, 5, 5, 16, 4, 20}), L({14, 7, 17, 10})}},
    {"clock", {arc(12, 13, 8, 0, 360, 28), L({12, 9, 12, 13, 15, 15}), L({9, 2, 15, 2})}},
    {"display", {L({5.5f, 4, 18.5f, 4}), arc(18.5f, 6.5f, 2.5f, -90, 0, 5), L({21, 6.5f, 21, 13.5f}), arc(18.5f, 13.5f, 2.5f, 0, 90, 5), L({18.5f, 16, 5.5f, 16}),
                 arc(5.5f, 13.5f, 2.5f, 90, 180, 5), L({3, 13.5f, 3, 6.5f}), arc(5.5f, 6.5f, 2.5f, 180, 270, 5), L({8, 20, 16, 20}), L({12, 16, 12, 20})}},
    {"info", {arc(12, 12, 9, 0, 360, 30), L({12, 11, 12, 17}), L({12, 7.5f, 12, 8})}},
    {"flame", {L({12, 3, 13, 6.5f, 17, 9, 17, 13}), arc(12, 13, 5, 0, 180, 12), L({7, 13, 7.5f, 11, 9.4f, 8.4f, 10.5f, 10.5f, 11.5f, 11.4f, 11.6f, 7, 12, 3})}},
    {"medal", {L({8, 3, 10, 8}), L({16, 3, 14, 8}), arc(12, 14, 6, 0, 360, 24)}},
    {"lock", {L({7.5f, 11, 16.5f, 11}), arc(16.5f, 13.5f, 2.5f, -90, 0, 4), L({19, 13.5f, 19, 18.5f}), arc(16.5f, 18.5f, 2.5f, 0, 90, 4), L({16.5f, 21, 7.5f, 21}),
              arc(7.5f, 18.5f, 2.5f, 90, 180, 4), L({5, 18.5f, 5, 13.5f}), arc(7.5f, 13.5f, 2.5f, 180, 270, 4), L({8, 11, 8, 8}), arc(12, 8, 4, 180, 360, 10), L({16, 8, 16, 11})}},
    {"check", {L({5, 12.5f, 10, 17.5f, 19, 7})}},
    {"cross", {L({6, 6, 18, 18}), L({18, 6, 6, 18})}},
    {"play", {L({8, 5, 8, 19, 19, 12, 8, 5})}},
    {"pause", {L({9.5f, 6, 9.5f, 18}), L({14.5f, 6, 14.5f, 18})}},
    {"gear", {arc(12, 12, 3, 0, 360, 14), L({12, 2.5f, 12, 5.5f}), L({12, 18.5f, 12, 21.5f}), L({2.5f, 12, 5.5f, 12}), L({18.5f, 12, 21.5f, 12}),
              L({5.3f, 5.3f, 7.4f, 7.4f}), L({16.6f, 16.6f, 18.7f, 18.7f}), L({5.3f, 18.7f, 7.4f, 16.6f}), L({16.6f, 7.4f, 18.7f, 5.3f})}},
    {"arrow", {L({5, 12, 19, 12}), L({13, 6, 19, 12, 13, 18})}},
    {"back", {L({19, 12, 5, 12}), L({11, 6, 5, 12, 11, 18})}},
    {"star", {L({12, 3, 14.6f, 8.6f, 20.6f, 9.2f, 16, 13.3f, 17.3f, 19.3f, 12, 16.2f, 6.7f, 19.3f, 8, 13.3f, 3.4f, 9.2f, 9.4f, 8.6f, 12, 3})}},
    {"book", {L({12, 6, 12, 20}), L({12, 6, 9, 4.5f, 3, 4.5f, 3, 18.5f, 9, 18.5f, 12, 20}), L({12, 6, 15, 4.5f, 21, 4.5f, 21, 18.5f, 15, 18.5f, 12, 20})}},
    {"map", {L({3, 6, 9, 4, 15, 6, 21, 4, 21, 18, 15, 20, 9, 18, 3, 20, 3, 6}), L({9, 4, 9, 18}), L({15, 6, 15, 20})}},
    {"grid", {L({4, 4, 10, 4, 10, 10, 4, 10, 4, 4}), L({14, 4, 20, 4, 20, 10, 14, 10, 14, 4}), L({4, 14, 10, 14, 10, 20, 4, 20, 4, 14}), L({14, 14, 20, 14, 20, 20, 14, 20, 14, 14})}},
    {"eraser", {L({4, 15, 13, 6, 19, 12, 10, 21, 7, 21, 4, 18, 4, 15}), L({9, 10, 15, 16}), L({10, 21, 20, 21})}},
    {"undo", {L({9, 6, 4, 11, 9, 16}), L({4, 11, 14, 11}), arc(14, 15, 4, -90, 90, 10), L({14, 19, 10, 19})}},
    {"eye", {arc(12, 18, 11, 220, 320, 14), arc(12, 6, 11, 40, 140, 14), arc(12, 12, 3, 0, 360, 14)}},
  };
  return I;
}
}
void lineIcon(Win& w, const std::string& name, SDL_Rect box, Col c, float thick) {
  auto it = icons().find(name); if (it == icons().end()) return;
  const float k = 109.f / 24.f;  // drawStrokeIn works in the 109-unit glyph space
  for (const Poly& p : it->second) {
    Stroke s; for (const Pt& q : p) s.push_back({q.x * k, q.y * k});
    if (s.size() == 1) s.push_back(s[0]);
    drawStrokeIn(w, s, box, c, thick * box.w / 24.f);
  }
}

// ------------------------------------------------------------------ text
int textWidth(Win& w, int font, const std::string& s) {
  if (s.empty()) return 0;
  int tw = 0, th = 0; textTex(w, font, s, Col{255, 255, 255, 255}, &tw, &th); return tw;
}

void drawText(Win& w, const std::string& s, SDL_Rect box, Col c, int maxFont, Align al) {
  if (s.empty()) return;
  for (int f = maxFont; f < F_COUNT; f++) {
    int tw, th; SDL_Texture* t = textTex(w, f, s, c, &tw, &th); if (!t) return;
    if ((tw <= box.w && th <= box.h + th / 4) || f == F_COUNT - 1) {
      int x = al == A_LEFT ? box.x : al == A_RIGHT ? box.x + box.w - tw : box.x + (box.w - tw) / 2;
      SDL_Rect d{x, box.y + (box.h - th) / 2, tw, th};
      if (tw > box.w) { d.w = box.w; d.x = box.x; }
      SDL_RenderCopy(w.r, t, nullptr, &d); return;
    }
  }
}

int drawWrapped(Win& w, const std::string& s, SDL_Rect box, Col c, int font, Align al) {
  std::vector<std::string> words; std::string cur;
  for (char ch : s) { if (ch == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); } else cur += ch; }
  if (!cur.empty()) words.push_back(cur);
  std::vector<std::string> lines; std::string line;
  for (auto& wd : words) {
    std::string cand = line.empty() ? wd : line + " " + wd;
    if (!line.empty() && textWidth(w, font, cand) > box.w) { lines.push_back(line); line = wd; } else line = cand;
  }
  if (!line.empty()) lines.push_back(line);
  int lh = 0; { int tw, th; textTex(w, font, "Ag", FG, &tw, &th); lh = th; }
  int y = box.y;
  for (auto& l : lines) {
    if (y + lh > box.y + box.h + lh / 3) break;
    drawText(w, l, SDL_Rect{box.x, y, box.w, lh}, c, font, al); y += lh;
  }
  return y - box.y;
}

// ------------------------------------------------------------------ pixel icons
void pixIcon(Win& w, const std::vector<const char*>& rows, int x, int y, int px, Col a, Col b) {
  for (size_t ry = 0; ry < rows.size(); ry++)
    for (int rx = 0; rows[ry][rx]; rx++) {
      char ch = rows[ry][rx];
      if (ch == '#') fillRect(w, SDL_Rect{x + rx * px, y + int(ry) * px, px, px}, a);
      else if (ch == 'B') fillRect(w, SDL_Rect{x + rx * px, y + int(ry) * px, px, px}, b);
    }
}
void iconBanana(Win& w, int x, int y, int px) {
  pixIcon(w, {"......##..", ".....#BB#.", "....#BBB#.", "...#BBBB#.", "..#BBBB#..", ".#BBBB#...", "#BBBB#....", "#BBB#.....", ".###......"},
          x, y, px, hex(0x8A6B12), ACCENT);
}
void iconLock(Win& w, int x, int y, int px, Col c) { pixIcon(w, {".###.", "#...#", "#...#", "#####", "##.##", "##.##", "#####"}, x, y, px, c); }
void iconCheck(Win& w, int x, int y, int px, Col c) { pixIcon(w, {".....#", "....#.", "#..#..", ".##...", "......"}, x, y, px, c); }
void iconCross(Win& w, int x, int y, int px, Col c) { pixIcon(w, {"#...#", ".#.#.", "..#..", ".#.#.", "#...#"}, x, y, px, c); }

// Progress ring: a full track, then `frac` of it filled clockwise from the top with round ends (triangles, one call each).
void drawRing(Win& w, int cx, int cy, int rad, int thick, float frac, Col fill, Col track) {
  auto arc = [&](float from, float to, Col c) {
    if (to <= from) return;
    const SDL_Color col{c.r, c.g, c.b, 255};
    float ro = float(rad), ri = float(rad - thick);
    int seg = std::max(4, int(64 * (to - from)));
    std::vector<SDL_Vertex> v;
    auto pt = [&](float r, float t) { float a = -1.5707963f + 6.2831853f * t; return SDL_FPoint{cx + r * std::cos(a), cy + r * std::sin(a)}; };
    for (int i = 0; i < seg; i++) {
      float t0 = from + (to - from) * i / seg, t1 = from + (to - from) * (i + 1) / seg;
      SDL_FPoint a = pt(ro, t0), b = pt(ri, t0), c2 = pt(ro, t1), d = pt(ri, t1);
      for (SDL_FPoint q : {a, b, d, a, d, c2}) v.push_back(SDL_Vertex{q, col, SDL_FPoint{0, 0}});
    }
    SDL_RenderGeometry(w.r, nullptr, v.data(), int(v.size()), nullptr, 0);
  };
  arc(0.f, 1.f, track);
  frac = std::clamp(frac, 0.f, 1.f);
  if (frac <= 0.f) return;
  arc(0.f, frac, fill);
  float mid = rad - thick / 2.f;  // round caps
  for (float t : {0.f, frac}) {
    float a = -1.5707963f + 6.2831853f * t;
    fillCircle(w, int(cx + mid * std::cos(a)), int(cy + mid * std::sin(a)), thick / 2, fill);
  }
}

// ------------------------------------------------------------------ glyphs
SDL_Point glyphToBox(Pt p, SDL_Rect b) { return {int(b.x + p.x * b.w / 109.f), int(b.y + p.y * b.h / 109.f)}; }
Pt boxToGlyph(int x, int y, SDL_Rect b) { return {(x - b.x) * 109.f / b.w, (y - b.y) * 109.f / b.h}; }

// Thick strokes as triangles (one SDL_RenderGeometry call): quads per segment plus round joints/caps.
// The old version drew dozens of 1-px lines per segment, which was far too slow for the device GPU.
void drawStrokeIn(Win& w, const Stroke& s, SDL_Rect box, Col c, float thick) {
  if (s.empty()) return;
  const float r = std::max(0.75f, thick / 2.f);
  const SDL_Color col{c.r, c.g, c.b, 255};
  std::vector<SDL_FPoint> p; p.reserve(s.size());
  for (const Pt& q : s) p.push_back({box.x + q.x * box.w / 109.f, box.y + q.y * box.h / 109.f});
  std::vector<SDL_Vertex> v; v.reserve(p.size() * 36);
  auto put = [&](SDL_FPoint a) { v.push_back(SDL_Vertex{a, col, SDL_FPoint{0, 0}}); };
  const int SEG = 10;
  auto disc = [&](SDL_FPoint o) {
    for (int k = 0; k < SEG; k++) {
      float a0 = 6.2831853f * k / SEG, a1 = 6.2831853f * (k + 1) / SEG;
      put(o); put({o.x + r * std::cos(a0), o.y + r * std::sin(a0)}); put({o.x + r * std::cos(a1), o.y + r * std::sin(a1)});
    }
  };
  disc(p[0]);
  for (size_t i = 1; i < p.size(); i++) {
    SDL_FPoint a = p[i - 1], b = p[i];
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) continue;
    float nx = -dy / len * r, ny = dx / len * r;
    SDL_FPoint a1{a.x + nx, a.y + ny}, a2{a.x - nx, a.y - ny}, b1{b.x + nx, b.y + ny}, b2{b.x - nx, b.y - ny};
    put(a1); put(a2); put(b2); put(a1); put(b2); put(b1);
    disc(b);
  }
  SDL_RenderGeometry(w.r, nullptr, v.data(), int(v.size()), nullptr, 0);
}

void drawStrokeNumbers(Win& w, const Glyph& g, SDL_Rect box, int highlightUpTo, int count) {
  int r = std::max(6, box.w / 26);
  for (int i = 0; i < count && i < int(g.strokes.size()); i++) {
    if (g.strokes[i].empty()) continue;
    SDL_Point p = glyphToBox(g.strokes[i][0], box);
    bool on = i < highlightUpTo;
    fillCircle(w, p.x, p.y, r, on ? ACCENT : PANEL2);
    drawText(w, std::to_string(i + 1), SDL_Rect{p.x - r, p.y - r, 2 * r, 2 * r}, on ? ON_ACCENT : FG, r >= 12 ? F_TINY : F_MICRO);
  }
}

// ------------------------------------------------------------------ windows
bool makeWin(Win& w, const char* title, int x, int y, int ww, int wh, bool onDisplay, int display) {
  Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
  if (onDisplay) {
    SDL_Rect b{}; SDL_GetDisplayBounds(display, &b); x = b.x; y = b.y; ww = b.w; wh = b.h;
    // KMSDRM only puts a window on a panel when it is fullscreen. KT_NOFS=1 falls back to borderless.
    flags = SDL_WINDOW_SHOWN | (getenv("KT_NOFS") ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_FULLSCREEN_DESKTOP);
  }
  w.w = SDL_CreateWindow(title, x, y, ww, wh, flags);
  if (!w.w) { fprintf(stderr, "  create window '%s' failed: %s\n", title, SDL_GetError()); return false; }
  bool soft = getenv("KT_SOFTWARE") != nullptr;
  // With two windows on two CRTCs, per-window vsync can stall the other panel: off by default on device.
  bool vsync = !onDisplay || getenv("KT_VSYNC");
  Uint32 rf = soft ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
  w.r = SDL_CreateRenderer(w.w, -1, rf);
  if (!w.r) { fprintf(stderr, "  accelerated renderer for '%s' failed (%s), trying software\n", title, SDL_GetError()); w.r = SDL_CreateRenderer(w.w, -1, SDL_RENDERER_SOFTWARE); }
  if (!w.r) { fprintf(stderr, "  create renderer '%s' failed: %s\n", title, SDL_GetError()); return false; }
  SDL_RenderSetLogicalSize(w.r, LW, LH); w.id = SDL_GetWindowID(w.w);
  int ow = 0, oh = 0; SDL_GetRendererOutputSize(w.r, &ow, &oh);
  w.scale = std::min(ow / float(LW), oh / float(LH));
  SDL_RendererInfo ri{}; SDL_GetRendererInfo(w.r, &ri);
  fprintf(stderr, "  window '%s': id=%u display=%d output=%dx%d scale=%.2f flags=0x%x renderer=%s vsync=%d\n", title, w.id,
          SDL_GetWindowDisplayIndex(w.w), ow, oh, w.scale, SDL_GetWindowFlags(w.w), ri.name, vsync ? 1 : 0);
  return true;
}

bool saveShot(Win& w, const std::string& path) {
  int ow = 0, oh = 0; SDL_GetRendererOutputSize(w.r, &ow, &oh);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888); if (!s) return false;
  int ok = SDL_RenderReadPixels(w.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  int saved = ok == 0 ? SDL_SaveBMP(s, path.c_str()) : -1; SDL_FreeSurface(s); return saved == 0;
}

}  // namespace kt
