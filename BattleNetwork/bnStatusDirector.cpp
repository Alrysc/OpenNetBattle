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
    queuedStatuses &= ~Hit::drag;
    return;
  }

  // Do not process other flags if Drag is a current status
  if((currentStatuses & Hit::drag) == Hit::drag) {
    return;
  }

  
  if (queuedStatuses == 0) {
    return;
  }

  ProcessFlags(queuedStatuses);
  queuedStatuses = Hit::none;
}

void StatusBehaviorDirector::ProcessFlags(Hit::Flags attack) {
  // Timestop check. 
  // TODO: Behavior does not currently happen here, but instead in Entity::Hit.
  // Move that here, or remove this check.
  if (false && (currentStatuses & Hit::freeze) == Hit::freeze) {
    attack &= ~Hit::freeze;
  }

  // Retangible removes active flash, but not queued.
  if ((attack & Hit::retangible) == Hit::retangible) {
    currentStatuses &= ~Hit::flash;
  }

  // Flinch|Flash cancels existing Freeze|Stun
  if ((attack & (Hit::flinch | Hit::flash)) == (Hit::flinch | Hit::flash)) {
    // If stun is already active, flinch | flash will prevent it from 
    // being added by this attack. That also means a freeze could be
    // committed.
    if ((currentStatuses & Hit::stun) == Hit::stun) {
      attack &= ~Hit::stun;
    }

    currentStatuses &= ~(Hit::freeze | Hit::stun);
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

    if ((currentStatuses & Hit::drag) == Hit::drag) {
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

  if ((currentStatuses & Hit::drag) == Hit::drag) {
    AppliedStatus& drag = GetStatus(Hit::drag);

    drag.remainingTime -= _elapsed;

    if (drag.remainingTime > frames(0)) {
      return;
    }

    currentStatuses &= ~Hit::drag;

    /* 
      Other statuses never tick if Drag was handled during update, even
      if Drag ended on this tick.

      This is also safe with regards to Character::CanAttack's goal - even 
      though Drag ended and a queued blocking status has not become active, 
      CanAttack will return false this frame because of the cached part of 
      CanAttack. It will also still return false for all relevant parts of 
      the Entity::Update routine next frame, because a queued blocking status 
      would become active near start of update, when StatusBehaviorDirector::OnUpdate 
      next runs.
    */
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

const Hit::Flags StatusBehaviorDirector::GetCurrentStatuses() const {
  return currentStatuses;
}

StatusBehaviorDirector::~StatusBehaviorDirector() {
};
