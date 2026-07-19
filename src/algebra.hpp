#pragma once

#include "ts_node_wrapper.hpp"
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <vector>

namespace GA {

class GASpace;
using RankTy = unsigned;
using RankSet = std::set<RankTy>;

class Type {
  GASpace &Space;
  RankSet Ranks;
  std::set<std::string_view> Aliases;

  explicit Type(GASpace &Space, const RankSet &Ranks)
      : Space(Space), Ranks(Ranks) {};

public:
  static Type *get(GASpace &Space, const RankSet &Ranks);
  static Type *get(GASpace &Space, const TSNodeWrapper &TSN);
  GASpace &getSpace() const { return Space; }
  RankSet &getRanks() { return Ranks; }
  const RankSet &getRanks() const { return Ranks; }
  size_t dof() const;
  void print(std::ostream &OS) const;
};

class Element {
  GASpace &Space;
  std::vector<bool> BVec;
  double Val;

  explicit Element(GASpace &Space, const std::vector<bool> &BVec, double Val)
      : Space(Space), BVec(BVec), Val(Val) {};

public:
  static Element *create(GASpace &Space, const std::vector<size_t> &Indices,
                         double Mul = 1.0);
  static Element *create(GASpace &Space, const TSNodeWrapper &TSN);

  GASpace &getSpace() const { return Space; }
  RankTy rank() const;
  size_t id() const;
  void print(std::ostream &OS) const;
};

class GASpace {
  std::vector<double> Sign;

  std::map<RankSet, std::unique_ptr<Type>> Types;
  std::map<std::string, Type *, std::less<>> TypeAliases;
  friend class Type;

  std::vector<std::unique_ptr<Element>> Elements;
  friend class Element;

public:
  GASpace() = default;
  GASpace(const GASpace &) = delete;
  GASpace &operator=(const GASpace &) = delete;

  GASpace(GASpace &&) = default;
  GASpace &operator=(GASpace &&) = default;

  GASpace(const std::vector<double> &Sign) : Sign(Sign) {}
  GASpace(const TSNodeWrapper &TSN);
  size_t dim() const { return Sign.size(); }
  void print(std::ostream &OS) const;
};

} // namespace GA

std::ostream &operator<<(std::ostream &OS, const GA::RankSet &Ranks);

inline std::ostream &operator<<(std::ostream &OS, const GA::GASpace &Space) {
  Space.print(OS);
  return OS;
}

inline std::ostream &operator<<(std::ostream &OS, const GA::Type &Type) {
  Type.print(OS);
  return OS;
}

inline std::ostream &operator<<(std::ostream &OS, const GA::Element &Element) {
  Element.print(OS);
  return OS;
}
