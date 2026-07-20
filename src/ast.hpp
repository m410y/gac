#pragma once

#include "algebra.hpp"
#include "codegen.hpp"
#include "ts_node_wrapper.hpp"
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct FuncProto {
  std::string Name;
  GA::Type *RetType;
  using Param = std::pair<GA::Type *, std::optional<std::string>>;
  std::vector<Param> Params;
  bool IsDefined;

  void print(std::ostream &OS) const;
  bool operator==(const FuncProto &Right) const;
  bool operator!=(const FuncProto &Right) const { return !(*this == Right); }

private:
  FuncProto(std::string_view Name, GA::Type *RetType,
            const std::vector<Param> &Params, bool IsDefined)
      : Name(Name), RetType(RetType), Params(Params), IsDefined(IsDefined) {}
  friend class ParseContext;
};

typedef std::shared_ptr<FuncProto> FuncPtr;

inline std::ostream &operator<<(std::ostream &OS, const FuncProto &Proto) {
  Proto.print(OS);
  return OS;
}

std::ostream &operator<<(std::ostream &OS, const FuncProto::Param &Param);
std::ostream &operator<<(std::ostream &OS, FuncPtr Proto);

class ParseContext {
  GA::GASpace *Space;
  std::map<std::string, GA::Type *, std::less<>> Types;
  std::map<std::string, FuncPtr, std::less<>> Funcs;

  FuncPtr createFunc(const FuncProto &Proto);

public:
  ParseContext() : Space(nullptr), Types(), Funcs() {}

  GA::GASpace &getSpace() const;
  void setSpace(GA::GASpace *Space);

  GA::Type *getVarType(std::string_view Name) const;
  void setVarType(std::string_view Name, GA::Type *Type);

  FuncPtr getFunc(std::string_view Name) const;
  FuncPtr declareFunc(std::string_view Name, GA::Type *RetType,
                      const std::vector<FuncProto::Param> &Params) {
    return createFunc(FuncProto(Name, RetType, Params, false));
  }
  FuncPtr defineFunc(std::string_view Name, GA::Type *RetType,
                     const std::vector<FuncProto::Param> &Params) {
    return createFunc(FuncProto(Name, RetType, Params, true));
  }
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
  static GA::Type *getType(const NodePtr &Node);
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
  void print(std::ostream &OS) const override { OS << El; }
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
  FuncPtr Proto;
  std::vector<NodePtr> Args;

  explicit CallExpression(FuncPtr Proto, std::vector<NodePtr> Args)
      : Proto(Proto), Args(std::move(Args)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  GA::Type *getType() const override { return Proto->RetType; }
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
    OS << "<" << Expr << ">" << Type;
  };
  llvm::Value *codegen(BuildContext &Context) const override;
};

enum BinOp { assign, plus, minus, geom, dot, wedge, vee, scalar };
template <BinOp Op> class BinaryExpression : public Expression {
  NodePtr Left;
  NodePtr Right;

  BinaryExpression(NodePtr Left, NodePtr Right)
      : Left(std::move(Left)), Right(std::move(Right)) {};

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context) {
    NodePtr Left = Node::create(TSN.child(0), Context);
    NodePtr Right = Node::create(TSN.child(1), Context);
    return std::unique_ptr<BinaryExpression<Op>>(
        new BinaryExpression<Op>(std::move(Left), std::move(Right)));
  }
  GA::Type *getType() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &) const override { return nullptr; };
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

class FunctionDeclaration : public Statement {
  FuncPtr Proto;

  FunctionDeclaration(FuncPtr Proto) : Proto(Proto) {}

public:
  static NodePtr create(const TSNodeWrapper &TSN, ParseContext &Context);
  void print(std::ostream &OS) const override { OS << Proto << "\n"; }
  void codegen(BuildContext &Context) const override;
};

class FunctionDefinition : public Statement {
  FuncPtr Proto;
  std::vector<NodePtr> Body;

  FunctionDefinition(FuncPtr Proto, std::vector<NodePtr> Body)
      : Proto(Proto), Body(std::move(Body)) {};

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
