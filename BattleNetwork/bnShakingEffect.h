#pragma once
#include "bnComponent.h"
#include <SFML/Graphics.hpp>
#include "frame_time_t.h"
class BattleSceneBase;
class Entity;


class ShakingEffect : public Component {
private:
  bool isShaking;
  frame_time_t shakeDur; /*!< Duration of shake effect */
  double stress; /*!< How much stress to apply to shake */
  frame_time_t shakeProgress;
  sf::Vector2f startPos;
  BattleSceneBase* bscene;
public:
  ShakingEffect(std::weak_ptr<Entity> owner);
  ~ShakingEffect();
  void OnUpdate(double _elapsed) override;
  void Inject(BattleSceneBase&) override;
};
