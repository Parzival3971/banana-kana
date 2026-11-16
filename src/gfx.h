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

// "Handheld" palettes (light and dark): pale striped top screen, dotted bottom screen, white cards with a hard drop
// shadow, glossy pill buttons. ACCENT is the current MODE colour (Lesson orange, Review blue, Words green, Mix-ups
// purple, Timed red, Stats/Settings teal): a fill; ACCENT_TXT is that colour as readable text; ON_ACCENT is text on it.
struct Theme { Col bg, panel, panel2, line, fg, dim, accent, accentTxt, onAccent, good, goodFill, bad, badFill,
               canvas, ghost, ghostOk, disLine, disTxt, track, frame; };
extern const Theme DARK_THEME, LIGHT_THEME;
extern Col BG, PANEL, PANEL2, LINE, FG, DIM, ACCENT, ACCENT_TXT, ON_ACCENT, GOOD, GOOD_FILL, BAD, BAD_FILL, CANVAS, GHOST,
    GHOST_OK, DIS_LINE, DIS_TXT, TRACK;
extern Col BG2, CARD_SH, PILL_LINE, PILL_SH, PILL_HI, PILL_LO, ACC_LINE, ACC_SH, ACC_HI, ACC_LO, PAPER_LINE, PAPER_MARGIN;
extern bool gLight;
void applyTheme(bool light);
enum ModeCol { MC_ORANGE, MC_BLUE, MC_GREEN, MC_PURPLE, MC_RED, MC_TEAL, MC_COUNT };
Col modeCol(ModeCol m);     // the fill colour of a mode, without changing ACCENT
void setAccent(ModeCol m);  // sets ACCENT, ACCENT_TXT, ON_ACCENT and the ACC_* pill shades

struct Win {
  SDL_Window* w = nullptr; SDL_Renderer* r = nullptr; Uint32 id = 0;
  std::map<std::string, SDL_Texture*> cache;
  float scale = 1.f;  // output pixels per logical pixel (text is rasterised at this scale)
};

enum FontId { F_HUGE, F_BIG, F_MID, F_SMALL, F_TINY, F_MICRO, F_COUNT };
bool loadFonts(const std::string& dataDir, float scale, std::string* err);
void clearTextCache(Win& w);
void setKanaHand(bool on);
void setBold(bool on);       // Latin text in the bold UI font at every size (until setBold(false))  // kana in the handwriting font (kana_hand.ttf) instead of rounded print

enum Align { A_CENTER, A_LEFT, A_RIGHT };
void clearWin(Win& w, Col c);
void fillRect(Win& w, SDL_Rect r, Col c);
void fillRound(Win& w, SDL_Rect r, int rad, Col c);
void boxRound(Win& w, SDL_Rect r, int rad, Col fill, Col line, int t = 3);
void fillCircle(Win& w, int cx, int cy, int rad, Col c);
void drawRing(Win& w, int cx, int cy, int rad, int thick, float frac, Col fill, Col track);  // progress ring from 12 o'clock
void gradRound(Win& w, SDL_Rect r, int rad, Col top, Col bottom);   // vertical gradient, one geometry call
void card(Win& w, SDL_Rect r, int rad = 20, Col fill = Col{0, 0, 0, 0});  // white (PANEL) card + hard drop shadow
// Glossy pill/tile: drop shadow, outline, top-to-bottom gradient. kind 0 = plain (white), 1 = accent (mode colour),
// 2 = disabled (flat, dashed look). pressed sinks it. Returns the face rect (for placing content).
SDL_Rect pill(Win& w, SDL_Rect r, int rad, int kind, bool pressed = false);
void drawBackdrop(Win& w, bool topScreen);  // stripes (top) / dots (bottom), or the season's pattern; cached geometry
// Seasons (0.6.0): -1 plain, 0 spring (sakura), 1 summer (seigaiha + fireflies), 2 autumn (momiji), 3 winter (snow).
// setSeason tints BG/BG2 for the season (call after applyTheme); drawSeasonParticles draws the drifting petals,
// fireflies, leaves or snow on top of a screen at time t (seconds).
void setSeason(int season);
int season();
void drawSeasonParticles(Win& w, bool topScreen, float t);
int backdropTriangles();  // triangles in the last backdrop drawn (the self-test keeps every season under a budget)
// Line icons (24x24 design grid, round strokes): review mixups words timed stats pencil clock script display chart
// info flame medal lock check cross play pause gear arrow back star book map grid pen eraser
void lineIcon(Win& w, const std::string& name, SDL_Rect box, Col c, float thick = 2.2f);
// Filled streak flame (24-grid shape scaled to box): outer + inner tongue, soft centre glow. flick -1..1 sways the tips.
void drawFlame(Win& w, SDL_Rect box, Col outer, Col inner, float flick = 0.f);
int textWidth(Win& w, int font, const std::string& s);  // logical px
// Draws text in `box`, using the biggest font <= maxFont that fits on one line.
void drawText(Win& w, const std::string& s, SDL_Rect box, Col c, int maxFont = F_MID, Align al = A_CENTER);
// Wraps on spaces; returns the height used.
int drawWrapped(Win& w, const std::string& s, SDL_Rect box, Col c, int font, Align al = A_LEFT);
int wrappedHeight(Win& w, const std::string& s, int width, int font);  // height drawWrapped needs

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
