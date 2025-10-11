#include "bnEntity.h"
#include "bnComponent.h"
#include "bnTile.h"
#include "bnField.h"
#include "bnPlayer.h"
#include "bnWaterSplash.h"
#include "bnShakingEffect.h"
#include "bnShaderResourceManager.h"
#include "bnTextureResourceManager.h"
#include "bnAudioResourceManager.h"
#include <cmath>
#include <Swoosh/Ease.h>
#include "bnMoveEvent.h"

long Entity::numOfIDs = 0;

bool EntityComparitor::operator()(std::shared_ptr<Entity> f, std::shared_ptr<Entity> s) const
{
  return f->GetID() < s->GetID();
}

bool EntityComparitor::operator()(Entity* f, Entity* s) const
{
  return f->GetID() < s->GetID();
}

// First entity ID begins at 1
Entity::Entity() : 
  lastComponentID(0),
  height(0),
  moveCount(0),
  channel(nullptr),
  mode(Battle::TileHighlight::none),
  hitboxProperties(Hit::DefaultProperties),
  CounterHitPublisher(),
  statuses(*this)
{
  ID = ++Entity::numOfIDs;

  SetColorMode(ColorMode::additive);
  setColor(NoopCompositeColor(ColorMode::additive));

  if (sf::Shader* shader = Shaders().GetShader(ShaderType::BATTLE_CHARACTER)) {
    SetShader(shader);
    SmartShader& smartShader = GetShader();
    smartShader.SetUniform("texture", sf::Shader::CurrentTexture);
    smartShader.SetUniform("additiveMode", true);
    smartShader.SetUniform("swapPalette", false);
    baseColor = sf::Color(0, 0, 0, 0);
  }

  using namespace std::placeholders;
  auto handler = std::bind(&Entity::HandleMoveEvent, this, _1, _2);
  actionQueue.RegisterType<MoveEvent>(ActionTypes::movement, handler);

  whiteout = Shaders().GetShader(ShaderType::WHITE);
  stun = Shaders().GetShader(ShaderType::YELLOW);
  root = Shaders().GetShader(ShaderType::BLACK);
  setColor(NoopCompositeColor(GetColorMode()));

  shadow = std::make_shared<SpriteProxyNode>();
  shadow->SetLayer(1);
  shadow->Hide(); // default: hidden
  AddNode(shadow);

  iceFx = std::make_shared<SpriteProxyNode>();
  iceFx->setTexture(Textures().LoadFromFile(TexturePaths::ICE_FX));
  iceFx->SetLayer(-2);
  iceFx->Hide(); // default: hidden
  AddNode(iceFx);

  blindFx = std::make_shared<SpriteProxyNode>();
  blindFx->setTexture(Textures().LoadFromFile(TexturePaths::BLIND_FX));
  blindFx->SetLayer(-2);
  blindFx->Hide(); // default: hidden
  AddNode(blindFx);

  confusedFx = std::make_shared<SpriteProxyNode>();
  confusedFx->setTexture(Textures().LoadFromFile(TexturePaths::CONFUSED_FX));
  confusedFx->SetLayer(-2);
  confusedFx->Hide(); // default: hidden
  AddNode(confusedFx);

  iceFxAnimation = Animation(AnimationPaths::ICE_FX);
  blindFxAnimation = Animation(AnimationPaths::BLIND_FX);
  confusedFxAnimation = Animation(AnimationPaths::CONFUSED_FX);
}

Entity::~Entity() {
  std::shared_ptr<Field> f = field.lock();
  if (!f) return;
  
  f->ClearAllReservations(ID);
}

void Entity::Cleanup() {
  FreeAllComponents();
}

void Entity::SortComponents()
{
  // Newest components appear first in the list for easy referencing
  std::sort(components.begin(), components.end(), [](std::shared_ptr<Component>& a, std::shared_ptr<Component>& b) { return a->GetID() > b->GetID(); });
}

void Entity::ClearPendingComponents()
{
  queuedComponents.clear();
}

void Entity::ReleaseComponentsPendingRemoval()
{
  // `delete` may kick off deconstructors that Eject() other components
  std::list<Entity::ComponentBucket> copy = queuedComponents;

  // Remove the component from our list
  for (Entity::ComponentBucket& bucket : copy) {
    if (bucket.action != ComponentBucket::Status::remove) continue;

    auto iter = std::find(components.begin(), components.end(), bucket.pending);

    if (iter != components.end()) {
      components.erase(iter);
    }
  }
}

void Entity::InsertComponentsPendingRegistration()
{
  bool sort = queuedComponents.size();

  for (Entity::ComponentBucket& bucket : queuedComponents) {
    if (bucket.action == ComponentBucket::Status::add) {
      components.push_back(bucket.pending);
    }
  }

  sort ? SortComponents() : void(0);
}

void Entity::UpdateMovement(double elapsed) {
  if (!currMoveEvent) {
    if (tile) {
      RefreshPosition();
    }
    return;
  }

  currMoveEvent->OnUpdate(from_seconds(elapsed));

  if (currMoveEvent->IsFinished()) {
    FinishMove();
  }

  if (tile) {
    RefreshPosition();
  }
}

void Entity::SetFrame(unsigned frame)
{
  this->frame = frame;
}

void Entity::Spawn(Battle::Tile& start)
{
  if (!hasSpawned) {
    if (GetFacing() == Direction::none) {
      SetFacing(start.GetFacing());
    }

    hasSpawned = true;

    OnSpawn(start);
  }
}

bool Entity::HasSpawned() {
  return hasSpawned;
}

void Entity::BattleStart()
{
  if (fieldStart) return;

  fieldStart = true;
  OnBattleStart();
}

void Entity::BattleStop()
{
  if (!fieldStart) return;
  OnBattleStop();
}

const float Entity::GetHeight() const {
  return height;
}

void Entity::SetHeight(const float height) {
  Entity::height = std::fabs(height);
}

VirtualInputState& Entity::InputState()
{
  return inputState;
}

const bool Entity::IsSuperEffective(Element _other) const {
  switch(GetElement()) {
    case Element::aqua:
      return _other == Element::elec;
      break;
    case Element::fire:
      return _other == Element::aqua;
      break;
    case Element::wood:
      return _other == Element::fire;
      break;
    case Element::elec:
      return _other == Element::wood;
      break;
    case Element::sword:
      return _other == Element::breaker;
      break;
    case Element::wind:
      return _other == Element::sword;
      break;
    case Element::cursor:
      return _other == Element::wind;
      break;
    case Element::breaker:
      return _other == Element::cursor;
      break;
  }
    
  return false;
}

