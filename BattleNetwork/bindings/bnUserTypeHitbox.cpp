#ifdef BN_MOD_SUPPORT

#include <optional>
#include "bnUserTypeHitbox.h"
#include "bnUserTypeEntity.h"
#include "bnScriptedArtifact.h"
#include "bnScriptedCharacter.h"
#include "bnScriptedSpell.h"
#include "bnScriptedObstacle.h"
#include "bnScriptedPlayer.h"
#include "../bnHitboxSpell.h"
#include "../bnSharedHitbox.h"

void DefineHitboxUserTypes(sol::state& state, sol::table& battle_namespace) {
  auto hitbox_table = battle_namespace.new_usertype<WeakWrapper<HitboxSpell>>("Hitbox",
    sol::factories([] (Team team) -> WeakWrapper<HitboxSpell> {
      auto spell = std::make_shared<HitboxSpell>(team);
      auto wrappedSpell = WeakWrapper(spell);
      wrappedSpell.Own();
      return wrappedSpell;
    }),
    sol::meta_function::index, []( sol::table table, const std::string key ) { 
      ScriptResourceManager::PrintInvalidAccessMessage( table, "Hitbox", key );
    },
    sol::meta_function::new_index, []( sol::table table, const std::string key, sol::object obj ) { 
      ScriptResourceManager::PrintInvalidAssignMessage( table, "Hitbox", key );
    },
    "set_callbacks", [](WeakWrapper<HitboxSpell>& spell, sol::object luaAttackCallbackObject, sol::object luaCollisionCallbackObject) {
      ExpectLuaFunction(luaAttackCallbackObject);
      ExpectLuaFunction(luaCollisionCallbackObject);

      auto attackCallback = [luaAttackCallbackObject] (std::shared_ptr<Entity> e) {
        sol::protected_function luaAttackCallback = luaAttackCallbackObject;
        auto result = luaAttackCallback(WeakWrapper(e));

        if (!result.valid()) {
          sol::error error = result;
          Logger::Log(LogLevel::critical, error.what());
        }
      };

      auto collisionCallback = [luaCollisionCallbackObject] (const std::shared_ptr<Entity> e) {
        sol::protected_function luaCollisionCallback = luaCollisionCallbackObject;
        auto result = luaCollisionCallback(WeakWrapper(e));

        if (!result.valid()) {
          sol::error error = result;
          Logger::Log(LogLevel::critical, error.what());
        }
      };

      spell.Unwrap()->AddCallback(attackCallback, collisionCallback);
    }
  );
  DefineEntityFunctionsOn(hitbox_table);

  battle_namespace.new_usertype<SharedHitbox>("SharedHitbox",
    sol::factories(
      [] (WeakWrapper<Entity>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<Character>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<ScriptedCharacter>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<Player>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<ScriptedPlayer>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<ScriptedSpell>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<ScriptedObstacle>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      },
      [] (WeakWrapper<Obstacle>& e, float f) -> WeakWrapper<Entity> {
        std::shared_ptr<Entity> spell = std::make_shared<SharedHitbox>(e.Unwrap(), f);
        auto wrappedSpell = WeakWrapper(spell);
        wrappedSpell.Own();
        return wrappedSpell;
      }
    )
  );

  auto createHitProps =
    [](int damage,
      Hit::Flags flags,
      Element element,
      Element secondaryElement,
      std::optional<Hit::Context> optCtx,
      Hit::Drag drag) {
    Hit::Properties props = { static_cast<uint32_t>(damage), flags, element, secondaryElement, 0, drag };

    if (optCtx) {
      props.context = *optCtx;
      props.aggressor = props.context.aggressor;
    }

    return props;
  };

  state.new_usertype<Hit::Properties>("HitProps",
    sol::factories(
      // deprecated API in v2.5
      createHitProps,
      [createHitProps](int damage, Hit::Flags flags, Element element, std::optional<Hit::Context> optCtx, Hit::Drag drag) {
        return createHitProps(damage, flags, element, Element::none, optCtx, drag);
      },
      // Cover for scripters who passed in Entity ID, which did nothing but is considered an 
      // error now without this constructor
      [createHitProps](int damage, Hit::Flags flags, Element element, EntityID_t id, Hit::Drag drag) {
        return createHitProps(damage, flags, element, Element::none, std::nullopt, drag);
      },
      [createHitProps](std::optional<Hit::Context> optCtx) -> Hit::Properties {
        return createHitProps(0, Hit::none, Element::none, Element::none, optCtx, Hit::Drag{});
      }
    ),
    // deprecated API in v2.5
    "aggressor", &Hit::Properties::aggressor,
    "damage", &Hit::Properties::damage,
    "drag", &Hit::Properties::drag,
    "element", &Hit::Properties::element,
    "element2", &Hit::Properties::secondaryElement,
    "flags", &Hit::Properties::flags,

    // New API in v2.5
    "from", [](Hit::Properties& self, Hit::Context ctx) -> Hit::Properties& { self.aggressor = ctx.aggressor; self.context = ctx; return self; },
    "dmg", [](Hit::Properties& self, int damage) -> Hit::Properties& { self.damage = static_cast<uint32_t>(damage); return self; },
    "drg", [](Hit::Properties& self, Hit::Drag drag) -> Hit::Properties& { self.drag = drag; return self; },
    "elem", [](Hit::Properties& self, Element element) -> Hit::Properties& { self.element = element;  return self; },
    "elem2", [](Hit::Properties& self, Element element) -> Hit::Properties& { self.secondaryElement = element;  return self; },

    // Add specific flags, some with duration
    "retangible", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::retangible;  return self; },
    "stun", sol::overload(
      [](Hit::Properties& self, frame_time_t duration) -> Hit::Properties& { 
        self.flags = self.flags | Hit::stun;  
        self.stun_duration = duration;
        return self; 
      },
      [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::stun;  return self; }
    ),
    "pierce", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::pierce;  return self; },
    "flinch", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::flinch;  return self; },
    "shake", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::shake;  return self; },
    "freeze", sol::overload(
      [](Hit::Properties& self, frame_time_t duration) -> Hit::Properties& { 
        self.flags = self.flags | Hit::freeze;  
        self.freeze_duration = duration;
        return self; 
      },
      [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::freeze;  return self; }
     ),
    "flash", sol::overload(
      [](Hit::Properties& self, frame_time_t duration) -> Hit::Properties& { 
        self.flags = self.flags | Hit::flash;
        self.flash_duration = duration;
        return self; 
      },
      [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::flash;  return self; }
     ),
    "breaking", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::breaking;  return self; },
    "impact", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::impact;  return self; },
    "drag", [](Hit::Properties& self, Hit::Drag drag) -> Hit::Properties& { 
      self.flags = self.flags | Hit::drag;
      self.drag = drag;
      return self;
    },
    "no_counter", [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::no_counter;  return self; },
    "root", sol::overload(
      [](Hit::Properties& self, frame_time_t duration) -> Hit::Properties& {
        self.flags = self.flags | Hit::root;
        self.root_duration = duration;
        return self;
      },
      [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::root;  return self; }
     ),
    "blind", sol::overload(
      [](Hit::Properties& self, frame_time_t duration) -> Hit::Properties& {
        self.flags = self.flags | Hit::blind;
        self.blind_duration = duration;
        return self;
      },
      [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::blind;  return self; }
     ),
    "confuse", sol::overload(
      [](Hit::Properties& self, frame_time_t duration) -> Hit::Properties& {
        self.flags = self.flags | Hit::confuse;
        self.confuse_duration = duration;
        return self;
      },
      [](Hit::Properties& self) -> Hit::Properties& { self.flags = self.flags | Hit::pierce;  return self; }
    )
  );

  state.new_enum("Hit",
    "None", Hit::none,
    "Flinch", Hit::flinch,
    "Flash", Hit::flash,
    "Stun", Hit::stun,
    "Root", Hit::root,
    "Impact", Hit::impact,
    "Shake", Hit::shake,
    "Pierce", Hit::pierce,
    "Retangible", Hit::retangible,
    "Breaking", Hit::breaking,
    "Bubble", Hit::bubble,
    "Freeze", Hit::freeze,
    "Drag", Hit::drag,
    "Blind", Hit::blind,
    "NoCounter", Hit::no_counter,
    "Confuse", Hit::confuse
  );

  state.new_usertype<Hit::Drag>("Drag",
    sol::factories(
      [] (Direction dir, unsigned count) { return Hit::Drag{ dir, count }; },
      [] { return Hit::Drag{ Direction::none, 0 }; }
    ),
    "None", sol::property([] { return Hit::Drag{ Direction::none, 0 }; }),
    sol::meta_function::index, []( sol::table table, const std::string key ) { 
      ScriptResourceManager::PrintInvalidAccessMessage( table, "Drag", key );
    },
    sol::meta_function::new_index, []( sol::table table, const std::string key, sol::object obj ) { 
      ScriptResourceManager::PrintInvalidAssignMessage( table, "Drag", key );
    },
    "direction", &Hit::Drag::dir,
    "count", &Hit::Drag::count
  );
}
#endif
