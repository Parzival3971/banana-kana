// Sound effects, synthesised at start-up (no audio files, nothing to license).
#pragma once

namespace snd {
enum class Sfx { Tap, Right, Wrong, Known, Unlock, Goal, Tick, Count };
bool init();             // opens the default audio device; false (and silent) if there is none
void setVolume(int v);   // 0 off, 1 quiet, 2 normal, 3 loud
void play(Sfx s);        // replaces whatever is playing
void quit();
// Rumble on the gamepad (RG DS Plus joypad has FF_RUMBLE). play() also buzzes for Right/Wrong/Known/Unlock/Goal.
void setController(void* sdlGameController);  // SDL_GameController*, or nullptr
void setRumble(int level);                     // 0 off, 1 light, 2 strong
void rumble(Sfx s);                            // just the buzz, no sound
bool canRumble();
}  // namespace snd
