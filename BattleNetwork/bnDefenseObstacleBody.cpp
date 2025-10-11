#include "bnDefenseObstacleBody.h"

DefenseObstacleBody::DefenseObstacleBody() : DefenseRule(Priority(1), DefenseOrder::collisionOnly) {
}

DefenseObstacleBody::~DefenseObstacleBody() { }

Hit::Properties& DefenseObstacleBody::FilterStatuses(Hit::Properties& statuses)
{
  // obstacles are immune to most flags
  statuses.flags &= ~Hit::flash;
  statuses.flags &= ~Hit::flinch;
  statuses.flags &= ~Hit::stun;
  statuses.flags &= ~Hit::freeze;
  statuses.flags &= ~Hit::root;
  statuses.flags &= ~Hit::blind;
  statuses.flags &= ~Hit::confuse;
  return statuses;
}

void DefenseObstacleBody::CanBlock(DefenseFrameStateJudge& judge, std::shared_ptr<Entity> attacker, std::shared_ptr<Entity> owner) {
  // doesn't block anything
}
