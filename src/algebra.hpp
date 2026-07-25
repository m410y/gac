#pragma once

#include <algorithm>
#include <bit>
#include <bitset>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ga {

// Index Dimension, every value corresponds to separate dimension.
using ID = unsigned;
using RankSet = std::set<ID>;
constexpr auto MAX_DIMS = 8 * sizeof(size_t);
using BitVec = std::bitset<MAX_DIMS>;

constexpr size_t binomial(ID n, ID k) {
  if (n < k)
    return 0;

  if (n < 2 || k == 0 || k == n)
    return 1;

  size_t res = 1;
  for (ID i = 1u; i <= std::min(k, n - k); i++) {
    size_t resd = res / i;
    size_t resm = res % i;
    ID mul = (n - i + 1);
    res = resd * mul + (resm * mul) / i;
  }

  return res;
}

template <class R> class MVector;
template <class R> class Element;
template <template <class R> class T, class R> class Value;

//=============================================================================
// Space
//=============================================================================

// Mother class for all the algebra. Contains every type object and therefore
// if original space is deleted, every type pointer from it becomes invalid.
template <class R, class Comp = std::less<>> class Space {
  // Space signature
  std::vector<R> Sign;

  using Type = std::unique_ptr<MVector<R>>;
  // Containers for Types
  Type MVec = nullptr;
  std::vector<Type> DRankeds{};
  std::unordered_map<BitVec, Type> Elements{};

  void check() {
    if (dim() > MAX_DIMS)
      throw std::invalid_argument("Signature is too long");
  }

public:
  explicit Space(const std::vector<R> &Sign) : Sign(Sign) { check(); }
  explicit Space(std::initializer_list<R> Sign) : Sign(Sign) { check(); }

  // The only way to get new types from the space. Returned pointer is
  // guaranteed to be the same for one argument.
  template <template <class> class T, class... Args> T<R> *get(Args &&...args);

  // Returns element type by basis vectors in space
  Value<Element, R> element(std::vector<ID> indices) {
    R val = 1;
    for (size_t i = 0; i < indices.size(); i++)
      for (size_t j = 1; i + j < indices.size(); j++) {
        ID &left = indices[j - 1];
        ID &right = indices[j];

        if (Comp()(left, right))
          continue;
        else if (left == right) {
          val *= Sign[right];
          continue;
        }

        std::swap(left, right);
        val *= -1;
      }

    BitVec BVec{};
    for (ID i : indices) {
      if (i >= dim())
        throw std::out_of_range("Index exceeded space dimension");

      BVec[i].flip();
    }

    auto E = static_cast<Element<R> *>(this->get<Element>(BVec));
    return Value(E, val);
  }

  // Number of dimensions in the space.
  constexpr ID dim() const { return Sign.size(); }

  // Signature for i-th basis vector
  R signAt(ID i) const { return this->Sign.at(i); }
};

//=============================================================================
// MVector
//=============================================================================

// Effectively multivector is a singleton type for a space.
template <class R> class MVector {
protected:
  Space<R> &_Space;

  MVector(Space<R> &Space, const RankSet &Ranks) : _Space(Space) {
    if (std::ranges::any_of(Ranks, [&](ID rank) { return rank > Space.dim(); }))
      throw std::invalid_argument("Rank cannot exceed space dimension");
  }
  friend class Space<R>;

public:
  MVector(const MVector &) = delete;
  MVector &operator=(const MVector &) = delete;

  Space<R> &getSpace() { return _Space; }
  const Space<R> &getSpace() const { return _Space; }

  virtual ~MVector() = default;

  // Number of real components in multivector.
  virtual size_t size() const { return size_t(1) << this->_Space.dim(); }

  // Set of ranks, presented in multivector.
  virtual RankSet ranks() const {
    // probably I want to get it from the `Space`.
    RankSet RS;
    for (ID i : std::views::iota(0u, this->_Space.dim() + 1))
      RS.emplace_hint(RS.end(), i);
    return RS;
  }

  virtual std::pair<MVector<R> *, R> prod(Element<R> *other) = 0;
};

template <class R> inline bool sameSpace(MVector<R> *left, MVector<R> *right) {
  return &left->getSpace() == &right->getSpace();
}

//=============================================================================
// Ranked
//=============================================================================

// Abstract class that represents multivector with all components of single
// rank.
template <class R> class Ranked : public MVector<R> {
protected:
  Ranked(Space<R> &Space, ID rank) : MVector<R>(Space, {rank}) {}

public:
  virtual size_t size() const = 0;
  virtual RankSet ranks() const { return {rank()}; }
  virtual ID rank() const = 0;
};

