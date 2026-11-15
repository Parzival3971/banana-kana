// Sound effects, synthesised at start-up (no audio files, nothing to license).
#pragma once
#include <algorithm>
#include <vector>

namespace snd {
enum class Sfx { Tap, Right, Wrong, Known, Unlock, Goal, Tick, Count };
bool init();             // opens the default audio device; false (and silent) if there is none
void setVolume(int v);   // 0 off, 1 quiet, 2 normal, 3 loud
void play(Sfx s);        // replaces whatever is playing
void quit();
// Rumble on the gamepad (RG DS Plus joypad has FF_RUMBLE). play() also buzzes for Right/Wrong/Known/Unlock/Goal.
void setController(void* sdlGameController);  // SDL_GameController*, or nullptr
void setRumble(int level);                     // 0 off, 1 light, 2 strong
void rumble(Sfx s);                            // just the buzz, no sound (replaces any pattern still playing)
bool canRumble();
void tick(unsigned nowMs);                     // call every frame: plays the next pulse of a pattern when it's due

// Rumble patterns, so right and wrong feel different on a single motor (user, 2026-11-15):
//   right = one tick, wrong = two buzzes, learned = three quick ticks, unlock / daily goal = one long rumble.
// The RG DS Plus motor follows the strong channel and needs about 0x5000 for 80 ms to spin up, hence the floor.
struct Pulse { int at, strength, ms; };        // start (ms after the first), 0..0xFFFF, length
constexpr int RUMBLE_FLOOR = 0x5800, RUMBLE_MIN_MS = 80;
inline std::vector<Pulse> rumblePattern(Sfx s, int level) {
  if (level <= 0) return {};
  std::vector<Pulse> p;
  switch (s) {
    case Sfx::Right:  p = {{0, 0x7000, 90}}; break;
    case Sfx::Wrong:  p = {{0, 0xC000, 110}, {220, 0xC000, 110}}; break;
    case Sfx::Known:  p = {{0, 0x9000, 80}, {160, 0x9000, 80}, {320, 0x9000, 80}}; break;
    case Sfx::Unlock: case Sfx::Goal: p = {{0, 0xD000, 420}}; break;
    default: return {};
  }
  for (Pulse& x : p) {
    x.strength = std::min(0xFFFF, std::max(RUMBLE_FLOOR, level == 1 ? x.strength * 4 / 5 : x.strength));
    if (level >= 2 && p.size() == 1) x.ms += x.ms / 3;   // strong: single buzzes last longer (gaps stay the same)
    x.ms = std::max(RUMBLE_MIN_MS, x.ms);
  }
  return p;
}
}  // namespace snd
