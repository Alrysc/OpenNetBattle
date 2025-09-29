#pragma once
#include <functional>
#include "bnEntity.h"
#include "bnFrameTimeUtils.h"


class Entity;
class Battle::Tile;
class MoveAction;

typedef std::function<void()> VoidCallback;

struct MoveEvent {
  std::shared_ptr<MoveAction> move;
};

/*
  Contains data used to create a generic MoveEventClass.
  Useful as shorthand for creating a new MoveEventClass through
  RawMoveEvent, or as an entry point to creating a C++ MoveEventClass
  from scripting.
*/
struct MoveData {
  Battle::Tile* dest{ nullptr };
  frame_time_t deltaFrames{};  //!< Frames between tile A and B. If 0, teleport. Else, we could be sliding
  frame_time_t delayFrames{};  //!< Startup lag to be used with animations
  frame_time_t endlagFrames{}; //!< Wait period before action is complete
  float height{};              //!< If this is non-zero with delta frames, the character will effectively jump
  VoidCallback onBegin = [] {};

  bool immutable{ false }; //!< Some move events cannot be cancelled or interupted
};

class MoveAction {
public:
  MoveData data;

  MoveAction(std::weak_ptr<Entity> owner, const MoveData& data);

  // The underlining move event data may have completed, but the move action
  // as a whole may queue additional move events (e.g. DragAction).
  // If [IsPendingFinish] is true, then this action is also a candidate,
  // but not necessarily going to return true.
  // This will only return true when the MoveAction is fully completed.
  bool IsFinished() const;

  float GetHeight() const;

  bool IsJumping() const;
  bool IsSliding() const;
  bool IsTeleporting() const;
  void OnUpdate(frame_time_t elapsed);

  // Explicitly calls one frame of [OnUpdate].
  virtual void Update() { OnUpdate(frames(1)); }
protected:
  std::weak_ptr<Entity> owner;
  frame_time_t elapsedFrames{};
  /*
    Whether or not the MoveEvent is complete.
    When true, [OnUpdate] is a no-op.
 */
  bool completed{ false };
  // Whether or not the destination was reached. Only false during OnPostMove 
  // if OnUpdate determined the dest could not be reached.
  bool reachedDest{ false };

  virtual void Begin();

  /*
    Called during [OnUpdate] to determine whether or not the current
    movement action animation is finished. If true, [OnUpdate] will call [OnPostMove].
    Note, while the move action's animation may be finished, this does not
    necessarily indicate that this is the last move.
  */
  virtual bool IsPendingFinish() const;

  /*
    Run by OnUpdate when IsFinishedMoving returns true.
    However this may not be the last move.
    It will perform final events for the movement, such as marking completed
    or making calls for the additional movement.
  */
  virtual void OnPostMove();

  /*
    Terminates the MoveEvent
  
  virtual void Interrupt() = 0;
  */

  /*
    Prepares the MoveEvent for a new movement using given
    parameters.
  */
  virtual void ResetWith(const MoveData& newData);

  // Helpers for accessing protected members on Entity 
  // through MoveAction friendship
  sf::Vector2f GetOwnerStartPosition();
  void SetOwnerStartPosition(sf::Vector2f offset);
  void SetOwnerJumpHeight(float height);
  void SetOwnerPreviousDirection(Direction dir);
  Battle::Tile* GetOwnerPreviousTile();
  void UpdateMoveStartPosition();
  // Sets Entity::previous to data.dest (or Entity's current Tile if nullptr). 
  // Used as part of ResetWith to record previous Tile, which allows AdoptTile
  // to work without removing the Entity on multiple Tiles. 
  void UpdatePreviousTile();
};

class DragAction : public MoveAction {
private:
  /* 
    Whether or not the last movement is in progress.
    After this movement finished, completed is set true.
  */
  bool startedFinalMove{ false };
  /*
    Whether or not the first movement is in progress.
    Used to determine movement timing.
    Set false during PostMove.
  */
  bool firstMove{ true };
  Hit::Drag drag{};

  /*
    Determines destination and move time based on drag.
    Sets dest modifies drag, and may change firstMove and startedFinalMove.
  */
  void PrepareMovement();
  /*
    Resets movement with parameters for the last movement 
    of Drag. Targets the current Tile as the destination, 
    and uses a high move time based on firstMove to keep 
    the Entity in place.
  */
  void PrepareFinalMove();
protected:
  void Begin() override;
  void OnPostMove() override;

public:
  DragAction(std::weak_ptr<Entity> owner, Hit::Drag drag);

};