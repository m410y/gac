#pragma once

#include "algebra.hpp"
#include "codegen.hpp"
#include "ts_node_wrapper.hpp"
#include <map>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ParseContext {
  ParseContext() : Types(), Space(nullptr) {}

  std::map<std::string, GA::Type *, std::less<>> Types;
  GA::GASpace &getSpace() {
    if (!Space)
      throw std::runtime_error("No space in context");

    return *Space;
  }
  void setSpace(GA::GASpace *Space) { this->Space = Space; }

private:
  GA::GASpace *Space;
};

class Node;
typedef std::unique_ptr<Node> NodePtr;

class Node {
public:
  virtual ~Node() = default;
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  virtual void print(std::ostream &OS) const = 0;
};

inline std::ostream &operator<<(std::ostream &OS, const NodePtr &NPtr) {
  if (NPtr)
    NPtr->print(OS);

  return OS;
}

//=============================================================================
// Expressions
//=============================================================================

class Expression : public Node {
public:
  virtual GA::Type *getType() const = 0;
  virtual llvm::Value *codegen(BuildContext &Context) const = 0;
};

class Literal : public Expression {
  GA::Element *El;

  explicit Literal(GA::Element *El) : El(El) {}

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override {
    return GA::Type::get(El->getSpace(), {El->rank()});
  }
  void print(std::ostream &OS) const override { OS << *El; }
  llvm::Value *codegen(BuildContext &Context) const override;
};

class Variable : public Expression {
  std::string Name;
  GA::Type *Type;

  explicit Variable(std::string_view Name, GA::Type *Type)
      : Name(Name), Type(Type) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override { return Type; }
  void print(std::ostream &OS) const override { OS << Name; }
  llvm::Value *codegen(BuildContext &Context) const override;
};

class CallExpression : public Expression {
  std::string_view Name;
  GA::Type *Type;
  std::vector<NodePtr> Args;

  explicit CallExpression(std::string_view Name, GA::Type *Type,
                          std::vector<NodePtr> Args)
      : Name(Name), Type(Type), Args(std::move(Args)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override { return Type; }
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class UnaryMinus : public Expression {
  NodePtr Expr;

  explicit UnaryMinus(NodePtr Expr) : Expr(std::move(Expr)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override {
    return dynamic_cast<Expression *>(Expr.get())->getType();
  }
  void print(std::ostream &OS) const override { OS << "-" << Expr; };
  llvm::Value *codegen(BuildContext &Context) const override;
};

class Projection : public Expression {
  NodePtr Expr;
  GA::Type *Type;

  Projection(NodePtr Expr, GA::Type *Type)
      : Expr(std::move(Expr)), Type(Type) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override { return Type; }
  void print(std::ostream &OS) const override {
    OS << "<" << Expr << ">" << *Type;
  };
  llvm::Value *codegen(BuildContext &Context) const override;
};

class BinaryExpression : public Expression {
  enum BinOp { assign, plus, minus, geom, dot, wedge, vee, scalar } Op;
  NodePtr Left;
  NodePtr Right;

  static std::map<std::string, BinaryExpression::BinOp, std::less<>> NodeOp;
  static std::map<BinOp, std::string> OpStr;

  BinaryExpression(BinOp Op, NodePtr Left, NodePtr Right)
      : Op(Op), Left(std::move(Left)), Right(std::move(Right)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

//=============================================================================
// Local scope statements
//=============================================================================

class Statement : public Node {
public:
  virtual void codegen(BuildContext &Context) const = 0;
};

class VariableDeclaration : public Statement {
  NodePtr Var;

  VariableDeclaration(NodePtr Var) : Var(std::move(Var)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  void print(std::ostream &OS) const override;
  void codegen(BuildContext &Context) const override;
};

class VariableDefinition : public Statement {
  NodePtr Decl;
  NodePtr Expr;

  VariableDefinition(NodePtr Decl, NodePtr Expr)
      : Decl(std::move(Decl)), Expr(std::move(Expr)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  void print(std::ostream &OS) const override { OS << Decl << " = " << Expr; }
  void codegen(BuildContext &Context) const override;
};

class ReturnStatement : public Statement {
  NodePtr Expr;

  ReturnStatement(NodePtr Expr) : Expr(std::move(Expr)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  void print(std::ostream &OS) const override { OS << "return " << Expr; }
  void codegen(BuildContext &Context) const override;
};

//=============================================================================
// Top-level statements
//=============================================================================

class UsingStatement : public Statement {
  GA::GASpace Space;

  UsingStatement(GA::GASpace &&Space) : Space(std::move(Space)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  void print(std::ostream &OS) const override {
    OS << "using " << Space << "\n";
  }
  void codegen(BuildContext &Context) const override;
};

class FunctionDefinition : public Statement {
  std::string Name;
  GA::Type *Type;
  std::vector<NodePtr> Params;
  std::vector<NodePtr> Body;

  FunctionDefinition(std::string_view Name, GA::Type *Type,
                     std::vector<NodePtr> Params, std::vector<NodePtr> Body)
      : Name(Name), Type(Type), Params(std::move(Params)),
        Body(std::move(Body)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  void print(std::ostream &OS) const override;
  void codegen(BuildContext &Context) const override;
};

//=============================================================================
// Syntax Tree
//=============================================================================

class SyntaxTree {
  std::vector<NodePtr> Statements;

public:
  SyntaxTree(const TSNodeWrapper &Root);
  friend std::ostream &operator<<(std::ostream &OS, const SyntaxTree &Tree);
  void codegen(BuildContext &Context) const;
};
