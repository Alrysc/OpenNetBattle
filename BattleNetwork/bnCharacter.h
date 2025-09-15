#pragma once
#include "bnEntity.h"
#include "bnDefenseFrameStateJudge.h"
#include "bnDefenseRule.h"
#include "bnHitProperties.h"
#include "bnTile.h"
#include "bnActionQueue.h"
#include "bnCardUsePublisher.h"

#include <string>
#include <functional>
#include "stx/memory.h"

namespace Battle {
  class Tile;
}

class DefenseRule;
class Spell;
class CardAction;
class SelectedCardsUI;
class CardPackageManager;

struct CardEvent {
  std::shared_ptr<CardAction> action;
};

struct PeekCardEvent {
  SelectedCardsUI* publisher{ nullptr };
};

constexpr frame_time_t CARD_ACTION_ARTIFICIAL_LAG = frames(5);

/**
 * @class Character
 * @author mav
 * @date 05/05/19
 * @brief Characters are mobs, enemies, and players. They have health and can take hits.
 */
class Character: 
  public Entity,
  public CardActionUsePublisher
{
  friend class Field;
  friend class AnimationComponent;

private:
  std::vector<std::shared_ptr<CardAction>> asyncActions;
  std::shared_ptr<CardAction> currCardAction{ nullptr };
  frame_time_t cardActionStartDelay{0};
public:

  /**
   * @class Rank
   * @breif A single definition of a character can have adjustments based on rank
   */
  enum class Rank : const int {
    _1,
    _2,
    _3,
    SP,
    EX,
    Rare1,
    Rare2,
    NM,
    SIZE
  };

  Character(Rank _rank = Rank::_1);
  virtual ~Character();
  virtual void Cleanup() override;

  virtual void OnBattleStop() override;

  virtual void MakeIdle();
  virtual bool IsIdle() const;

  const bool IsLockoutAnimationComplete();

  const std::vector<std::shared_ptr<CardAction>> AsyncActionList() const;
  std::shared_ptr<CardAction> CurrentCardAction();

  void Update(double elapsed) override;
  
  /**
  * @brief Default characters cannot move onto occupied, broken, or empty tiles
  * @param next
  * @return true if character can move to tile, false otherwise
  */
  virtual bool CanMoveTo(Battle::Tile* next) override;

  /**
  * @brief Whether or not characters are allowed to begin a new action, as 
  * determined by CanAttackImpl and actionBlocked.
  *
  * Currently does not indicate that the Character actually can act, but 
  * expect that some behavior is delayed or skipped while this returns false.
  *
  * @return false if character is unable to act based on CanAttackImpl, or if 
  * CanAttackImpl returned false during the previous Update. Otherwise, true.
  */
  const bool CanAttack() const;

  /**
   * @brief Get the rank of this character
   * @return const Rank
   */
  const Rank GetRank() const;

  void AddAction(const CardEvent& event, const ActionOrder& order);
  void AddAction(const PeekCardEvent& event, const ActionOrder& order);
  void HandleCardEvent(const CardEvent& event, const ActionQueue::ExecutionType& exec);
  void HandlePeekEvent(const PeekCardEvent& event, const ActionQueue::ExecutionType& exec);

protected:
  Character::Rank rank;
  /* 
    Cached result of CanAttackImpl. Part of what determines whether or not 
    characters are free to act.
    
    This exists in order to enforce the idea that characters cannot act on 
    the frame that they visibly become actionable, for example on the frame 
    a CardAction ends or the frame stun ends. 

    Set true based on a call to CanAttackImpl done at the end of Update.
    Also set true by MakeActionable.
  */ 
  bool actionBlocked = false;

  /**
    @brief Called by CanAttack as part of the determination of whether or not 
    characters are free to act.
    @returns true if the Character can act, otherwise false
  */
  virtual const bool CanAttackImpl() const;

};
