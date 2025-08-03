#include "bnShakingEffect.h"
#include "bnEntity.h"
#include "battlescene/bnBattleSceneBase.h"

ShakingEffect::ShakingEffect(std::weak_ptr<Entity> owner) : 
  shakeDur(frames(21)),
  stress(3),
  shakeProgress(frames(0)),
  startPos(owner.lock()->getPosition()),
  bscene(nullptr),
  isShaking(false),
  Component(owner, Component::lifetimes::ui)
{
}

ShakingEffect::~ShakingEffect()
{
}

void ShakingEffect::OnUpdate(double _elapsed) {
  auto owner = GetOwner();
  shakeProgress += frames(1);

  if (owner && shakeProgress <= shakeDur) {
    // Drop off to zero by end of shake
    double currStress = stress * (1.0 - (shakeProgress.count() / (double)shakeDur.count()));

    int randomAngle = (int)(shakeProgress.count()) * (rand() % 360);
    randomAngle += (150 + (rand() % 60));

    auto shakeOffset = sf::Vector2f(std::sin(static_cast<float>(randomAngle * currStress)), std::cos(static_cast<float>(randomAngle * currStress)));

    // We add, reposition, and then reset the tile offset so that we do not
    // accidentally accumulate the shake noise which will misplace the entity
    // over several frames.
    // Simply: the entity will snap back to its previous position the next frame.
    const sf::Vector2f tileOffset = owner->GetTileOffset();
    owner->SetTileOffset(tileOffset + shakeOffset);
    owner->RefreshPosition();
    owner->SetTileOffset(tileOffset);
  }
  else {
    Eject();
  }
}

void ShakingEffect::Inject(BattleSceneBase&bscene)
{
  bscene.Inject(shared_from_base<ShakingEffect>());
  ShakingEffect::bscene = &bscene;
}
