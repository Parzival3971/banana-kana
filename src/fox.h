// Kitsu, the streak pet: an original anime-style kitsune drawn with flat cel colour and plum ink outlines.
// A high-resolution four-stage/three-mood atlas keeps the same character sharp from tiny status icons through the
// large Your fox portrait; lightweight vector overlays add accessories, seasons and fox-fire on either renderer.
#pragma once
#include "gfx.h"
#include "core.h"

namespace kt {

enum class FoxMood { Asleep, Awake, Happy };

struct FoxLook {
  int stage = 1;         // 0 egg, 1 kit (1 tail), 2 three tails, 3 five tails, 4 nine tails (kyubi)
  FoxMood mood = FoxMood::Awake;
  float t = 0.f;         // seconds: breathing, blinking, swaying tails, fox-fire (0 = still)
  int accessory = -1;    // petAccessories() index, -1 none
  int season = -1;       // -1 none, 0 sakura on the head, 1 uchiwa fan, 2 momiji leaf, 3 winter scarf
};

// Registers the shared 4-stage × 3-mood anime atlas for one screen renderer. The procedural egg remains the fallback.
bool loadAnimeFoxAtlas(Win& w, const std::string& dataDir);

// Draws the fox standing on (cx, baseY). `height` is the full height in logical pixels (ears to feet).
// Returns the number of triangles drawn (the self-test keeps it within budget).
int drawFox(Win& w, float cx, float baseY, float height, const FoxLook& look);

}  // namespace kt