bool Entity::HasInit() {
  return hasInit;
}

void Entity::Init() {
  hasInit = true;
}

void Entity::HandleNewStatuses(const Hit::Flags prevStatuses, Hit::Flags& appliedStatuses) {

  // Some statuses clear the action queue.
  // TODO: Neither FinishMove nor clearing the queue ends the animation initiated 
  // by PlayerControlledState. This might be smoothly handled if the move
  // animation was actually a CardAction. Otherwise, make sure it's safe to enter
  // idle right here and do that instead.
  if (appliedStatuses & (Hit::freeze | Hit::stun)) {
    FinishMove();
    actionQueue.ClearQueue(ActionQueue::CleanupType::allow_interrupts);
  }

  if ((appliedStatuses & Hit::freeze) == Hit::freeze) {
    IceFreeze();
    // IceFreeze removes flash
    appliedStatuses &= ~Hit::flash;
  }


  // Avoid resetting the animation
  if ((appliedStatuses & Hit::blind) == Hit::blind && !(prevStatuses & Hit::blind)) {
    Blind();
  }

  // Avoid resetting the animation
  if ((appliedStatuses & Hit::confuse) == Hit::confuse && !(prevStatuses & Hit::confuse)) {
    Confuse();
  }

  if ((appliedStatuses & Hit::retangible) == Hit::retangible) {
    SetPassthrough(false);
  }

  // Now that all other behavior is done, run status callbacks

  // a re-usable thunk for custom status effects
  auto flagCheckThunk = [this](const Hit::Flags& toCheck) {
    if (Entity::StatusCallback& func = statusCallbackHash[toCheck]) {
      func();
    }
  };

  Hit::Flags statusCheck = appliedStatuses;
  /// Run all status callbacks, starting from lowest set bit
  Hit::Flags checkIdx = statusCheck & -statusCheck;
  while (statusCheck > 0) {
    if (statusCheck & checkIdx) {
      flagCheckThunk(checkIdx);
    }

    statusCheck = statusCheck & ~checkIdx;
    checkIdx = checkIdx << 1;
  }
}

void Entity::Update(double _elapsed) {
  ResolveFrameBattleDamage();

  if (fieldStart && ((maxHealth > 0 && health <= 0) || IsDeleted())) {
    // Ensure entity is deleted if health is zero
    if (manualDelete == false) {
      Delete();
    }

    // Ensure health is zero if marked for immediate deletion
    health = 0;

    // Ensure status effects do not play out
    statuses.ClearAllStatuses();
  }

  // reset base color
  setColor(NoopCompositeColor(GetColorMode()));

  statusShaderTimer++;

  // Used to determine if Sprite should be revealed this frame, 
  // when flashing was active and became inactive this frame.
  bool wasFlashing = statuses.IsApplied(Hit::flash);

  Hit::Flags prevStatuses = statuses.GetCurrentStatuses();
  Hit::Flags queuedStatuses = statuses.GetQueuedStatuses();

  statuses.ProcessPendingStatuses();

  // Tick all statuses at once
  statuses.OnUpdate(_elapsed);

  Hit::Flags applied = (queuedStatuses & ~statuses.GetQueuedStatuses() & statuses.GetCurrentStatuses());
  HandleNewStatuses(prevStatuses, applied);

  RefreshShader();

  bool stunned = statuses.IsApplied(Hit::stun);
  bool frozen = statuses.IsApplied(Hit::freeze);
  bool blind = statuses.IsApplied(Hit::blind);
  bool confused = statuses.IsApplied(Hit::confuse);

  // TODO: Determine if Drag should also be checked here.
  // The answer is likely yes.
  bool canUpdateThisFrame = !(frozen || stunned);

  if (!hit) {
    AppliedStatus& flash = statuses.GetStatus(Hit::flash);

    if (statuses.IsApplied(Hit::flash)) {
      unsigned frame = flash.remainingTime.count() % 4;
      if (frame < 2 || statuses.IsApplied(Hit::drag)) {
        Reveal();
      }
      else {
        Hide();
      }
    }
    // Flash became inactive this frame. Reveal.
    else if (wasFlashing) {
      Reveal();
    }
  }

  // assume this is hidden, will flip to visible if not
  iceFx->Hide();
  if (frozen) {
    iceFxAnimation.Update(_elapsed, iceFx->getSprite());
    iceFx->Reveal();
  }

  // assume this is hidden, will flip to visible if not
  blindFx->Hide();
  if (blind) {
    blindFxAnimation.Update(_elapsed, blindFx->getSprite());
    blindFx->Reveal();
  }

  // assume this is hidden, will flip to visible if not
  confusedFx->Hide();
  if (confused) {
    confusedFxAnimation.Update(_elapsed, confusedFx->getSprite());
    confusedFx->Reveal();
    confuseSfxCooldown -= from_seconds(_elapsed);
    // Unclear if 55f is the correct timing: this seems to be the one used in source, though, as the confusion SFX only plays twice during a 110f confusion period.
    constexpr frame_time_t CONFUSED_SFX_INTERVAL{ 55 };
    if (confuseSfxCooldown <= frames(0)) {
      static std::shared_ptr<sf::SoundBuffer> confusedsfx = Audio().LoadFromFile(SoundPaths::CONFUSED_FX);
      Audio().Play(confusedsfx, AudioPriority::highest);
      confuseSfxCooldown = CONFUSED_SFX_INTERVAL;
    }
  }
  else {
    confuseSfxCooldown = frames(0);
  }
  
  if(canUpdateThisFrame) {
    OnUpdate(_elapsed);
  }

  isUpdating = true;

  actionQueue.Process();

  UpdateMovement(_elapsed);

  sf::Uint8 alpha = getSprite().getColor().a;
  for (std::shared_ptr<SceneNode>& child : GetChildNodes()) {
    SpriteProxyNode* sprite = dynamic_cast<SpriteProxyNode*>(child.get());
    if (sprite) {
      sf::Color color = sprite->getColor();
      sprite->setColor(sf::Color(color.r, color.g, color.b, alpha));
    }
  }

  // Update all components
  for (std::shared_ptr<Component>& component : components) {
    // respectfully only update local components
    // anything shared with the battle scene needs to update those components
    if (component->Lifetime() == Component::lifetimes::local) {
      component->Update(_elapsed);
    }
  }

  ReleaseComponentsPendingRemoval();
  InsertComponentsPendingRegistration();
  ClearPendingComponents();

  isUpdating = false;

  // If the counterSlideOffset has changed from 0, it's due to the character
  // being deleted on a counter frame. Begin animating the counter-delete slide
  if (counterSlideOffset.x != 0 || counterSlideOffset.y != 0) {
    counterSlideDelta += static_cast<float>(_elapsed);
    
    float delta = swoosh::ease::linear(counterSlideDelta, 0.10f, 1.0f);
    sf::Vector2f offset = delta * counterSlideOffset;

    // Add this offset onto our offsets
    setPosition(tile->getPosition().x + offset.x, tile->getPosition().y + offset.y);
  }
}


