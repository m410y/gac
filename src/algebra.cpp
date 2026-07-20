#include "algebra.hpp"
#include "ts_node_wrapper.hpp"
#include <array>
#include <sstream>

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

using namespace GA;

Type *Type::get(GASpace &Space, const RankSet &Ranks) {
  std::unique_ptr<Type> NewType(new Type(Space, Ranks));
  Space.Types.insert({Ranks, std::move(NewType)});
  return Space.Types[Ranks].get();
}

size_t Type::dof() const {
  size_t res = 0;
  size_t dim = Space.dim();
  for (const RankTy &Rank : Ranks)
    res += binomial(dim, Rank);

  return res;
}

Element *Element::create(GASpace &Space, const std::vector<size_t> &Indices,
                         double Mul) {
  std::vector<bool> BVec(Space.dim());

  for (size_t i : Indices) {
    if (i >= BVec.size())
      throw std::runtime_error("Basis index is out of range");

    BVec[i].flip();
  }

  std::vector<size_t> Perm = Indices;
  // bubble sort
  for (size_t i = 0; i < Perm.size(); i++)
    for (size_t j = 1; i + j < Perm.size(); j++) {
      size_t &left = Perm[j - 1];
      size_t &right = Perm[j];

      if (left < right)
        continue;
      else if (left == right)
        Mul *= Space.Sign[right];

      std::swap(left, right);
      Mul *= -1.0;
    }

  Space.Elements.push_back(
      std::unique_ptr<Element>(new Element(Space, std::move(BVec), Mul)));
  return Space.Elements.back().get();
}

RankTy Element::rank() const {
  RankTy acc = 0;
  for (auto bit : BVec)
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

//=============================================================================
// Printing
//=============================================================================

static std::ostream &operator<<(std::ostream &OS, const GA::GASpace &Space) {
  Space.print(OS);
  return OS;
}

static std::ostream &operator<<(std::ostream &OS, const GA::Type &Type) {
  Type.print(OS);
  return OS;
}

static std::ostream &operator<<(std::ostream &OS, const GA::Element &Element) {
  Element.print(OS);
  return OS;
}

#include "iohelper.hpp"

std::ostream &operator<<(std::ostream &OS, GA::Type *Type) {
  if (!Type)
    throw std::runtime_error("Attempt to dereference nullptr Type");

  return OS << *Type;
}

std::ostream &operator<<(std::ostream &OS, GA::Element *Element) {
  if (!Element)
    throw std::runtime_error("Attempt to dereference nullptr Element");

  return OS << *Element;
}

std::ostream &operator<<(std::ostream &OS, GA::GASpace *Space) {
  if (!Space)
    throw std::runtime_error("Attempt to dereference nullptr Element");

  return OS << *Space;
}

void Type::print(std::ostream &OS) const {
  printSeparated(OS, Ranks, "{", ",", "}");
}

std::string Type::getName() const {
  std::ostringstream OSS;
  print(OSS);
  return OSS.str();
}

void Element::print(std::ostream &OS) const {
  OS << Val;

  if (rank() == 0)
    return;

  OS << "e";
  for (size_t i = 0; i < BVec.size(); i++) {
    if (BVec[i])
      OS << static_cast<int>(i);
  }
}

void GASpace::print(std::ostream &OS) const {
  printSeparated(OS, Sign, "{", ",", "}");
}

//=============================================================================
// Constructors from tree-sitter
//=============================================================================

Type *Type::get(GASpace &Space, const TSNodeWrapper &TSN) {
  if (TSN.type() == "int_literal")
    return Type::get(Space, {TSN.parse<RankTy>()});

  auto AliasIt = Space.TypeAliases.find(TSN.str());
  if (AliasIt != Space.TypeAliases.end())
    return AliasIt->second;

  RankSet Ranks;
  for (const auto &Child : TSN.children())
    for (RankTy Rank : get(Space, Child)->Ranks)
      Ranks.insert(Rank);

  return get(Space, Ranks);
}

Element *Element::create(GASpace &Space, const TSNodeWrapper &TSN) {
  std::string_view Str = TSN.str();

  if (Str.front() == 'e') {
    Str.remove_prefix(1);
    std::vector<size_t> Indices;
    for (char c : Str)
      Indices.push_back(c - '0');

    return Element::create(Space, Indices);
  }

  double mul;
  std::from_chars(Str.data(), Str.data() + Str.size(), mul);
  return Element::create(Space, {}, mul);
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
