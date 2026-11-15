// Sound effects, synthesised at start-up (no audio files, nothing to license).
#pragma once

namespace snd {
enum class Sfx { Tap, Right, Wrong, Known, Unlock, Goal, Tick, Count };
bool init();             // opens the default audio device; false (and silent) if there is none
void setVolume(int v);   // 0 off, 1 quiet, 2 normal, 3 loud
void play(Sfx s);        // replaces whatever is playing
void quit();
}  // namespace snd
