// Drawing helpers: theme colours, fonts and mixed-script text, rounded shapes, pixel icons, glyph strokes.
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

#include <map>
#include <string>
#include <vector>

#include "core.h"

namespace kt {

constexpr int LW = 640, LH = 480;  // logical size of each screen

struct Col { Uint8 r = 0, g = 0, b = 0, a = 255; };
inline bool sameCol(Col a, Col b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
constexpr Col hex(unsigned v) { return Col{Uint8(v >> 16), Uint8(v >> 8), Uint8(v), 255}; }
Col mix(Col a, Col b, float t);  // a..b

// BananaOS-style palettes: banana yellow on warm neutrals. ACCENT is a fill; ACCENT_TXT is yellow used as text
// (a dark gold on the light theme, where yellow text would be unreadable). ON_ACCENT is text placed on ACCENT.
struct Theme { Col bg, panel, panel2, line, fg, dim, accent, accentTxt, onAccent, good, goodFill, bad, badFill,
               canvas, ghost, ghostOk, disLine, disTxt, track, frame; };
extern const Theme DARK_THEME, LIGHT_THEME;
extern Col BG, PANEL, PANEL2, LINE, FG, DIM, ACCENT, ACCENT_TXT, ON_ACCENT, GOOD, GOOD_FILL, BAD, BAD_FILL, CANVAS, GHOST,
    GHOST_OK, DIS_LINE, DIS_TXT, TRACK;
void applyTheme(bool light);

struct Win {
  SDL_Window* w = nullptr; SDL_Renderer* r = nullptr; Uint32 id = 0;
  std::map<std::string, SDL_Texture*> cache;
  float scale = 1.f;  // output pixels per logical pixel (text is rasterised at this scale)
};

enum FontId { F_HUGE, F_BIG, F_MID, F_SMALL, F_TINY, F_MICRO, F_COUNT };
bool loadFonts(const std::string& dataDir, float scale, std::string* err);
void clearTextCache(Win& w);
void setKanaHand(bool on);  // kana in the handwriting font (kana_hand.ttf) instead of rounded print

enum Align { A_CENTER, A_LEFT, A_RIGHT };
void clearWin(Win& w, Col c);
void fillRect(Win& w, SDL_Rect r, Col c);
void fillRound(Win& w, SDL_Rect r, int rad, Col c);
void boxRound(Win& w, SDL_Rect r, int rad, Col fill, Col line, int t = 3);
void fillCircle(Win& w, int cx, int cy, int rad, Col c);
int textWidth(Win& w, int font, const std::string& s);  // logical px
// Draws text in `box`, using the biggest font <= maxFont that fits on one line.
void drawText(Win& w, const std::string& s, SDL_Rect box, Col c, int maxFont = F_MID, Align al = A_CENTER);
// Wraps on spaces; returns the height used.
int drawWrapped(Win& w, const std::string& s, SDL_Rect box, Col c, int font, Align al = A_LEFT);

// Pixel icons: '#' = a, 'B' = b.
void pixIcon(Win& w, const std::vector<const char*>& rows, int x, int y, int px, Col a, Col b = Col{});
void iconBanana(Win& w, int x, int y, int px);
void iconLock(Win& w, int x, int y, int px, Col c);
void iconCheck(Win& w, int x, int y, int px, Col c);
void iconCross(Win& w, int x, int y, int px, Col c);

// Glyph drawing (KanjiVG 109x109 space mapped into `box`).
SDL_Point glyphToBox(Pt p, SDL_Rect box);
Pt boxToGlyph(int x, int y, SDL_Rect box);
void drawStrokeIn(Win& w, const Stroke& s, SDL_Rect box, Col c, float thick);
void drawStrokeNumbers(Win& w, const Glyph& g, SDL_Rect box, int highlightUpTo, int count);

bool makeWin(Win& w, const char* title, int x, int y, int ww, int wh, bool onDisplay, int display);
bool saveShot(Win& w, const std::string& path);

}  // namespace kt