void Entity::SetPalette(const std::shared_ptr<sf::Texture>& palette)
{
  SmartShader& smartShader = GetShader();

  if (palette.get() == nullptr) {
    smartShader.SetUniform("swapPalette", false);
    swapPalette = false;
    return;
  }

  swapPalette = true;
  this->palette = palette;
  smartShader.SetUniform("swapPalette", true);
  smartShader.SetUniform("palette", this->palette);
}

std::shared_ptr<sf::Texture> Entity::GetPalette()
{
  return palette;
}

void Entity::StoreBasePalette(const std::shared_ptr<sf::Texture>& palette)
{
  basePalette = palette;
}

std::shared_ptr<sf::Texture> Entity::GetBasePalette()
{
  return basePalette;
}

void Entity::RefreshShader()
{
  std::shared_ptr<Field> field = this->field.lock();

  if (!field) {
    return;
  }

  sf::Shader* shader = Shaders().GetShader(ShaderType::BATTLE_CHARACTER);
  SmartShader& smartShader = GetShader();

  if (shader != smartShader.Get()) {
    SetShader(shader);
  }

  if (!smartShader.HasShader()) return;

  smartShader.SetUniform("swapPalette", swapPalette);
  smartShader.SetUniform("palette", palette);

  AppliedStatus& flash = statuses.GetStatus(Hit::flash);
  bool flashing = statuses.IsApplied(Hit::flash);
  bool stunned = statuses.IsApplied(Hit::stun);
  bool frozen = statuses.IsApplied(Hit::freeze);
  bool rooted = statuses.IsApplied(Hit::root);

  // state checks
  bool stunFrame = statusShaderTimer % 4 < 2;
  bool rootFrame = statusShaderTimer % 4 < 2;
  counterFrameFlag = counterFrameFlag % 4;
  counterFrameFlag++;

  /*
    TODO: Flash uses its own timer.
    Stun and Root use the same timer as each other, and only stun colors if both active.
    Freeze overrides Root color as well.
  */

  bool whiteout = hit && !isTimeFrozen;
  vector<float> states = {
    static_cast<float>(whiteout),                           // WHITEOUT
    static_cast<float>(rooted && (flashing || rootFrame)),  // BLACKOUT
    static_cast<float>(stunned && (flashing || stunFrame)), // HIGHLIGHT
    static_cast<float>(frozen)                              // ICEOUT
  };

  smartShader.SetUniform("states", states);
  smartShader.SetUniform("additiveMode", GetColorMode() == ColorMode::additive);

  bool enabled = states[0] || states[1];

  if (enabled) return;

  if (counterable && field->DoesRevealCounterFrames() && counterFrameFlag < 2) {
    // Highlight when the character can be countered
    setColor(sf::Color(55, 55, 255, getColor().a));
  }
}

void Entity::draw(sf::RenderTarget& target, sf::RenderStates states) const
{
  // NOTE: This function does not call the parent implementation
  //       This function is a special behavior for battle characters to
  //       color their attached nodes correctly in-game

  if (!SpriteProxyNode::show) return;

  SmartShader& smartShader = GetShader();

  // combine the parent transform with the node's one
  sf::Transform combinedTransform = getTransform();

  states.transform *= combinedTransform;

  std::vector<SceneNode*> copies;
  copies.reserve(childNodes.size() + 1);

  for (std::shared_ptr<SceneNode>& child : childNodes) {
    copies.push_back(child.get());
  }

  copies.push_back((SceneNode*)this);

  std::sort(copies.begin(), copies.end(), [](SceneNode* a, SceneNode* b) { return (a->GetLayer() > b->GetLayer()); });

  // draw its children
  for (std::size_t i = 0; i < copies.size(); i++) {
    SceneNode* currNode = copies[i];

    if (!currNode) continue;

    // If it's time to draw our scene node, we draw the proxy sprite
    if (currNode == this) {
      sf::Shader* s = smartShader.Get();

      if (s) {
        states.shader = s;
      }

      target.draw(getSpriteConst(), states);
    }
    else {
      SpriteProxyNode* asSpriteProxyNode{ nullptr };
      SmartShader temp(smartShader);

      /**
      hack for now.
      form overlay nodes (like helmet and shoulder pads)
      are already colored to the desired palette. So we do not apply palette swapping.
      **/
      bool needsRevert = false;
      sf::Color tempColor = sf::Color::White;
      if (currNode->HasTag(Player::FORM_NODE_TAG)) {
        asSpriteProxyNode = dynamic_cast<SpriteProxyNode*>(currNode);

        if (asSpriteProxyNode) {
          smartShader.SetUniform("swapPalette", false);
          tempColor = asSpriteProxyNode->getColor();
          asSpriteProxyNode->setColor(sf::Color(0, 0, 0, getColor().a));
          needsRevert = true;
        }
      }

      // Apply and return shader if applicable
      sf::Shader* s = smartShader.Get();

      if (s && currNode->IsUsingParentShader()) {
        if (auto asSpriteProxyNode = dynamic_cast<SpriteProxyNode*>(currNode)) {
          asSpriteProxyNode->setColor(this->getColor());
        }

        states.shader = s;
      }

      target.draw(*currNode, states);

      // revert color
      if (asSpriteProxyNode && needsRevert) {
        asSpriteProxyNode->setColor(tempColor);
      }

      // revert uniforms from this pass
      smartShader = temp;
    }
  }
}

