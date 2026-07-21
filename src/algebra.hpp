#pragma once

#include <optional>
class TSNodeWrapper;

#include <map>
#include <memory>
#include <set>
#include <string>
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
  std::string getName() const;
};

class Element {
  GASpace &Space;
  std::vector<bool> BVec;
  double Mul;

  explicit Element(GASpace &Space, const std::vector<bool> &BVec, double Mul)
      : Space(Space), BVec(BVec), Mul(Mul) {};

public:
  static Element *create(GASpace &Space, const std::vector<size_t> &Indices,
                         double Mul = 1.0);
  static Element *create(GASpace &Space, const TSNodeWrapper &TSN);

  GASpace &getSpace() const { return Space; }
  RankTy rank() const;
  Type *getType() const { return Type::get(Space, {rank()}); }
  size_t id() const;
  std::vector<double> getValues() const {
    std::vector<double> Values(getType()->dof(), 0.0);
    Values[id()] = 1.0;
    return Values;
  }
  void print(std::ostream &OS) const;
};

class GASpace {
  std::optional<std::string> Name;
  std::vector<double> Sign;

  std::map<RankSet, std::unique_ptr<Type>> Types;
  std::map<std::string, Type *, std::less<>> TypeAliases;
  friend class Type;

  std::vector<std::unique_ptr<Element>> Elements;
  friend class Element;

public:
  GASpace(const std::vector<double> &Sign) : Sign(Sign) {}
  GASpace(const TSNodeWrapper &TSN);
  size_t dim() const { return Sign.size(); }
  void print(std::ostream &OS) const;
};

} // namespace GA

std::ostream &operator<<(std::ostream &OS, GA::Type *Type);
std::ostream &operator<<(std::ostream &OS, GA::Element *Element);
std::ostream &operator<<(std::ostream &OS, GA::GASpace *GASpace);