//=============================================================================
// DRanked
//=============================================================================

// Dense ranked multivector. Contains all single ranked basis multivectors.
template <class R> class DRanked : public Ranked<R> {
  ID Rank;
  DRanked(Space<R> &Space, ID rank) : Ranked<R>(Space, rank), Rank(rank) {}
  friend class Space<R>;

public:
  size_t size() const override { return binomial(this->_Space.dim(), Rank); }
  ID rank() const override { return Rank; }
};

//=============================================================================
// Element
//=============================================================================

// Represents any basis multivector in algebra.
template <class R> class Element : public Ranked<R> {
  BitVec BVec;

  Element(Space<R> &Space, const BitVec &BVec)
      : Ranked<R>(Space, BVec.count()), BVec(BVec) {}
  friend class Space<R>;

public:
  size_t size() const override { return 1; }
  ID rank() const override { return BVec.count(); }

  std::pair<MVector<R> *, R> prod(Element<R> *other) override {
    if (!sameSpace(this, other))
      throw std::invalid_argument("Elements must be from the same space");
    R mul = 1;
    size_t common = this->BVec.to_ullong() & other->BVec.to_ullong();
    while (common) {
      // counting how many indices we skip
      ID i = std::countr_zero(common); // first common index
      size_t mask = (1ULL << i) - 1;
      size_t swaps = std::popcount(this->BVec.to_ullong() & mask);

      // multiplying by permutation sign and signature
      mul *= swaps % 2 ? -1 : 1;
      mul *= this->_Space.signAt(i);

      // unset bit at index i
      common &= (common - 1);
    }
    auto Type = this->_Space.template get<Element>(this->BVec ^ other->BVec);
    return {Type, mul};
  }
};

//=============================================================================
// Type factory
//=============================================================================

template <class R, class Comp>
template <template <class> class T, class... Args>
T<R> *Space<R, Comp>::get(Args &&...args) {
  if constexpr (std::is_same_v<T<R>, MVector<R>>) {
    static_assert(sizeof...(Args) == 0, "MVector doesn't accept parameters");
    if (!MVec)
      MVec = std::unique_ptr<MVector<R>>(new MVector<R>(*this));

    return static_cast<T<R> *>(MVec.get());
  } else if constexpr (std::is_same_v<T<R>, DRanked<R>>) {
    static_assert(sizeof...(Args) == 1,
                  "DRanked accepts only one ID parameter");
    ID rank = std::get<0>(std::tuple<Args...>{std::forward<Args>(args)...});

    if (rank >= DRankeds.size())
      DRankeds.resize(rank + 1);

    if (!DRankeds[rank])
      DRankeds[rank] = std::unique_ptr<DRanked<R>>(new DRanked<R>(*this, rank));

    return static_cast<T<R> *>(DRankeds[rank].get());
  } else if constexpr (std::is_same_v<T<R>, Element<R>>) {
    static_assert(sizeof...(Args) == 1,
                  "Element accepts only one BitVec parameter");
    BitVec BVec(std::get<0>(std::tuple<Args...>{std::forward<Args>(args)...}));

    auto [It, _] = Elements.try_emplace(
        BVec, std::unique_ptr<Element<R>>(new Element<R>(*this, BVec)));

    return static_cast<T<R> *>(It->second.get());
  } else {
    static_assert(sizeof(T<R>) == 0, "Unsupported type");
    return nullptr;
  }
}

//=============================================================================
// Value
//=============================================================================

template <template <class> class T, class R> class Value {
  std::unordered_map<T<R> *, R> Vals;

  Value(const std::unordered_map<T<R> *, R> &Vals) : Vals(Vals) {}
  friend class Space<R>;

public:
  template <class V> Value(T<R> *E, V val) : Vals{{E, val}} {}

  Value<T, R> operator+(const Value<T, R> &other) const {
    auto Res = this->Vals;
    for (const auto &[E, val] : other.Vals)
      Res[E] += val;
    return Res;
  }

  Value<T, R> operator-(const Value<T, R> &other) const {
    auto Res = this->Vals;
    for (const auto &[E, val] : other.Vals)
      Res[E] -= val;
    return Res;
  }

  Value<T, R> operator*(Value<T, R> other) const {
    std::unordered_map<T<R> *, R> Res{};
    for (const auto &[LE, lval] : this->Vals)
      for (const auto &[RE, rval] : other.Vals) {
        auto [E, mul] = LE->prod(RE);
        Res[static_cast<T<R> *>(E)] += mul * lval * rval;
      }
    return Res;
  }

  auto &values() { return Vals; }
  auto &values() const { return Vals; }
};

} // namespace ga