void Entity::SetAlpha(int value)
{
  alpha = value;
  sf::Color c = getColor();
  c.a = alpha;

  setColor(c);
}

int Entity::GetAlpha()
{
  return getColor().a;
}

bool Entity::Teleport(Battle::Tile* dest, ActionOrder order, std::function<void()> onBegin) {
  if (dest && CanMoveTo(dest)) {
    frame_time_t endlagDelay = moveEndlagDelay ? *moveEndlagDelay : frame_time_t{};
    
    MoveEvent event = {
      std::make_shared<MoveAction>(*this, MoveData{dest, frames(0), moveStartupDelay, endlagDelay, 0.f, onBegin})
    };
    actionQueue.Add(event, order, ActionDiscardOp::until_eof);

    return true;
  }

  return false;
}

bool Entity::Slide(Battle::Tile* dest, 
  const frame_time_t& slideTime, const frame_time_t& endlag, ActionOrder order, std::function<void()> onBegin)
{
  if (dest && CanMoveTo(dest)) {
    frame_time_t endlagDelay = moveEndlagDelay ? *moveEndlagDelay : endlag;
    MoveEvent event = {
      std::make_shared<MoveAction>(*this, MoveData{dest, slideTime, frames(0), endlagDelay, 0.f, onBegin})
    };
    actionQueue.Add(event, order, ActionDiscardOp::until_eof);

    return true;
  }

  return false;
}

bool Entity::Jump(Battle::Tile* dest, float destHeight, 
  const frame_time_t& jumpTime, const frame_time_t& endlag, ActionOrder order, std::function<void()> onBegin)
{
  destHeight = std::max(destHeight, 0.f); // no negative jumps

  if (dest && CanMoveTo(dest)) {
    frame_time_t endlagDelay = moveEndlagDelay ? *moveEndlagDelay : endlag;

    
    MoveEvent event = {
      std::make_shared<MoveAction>(*this, MoveData{dest, jumpTime, frames(0), endlagDelay, destHeight, onBegin})
    };
    actionQueue.Add(event, order, ActionDiscardOp::until_eof);

    return true;
  }

  return false;
}

void Entity::FinishMove()
{
  slideFromDrag = false;
  if (!currMoveEvent) {
    return;
  }

  // completes the move or moves the object back
  if (currMoveEvent->data.dest) {
    AdoptNextTile();
    tileOffset = {};
  }

  currMoveEvent = nullptr;
  actionQueue.ClearFilters();
  actionQueue.Pop();
}

void Entity::EndDrag() {
  statuses.ClearStatuses(Hit::drag);
  slideFromDrag = false;
  FinishMove();
}

bool Entity::RawMoveEvent(const MoveEvent& event, ActionOrder order)
{
  if (event.move->data.dest && CanMoveTo(event.move->data.dest)) {
    actionQueue.Add(event, order, ActionDiscardOp::until_eof);

    return true;
  }

  return false;
}

bool Entity::RawMoveEvent(const MoveData& data, ActionOrder order) {
  if (data.dest) {
    const MoveEvent e = {
      std::make_shared<MoveAction>(*this, data)
    };
    return RawMoveEvent(e, order);
  }

  return false;
}

void Entity::HandleMoveEvent(MoveEvent& event, const ActionQueue::ExecutionType& exec)
{
  if (exec == ActionQueue::ExecutionType::interrupt) {
    FinishMove();
    return;
  }

  // TODO: Hack. Root blocks Drag from being added, which means slideFromDrag is never set false
  // if move was
  if (!currMoveEvent && (!IsRooted() || dynamic_cast<DragAction*>(event.move.get()))) {
    UpdateMoveStartPosition();
    FilterMoveEvent(event);
    currMoveEvent = event.move;
    moveEventFrame = this->frame;
    previous = tile;
    actionQueue.CreateDiscardFilter(ActionTypes::buster, ActionDiscardOp::until_resolve);
    actionQueue.CreateDiscardFilter(ActionTypes::peek_card, ActionDiscardOp::until_resolve);
  }
}

// Default implementation of CanMoveTo() checks 
// 1) if the tile is walkable
// 2) if not, if the entity can float 
// 3) if the tile is valid and the next tile is the same team
bool Entity::CanMoveTo(Battle::Tile * next)
{
  bool valid = Teammate(next->GetTeam()) && !next->IsReservedByCharacter({ GetID() });

  if (HasAirShoe() && !next->IsEdgeTile()) {
    return valid;
  }

  return next->IsWalkable() && valid;
}

const long Entity::GetID() const
{
  return ID;
}

/** \brief Unkown team entities are friendly to all spaces @see Cubes */
bool Entity::Teammate(Team _team) const {
  return (team == Team::unknown) || (_team == Team::unknown) || (team == _team);
}

void Entity::SetTile(Battle::Tile* _tile) {
  // If this entity is not moving, we can safely
  // refresh their position to the new tile
  if(!IsMoving() && _tile) {
    setPosition(_tile->getPosition() + Entity::drawOffset);
  }

  tile = _tile;
}

Battle::Tile* Entity::GetTile(Direction dir, unsigned count) const {
  Battle::Tile* next = tile;

  while (count > 0) {
    next = next + dir;
    count--;
  }

  return next;
}

Battle::Tile* Entity::GetCurrentTile() const {
  return GetTile();
}

const sf::Vector2f Entity::GetTileOffset() const
{
  return this->tileOffset;
}

void Entity::SetTileOffset(const sf::Vector2f& offset) {
  tileOffset = offset;
}

void Entity::RefreshPosition() {
  setPosition(tile->getPosition() + tileOffset + drawOffset);
}

void Entity::SetDrawOffset(const sf::Vector2f& offset)
{
  drawOffset = offset;
}

void Entity::SetDrawOffset(float x, float y)
{
  drawOffset = { x, y };
}

const sf::Vector2f Entity::GetDrawOffset() const
{
  return drawOffset;
}

const bool Entity::IsSliding() const
{
  bool is_moving = currMoveEvent && currMoveEvent->IsSliding();

  return is_moving;
}

const bool Entity::IsJumping() const
{
  bool is_moving = currMoveEvent && currMoveEvent->IsJumping();

  return is_moving && currJumpHeight > 0.f;
}

