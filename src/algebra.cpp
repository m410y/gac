#include "algebra.hpp"
#include "iohelper.hpp"
#include "ts_node_wrapper.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <ostream>
#include <string_view>

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
  Space.Types.insert({Ranks, std::unique_ptr<Type>(new Type(Space, Ranks))});
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

  for (size_t i : Indices)
    BVec[i].flip();

  auto Perm = Indices;
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
  return std::accumulate(BVec.begin(), BVec.end(), RankTy(0));
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

void Type::print(std::ostream &OS) const {
  printSeparated(OS, Ranks, "{", ",", "}");
}

void Element::print(std::ostream &OS) const {
  OS << Val;

  if (rank())
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

  auto AliasIt = Space.TypeAliases.find(TSN.string());
  if (AliasIt != Space.TypeAliases.end())
    return AliasIt->second;

  RankSet Ranks;
  for (const auto &Child : TSN.children())
    Ranks.merge(get(Space, Child)->Ranks);

  return get(Space, Ranks);
}

Element *Element::create(GASpace &Space, const TSNodeWrapper &TSN) {
  std::string_view Str = TSN.string();

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
  const std::array DefaultSign = {1.0, -1.0, 0.0};
  std::string_view Type = TSN.type();
  if (Type == "simple_metric") {
    for (char c : TSN.string()) {
      switch (c) {
      case '+':
        Sign.push_back(DefaultSign[0]);
        break;
      case '-':
        Sign.push_back(DefaultSign[1]);
        break;
      case '0':
        Sign.push_back(DefaultSign[2]);
        break;
      }
    }
  } else if (Type == "compact_metric") {
    size_t i = 0;
    for (const auto &Child : TSN.children()) {
      size_t n = Child.parse<size_t>();
      for (size_t j = 0; j < n; j++)
        Sign.push_back(DefaultSign[i]);

      if (++i > DefaultSign.size())
        break;
    }
  } else if (Type == "general_metric") {
    for (const auto &Child : TSN.children())
      Sign.push_back(Child.parse<double>());
  } else
    std::cerr << "Unknown metric " << std::quoted(Type) << "\n";
}
