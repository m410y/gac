#pragma once

class TSNodeWrapper;

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace GA {

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

using ID = unsigned;
using IDComp = std::less<>; // TODO create my own comparator
using IDSet = std::set<ID, IDComp>;
using BitVec = std::vector<bool>;

class Type;
class RankedType;
class Element;
class ElementValue;

class GASpace {
  std::vector<double> Sign;
  std::string Name;
  std::map<std::string, Type *, std::less<>> Aliases;
  std::map<IDSet, std::unique_ptr<RankedType>> RankedTypes;
  std::map<IDSet, std::unique_ptr<Element>> Elements;

public:
  GASpace(const GASpace &) = delete;
  GASpace &operator=(const GASpace &) = delete;

  GASpace(const std::vector<double> &Sign, std::string_view Name = "")
      : Sign(Sign), Name(Name) {}
  GASpace(const TSNodeWrapper &TSN);

  RankedType *getRanked(const IDSet &Ranks, std::string_view Name = "");
  RankedType *getRanked(std::string_view Name);
  RankedType *getRanked(const TSNodeWrapper &TSN);

  Element *getElement(const BitVec &BVec);
  Element *getElement(const IDSet &Indices);

  ElementValue getElement(std::vector<ID> Indices);
  ElementValue getElement(const TSNodeWrapper &TSN);

  void setAlias(std::string_view Alias, Type *Type);
  std::string getDefaultName(const IDSet &Ranks);

  void print(std::ostream &OS) const;
  constexpr ID dim() const { return Sign.size(); }
  constexpr size_t size(ID rank) const { return binomial(dim(), rank); }
};

class Type {
  GASpace &Space;
  std::string_view Name;

public:
  Type(GASpace &Space, std::string_view Name = "") : Space(Space), Name(Name) {}
  GASpace &getSpace() const { return Space; }
  virtual ~Type() = default;

  void setName(std::string_view NewName) { Name = NewName; };
  std::string_view getName();

  virtual IDSet ranks() const = 0;
  virtual void print(std::ostream &OS) const = 0;
};

class Element : public Type {
  BitVec BVec;

public:
  Element(GASpace &Space, const BitVec &BVec) : Type(Space), BVec(BVec) {};

  constexpr ID rank() const {
    ID acc = 0;
    for (ID i = 0; i < BVec.size(); i++)
      acc += BVec[i];

    return acc;
  }

  constexpr size_t size() const { return getSpace().size(rank()); }

  constexpr size_t id() const {
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

  IDSet ranks() const override { return {rank()}; }
  void print(std::ostream &OS) const override;
};

class RankedType : public Type {
  IDSet Ranks;

public:
  RankedType(GASpace &Space, const IDSet &Ranks, std::string_view Name)
      : Type(Space, Name), Ranks(Ranks) {};

  IDSet ranks() const override { return Ranks; }
  void print(std::ostream &OS) const override;
};

class Value {
  GASpace &Space;

public:
  Value(GASpace &Space) : Space(Space) {}
  GASpace &getSpace() const { return Space; }
  virtual ~Value() = default;

  virtual Type *getType() const = 0;
  virtual void print(std::ostream &OS) const = 0;
};

class ElementValue : public Value {
  Element *El;
  double Val;

public:
  ElementValue(const Element &El, double Val)
      : Value(El.getSpace()), Val(Val) {}

  std::vector<double> getValues() const {
    std::vector<double> Values(El->size(), 0.0);
    Values[El->id()] = Val;
    return Values;
  }

  Type *getType() const override { return El; }
  void print(std::ostream &OS) const override;
};

} // namespace GA

inline std::ostream &operator<<(std::ostream &OS, GA::Type *Type) {
  return OS << Type->getName();
}

inline std::ostream &operator<<(std::ostream &OS, const GA::ElementValue &Val) {
  Val.print(OS);
  return OS;
}

inline std::ostream &operator<<(std::ostream &OS, const GA::GASpace &GASpace) {
  GASpace.print(OS);
  return OS;
}
