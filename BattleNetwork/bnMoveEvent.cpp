#include "bnMoveEvent.h"
#include "bnTile.h"
#include "bnWaterSplash.h"
#include "bnField.h"
#include <Swoosh/Ease.h>

/// class MoveAction ///

MoveAction::MoveAction(std::weak_ptr<Entity> owner, const MoveData& data)
  : owner(owner), data(data) 
{
}

void MoveAction::Begin() 
{
  if (!data.onBegin)
    return;

  data.onBegin();
  data.onBegin = nullptr;
}


bool MoveAction::IsFinished() const
{
  return completed;
}

float MoveAction::GetHeight() const 
{
  return data.height;
}

//!< helper function true if jumping
bool MoveAction::IsJumping() const 
{
  return data.dest && data.height > 0.f && data.deltaFrames > frames(0);
}

//!< helper function true if sliding
bool MoveAction::IsSliding() const 
{
  return data.dest && data.deltaFrames > frames(0) && data.height <= 0.0f;
}

//!< helper function true if normal moving
bool MoveAction::IsTeleporting() const 
{
  return data.dest && data.deltaFrames == frames(0) && (+data.height) == 0.0f;
}

sf::Vector2f MoveAction::GetOwnerStartPosition() 
{
  return owner.lock()->moveStartPosition;
}

void MoveAction::SetOwnerStartPosition(sf::Vector2f offset) 
{
  owner.lock()->moveStartPosition = offset;
}

void MoveAction::SetOwnerJumpHeight(float height) 
{
  owner.lock()->currJumpHeight = height;
}

void MoveAction::SetOwnerPreviousDirection(Direction dir) 
{
  owner.lock()->previousDirection = dir;
}

Battle::Tile* MoveAction::GetOwnerPreviousTile() 
{
  return owner.lock()->previous;
}

void MoveAction::UpdateMoveStartPosition() {
  owner.lock()->UpdateMoveStartPosition();
}

void MoveAction::OnUpdate(frame_time_t elapsed) {
  if (completed) {
    return;
  }

  // Some MoveActions may determine Tile in Begin. Let them do so 
  // before terminating for nullptr dest.
  Begin();

  auto owner = this->owner.lock();

  // Only move if we have a valid next tile pointer.
  // Move is marked complete if there is no destination.
  if (!data.dest)
  {
    if (owner->GetTile())
    {
      owner->RefreshPosition();
    }

    completed = true;
    return;
  }

  // Only move if we have a valid next tile pointer

  Battle::Tile* next = data.dest;
  Battle::Tile* currTile = owner->GetTile();

  elapsedFrames += elapsed;

  if (elapsedFrames > data.delayFrames) {
    // Get a value from 0.0 to 1.0
    float duration = seconds_cast<float>(data.deltaFrames);
    float delta = swoosh::ease::linear(static_cast<float>((elapsedFrames - data.delayFrames).asSeconds().value), duration, 1.0f);

    sf::Vector2f pos = GetOwnerStartPosition();
    sf::Vector2f tar = next->getPosition();

    sf::Vector2f tileOffset = owner->GetTileOffset();
    // Interpolate the sliding position from the start position to the end position
    sf::Vector2f interpol = tar * delta + (pos * (1.0f - delta));
    tileOffset = interpol - pos;

    // Once halfway, entities switch to the next tile
    // and the slide position offset must be readjusted 
    if (delta >= 0.5f) {
      // conditions of the target tile may change, ensure by the time we switch
      if (owner->CanMoveTo(next)) {
        reachedDest = true;
        if (currTile != next) {
          owner->AdoptNextTile();
        }

        // Adjust for the new current tile, begin halfway approaching the current tile
        tileOffset = -tar + pos + tileOffset;
      }
      else {
        // Slide back into the origin tile if we can no longer slide to the next tile
        SetOwnerStartPosition(next->getPosition());
        data.dest = currTile;

        tileOffset = -tar + pos + tileOffset;
      }
    }

      

    float heightElapsed = static_cast<float>((elapsedFrames - data.delayFrames).asSeconds().value);
    float heightDelta = swoosh::ease::wideParabola(heightElapsed, duration, 1.0f);
      
    SetOwnerJumpHeight(heightDelta * data.height);
    tileOffset.y -= owner->GetCurrJumpHeight();
    owner->SetTileOffset(tileOffset);

    // When delta is 1.0, the slide duration is complete
    if (delta == 1.0f)
    {
      // Slide or jump is complete, clear the tile offset used in those animations
      tileOffset = { 0, 0 };
      owner->SetTileOffset(tileOffset);

      if (IsPendingFinish()) {
        OnPostMove();
      }
    }
  }
}

bool MoveAction::IsPendingFinish() const {
  return elapsedFrames >= (data.delayFrames + data.deltaFrames + data.endlagFrames);
}

