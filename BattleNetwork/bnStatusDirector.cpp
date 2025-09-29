#include "bnStatusDirector.h"
#include "bnEntity.h"
#include "bnHitProperties.h"

StatusBehaviorDirector::StatusBehaviorDirector(Entity& owner) : owner(owner), queuedStatuses{ 0 }, currentStatuses{ 0 } {
    currentStatuses = {};
}

void StatusBehaviorDirector::AddStatus(Hit::Flags statusFlag, frame_time_t maxCooldown) {
    AppliedStatus& statusToCheck = GetStatus(statusFlag);
    statusToCheck.remainingTime = maxCooldown;
    queuedStatuses = queuedStatuses | statusFlag;
}

void StatusBehaviorDirector::AddStatus(Hit::Flags statusFlag) {
  // Slight hack. A flag with less than 2 frames on it will not always be 
  // observable. This is because OnUpdate ticks the time down by 1 and then removes 
  // time <= 0. If they were added with frames(1), they would tick and be removed 
  // before status callbacks run.
  AddStatus(statusFlag, frames(2));
}

Hit::Flags StatusBehaviorDirector::GetAppliedFlags(Hit::Flags flags) {
  // Start from lowest set bit
  Hit::Flags i = flags & -flags;
  Hit::Flags m = ~0;

  static const Hit::Flags stunMask = ~(Hit::flinch | Hit::freeze);
  static const Hit::Flags freezeMask = ~(Hit::flinch | Hit::flash);

  // NOTE: Flag order is important. stun < freeze < drag
  while (i != 0) {
    switch (i & flags & m) {
    case Hit::stun:
      m = m & stunMask;
      break;
    case Hit::freeze:
      m = m & freezeMask;
      break;
    case Hit::drag:
      m = (m & ~Hit::freeze) | ~freezeMask;
      break;
    }
    i = i << 1;
  }

  return m & flags;
}

void StatusBehaviorDirector::ProcessPendingStatuses() {
  // Process only Drag if it's queued
  if ((queuedStatuses & Hit::drag) == Hit::drag) {
    ProcessFlags(Hit::drag);
    // Drag is a special case where most behavior is handled 
    // based on this bool. Set true after processing.
    owner.slideFromDrag = true;
    queuedStatuses &= ~Hit::drag;
    return;
  }

  // Do not process other flags if Drag is a current status.
  // Base this on Entity::slideFromDrag, as it more accurately 
  // represents the special case of Drag.
  if(owner.slideFromDrag) {
    return;
  }

  
  if (queuedStatuses == 0) {
    return;
  }

  ProcessFlags(queuedStatuses);
  queuedStatuses = Hit::none;
}

void StatusBehaviorDirector::ProcessFlags(Hit::Flags attack) {
  const Hit::Flags flinch_flash = Hit::flinch | Hit::flash;

  // Retangible removes active flash, but not queued.
  if ((attack & Hit::retangible) == Hit::retangible) {
    currentStatuses &= ~Hit::flash;
  }

  // Flinch|Flash cancels existing Freeze|Stun
  if ((attack & flinch_flash) == flinch_flash) {
    // If stun is already active, flinch | flash will prevent it from 
    // being added by this attack. That also means a freeze could be
    // committed.
    if ((currentStatuses & Hit::stun) == Hit::stun) {
      attack &= ~Hit::stun;
    }

    currentStatuses &= ~(Hit::freeze | Hit::stun);
  }
  // If attack did not have Flinch|Flash, some statuses are interested in 
  // checking one of these flags.
  else {
    // If stunned is active, prevent flinch
    /*
      This should mean that an attack which ends stun and flinches will not flinch.
      The only way for that to be the case when the attack doesn't also flash is 
      with Drag | Flinch. TODO: Does an attack like that exist to test?
    */
    if (((currentStatuses & Hit::stun) == Hit::stun) && (attack & Hit::flinch)) {
      attack &= ~Hit::flinch;
    }
  }

  // Drag cancels existing and queued Freeze
  // Additionally cancels current or queued Stun and Freeze if Player has Drag
  if ((attack & Hit::drag) == Hit::drag) {
    currentStatuses &= ~Hit::freeze;
    // TODO: It would be more correct to handle this in GetAppliedFlags, and 
    // GetAppliedFlags is set up to do this. But because Drag handling only looks
    // at the Drag flag, the queued status is never removed. Find a way to make this 
    // cleaner.
    queuedStatuses &= ~Hit::freeze;

    // Cancel current or queued Stun and Freeze if Player has Drag.
    // Base this on Entity::slideFromDrag, as it more accurately 
    // represents the special case of Drag.
    if (owner.slideFromDrag) {
      currentStatuses &= ~(Hit::stun | Hit::freeze);
      queuedStatuses &= ~(Hit::stun | Hit::freeze);
    }
  }

  Hit::Flags toApply = GetAppliedFlags(attack);
  currentStatuses |= toApply;
}

