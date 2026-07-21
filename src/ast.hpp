#pragma once

#include "algebra.hpp"
#include "ts_node_wrapper.hpp"

class BuildContext;

#include <llvm/IR/Function.h>
#include <map>

class Node;
typedef std::unique_ptr<Node> NodePtr;

class Node {
public:
  virtual ~Node() = default;
  virtual void verify() const = 0;
  virtual std::vector<Node *> children() const = 0;
  virtual void print(std::ostream &) const {};
  virtual std::string dump() const;
  virtual llvm::Value *codegen(BuildContext &) const { return nullptr; };
};

inline std::ostream &operator<<(std::ostream &OS, const Node &N) {
  N.print(OS);
  return OS;
}

inline std::ostream &operator<<(std::ostream &OS, Node *NPtr) {
  if (NPtr)
    NPtr->print(OS);

  return OS;
}

class ParseContext;

template <class T>
std::unique_ptr<T> create(const TSNodeWrapper &TSN, ParseContext &Context);

template <>
NodePtr create<Node>(const TSNodeWrapper &TSN, ParseContext &Context);

struct FuncProto {
  std::string Name;
  GA::Type *RetType;
  using Param = std::pair<GA::Type *, std::optional<std::string>>;
  std::vector<Param> Params;
  bool IsDefined;

  FuncProto(std::string_view Name, GA::Type *RetType,
            const std::vector<Param> &Params, bool IsDefined)
      : Name(Name), RetType(RetType), Params(Params), IsDefined(IsDefined) {}

  void verify() const;
  void print(std::ostream &OS) const;
  bool operator==(const FuncProto &Right) const;
  bool operator!=(const FuncProto &Right) const { return !(*this == Right); }
  llvm::Function *codegen(BuildContext &Context);
};

typedef std::shared_ptr<FuncProto> FuncPtr;

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
  void clearVars() { Types.clear(); }

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

//=============================================================================
// Expressions
//=============================================================================

class Expression;
typedef std::unique_ptr<Expression> ExprPtr;

class Expression : public Node {
public:
  void verify() const override;
  virtual GA::Type *getType() const = 0;
};

class Literal : public Expression {
  GA::Element *El;

public:
  explicit Literal(GA::Element *El) : El(El) {}
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {}; }
  GA::Type *getType() const override {
    return GA::Type::get(El->getSpace(), {El->rank()});
  }
  void verify() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class Variable : public Expression {
  std::string Name;
  GA::Type *Type;

public:
  explicit Variable(std::string_view Name, GA::Type *Type)
      : Name(Name), Type(Type) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {}; }
  void verify() const override;
  GA::Type *getType() const override { return Type; }
  std::string_view getName() const { return Name; }
  void print(std::ostream &OS) const override { OS << Name; }
  llvm::Value *codegen(BuildContext &Context) const override;
};

class CallExpression : public Expression {
  FuncPtr Proto;
  std::vector<NodePtr> Args;

public:
  explicit CallExpression(FuncPtr Proto, std::vector<NodePtr> Args)
      : Proto(Proto), Args(std::move(Args)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  void verify() const override;
  std::vector<Node *> children() const override {
    std::vector<Node *> Childs;
    for (const auto &Node : Args)
      Childs.push_back(Node.get());

    return Childs;
  }
  GA::Type *getType() const override { return Proto->RetType; }
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class UnaryMinus : public Expression {
  ExprPtr Expr;

public:
  explicit UnaryMinus(ExprPtr Expr) : Expr(std::move(Expr)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {Expr.get()}; }
  void verify() const override;
  GA::Type *getType() const override {
    return dynamic_cast<Expression *>(Expr.get())->getType();
  }
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class Projection : public Expression {
  ExprPtr Expr;
  GA::Type *Type;

public:
  Projection(ExprPtr Expr, GA::Type *Type)
      : Expr(std::move(Expr)), Type(Type) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  void verify() const override;
  std::vector<Node *> children() const override { return {Expr.get()}; }
  GA::Type *getType() const override { return Type; }
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

enum BinOp { assign, plus, minus, geom, dot, wedge, vee, scalar };
template <BinOp Op> class BinaryExpression : public Expression {
  ExprPtr Left;
  ExprPtr Right;

public:
  BinaryExpression(ExprPtr Left, ExprPtr Right)
      : Left(std::move(Left)), Right(std::move(Right)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context) {
    ExprPtr Left = create<Expression>(TSN.child(0), Context);
    ExprPtr Right = create<Expression>(TSN.child(1), Context);
    return std::make_unique<BinaryExpression<Op>>(std::move(Left),
                                                  std::move(Right));
  }
  void verify() const override;
  std::vector<Node *> children() const override {
    return {Left.get(), Right.get()};
  }
  GA::Type *getType() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &) const override { return nullptr; };
};

//=============================================================================
// Local scope statements
//=============================================================================

class Statement : public Node {};

class VariableDeclaration : public Statement {
  std::unique_ptr<Variable> Var;
  friend class VariableDefinition;

public:
  VariableDeclaration(std::unique_ptr<Variable> Var) : Var(std::move(Var)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {Var.get()}; }
  void verify() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class VariableDefinition : public Statement {
  std::unique_ptr<VariableDeclaration> Decl;
  ExprPtr Expr;

public:
  VariableDefinition(std::unique_ptr<VariableDeclaration> Decl, ExprPtr Expr)
      : Decl(std::move(Decl)), Expr(std::move(Expr)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override {
    return {Decl.get(), Expr.get()};
  }
  void verify() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class ReturnStatement : public Statement {
  ExprPtr Expr;

public:
  ReturnStatement(ExprPtr Expr) : Expr(std::move(Expr)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {Expr.get()}; }
  void verify() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

//=============================================================================
// Top-level statements
//=============================================================================

class UsingStatement : public Statement {
  std::unique_ptr<GA::GASpace> Space;

public:
  UsingStatement(std::unique_ptr<GA::GASpace> Space)
      : Space(std::move(Space)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {}; }
  void verify() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class FunctionDeclaration : public Statement {
  FuncPtr Proto;

public:
  FunctionDeclaration(FuncPtr Proto) : Proto(Proto) {}
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  std::vector<Node *> children() const override { return {}; }
  void verify() const override;
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

class FunctionDefinition : public Statement {
  FuncPtr Proto;
  std::vector<NodePtr> Body;

public:
  FunctionDefinition(FuncPtr Proto, std::vector<NodePtr> Body)
      : Proto(Proto), Body(std::move(Body)) {};
  static NodePtr createNode(const TSNodeWrapper &TSN, ParseContext &Context);
  void verify() const override;
  std::vector<Node *> children() const override {
    std::vector<Node *> Childs;
    for (const auto &Node : Body)
      Childs.push_back(Node.get());

    return Childs;
  }
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};

//=============================================================================
// Syntax Tree
//=============================================================================

class SyntaxTree : public Node {
  std::vector<NodePtr> Statements;

public:
  SyntaxTree(const TSNodeWrapper &Root);
  virtual void verify() const override;
  std::vector<Node *> children() const override {
    std::vector<Node *> Childs;
    for (const auto &Node : Statements)
      Childs.push_back(Node.get());

    return Childs;
  }
  void print(std::ostream &OS) const override;
  llvm::Value *codegen(BuildContext &Context) const override;
};