void MoveAction::OnPostMove() {
  auto owner = this->owner.lock();

  Battle::Tile* prevTile = GetOwnerPreviousTile();
  Direction previousDirection = owner->GetMoveDirection();
  Battle::Tile* currTile = owner->GetTile();

  /*
    Do not check ice slide if the same Tile was moved to.
    This prevents a case where sliding to your own Tile would
    infinitely slide in place.

    This same check is not used on Sand or Sea, so an Entity would
    become rooted when moving to their own Tile in those cases.
  */
  const bool sameTile = prevTile == currTile;
  bool willIceSlide = false;

  std::shared_ptr<Field> field = owner->GetField();

  if (!sameTile && currTile->GetState() == TileState::ice && !owner->HasFloatShoe()) {
    const int tileX = currTile->GetX();
    const int tileY = currTile->GetY();

    Battle::Tile* next = nullptr;
    if (prevTile->GetX() > tileX) {
      next = field->GetAt(tileX - 1, tileY);
      previousDirection = Direction::left;
    }
    else if (prevTile->GetX() < tileX) {
      next = field->GetAt(tileX + 1, tileY);
      previousDirection = Direction::right;
    }
    else if (prevTile->GetY() < tileY) {
      next = field->GetAt(tileX, tileY + 1);
      previousDirection = Direction::down;
    }
    else if (prevTile->GetY() > tileY) {
      next = field->GetAt(tileX, tileY - 1);
      previousDirection = Direction::up;
    }

    // If the next tile is not available, not ice, or we are ice element, don't slide
    bool notIce = (next && currTile->GetState() != TileState::ice);
    bool cannotMove = (next && !owner->CanMoveTo(next));
    bool weAreIce = (owner->GetElement() == Element::aqua);
    bool cancelSlide = (notIce || cannotMove || weAreIce);

    willIceSlide = owner->WillSlideOnTiles() && !cancelSlide;
  }

  SetOwnerPreviousDirection(previousDirection);

  // If we slide onto an ice block and we don't have float shoe enabled, slide
  if (willIceSlide) {
    ResetWith({ currTile + previousDirection, frames(4), frames(0), frames(0), 0.f, nullptr });
    return;
  }

  completed = true;

  // TODO: Determine if these really should wait for endlag to be finished.
  // It's possible OnPostMove should run before endlag is considered, which 
  // could overwrite endlag for ice slide.
  if (currTile->GetState() == TileState::sea && owner->GetElement() != Element::aqua && !owner->HasFloatShoe()) {
    owner->AddStatus(Hit::root, frames(20));
    auto splash = std::make_shared<WaterSplash>();
    field->AddEntity(splash, *currTile);
  }
  else if (currTile->GetState() == TileState::sand && !owner->HasFloatShoe()) {
    owner->AddStatus(Hit::root, frames(20));
  }
}

void MoveAction::UpdatePreviousTile() {
  auto owner = this->owner.lock();
  owner->previous = data.dest ? data.dest : owner->GetTile();
}

void MoveAction::ResetWith(const MoveData& newData)
{
  elapsedFrames = frames(0);
  reachedDest = false;
  UpdatePreviousTile();

  data = newData;
  // Calculate our new Entity's position
  // Without this, Entity will appear offset and then slingshot back as ice
  // move starts.
  UpdateMoveStartPosition();
}



// dest is nullptr until Begin
DragAction::DragAction(std::weak_ptr<Entity> owner, Hit::Drag drag) : drag(drag), MoveAction(owner, {}) 
{
}

void DragAction::Begin() {
  if (!firstMove) {
    return;
  }
  PrepareMovement();
  MoveAction::Begin();
  firstMove = false;
}

void DragAction::PrepareFinalMove() {
  startedFinalMove = true;
  auto owner = this->owner.lock();
  /*
    On timing:

    Character::CanAttack should return false 26 times (25 while moving, +1 for the cached time)
    if firstMove is true. Otherwise, or 26 times (25 while moving +1 for the cached time) otherwise.
    These timings achieve this.

    Otherwise, the total inactionable time is 27 frames if pushed one Tile, 31 if two, etc. 
    A move time of 23 achieves this, since each move is 4 frames.

    Note that, because 
  */
  ResetWith(MoveData{ owner->GetTile(), frames(firstMove ? 26 : 23), frames(0), frames(0), 0.f, nullptr});
}

void DragAction::PrepareMovement() {
  if (completed) {
    return;
  }

  auto owner = this->owner.lock();
  Battle::Tile* currTile = owner->GetTile();
  const bool noDir = drag.dir == Direction::none;
  Battle::Tile* dest = noDir ? currTile : owner->GetTile(drag.dir, 1);

  if (!(dest && currTile)) {
    PrepareFinalMove();
    return;
  }


  const bool canReachDest = owner->Teammate(currTile->GetTeam()) && owner->CanMoveTo(dest);
  // False if firstMove true, because reachedDest is always false on the first 
  // movement. 
  const bool canIceSlide = this->reachedDest && owner->WillSlideOnTiles() && currTile->GetState() == TileState::ice && owner->GetElement() != Element::aqua;


  // Ice slide allows 0 count Drag to continue moving.
  if (noDir || !canReachDest || (drag.count == 0 && !canIceSlide)) {
    PrepareFinalMove();
    return;
  }

  if (drag.count > 0) {
    drag.count--;
  }

  ResetWith(MoveData{ dest, frames(4), frames(0), frames(0), 0.f, nullptr });
}
void DragAction::OnPostMove() {
  // The final movement has finished. The action is done.
  if (startedFinalMove) {
    completed = true;
    return;
  }

  PrepareMovement();
}
