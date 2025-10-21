#pragma once

#include "../bnEmotions.h"
#include "../bnInbox.h"

namespace Overworld {
  struct PlayerSession {
    int health{};
    int maxHealth{};
    int money{};
    int fragments{};
    Emotion emotion{};
    Inbox inbox;
  };
}
