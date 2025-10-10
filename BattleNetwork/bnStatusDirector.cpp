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

  static const Hit::Flags stunMask = ~(Hit::flinch | Hit::freeze | Hit::confuse);
  static const Hit::Flags freezeMask = ~(Hit::flinch | Hit::flash | Hit::confuse);

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
  // Process only Drag and Flinch if Drag is queued. 
  if ((queuedStatuses & Hit::drag) == Hit::drag) {
    ProcessFlags(queuedStatuses & (Hit::drag | Hit::flinch));

    // Drag is a special case where most behavior is handled 
    // based on this bool. Set true after processing.
    owner.slideFromDrag = true;

    queuedStatuses &= ~(Hit::drag | Hit::flinch);
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

  // Drag cancels existing and queued Freeze
  // Additionally cancels current or queued Stun and Freeze if Player has Drag
  // Does not take freeze out of the attack, since that will be handled by 
  // GetAppliedFlags.
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

  // Flinch|Flash cancels existing Freeze|Stun
  if ((attack & flinch_flash) == flinch_flash) {
    // If stun is already active, flinch | flash will prevent it from 
    // being added by this attack. That also means a freeze could be
    // committed, which is correct. Removing Freeze and Stun after this 
    // makes no difference in the end result if Freeze is finally 
    // applied.
    if ((currentStatuses & Hit::stun) == Hit::stun) {
      attack &= ~Hit::stun;
    }

    currentStatuses &= ~(Hit::freeze | Hit::stun);
  }
  // If attack did not have Flinch|Flash, some statuses are interested in 
  // checking one of these flags.
  else {
    // If stunned is active, prevent flinch.
    // Note that this correctly does not happen if Drag removed the active 
    // Hit::stun, as that check ran before this one.
    //
    // This correctly implies that an attack that confuses and flinches, but does 
    // not flash, will cancel stun and still will not flinch. 
    if ((currentStatuses & Hit::stun) == Hit::stun) {
      attack &= ~Hit::flinch;
    }
  }

  Hit::Flags toApply = GetAppliedFlags(attack);

  // At this point, toApply can have Stun OR Freeze, but not both.
  // If Freeze is there, remove active Stun, and vice versa.
  // Both remove active Confuse.
  if (toApply & Hit::stun) {
    currentStatuses &= ~(Hit::freeze | Hit::confuse);
  } else if (toApply & Hit::freeze) {
    currentStatuses &= ~(Hit::stun | Hit::confuse);
  }

  // If Confuse is still here after GetAppliedFlags, it must not have been filtered 
  // off by Stun or Freeze. It will remove an active Stun or Freeze.
  if (toApply & Hit::confuse) {
    currentStatuses &= ~(Hit::stun | Hit::freeze);
  }

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
  // Because this is set false after move ends, Drag ends at EoF 
  // instead of start of next frame after movement, contrary to 
  // other statuses.
  if (owner.slideFromDrag) {
    AppliedStatus& drag = GetStatus(Hit::drag);

    /*
      Tick time down and remove Drag even though Drag effects may continue.

      Drag is a special case where the status is active for an 
      indeterminable amount of time, so its effects are based on 
      Entity::sildeFromDrag instead of this tracked Hit::drag. This means the
      status can safely be removed long before its effects are over. 

      To trigger status callbacks on hit, Hit::drag still passes through the 
      StatusBehaviorDirector, so tick it down and remove as with other statuses. 

      Note that Flinch and Flash are allowed to process with Flinch, but do not 
      count down here. Flinch's remaining time is inconsequential to the Entity.
    */
    
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

void StatusBehaviorDirector::ClearAllStatuses() {
  for (auto& [_, status] : statusMap) {
      status.remainingTime = frames(0);
  }

  queuedStatuses = Hit::none;
  currentStatuses = Hit::none;
};

void StatusBehaviorDirector::ClearStatuses(Hit::Flags flags) {

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