const bool Entity::IsTeleporting() const
{
  bool is_moving = currMoveEvent && currMoveEvent->IsTeleporting();

  return is_moving;
}

const bool Entity::IsMoving() const
{
  return IsSliding() || IsJumping() || IsTeleporting();
}

void Entity::SetField(std::shared_ptr<Field> _field) {
  assert(_field && "field was nullptr");
  field = _field;
  channel = EventBus::Channel(_field->scene);
}

std::shared_ptr<Field> Entity::GetField() const {
  return field.lock();
}

bool Entity::IsOnField() const {
  return !field.expired();
}

Team Entity::GetTeam() const {
  return team;
}
void Entity::SetTeam(Team _team) {
  team = _team;
}

void Entity::SetPassthrough(bool state)
{
  passthrough = state;
  Reveal();
}

bool Entity::IsPassthrough()
{
  return passthrough;
}

void Entity::SetFloatShoe(bool state)
{
  floatShoe = state;
}

void Entity::SetAirShoe(bool state) {
  airShoe = state;
}

void Entity::SlidesOnTiles(bool state)
{
  slidesOnTiles = state;
}

bool Entity::HasFloatShoe()
{
  return floatShoe;
}

bool Entity::HasAirShoe() {
  return airShoe;
}

bool Entity::WillSlideOnTiles()
{
  return slidesOnTiles;
}

void Entity::SetMoveDirection(Direction dir) {
  direction = dir;
}

Direction Entity::GetMoveDirection()
{
  return direction;
}

void Entity::SetFacing(Direction facing)
{
  if (facing == Direction::left) {
    neverFlip? void(0) : setScale(-2.f, 2.f); // flip standard facing right sprite
  }
  else if (facing == Direction::right) {
    neverFlip? void(0) : setScale(2.f, 2.f); // standard facing
  }
  else {
    return;
  }

  this->facing = facing;
}

Direction Entity::GetFacing()
{
  return facing;
}

Direction Entity::GetFacingAway()
{
  return Reverse(facing);
}

Direction Entity::GetPreviousDirection()
{
  return previousDirection;
}

void Entity::Delete()
{
  if (deleted) return;

  deleted = true;

  statuses.ClearAllStatuses();

  OnDelete();
}

void Entity::Erase()
{
  flagForErase = true;
}

bool Entity::IsDeleted() const {
  return deleted;
}

bool Entity::WillEraseEOF() const
{
    return flagForErase;
}

void Entity::SetElement(Element _elem)
{
  element = _elem;
}

const Element Entity::GetElement() const
{
  return element;
}

void Entity::AdoptNextTile()
{
  Battle::Tile* next = currMoveEvent->data.dest;
  if (next == nullptr) {
    return;
  }

  if (previous != nullptr && previous != next) {
    previous->RemoveEntityByID(GetID());

    // If removing an entity and the tile was broken, crack the tile
    previous->HandleMove(shared_from_this());
  }

  previous = tile;

  if (!IsMoving()) {
    setPosition(next->getPosition() + Entity::drawOffset);
  }

  next->AddEntity(shared_from_this());

  // Slide if the tile we are moving to is ICE
  if (next->GetState() != TileState::ice || HasFloatShoe()) {
    // TODO: Determine if this should only be incremented when 
    // move is voluntary. Does your rank go down when pushed?

    // If not using animations, then 
    // adopting a tile is the last step in the move procedure
    // Increase the move count
    moveCount++;
  }
}

void Entity::ToggleTimeFreeze(bool state)
{
  isTimeFrozen = state;
}

const bool Entity::IsTimeFrozen()
{
  return isTimeFrozen;
}

void Entity::FreeAllComponents()
{
  for (int i = 0; i < components.size(); i++) {
    components[i]->Eject();
  }

  ReleaseComponentsPendingRemoval();

  components.clear();
}

const EventBus::Channel& Entity::EventChannel() const
{
  return channel;
}

void Entity::FreeComponentByID(Component::ID_t ID) {
  auto iter = components.begin();
  while(iter != components.end()) {
    std::shared_ptr<Component> component = *iter;

    if (component->GetID() == ID) {
      // Safely delete component by queueing it
      queuedComponents.insert(queuedComponents.begin(), ComponentBucket{ component, ComponentBucket::Status::remove });
      return; // found and handled, quit early.
    }

    iter = std::next(iter);
  }
}

const float Entity::GetElevation() const
{
  return elevation;
}

void Entity::SetElevation(const float elevation)
{
  this->elevation = elevation;
}

std::shared_ptr<Component> Entity::RegisterComponent(std::shared_ptr<Component> c) {
  if (c == nullptr) return nullptr;

  auto iter = std::find(components.begin(), components.end(), c);
  if (iter != components.end())
    return *iter;

  if (isUpdating) {
    queuedComponents.insert(queuedComponents.begin(), ComponentBucket{ c, ComponentBucket::Status::add });
  }
  else {
    components.push_back(c);
    SortComponents();
  }

  return c;
}

void Entity::UpdateMoveStartPosition()
{
  if (tile) {
    moveStartPosition = sf::Vector2f(tileOffset.x + tile->getPosition().x, tileOffset.y + tile->getPosition().y);
  }
}

const int Entity::GetMoveCount() const
{
  return moveCount;
}

void Entity::ClearActionQueue()
{
  actionQueue.ClearQueue(ActionQueue::CleanupType::allow_interrupts);
}

const float Entity::GetJumpHeight() const
{
  return currMoveEvent ? currMoveEvent->GetHeight() : 0.f;
}

void Entity::ShowShadow(bool enabled)
{
  if (enabled) {
    shadow->Reveal();
  }
  else {
    shadow->Hide();
  }
}

void Entity::SetShadowSprite(Shadow type)
{
  switch (type) {
  case Entity::Shadow::none:
    shadow->Hide();
    break;
  case Entity::Shadow::small:
    {
      shadow->setTexture(Textures().LoadFromFile(TexturePaths::MISC_SMALL_SHADOW), true);
      sf::FloatRect bounds = shadow->getLocalBounds();
      shadow->setOrigin(sf::Vector2f(bounds.width * 0.5f, bounds.height * 0.5f));
    }
    break;
  case Entity::Shadow::big:
    {
      shadow->setTexture(Textures().LoadFromFile(TexturePaths::MISC_BIG_SHADOW), true);
      sf::FloatRect bounds = shadow->getLocalBounds();
      shadow->setOrigin(sf::Vector2f(bounds.width * 0.5f, bounds.height * 0.5f));
    }
    break;
  case Entity::Shadow::custom:
    // no op
  default:
    break;
  }
}

