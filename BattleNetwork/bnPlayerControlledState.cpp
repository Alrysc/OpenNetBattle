#include "bnPlayerControlledState.h"
#include "bnInputManager.h"
#include "bnPlayer.h"
#include "bnCardAction.h"
#include "bnTile.h"
#include "bnPlayerSelectedCardsUI.h"
#include "bnAudioResourceManager.h"

#include <iostream>

PlayerControlledState::PlayerControlledState() : 
  AIState<Player>(), 
  InputHandle(),
  isChargeHeld(false)
{
}


PlayerControlledState::~PlayerControlledState()
{
}

void PlayerControlledState::OnEnter(Player& player) {
  player.MakeIdle();
}

void PlayerControlledState::OnUpdate(double _elapsed, Player& player) {
  // Actions with animation lockout controls take priority over movement
  const bool lockout = player.IsLockoutAnimationComplete();
  const bool isIdle = player.IsIdle();
  const bool canAttack = player.CanAttack();
  const bool isMoving = player.IsMoving();
  const bool isDragged = player.IsStatusApplied(Hit::drag);

  // One of our ongoing animations is preventing us from charging
  if (!lockout) {
    player.chargeEffect->SetCharging(false);
    isChargeHeld = false;
    return;
  }

  /*
    This state may have been paused while the Player was stunned or 
    frozen (see Entity::Update). If so, the charge time may have been 
    reset since the last time this updated. This accounts for that by 
    resetting the isChargeHeld variable, avoiding scenarios where an 
    attack could be made because a Player had Shoot held before being 
    stunned and released once it had ended.
  */
  isChargeHeld = isChargeHeld && player.chargeEffect->GetChargeTime() > frames(0);

  const bool missChargeKey = isChargeHeld && !player.InputState().Has(InputEvents::held_shoot);

  // Are we creating an action this frame?
  if (player.InputState().Has(InputEvents::pressed_use_chip)) {
    std::shared_ptr<PlayerSelectedCardsUI> cardsUI = player.GetFirstComponent<PlayerSelectedCardsUI>();
   
    if (cardsUI && canAttack && cardsUI->UseNextCard()) {
      player.chargeEffect->SetCharging(false);
      isChargeHeld = false;
    }
    // If the card used was successful, we may have a card in queue
  }
  else if (player.InputState().Has(InputEvents::released_special)) {
    const std::vector<std::shared_ptr<CardAction>> actions = player.AsyncActionList();
    bool canUseSpecial = canAttack;

    // Just make sure one of these actions are not from an ability
    for (const std::shared_ptr<CardAction>& action : actions) {
      canUseSpecial = canUseSpecial && action->GetLockoutGroup() != CardAction::LockoutGroup::ability;
    }

    if (canUseSpecial) {
      player.UseSpecial();
    }
  } // queue attack based on input behavior (buster or charge?)
  else if (player.InputState().Has(InputEvents::released_shoot) || missChargeKey) {
    // This routine is responsible for determining the outcome of the attack.
    // It is not yet determined that the attack will be added to the queue. 
    // Whether it is or not, charge is reset.
    isChargeHeld = false;
    player.chargeEffect->SetCharging(false);
    

    // TODO: This condition could be somewhat complicated. 
    // It might make more sense to add a discard filter to ActionQueue that 
    // discards anything but movement while CanAttack is false.
    // It is like this now because you must be able to queue attack while
    // moving, but movement could be due to Drag, where you cannot queue.
    // You also cannot queue while you cannot act.
    if ((isMoving && !isDragged) || canAttack) {
      player.Attack();
    }
      

  } else if (player.InputState().Has(InputEvents::held_shoot)) {
    /* 
      During Drag, Player may not be moving, but could also not be idle.
      This means isIdle || IsMoving is false, yet they can charge.
      To cover for this case, check for Drag.
    */
    if (isIdle || isMoving || isDragged) {
      isChargeHeld = true;
      player.chargeEffect->SetCharging(true);
    }
  }

  // Movement increments are restricted based on anim speed at this time
  if (isMoving || !canAttack) return;

  Direction direction = Direction::none;
  if (player.InputState().Has(InputEvents::pressed_move_up) || player.InputState().Has(InputEvents::held_move_up)) {
    direction = Direction::up;
  }
  else if (player.InputState().Has(InputEvents::pressed_move_left) || player.InputState().Has(InputEvents::held_move_left)) {
    direction = player.GetTeam() == Team::red ? Direction::left : Direction::right;
  }
  else if (player.InputState().Has(InputEvents::pressed_move_down) || player.InputState().Has(InputEvents::held_move_down)) {
    direction = Direction::down;
  }
  else if (player.InputState().Has(InputEvents::pressed_move_right) || player.InputState().Has(InputEvents::held_move_right)) {
    direction = player.GetTeam() == Team::red ? Direction::right : Direction::left;
  }

  if (player.HasStatus(Hit::confuse)) {
    direction = Reverse(direction);
  }

  if(direction != Direction::none && isIdle && !player.IsRooted()) {
    Battle::Tile* next_tile = player.GetTile() + direction;
    std::shared_ptr<AnimationComponent> anim = player.GetFirstComponent<AnimationComponent>();

    auto onMoveBegin = [player = &player, next_tile, this, anim] {
      const std::string& move_anim = player->GetMoveAnimHash();

      anim->CancelCallbacks();

      auto idle_callback = [player]() {
        player->MakeIdle();
      };

      anim->SetAnimation(move_anim, idle_callback);

      //anim->SetInterruptCallback(idle_callback);
    };

    if (player.playerControllerSlide) {
      player.Slide(next_tile, player.slideFrames, frames(0), ActionOrder::voluntary);
    }
    else {
      player.Teleport(next_tile, ActionOrder::voluntary, onMoveBegin);
    }
  } 
}

void PlayerControlledState::OnLeave(Player& player) {
  /* Navis lose charge when we leave this state */
  player.Charge(false);
}
