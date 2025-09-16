#pragma once

#include "bnLogger.h"
#include "bnHitProperties.h"
#include "bnFrameTimeUtils.h"
#include "bnInputEvent.h"
#include <map>

struct AppliedStatus {
  Hit::Flags statusFlag{};
  frame_time_t remainingTime{};
};

class Entity;

class StatusBehaviorDirector {
public:
    StatusBehaviorDirector(Entity& owner);
    virtual ~StatusBehaviorDirector();
    void AddStatus(Hit::Flags statusFlag, frame_time_t maxCooldown);
    void AddStatus(Hit::Flags statusFlag);

    /*
      Ticks timers on all current statuses. The result of GetCurrentStatuses
      may be different before and after calling this.

      It is expected that ProcessPendingStatuses is called before this. These
      two functions are separate so that statuses may be updated separately 
      from processing, for example when the Entity is sliding from Drag.

      If Hit::drag is a current status, only its associated timer will be reduced.
      Remaining statuses will be skipped unless this is called while Hit::drag is 
      not a current status. This means that a call to OnUpdate that results in Hit::drag 
      being removed from the current statuses will not tick any other timers.
    */   
    void OnUpdate(double elapsed);
    AppliedStatus& GetStatus(Hit::Flags flag);
    const Hit::Flags GetQueuedStatuses() const;
    const Hit::Flags GetCurrentStatuses() const;
    void ClearStatus();
    /*
      Clear specific flags from queued and active statuses. 
      Parameter flags may contain multiple Hit::Flags bits set. Each corresponding 
      status will be cleared.
    */
    void ClearStatus(Hit::Flags flags);
    /*
      Process current queuedStatuses. The result of GetQueuedStatuses and 
      GetCurrentStatuses may be different before and after calling this. 
      
      If Hit::drag is queued or is a current status, statuses will only be partially 
      processed. 
    */
    void ProcessPendingStatuses();
    /*
      Returns true if flag is uncommitted or applied.
    */ 
    const bool HasStatus(Hit::Flags flag) const;
    /*
      Returns true only if flag isapplied.
    */
    const bool IsApplied(Hit::Flags flag) const;
private:
    Entity& owner;
    std::vector<InputEvent> lastFrameStates;
    std::map<Hit::Flags, AppliedStatus> statusMap;
    Hit::Flags currentStatuses{};
    Hit::Flags queuedStatuses{};

    void ProcessFlags(Hit::Flags attack);

    // Returns Hit::Flags that would be applied from input flags.
    Hit::Flags GetAppliedFlags(Hit::Flags flags);
};
