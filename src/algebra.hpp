#pragma once

class TSNodeWrapper;

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace GA {

class GASpace;
using ID = unsigned;
using IDComp = std::less<>;
using IDSet = std::set<ID, IDComp>;

class Type {
  GASpace &Space;

public:
  Type(GASpace &Space) : Space(Space) {}
  virtual ~Type() = default;
  GASpace &getSpace() const { return Space; }
  virtual IDSet ranks() = 0;
  virtual size_t dof() const = 0;
  virtual void print(std::ostream &OS) const = 0;
  virtual std::string getName() const;
};

class Element : public Type {
  std::vector<bool> BVec;

public:
  Element(GASpace &Space, const std::vector<bool> &BVec)
      : Type(Space), BVec(BVec) {};
  ID rank() const;
  size_t id() const;
  IDSet ranks() override { return {rank()}; }
  size_t dof() const override;
  void print(std::ostream &OS) const override;
};

class RankedType : public Type {
  IDSet Ranks;

public:
  RankedType(GASpace &Space, const IDSet &Ranks) : Type(Space), Ranks(Ranks) {};
  IDSet ranks() override { return Ranks; }
  size_t dof() const override;
  void print(std::ostream &OS) const override;
};

class Value {
  GASpace &Space;

public:
  Value(GASpace &Space) : Space(Space) {}
  virtual ~Value() = default;
  GASpace &getSpace() const { return Space; }
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
    std::vector<double> Values(El->dof(), 0.0);
    Values[El->id()] = Val;
    return Values;
  }
  Type *getType() const override { return El; }
  void print(std::ostream &OS) const override;
};

class GASpace {
  std::optional<std::string> Name;
  std::vector<double> Sign;
  std::map<std::string, RankedType *, std::less<>> Aliases;
  std::map<IDSet, std::unique_ptr<RankedType>> RankedTypes;
  std::map<IDSet, std::unique_ptr<Element>> Elements;

public:
  GASpace(const GASpace &) = delete;
  GASpace &operator=(const GASpace &) = delete;

  GASpace(const std::vector<double> &Sign) : Sign(Sign) {}
  GASpace(const TSNodeWrapper &TSN);
  RankedType *getRanked(const IDSet &Ranks);
  RankedType *getRanked(const TSNodeWrapper &TSN);
  Element *getElement(const std::vector<bool> &BVec);
  Element *getElement(const IDSet &Indices);
  ElementValue getElement(std::vector<ID> Indices);
  ElementValue getElement(const TSNodeWrapper &TSN);
  void setAlias(Type *Type, std::string_view Name);
  size_t dim() const { return Sign.size(); }
  void print(std::ostream &OS) const;
};

} // namespace GA

std::ostream &operator<<(std::ostream &OS, GA::Type *Type);
std::ostream &operator<<(std::ostream &OS, GA::ElementValue Val);
std::ostream &operator<<(std::ostream &OS, GA::GASpace *GASpace);
