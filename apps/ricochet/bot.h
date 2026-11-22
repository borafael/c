#ifndef RICOCHET_BOT_H
#define RICOCHET_BOT_H

#include "game.h"

/* Autopilot: fills *in for one frame of play (--bot, the title screen's
 * background match, headless runs). */
void bot_input(const game *g, input *in, float dt);

#endif
