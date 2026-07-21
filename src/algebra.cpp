#include "algebra.hpp"
#include "ts_node_wrapper.hpp"
#include <array>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace GA;

constexpr size_t binomial(int n, int k) {
  if (k < 0 || n < k)
    return 0;

  if (n < 2 || k == 0 || k == n)
    return 1;

  size_t res = 1;
  for (int i = 1; i <= std::min(k, n - k); i++)
    res = (res * (n - i + 1)) / i;

  return res;
}

size_t Element::dof() const { return binomial(getSpace().dim(), rank()); }

size_t RankedType::dof() const {
  size_t res = 0;
  size_t dim = getSpace().dim();
  for (const ID &Rank : Ranks)
    res += binomial(dim, Rank);

  return res;
}

ID Element::rank() const {
  ID acc = 0;
  for (bool bit : BVec)
    acc += bit;

  return acc;
}

size_t Element::id() const {
  size_t res = 0;
  size_t k = rank();
  size_t n = BVec.size();

  for (size_t i = 0; i < n; i++) {
    if (BVec[i]) {
      k--;
      if (k == 0)
        break;

    } else
      res += binomial(n - i - 1, k - 1);
  }
  return res;
}

static IDSet bvec2indices(const std::vector<bool> BVec) {
  IDSet Indices;
  for (ID i = 0; i < BVec.size(); i++)
    if (BVec[i])
      Indices.insert(i);

  return Indices;
}

static std::vector<bool> indices2bvec(const IDSet &Indices, ID dim) {
  std::vector<bool> BVec(dim, 0);
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

RankedType *GASpace::getRanked(const IDSet &Ranks) {
  if (RankedTypes.count(Ranks))
    return RankedTypes.at(Ranks).get();

  auto NewType = std::make_unique<RankedType>(*this, Ranks);
  RankedType *Ptr = NewType.get();
  RankedTypes.insert({Ranks, std::move(NewType)});
  return Ptr;
}

Element *GASpace::getElement(const std::vector<bool> &BVec) {
  IDSet Indices = bvec2indices(BVec);

  if (Elements.count(Indices))
    return Elements.at(Indices).get();

  auto NewType = std::make_unique<Element>(*this, BVec);
  Element *Ptr = NewType.get();
  Elements.insert({Indices, std::move(NewType)});
  return Ptr;
}

Element *GASpace::getElement(const IDSet &Indices) {
  if (Elements.count(Indices))
    return Elements.at(Indices).get();

  std::vector<bool> BVec = indices2bvec(Indices, dim());

  auto NewType = std::make_unique<Element>(*this, BVec);
  Element *Ptr = NewType.get();
  Elements.insert({Indices, std::move(NewType)});
  return Ptr;
}

ElementValue GASpace::getElement(std::vector<ID> Indices) {
  std::vector<bool> BVec(dim(), 0);
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
// Printing
//=============================================================================

std::string Type::getName() const {
  std::ostringstream OSS;
  print(OSS);
  return OSS.str();
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

std::ostream &operator<<(std::ostream &OS, GA::Type *Type) {
  if (!Type)
    throw std::runtime_error("Type is nullptr");

  Type->print(OS);
  return OS;
}

std::ostream &operator<<(std::ostream &OS, GA::ElementValue Val) {
  Val.print(OS);
  return OS;
}

std::ostream &operator<<(std::ostream &OS, GA::GASpace *Space) {
  if (!Space)
    throw std::runtime_error("Space is nullptr");

  Space->print(OS);
  return OS;
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
  if (Name.has_value()) {
    OS << Name.value();
    return;
  }

  OS << "metric";
  printSeparated(OS, Sign, "{", ",", "}");
}

//=============================================================================
// Constructors from tree-sitter
//=============================================================================

RankedType *GASpace::getRanked(const TSNodeWrapper &TSN) {
  if (TSN.type() == "int_literal")
    return getRanked({TSN.parse<ID>()});

  auto AliasIt = Aliases.find(TSN.str());
  if (AliasIt != Aliases.end())
    return AliasIt->second;

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