void Entity::SetShadowSprite(std::shared_ptr<sf::Texture> customShadow)
{
  shadow->setTexture(customShadow, true);
  sf::FloatRect bounds = shadow->getLocalBounds();
  shadow->setOrigin(sf::Vector2f(bounds.width*0.5f, bounds.height*0.5f));
}

void Entity::ShiftShadow() {
  // counter offset the shadow node
  shadow->setPosition(0, 0.5f * (Entity::GetElevation() + Entity::GetCurrJumpHeight()));
}

const float Entity::GetCurrJumpHeight() const
{
  return currJumpHeight;
}

void Entity::ShareTileSpace(bool enabled)
{
  canShareTile = enabled;
}

const bool Entity::CanShareTileSpace() const
{
  return canShareTile;
}

void Entity::EnableTilePush(bool enabled)
{
  canTilePush = enabled;
}

void Entity::SetName(std::string name)
{
  this->name = name;
}

const std::string Entity::GetName() const
{
  return name;
}

const bool Entity::CanTilePush() const {
  return canTilePush;
}

const bool Entity::Hit(Hit::Properties props) {

  if (!hitboxEnabled) {
    return false;
  }

  if (health <= 0) {
    return false;
  }

  const Hit::Properties original = props;

  // If in time freeze, shake immediately on any contact
  if ((props.flags & Hit::shake) == Hit::shake && IsTimeFrozen()) {
    CreateComponent<ShakingEffect>(weak_from_this());
  }
  
  for (std::shared_ptr<DefenseRule>& defense : defenses) {
    props = defense->FilterStatuses(props);
  }

  // If the character itself is also super-effective,
  // double the damage independently from tile damage
  const bool isSuperEffective = IsSuperEffective(props.element) || IsSuperEffective(props.secondaryElement);
  const bool isFire = props.element == Element::fire || props.secondaryElement == Element::fire;
  const bool isElec = props.element == Element::elec || props.secondaryElement == Element::elec;
  const bool isAqua = props.element == Element::aqua || props.secondaryElement == Element::aqua;

  // super effective damage is x2
  if (isSuperEffective) {
    props.damage *= 2;
  }

  int tileDamage = 0;
  int extraDamage = 0;

  // Calculate elemental damage if the tile the character is on is super effective to it
  if (isFire
    && GetTile()->GetState() == TileState::grass) {
    tileDamage = props.damage;
    GetTile()->SetState(TileState::normal);
  }


  if (isElec
    && GetTile()->GetState() == TileState::sea) {
    tileDamage = props.damage;
  }

  
  if (isAqua
    && GetTile()->GetState() == TileState::ice) {
    props.flags |= Hit::freeze;
    GetTile()->SetState(TileState::normal);
  }

  if ((props.flags & Hit::breaking) == Hit::breaking && statuses.IsApplied(Hit::freeze)) {
    extraDamage = props.damage;
    // Breaking immediately ends freeze, before the next Entity update.
    // Seen by freeze being cleared during time freeze.
    // TODO: Likely related to this, breaking clears frozen even when 
    // damage is blocked by defenses. Find out if this can be done, and also how 
    // defenses that trigger actions interact with this, and compare to stun.
    ClearStatuses(Hit::freeze);
    iceFx->Hide();

    // Remove flinch from breaking attack if it did not have flinch | flash.
    // Attacks that break freeze but don't flinch and flash should not flinch.
    // This is here instead of in the StatusBehaviorDirector because freeze would 
    // have been cleared before flags are processed this frame, making it impossible 
    // to tell freeze was ended.
    if ((props.flags & (Hit::flash | Hit::flinch)) != (Hit::flash | Hit::flinch)) {
      props.flags = props.flags & ~Hit::flinch;
    }
  }
  

  int totalDamage = props.damage + (tileDamage + extraDamage);

  // Broadcast the hit before we apply statuses and change the entity's state flags
  if (totalDamage > 0) {
    SetHealth(GetHealth() - totalDamage);
    HitPublisher::Broadcast(*this, props);
  }

  if (IsTimeFrozen()) {
    props.flags |= Hit::no_counter;

    // Frozen Entities cannot be refrozen during timefreeze.
    // This is currently handled here instead of in the StatusBehaviorDirector 
    // because statuses will never update during timefreeze, and so cannot 
    // tell this flag was affected by it.
    if (IsIceFrozen()) {
      props.flags = props.flags & ~Hit::freeze;
    }
  }

  // Add to status queue for state resolution
  statusQueue.push(CombatHitProps{ original, props });

  if ((props.flags & Hit::impact) == Hit::impact) {
    this->hit = true; // flash white immediately
    RefreshShader();
  }

  if (GetHealth() <= 0) {
    SetShader(whiteout);
  }

  return true;
}

void Entity::RegisterStatusCallback(const Hit::Flags& flag, const StatusCallback& callback)
{
  statusCallbackHash[flag] = callback;
}

void Entity::ManualDelete()
{
  manualDelete = true;
}

void Entity::PrepareNextFrame()
{
  hit = false;
}

const bool Entity::UnknownTeamResolveCollision(const Entity& other) const
{
  return true; // by default unknown vs unknown spells attack eachother
}

const bool Entity::HasCollision(const Hit::Properties & props)
{
  // Pierce status hits even when passthrough or flinched
  if ((props.flags & Hit::pierce) != Hit::pierce) {
    if (statuses.IsApplied(Hit::flash) || IsPassthrough() || !hitboxEnabled) return false;
  }

  return true;
}

int Entity::GetHealth() const {
  return health;
}

void Entity::SetMaxHealth(int _health)
{
  maxHealth = _health;
}

const int Entity::GetMaxHealth() const
{
  return maxHealth;
}

