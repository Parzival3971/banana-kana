// Kitsu, the streak pet: an original kitsune drawn in a soft, shaded "3D" chibi style.
// Everything is triangle fans with per-vertex colours (a light centre fading to a darker rim), batched into one
// SDL_RenderGeometry call, so the fox stays sharp at any scale and cheap enough to animate on the device.
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

// Draws the fox standing on (cx, baseY). `height` is the full height in logical pixels (ears to feet).
// Returns the number of triangles drawn (the self-test keeps it within budget).
int drawFox(Win& w, float cx, float baseY, float height, const FoxLook& look);

// Home's one-tail Kitsu atlas. The texture belongs to the top screen renderer.
SDL_Texture* loadHomeFoxSprite(Win& w, const std::string& dataDir);
bool drawHomeFoxSprite(Win& w, SDL_Texture* sprite, const SDL_Rect& box, const FoxLook& look);

}  // namespace kt
