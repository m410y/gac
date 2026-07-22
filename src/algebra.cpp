#include "algebra.hpp"
#include "ts_node_wrapper.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace GA;

static IDSet bvec2ind(const BitVec BVec) {
  IDSet Indices;
  for (ID i = 0; i < BVec.size(); i++)
    if (BVec[i])
      Indices.insert(i);

  return Indices;
}

static BitVec ind2bvec(const IDSet &Indices, ID dim) {
  BitVec BVec(dim, 0);
  for (ID i : Indices) {
    if (i >= dim)
      throw std::out_of_range("Too large index for current space");

    BVec[i] = 1;
  }

  return BVec;
}

//=============================================================================
// Type factory
//=============================================================================

RankedType *GASpace::getRanked(const IDSet &Ranks, std::string_view Name) {
  if (Ranks.empty())
    throw std::runtime_error("Ranks is empty");

  if (RankedTypes.count(Ranks))
    return RankedTypes.at(Ranks).get();

  const auto &[It, _] = RankedTypes.emplace(
      Ranks, std::make_unique<RankedType>(*this, Ranks, Name));

  RankedType *Ty = It->second.get();
  if (Name.empty())
    setAlias(getDefaultName(Ranks), Ty);

  return Ty;
}

Element *GASpace::getElement(const BitVec &BVec) {
  IDSet Indices = bvec2ind(BVec);
  if (Elements.count(Indices))
    return Elements.at(Indices).get();

  const auto &[It, _] =
      Elements.emplace(Indices, std::make_unique<Element>(*this, BVec));
  return It->second.get();
}

Element *GASpace::getElement(const IDSet &Indices) {
  if (Elements.count(Indices))
    return Elements.at(Indices).get();

  BitVec BVec = ind2bvec(Indices, dim());
  const auto &[It, _] =
      Elements.emplace(Indices, std::make_unique<Element>(*this, BVec));
  return It->second.get();
}

ElementValue GASpace::getElement(std::vector<ID> Indices) {
  BitVec BVec(dim(), 0);
  double Val = 1.0;

  for (ID i : Indices) {
    if (i >= dim())
      throw std::out_of_range("Too large index for current space");

    BVec[i].flip();
  }

  // bubble sort
  for (size_t i = 0; i < Indices.size(); i++)
    for (size_t j = 1; i + j < Indices.size(); j++) {
      ID &left = Indices[j - 1];
      ID &right = Indices[j];

      if (IDComp()(left, right))
        continue;
      else if (left == right)
        Val *= Sign[right];

      std::swap(left, right);
      Val *= -1.0;
    }

  Element *El = getElement(BVec);
  return ElementValue(*El, Val);
}

//=============================================================================
// Aliases
//=============================================================================

void GASpace::setAlias(std::string_view Alias, Type *Ty) {
  auto It = Aliases.find(Alias);
  if (It == Aliases.end()) {
    auto [It, _] = Aliases.emplace(std::string(Alias), Ty);
    Ty->setName(It->first);
  } else {
    It->second->setName("");
    It->second = Ty;
    Ty->setName(It->first);
  }
}

// I like this stuff
std::map<bool (*)(ID), const char *> DefaultNames = {
    {[](ID rank) { return rank == 0; }, "scalar"},
    {[](ID rank) { return rank == 1; }, "vector"},
    {[](ID rank) { return rank == 2; }, "bivector"},
    {[](ID rank) { return rank == 3; }, "trivector"},
    {[](ID rank) { return rank % 2 == 0; }, "spinor"},
    {[](ID) { return true; }, "mvector"}};

std::string GASpace::getDefaultName(const IDSet &Ranks) {
  for (auto &[Test, Name] : DefaultNames) {
    IDSet Set;
    for (ID rank = 0; rank <= dim(); rank++)
      if (Test(rank))
        Set.insert(rank);

    if (Set == Ranks)
      return Name;

    Set.clear();
    for (ID rank = 0; rank <= dim(); rank++)
      if (Test(rank))
        Set.insert(dim() - rank);

    if (Set == Ranks)
      return std::string("p") + Name;
  }
  return "";
}

// FIXME I hate this stuff
static const std::map<std::string_view, ID, std::less<>> DefaultAliases = {
    {"scalar", 0}, {"vector", 1}, {"bivector", 2}, {"trivector", 3}};

