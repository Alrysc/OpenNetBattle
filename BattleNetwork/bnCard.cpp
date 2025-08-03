#include "bnCard.h"
#include <iostream>
#include <algorithm>
#include <tuple>

namespace Battle {
  Card::Card() 
  { }

  Card::Card(const Card::Properties& props) : props(props), unmodded(props)
  { }

  Card::Card(const Battle::Card & copy) : props(copy.props), unmodded(copy.unmodded) 
  { }

  Card::~Card() {
    props = unmodded = Card::Properties();
  }

  Card::Properties& Card::GetProps() {
    return props;
  }

  const Card::Properties& Card::GetProps() const {
    return props;
  }

  Card::Properties& Card::GetBaseProps() {
    return unmodded;
  }

  const Card::Properties& Card::GetBaseProps() const {
    return unmodded;
  }

  const string Card::GetVerboseDescription() const {
    return props.verboseDescription;
  }

  const string Card::GetDescription() const {
    return props.description;
  }

  const string Card::GetShortName() const {
    return props.shortname;
  }

  const char Card::GetCode() const {
    return props.code;
  }

  const signed Card::GetDamage() const {
    return props.damage;
  }

  const CardClass Card::GetClass() const
  {
    return props.cardClass;
  }

  const unsigned Card::GetLimit() const
  {
    return props.limit;
  }

  const std::string Card::GetAction() const
  {
    return props.action;
  }

  const bool Card::CanBoost() const
  {
    return props.canBoost;
  }

  const std::string Card::GetUUID() const {
    return props.uuid;
  }

  const Element Card::GetElement() const
  {
    return props.element;
  }

  const Element Card::GetSecondaryElement() const
  {
    return (props.element == props.secondaryElement) ? Element::none : props.secondaryElement;
  }

  const bool Card::IsNaviSummon() const
  {
    return this->IsTaggedAs("navi");
  }

  const bool Card::IsTimeFreeze() const
  {
    return props.timeFreeze;
  }

  const bool Card::IsTaggedAs(const std::string& meta) const
  {
    auto iter = std::find(props.metaClasses.begin(), props.metaClasses.end(), meta);
    return iter != props.metaClasses.end();
  }

  void Card::ModDamage(int32_t modifier, usize id_hash) 
  {
    auto iter = prevModifiers.find(id_hash);
    if (iter != prevModifiers.end()) {
      int32_t& value = iter->second;
      if (modifier != value) {
        props.damage += modifier - value;
        value = modifier;
      }
      return;
    }

    if (unmodded.damage != 0) {
      props.damage += modifier;
      prevModifiers.insert(std::make_pair(id_hash, modifier));
    }
  }

  void Card::ClearMod(usize id_hash) 
  {
    auto iter = prevModifiers.find(id_hash);
    if (iter == prevModifiers.end()) return;
    props.damage -= iter->second;
    prevModifiers.erase(iter);
  }

  const bool Card::HasMod(usize id_hash) 
  {
    return prevModifiers.find(id_hash) != prevModifiers.end();
  }

  void Card::MultiplyDamage(unsigned int multiplier)
  {
    this->multiplier *= multiplier;
    props.damage *= multiplier;
  }

  const unsigned Card::GetMultiplier() const
  {
    return multiplier;
  }

  bool Card::Compare::operator()(const Battle::Card & lhs, const Battle::Card & rhs) const noexcept
  {
    return lhs < rhs;;
  }
}