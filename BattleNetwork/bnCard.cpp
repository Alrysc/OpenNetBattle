#include "bnCard.h"
#include <iostream>
#include <algorithm>
#include <tuple>

namespace Battle {
  Card::Card() : props(), unmodded(props) 
  { }

  Card::Card(const Card::Properties& props) : props(props), unmodded(props)
  { }

  Card::Card(const Battle::Card & copy) : props(copy.props), unmodded(copy.props) 
  { }

  Card::~Card() {
    props = unmodded = Card::Properties();
  }

  const Card::Properties& Card::GetUnmoddedProps() const
  {
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

  const bool Card::IsSupport() const
  {
    return this->IsTaggedAs("support");
  }

  const bool Card::IsTimeFreeze() const
  {
    return true || props.timeFreeze;
  }

  const bool Card::IsTaggedAs(const std::string& meta) const
  {
    auto iter = std::find(props.metaClasses.begin(), props.metaClasses.end(), meta);
    return iter != props.metaClasses.end();
  }

  void Card::ModDamage(int modifier)
  {
    if (unmodded.damage != 0) {
      props.damage += modifier;
    }
  }

  void Card::MultiplyDamage(unsigned int multiplier)
  {
    this->multiplier = multiplier;
    props.damage *= this->multiplier;
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