RankedType *GASpace::getRanked(std::string_view Name) {
  if (Name == "mvector") {
    IDSet Ranks;
    for (ID i = 0; i <= dim(); i++)
      Ranks.insert(i);

    return getRanked(Ranks, Name);
  }

  if (Name == "spinor") {
    IDSet Ranks;
    for (ID i = 0; i <= dim(); i += 2)
      Ranks.insert(i);

    return getRanked(Ranks, Name);
  }

  bool isPseudo = Name.front() == 'p';
  std::string_view ReducedName = Name;
  if (isPseudo)
    ReducedName.remove_prefix(1);

  auto It = DefaultAliases.find(ReducedName);
  if (It == DefaultAliases.end())
    return nullptr;

  ID Rank = isPseudo ? (dim() - It->second) : It->second;
  return getRanked({Rank}, Name);
}

//=============================================================================
// Printing
//=============================================================================

std::string_view Type::getName() {
  if (Name.empty()) {
    std::ostringstream OSS;
    print(OSS);
    Space.setAlias(OSS.str(), this);
  }
  return Name;
}

template <typename T>
static void printSeparated(std::ostream &OS, const T &Container,
                           std::string_view Start, std::string_view Sep,
                           std::string_view Stop) {
  OS << Start;
  bool begin = true;
  for (const auto &Element : Container)
    OS << (begin ? (begin = false, "") : Sep) << Element;

  OS << Stop;
}

void Element::print(std::ostream &OS) const {
  if (rank() == 0) {
    OS << "1";
    return;
  }

  OS << "e";
  for (size_t i = 0; i < BVec.size(); i++) {
    if (BVec[i])
      OS << static_cast<int>(i);
  }
}

void RankedType::print(std::ostream &OS) const {
  printSeparated(OS, Ranks, "{", ",", "}");
}

void ElementValue::print(std::ostream &OS) const {
  if (!El)
    throw std::runtime_error("Element is nullptr");

  OS << El << " " << Val;
}

void GASpace::print(std::ostream &OS) const {
  if (Name.empty()) {
    OS << "metric";
    printSeparated(OS, Sign, "{", ",", "}");
  } else
    OS << Name;
}

//=============================================================================
// Constructors from tree-sitter
//=============================================================================

RankedType *GASpace::getRanked(const TSNodeWrapper &TSN) {
  if (TSN.type() == "int_literal")
    return getRanked({TSN.parse<ID>()});

  RankedType *FromStr = getRanked(TSN.str());
  if (FromStr)
    return FromStr;

  IDSet Ranks;
  for (const auto &Child : TSN.children())
    for (ID rank : (getRanked(Child)->ranks()))
      Ranks.insert(rank);

  return getRanked(Ranks);
}

ElementValue GASpace::getElement(const TSNodeWrapper &TSN) {
  std::string_view Str = TSN.str();

  if (Str.front() == 'e') {
    Str.remove_prefix(1);
    std::vector<ID> Indices;
    for (char c : Str)
      Indices.push_back(c - '0');

    return getElement(Indices);
  }

  double mul;
  std::from_chars(Str.data(), Str.data() + Str.size(), mul);
  Element *El = getElement(IDSet{});
  return ElementValue(*El, mul);
}

GASpace::GASpace(const TSNodeWrapper &TSN) {
  std::array DefaultSigns = {1.0, -1.0, 0.0};
  std::string_view Type = TSN.type();
  if (Type == "simple_metric") {
    for (char c : TSN.str()) {
      switch (c) {
      case '+':
        Sign.push_back(DefaultSigns.at(0));
        break;
      case '-':
        Sign.push_back(DefaultSigns.at(1));
        break;
      case '0':
        Sign.push_back(DefaultSigns.at(2));
        break;
      }
    }
  } else if (Type == "compact_metric") {
    size_t i = 0;
    for (const auto &Child : TSN.children()) {
      size_t n = Child.parse<size_t>();
      for (size_t j = 0; j < n; j++)
        Sign.push_back(DefaultSigns[i]);

      if (++i > DefaultSigns.size())
        break;
    }
  } else if (Type == "general_metric") {
    for (const auto &Child : TSN.children())
      Sign.push_back(Child.parse<double>());
  } else
    throw std::runtime_error("Unknown metric " + std::string(Type));
}
