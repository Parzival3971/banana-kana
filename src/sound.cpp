#include "sound.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace snd {
namespace {
const int RATE = 22050;
SDL_AudioDeviceID dev = 0;
int volume = 2;
SDL_GameController* pad = nullptr;
int rumbleLevel = 1;
std::vector<int16_t> clips[int(Sfx::Count)];

// One note: a soft sine with a little second harmonic, quick attack, exponential fade.
void note(std::vector<float>& buf, float startS, float freq, float lenS, float amp, float bright = 0.25f) {
  size_t s0 = size_t(startS * RATE), n = size_t(lenS * RATE);
  if (buf.size() < s0 + n) buf.resize(s0 + n, 0.f);
  for (size_t i = 0; i < n; i++) {
    float t = float(i) / RATE, env = std::min(1.f, t / 0.006f) * std::exp(-t * 6.f / lenS);
    float ph = 2.f * float(M_PI) * freq * t;
    buf[s0 + i] += amp * env * (std::sin(ph) + bright * std::sin(2 * ph));
  }
}

std::vector<int16_t> pcm(const std::vector<float>& f) {
  std::vector<int16_t> o(f.size());
  for (size_t i = 0; i < f.size(); i++) o[i] = int16_t(std::max(-1.f, std::min(1.f, f[i])) * 32000);
  return o;
}

void build() {
  std::vector<float> b;
  note(b, 0, 1568, 0.04f, 0.18f, 0.1f); clips[int(Sfx::Tap)] = pcm(b); b.clear();                               // G6 blip
  note(b, 0, 1047, 0.12f, 0.35f); note(b, 0.08f, 1319, 0.22f, 0.35f); clips[int(Sfx::Right)] = pcm(b); b.clear();  // C6 E6
  note(b, 0, 247, 0.16f, 0.35f, 0.6f); note(b, 0.12f, 196, 0.26f, 0.35f, 0.6f); clips[int(Sfx::Wrong)] = pcm(b); b.clear();  // B3 G3
  note(b, 0, 1047, 0.1f, 0.3f); note(b, 0.07f, 1319, 0.1f, 0.3f); note(b, 0.14f, 1568, 0.3f, 0.32f);               // C E G
  note(b, 0.14f, 2093, 0.3f, 0.12f, 0.f); clips[int(Sfx::Known)] = pcm(b); b.clear();
  const float arp[5] = {523, 659, 784, 1047, 1319};                                                               // stage unlocked
  for (int i = 0; i < 5; i++) note(b, i * 0.09f, arp[i], i == 4 ? 0.5f : 0.16f, 0.3f);
  clips[int(Sfx::Unlock)] = pcm(b); b.clear();
  note(b, 0, 784, 0.12f, 0.3f); note(b, 0.12f, 784, 0.12f, 0.3f); note(b, 0.24f, 1047, 0.5f, 0.34f);              // daily goal
  note(b, 0.24f, 1319, 0.5f, 0.2f); clips[int(Sfx::Goal)] = pcm(b); b.clear();
  note(b, 0, 2093, 0.03f, 0.12f, 0.f); clips[int(Sfx::Tick)] = pcm(b); b.clear();
}
}  // namespace

bool init() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "audio: %s (sound off)\n", SDL_GetError()); return false; }
  SDL_AudioSpec want{}, have{};
  want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
  dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!dev) { fprintf(stderr, "audio: %s (sound off)\n", SDL_GetError()); return false; }
  build();
  SDL_PauseAudioDevice(dev, 0);
  fprintf(stderr, "audio: %s, %d Hz\n", SDL_GetCurrentAudioDriver(), have.freq);
  return true;
}

void setVolume(int v) { volume = std::max(0, std::min(3, v)); }

void play(Sfx s) {
  rumble(s);
  if (!dev || volume == 0) return;
  const auto& c = clips[int(s)]; if (c.empty()) return;
  static const float GAIN[4] = {0.f, 0.35f, 0.7f, 1.f};
  std::vector<int16_t> out(c.size());
  for (size_t i = 0; i < c.size(); i++) out[i] = int16_t(c[i] * GAIN[volume]);
  SDL_ClearQueuedAudio(dev);
  SDL_QueueAudio(dev, out.data(), Uint32(out.size() * sizeof(int16_t)));
}

void setController(void* gc) { pad = static_cast<SDL_GameController*>(gc); }
void setRumble(int level) { rumbleLevel = std::max(0, std::min(2, level)); }
bool canRumble() { return pad && SDL_GameControllerHasRumble(pad); }
static std::vector<Pulse> pending; static unsigned patternStart = 0;  // the pattern being played
static int rumbleLogged = 0;
static void buzz(const Pulse& p) {
  int rc = SDL_GameControllerRumble(pad, Uint16(p.strength), Uint16(p.strength), Uint32(p.ms));
  if (rumbleLogged < 8) { rumbleLogged++; fprintf(stderr, "  rumble +%d ms: 0x%04x for %d ms -> %s\n", p.at, p.strength, p.ms, rc == 0 ? "ok" : SDL_GetError()); fflush(stderr); }
}
void rumble(Sfx s) {
  if (!pad || rumbleLevel == 0) return;
  auto p = rumblePattern(s, rumbleLevel); if (p.empty()) return;
  patternStart = SDL_GetTicks(); buzz(p[0]);
  pending.assign(p.begin() + 1, p.end());
}
void tick(unsigned now) {
  while (!pending.empty() && now - patternStart >= unsigned(pending.front().at)) { buzz(pending.front()); pending.erase(pending.begin()); }
}
void quit() { if (dev) SDL_CloseAudioDevice(dev); dev = 0; }
}  // namespace snd