void Entity::ResolveFrameBattleDamage()
{
  if(IsDeleted()) return;

  std::shared_ptr<Character> frameCounterAggressor = nullptr;

  std::queue<CombatHitProps> append;

  // Adding drag creates a MmoveAction. Wait until statusQueue is done, 
  // then create the MoveAction if this is true.
  bool addDrag = false;
  Hit::Drag currentDrag{};

  while (!statusQueue.empty()) {
    CombatHitProps props = statusQueue.front();
    statusQueue.pop();

    // start of new scope
    {

      bool countered = IsCountered()
      && (props.filtered.flags & Hit::no_counter) == 0
      && (props.filtered.flags & Hit::impact) == Hit::impact
      && !frameCounterAggressor
      && props.filtered.aggressor;
      if (countered) {
        // Only consider a counter if there was an aggressor
        if (frameCounterAggressor = GetField()->GetCharacter(props.filtered.aggressor)) {
          // Counter stun takes priority over the attack's Stun duration.
          // Additionally, remove flashing from the countering hit.
          props.filtered.flags = props.filtered.flags & ~(Hit::stun | Hit::flash);
          statuses.AddStatus(Hit::stun, frames(150));
          OnCountered();
        } 
      }

      // Drag replaces current Drag effects.
      // Do not consider Drag if it has no direction
      if ((props.filtered.flags & Hit::drag) == Hit::drag && props.filtered.drag.dir != Direction::none) {
        addDrag = true;
        currentDrag = props.filtered.drag;
      }

      props.filtered.flags = props.filtered.flags & ~Hit::drag;

      const bool hasFlash = ((props.filtered.flags & Hit::flash) == Hit::flash);
      if (hasFlash) {
        statuses.AddStatus(Hit::flash, props.filtered.flash_duration);
      }

      props.filtered.flags = props.filtered.flags & ~Hit::flash;

      if ((props.filtered.flags & Hit::freeze)) {
        statuses.AddStatus(Hit::freeze, props.filtered.freeze_duration);
      }

      props.filtered.flags = props.filtered.flags & ~Hit::freeze;

      if ((props.filtered.flags & Hit::stun)) {
        statuses.AddStatus(Hit::stun, props.filtered.stun_duration);
      }

      props.filtered.flags = props.filtered.flags & ~Hit::stun;

      if ((props.filtered.flags & Hit::bubble)) {
        statuses.AddStatus(Hit::bubble, frames(150));
      }

      props.filtered.flags = props.filtered.flags & ~Hit::bubble;

      if ((props.filtered.flags & Hit::root)) {
        statuses.AddStatus(Hit::root, props.filtered.root_duration);
      }

      props.filtered.flags = props.filtered.flags & ~Hit::root;

      if ((props.filtered.flags & Hit::blind)) {
        statuses.AddStatus(Hit::blind, props.filtered.blind_duration);
      }

      props.filtered.flags = props.filtered.flags & ~Hit::blind;

      if ((props.filtered.flags & Hit::confuse)) {
        statuses.AddStatus(Hit::confuse, props.filtered.confuse_duration);
      }

      props.filtered.flags = props.filtered.flags & ~Hit::confuse;

      // Add the rest, starting from lowest set bit
      Hit::Flags curFlag = props.filtered.flags & -props.filtered.flags;
      while (props.filtered.flags > 0) {
        if (props.filtered.flags & curFlag) {
          statuses.AddStatus(curFlag);
          props.filtered.flags &= ~curFlag;
        }

        curFlag = curFlag << 1;
      }

      if (GetHealth() == 0) {
        currentDrag.dir = Direction::none; // Cancel slide post-status if blowing up
      }
    }
  } // end while-loop

  // A new Drag should immediately end current movement
  // TODO: Drag forcibly ends the movement. Find out if that counts as a movement, because FinishMove 
  // calls AdoptTile, which increases moveCount.
  if (addDrag) {
    bool activeDrag = slideFromDrag;
    FinishMove();
    // Preserve slideFromDrag. FinishMove sets false, but it must remain true 
    // if Drag was already in effect, for status processing purposes.
    // Otherwise, when this Hit::drag processes, it will process as if there was 
    // not already an active Drag.
    slideFromDrag = activeDrag;
    statuses.AddStatus(Hit::drag);
    
    
    actionQueue.ClearQueue(ActionQueue::CleanupType::allow_interrupts);
    /*
      Do not set slideFromDrag true here. This could interfere with status 
      processing after ResolveFrameBattleDamage. This will be set true 
      by the StatusBehaviorDirector instead.
    */

    actionQueue.Add(
      MoveEvent{
        std::make_shared<DragAction>(*this, currentDrag)
      },
      ActionOrder::immediate, ActionDiscardOp::until_resolve
    );

  }

  if (GetHealth() == 0) {
    // We are dying. Prevent special fx and status animations from triggering.
    statuses.ClearAllStatuses();

    while(statusQueue.size() > 0) {
      statusQueue.pop();
    }

    //FinishMove(); // cancels slide. TODO: obstacles do not use this but characters do!

    if(frameCounterAggressor) {
      // Slide entity back a few pixels
      counterSlideOffset = sf::Vector2f(50.f, 0.0f);
      CounterHitPublisher::Broadcast(*this, *frameCounterAggressor);
    }
  } else if (frameCounterAggressor) {
    CounterHitPublisher::Broadcast(*this, *frameCounterAggressor);
  }
}

void Entity::SetHealth(const int _health) {
  health = _health;

  if (maxHealth == 0) {
    maxHealth = health;
  }

  if (health > maxHealth) health = maxHealth;
  if (health < 0) health = 0;
}

const bool Entity::IsHitboxAvailable() const
{
  return hitboxEnabled && GetHealth() > 0;
}

void Entity::EnableHitbox(bool enabled)
{
  hitboxEnabled = enabled;
}

void Entity::AddDefenseRule(std::shared_ptr<DefenseRule> rule)
{
  if (!rule) return;

  auto iter = std::find_if(defenses.begin(), defenses.end(), [rule](auto other) { return rule->GetPriorityLevel() == other->GetPriorityLevel(); });

  if (rule && iter == defenses.end()) {
    defenses.push_back(rule);
    std::sort(defenses.begin(), defenses.end(), [](std::shared_ptr<DefenseRule> first,std::shared_ptr<DefenseRule> second) { return first->GetPriorityLevel() < second->GetPriorityLevel(); });
  }
  else {
    (*iter)->replaced = true; // Flag that this defense rule may be valid ptr, but is no longer in use
    (*iter)->OnReplace();
    RemoveDefenseRule(*iter); // will invalidate the iterator

    // call again, adding new rule this time
    AddDefenseRule(rule);
  }
}

