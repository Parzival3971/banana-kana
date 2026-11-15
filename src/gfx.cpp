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

const Theme DARK_THEME{hex(0x1D1B17), hex(0x2B2722), hex(0x383229), hex(0x4D463D), hex(0xF5F0E6), hex(0xBDB3A3),
  hex(0xFFD23F), hex(0xFFD23F), hex(0x1D1B17), hex(0x7BD88F), hex(0x1F3526), hex(0xF07A4F), hex(0x3A221A),
  hex(0x15130F), hex(0x5A5246), hex(0x3E5A45), hex(0x3A342C), hex(0x7A7064), hex(0x383229), hex(0x100F0D)};
const Theme LIGHT_THEME{hex(0xF7F2E8), hex(0xEDE5D6), hex(0xE0D6C3), hex(0xC9BBA3), hex(0x241F18), hex(0x6B6152),
  hex(0xFFD23F), hex(0x7A5A00), hex(0x241F18), hex(0x2E7D46), hex(0xD7EEDC), hex(0xB8432A), hex(0xF6DBD2),
  hex(0xFFFDF8), hex(0xD6CCBB), hex(0xBFD9C4), hex(0xDDD3C2), hex(0x8F846F), hex(0xE0D6C3), hex(0xE8E0D0)};
Col BG, PANEL, PANEL2, LINE, FG, DIM, ACCENT, ACCENT_TXT, ON_ACCENT, GOOD, GOOD_FILL, BAD, BAD_FILL, CANVAS, GHOST, GHOST_OK,
    DIS_LINE, DIS_TXT, TRACK;

void applyTheme(bool light) {
  const Theme& t = light ? LIGHT_THEME : DARK_THEME;
  BG = t.bg; PANEL = t.panel; PANEL2 = t.panel2; LINE = t.line; FG = t.fg; DIM = t.dim; ACCENT = t.accent; ACCENT_TXT = t.accentTxt;
  ON_ACCENT = t.onAccent; GOOD = t.good; GOOD_FILL = t.goodFill; BAD = t.bad; BAD_FILL = t.badFill; CANVAS = t.canvas;
  GHOST = t.ghost; GHOST_OK = t.ghostOk; DIS_LINE = t.disLine; DIS_TXT = t.disTxt; TRACK = t.track;
}

// ------------------------------------------------------------------ fonts
namespace {
const int FONT_PX[F_COUNT] = {170, 100, 52, 30, 22, 17};
TTF_Font* kanaF[F_COUNT];  // Zen Maru Gothic: kana
TTF_Font* uiF[F_COUNT];    // Pixelify Sans: English / UI
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
  if (jp && TTF_GlyphIsProvided32(kanaF[size], cp)) return kanaF[size];
  if (!jp && TTF_GlyphIsProvided32(uiF[size], cp)) return uiF[size];
  if (TTF_GlyphIsProvided32(kanaF[size], cp)) return kanaF[size];
  return fbF[size];
}
// Renders mixed kana/English text: each run uses the font that has its glyphs, aligned on a shared baseline.
SDL_Surface* renderMixed(int size, const std::string& s, SDL_Color col) {
  std::vector<std::pair<TTF_Font*, std::string>> runs;
  for (uint32_t cp : utf8Codepoints(s)) {
    TTF_Font* f = cp == ' ' ? uiF[size] : pickFont(size, cp);  // spaces always from the UI font (kana spaces are full-width)
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
  char key[48]; snprintf(key, sizeof key, "%d|%02x%02x%02x|", font, c.r, c.g, c.b);
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
  }
  return true;
}

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

// ------------------------------------------------------------------ glyphs
SDL_Point glyphToBox(Pt p, SDL_Rect b) { return {int(b.x + p.x * b.w / 109.f), int(b.y + p.y * b.h / 109.f)}; }
Pt boxToGlyph(int x, int y, SDL_Rect b) { return {(x - b.x) * 109.f / b.w, (y - b.y) * 109.f / b.h}; }

void drawStrokeIn(Win& w, const Stroke& s, SDL_Rect box, Col c, float thick) {
  int t = std::max(1, int(thick));
  SDL_SetRenderDrawColor(w.r, c.r, c.g, c.b, 255);
  for (size_t i = 1; i < s.size(); i++) {
    SDL_Point a = glyphToBox(s[i - 1], box), b = glyphToBox(s[i], box);
    for (int dx = -t / 2; dx <= t / 2; dx++)
      for (int dy = -t / 2; dy <= t / 2; dy++)
        if (dx * dx + dy * dy <= t * t / 4) SDL_RenderDrawLine(w.r, a.x + dx, a.y + dy, b.x + dx, b.y + dy);
  }
  if (s.size() == 1) { SDL_Point a = glyphToBox(s[0], box); fillCircle(w, a.x, a.y, t / 2, c); }
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