/*
  TODO: Should OnUpdate tick and remove at the same time? This may result in some off-by-one error. 
*/
void StatusBehaviorDirector::OnUpdate(double elapsed) {
  frame_time_t _elapsed = from_seconds(elapsed);

  // Update only Drag if Entity is under Drag.
  // Base this on Entity::slideFromDrag, as it more accurately 
  // represents the special case of Drag.
  // TODO: Because this is set false after move ends, Drag ends at EoF instead of start of next frame after movement. Good, bad?
  if (owner.slideFromDrag) {
    AppliedStatus& drag = GetStatus(Hit::drag);

    // Tick time down and remove even though Drag status is handled 
    // more through Entity::slideFromDrag. The Hit::drag tracked on 
    // this is still used to trigger status callbacks on hit, so tick 
    // it down and remove as normal. 
    drag.remainingTime -= _elapsed;

    if (drag.remainingTime > frames(0)) {
      return;
    }

    currentStatuses &= ~Hit::drag;

    return;
  }

  auto keyTestThunk = [this](const InputEvent& key) {
    return owner.InputState().Has(key);
  };

  bool anyKey = keyTestThunk(InputEvents::pressed_use_chip);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_move_down);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_move_up);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_move_left);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_move_right);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_shoot);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_special);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_shoulder_left);
  anyKey = anyKey || keyTestThunk(InputEvents::pressed_shoulder_right);

  Hit::Flags checkBit = 1;
  while (checkBit != 0) {
    Hit::Flags statusBit = currentStatuses & checkBit;
    checkBit = checkBit << 1;
    if (statusBit == 0) {
      continue;
    }

    AppliedStatus& status = GetStatus(statusBit);

    if (anyKey && (statusBit & (Hit::stun | Hit::freeze | Hit::bubble))) {
      status.remainingTime -= _elapsed;
    }

    status.remainingTime -= _elapsed;
    if (status.remainingTime <= frames(0)) {
      currentStatuses &= ~statusBit;
      continue;
    }
  }
};

AppliedStatus& StatusBehaviorDirector::GetStatus(Hit::Flags flag) {
  AppliedStatus& status = statusMap[flag];
  // Cover for default constructed if index did not exist
  status.statusFlag = flag;
  return status;
};

void StatusBehaviorDirector::ClearStatus() {
  for (auto& [_, status] : statusMap) {
      status.remainingTime = frames(0);
  }

  queuedStatuses = Hit::none;
  currentStatuses = Hit::none;
};

void StatusBehaviorDirector::ClearStatus(Hit::Flags flags) {

  // Start from lowest bit
  Hit::Flags curFlag = flags & -flags;
  while (flags > 0) {
    if (flags & curFlag) {
      AppliedStatus& status = statusMap[curFlag];
      status.remainingTime = frames(0);
      queuedStatuses &= ~curFlag;
      currentStatuses &= ~curFlag;

      flags &= ~curFlag;
    }

    curFlag = curFlag << 1;
  }
 
}

const Hit::Flags StatusBehaviorDirector::GetQueuedStatuses() const {
  return queuedStatuses;
}

const bool StatusBehaviorDirector::IsApplied(Hit::Flags flag) const {
  return (currentStatuses & flag) == flag;
}

const bool StatusBehaviorDirector::HasStatus(Hit::Flags flag) const {
  return ((currentStatuses | queuedStatuses) & flag) == flag;
}

const bool StatusBehaviorDirector::HasAnyStatusFrom(Hit::Flags flags) const {
  return ((currentStatuses | queuedStatuses) & flags);
}

const Hit::Flags StatusBehaviorDirector::GetCurrentStatuses() const {
  return currentStatuses;
}

StatusBehaviorDirector::~StatusBehaviorDirector() {
};