void Entity::RemoveDefenseRule(std::shared_ptr<DefenseRule> rule)
{
  RemoveDefenseRule(rule.get());
}

void Entity::RemoveDefenseRule(DefenseRule* rule)
{
  auto iter = std::find_if(defenses.begin(), defenses.end(), [rule](auto in) { return in.get() == rule; });

  if(iter != defenses.end())
    defenses.erase(iter);
}

void Entity::DefenseCheck(DefenseFrameStateJudge& judge, std::shared_ptr<Entity> in, const DefenseOrder& filter)
{
  std::vector<std::shared_ptr<DefenseRule>> copy = defenses;

  auto characterPtr = shared_from_base<Character>();

  for (int i = 0; i < copy.size(); i++) {
    if (copy[i]->GetDefenseOrder() == filter) {
      std::shared_ptr<DefenseRule> defenseRule = copy[i];
      judge.SetDefenseContext(defenseRule);
      defenseRule->CanBlock(judge, in, characterPtr);
    }
  }
}

bool Entity::IsCounterable()
{
  return counterable;
}

void Entity::ToggleCounter(bool on)
{
  counterable = on;
}

void Entity::NeverFlip(bool enabled)
{
  neverFlip = enabled;
}

// TODO: Replace all of these with one HasStatus
bool Entity::IsStunned()
{
  return statuses.HasStatus(Hit::stun);
}

bool Entity::IsRooted()
{
  return statuses.HasStatus(Hit::root);
}

bool Entity::IsIceFrozen() {
  return statuses.HasStatus(Hit::freeze);
}

bool Entity::IsBlind()
{
  return statuses.HasStatus(Hit::blind);
}

void Entity::AddStatus(Hit::Flags status) {
  statuses.AddStatus(status);
}

void Entity::AddStatus(Hit::Flags status, frame_time_t duration) {
  statuses.AddStatus(status, duration);
}

const bool Entity::HasStatus(Hit::Flags status) const {
  bool dragCheck = true;
  if (status == Hit::drag) {
    dragCheck = slideFromDrag;
    status &= ~Hit::drag;
  }

  return dragCheck && statuses.HasStatus(status);
}

const bool Entity::HasAnyStatusFrom(Hit::Flags status) const {
  if ((status & Hit::drag) == Hit::drag && slideFromDrag) {
    return true;
  }

  return statuses.HasAnyStatusFrom(status);
}

const bool Entity::IsStatusApplied(Hit::Flags status) const {
  bool dragCheck = true;
  if (status == Hit::drag) {
    dragCheck = slideFromDrag;
    status &= ~Hit::drag;
  }
  return dragCheck && statuses.IsApplied(status);
}

void Entity::ClearStatuses(Hit::Flags flags) {
  statuses.ClearStatuses(flags);
}

void Entity::IceFreeze()
{
  const float height = GetHeight();

  static std::shared_ptr<sf::SoundBuffer> freezesfx = Audio().LoadFromFile(SoundPaths::ICE_FX);
  // Becoming frozen instantly ends flashing, which includes removing its passthrough effect.
  // Removing flash here is redundant only if IceFreeze was called because Hit::freeze was added 
  // by the StatusBehaviorDirector. 
  // This is considered a reaction to becoming frozen, based on the interaction where qeueuing 
  // a freeze during timestop where a card activated some flashing effect results in the effect 
  // being cancelled.
  SetPassthrough(false);
  ClearStatuses(Hit::flash);
  Audio().Play(freezesfx, AudioPriority::highest);

  if (height <= 48) {
    iceFxAnimation << "small" << Animator::Mode::Loop;
    iceFx->setPosition(0, -height/2.f);
  }
  else if (height <= 75) {
    iceFxAnimation << "medium" << Animator::Mode::Loop;
    iceFx->setPosition(0, -height/2.f);
  }
  else {
    iceFxAnimation << "large" << Animator::Mode::Loop;
    iceFx->setPosition(0, -height/2.f);
  }

  iceFxAnimation.Refresh(iceFx->getSprite());
}

void Entity::Blind()
{
  float height = -GetHeight()/2.f;
  std::shared_ptr<AnimationComponent> anim = GetFirstComponent<AnimationComponent>();

  if (anim && anim->HasPoint("head")) {
    height = (anim->GetPoint("head") - anim->GetPoint("origin")).y;
  }

  blindFx->setPosition(0, height);
  blindFxAnimation << "default" << Animator::Mode::Loop;
  blindFxAnimation.Refresh(blindFx->getSprite());
}

void Entity::Confuse() {
  constexpr float OFFSET_Y = 10.f;

  float height = -GetHeight() - OFFSET_Y;
  std::shared_ptr<AnimationComponent> anim = GetFirstComponent<AnimationComponent>();

  if (anim && anim->HasPoint("head")) {
    height = (anim->GetPoint("head") - anim->GetPoint("origin")).y - OFFSET_Y;
  }

  confusedFx->setPosition(0, height);
  confusedFxAnimation << "default" << Animator::Mode::Loop;
  confusedFxAnimation.Refresh(confusedFx->getSprite());
}

bool Entity::IsCountered()
{
  return (counterable && !statuses.IsApplied(Hit::stun));
}

const Battle::TileHighlight Entity::GetTileHighlightMode() const {
  return mode;
}

void Entity::HighlightTile(Battle::TileHighlight mode)
{
  this->mode = mode;
}

void Entity::SetHitboxContext(Hit::Context context)
{
  hitboxProperties.context = context;
}

Hit::Context Entity::GetHitboxContext()
{
  return hitboxProperties.context;
}

void Entity::SetHitboxProperties(Hit::Properties props)
{
  hitboxProperties = props;
  hitboxProperties.flags |= props.context.flags;
  hitboxProperties.aggressor = props.context.aggressor;
}

const Hit::Properties Entity::GetHitboxProperties() const
{
  return hitboxProperties;
}

void Entity::IgnoreCommonAggressor(bool enable = true)
{
  ignoreCommonAggressor = enable;
}

const bool Entity::WillIgnoreCommonAggressor() const
{
  return ignoreCommonAggressor;
}
