#include "ast.hpp"
#include "algebra.hpp"
#include "iohelper.hpp"
#include "ts_node_wrapper.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <utility>
#include <vector>

//=============================================================================
// Expressions
//=============================================================================

NodePtr Literal::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  return std::unique_ptr<Literal>(
      new Literal(GA::Element::create(Context.getSpace(), TSN)));
}

NodePtr Variable::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  std::string Name(TSN.string());
  GA::Type *Type;
  try {
    Type = Context.Types.at(Name);
  } catch (std::out_of_range &e) {
    throw std::runtime_error("Undefined identifier " + Name);
  }
  return std::unique_ptr<Variable>(new Variable(Name, Type));
}

NodePtr CallExpression::create(const TSNodeWrapper &TSN,
                               ParseContext &Context) {
  std::string name(TSN.field("name").string());
  std::vector<NodePtr> Args;

  for (const auto &Arg : TSN.field("args").children())
    Args.push_back(Node::create(Arg, Context));

  return std::unique_ptr<CallExpression>(
      new CallExpression(name, Context.Types.at(name), std::move(Args)));
}

NodePtr UnaryMinus::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  return std::unique_ptr<UnaryMinus>(
      new UnaryMinus(Node::create(TSN.child(), Context)));
}

NodePtr Projection::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  GA::Type *Type = GA::Type::get(Context.getSpace(), TSN.field("ranks"));
  return std::unique_ptr<Projection>(
      new Projection(Node::create(TSN.child(), Context), Type));
}

std::map<std::string, BinaryExpression::BinOp, std::less<>>
    BinaryExpression::NodeOp = {
        {"assignment", assign},  {"binary_plus", plus},
        {"binary_minus", minus}, {"geometric_product", geom},
        {"dot_product", dot},    {"wedge_product", wedge},
        {"vee_product", vee},    {"scalar_product", scalar},
};

NodePtr BinaryExpression::create(const TSNodeWrapper &TSN,
                                 ParseContext &Context) {
  std::string_view Type = TSN.type();

  auto OpIt = NodeOp.find(Type);
  if (OpIt == NodeOp.end())
    throw std::runtime_error("Unknown binary operator " + std::string(Type));

  return std::unique_ptr<BinaryExpression>(
      new BinaryExpression(OpIt->second, Node::create(TSN.child(0), Context),
                           Node::create(TSN.child(1), Context)));
}

//=============================================================================
// Local scope statements
//=============================================================================

NodePtr VariableDeclaration::create(const TSNodeWrapper &TSN,
                                    ParseContext &Context) {
  GA::Type *Type = GA::Type::get(Context.getSpace(), TSN.field("type"));
  TSNodeWrapper VarNode = TSN.field("name");
  std::string Name(VarNode.string());
  auto TypesIt = Context.Types.find(Name);
  if (TypesIt != Context.Types.end())
    throw std::runtime_error("Redefinition of variable " + Name);

  Context.Types.insert({Name, Type});
  return std::unique_ptr<VariableDeclaration>(
      new VariableDeclaration(Node::create(VarNode, Context)));
}

NodePtr VariableDefinition::create(const TSNodeWrapper &TSN,
                                   ParseContext &Context) {
  return std::unique_ptr<VariableDefinition>(
      new VariableDefinition(Node::create(TSN.field("decl"), Context),
                             Node::create(TSN.field("expr"), Context)));
}

NodePtr ReturnStatement::create(const TSNodeWrapper &TSN,
                                ParseContext &Context) {
  return std::unique_ptr<ReturnStatement>(
      new ReturnStatement(Node::create(TSN.child(), Context)));
}

//=============================================================================
// Top-level statements
//=============================================================================

NodePtr UsingStatement::create(const TSNodeWrapper &TSN,
                               ParseContext &Context) {
  std::unique_ptr<UsingStatement> NewSpace(
      new UsingStatement(GA::GASpace(TSN.child())));
  Context.setSpace(&NewSpace->Space);
  return std::move(NewSpace);
}

NodePtr FunctionDefinition::create(const TSNodeWrapper &TSN,
                                   ParseContext &Context) {
  std::string Name(TSN.field("name").string());
  GA::Type *Type = GA::Type::get(Context.getSpace(), TSN.field("type"));

  auto TypesIt = Context.Types.find(Name);
  if (TypesIt != Context.Types.end())
    throw std::runtime_error("Redefinition of variable " + Name);

  Context.Types.insert({Name, Type});

  std::vector<NodePtr> Params;
  for (auto &variable_decl : TSN.field("params").children())
    Params.push_back(Node::create(variable_decl, Context));

  std::vector<NodePtr> Body;
  for (auto &statement : TSN.field("body").children())
    Body.push_back(Node::create(statement, Context));

  return std::unique_ptr<FunctionDefinition>(
      new FunctionDefinition(Name, Type, std::move(Params), std::move(Body)));
}

//=============================================================================
// Syntax tree
//=============================================================================

SyntaxTree::SyntaxTree(const TSNodeWrapper &root) {
  ParseContext Context;
  for (auto &top_level_statement : root.children())
    Statements.push_back(Node::create(top_level_statement, Context));
}

static NodePtr trivial_node(const TSNodeWrapper &TSN, ParseContext &Context) {
  return Node::create(TSN.child(), Context);
}

static const std::map<
    std::string, std::function<NodePtr(const TSNodeWrapper &, ParseContext &)>,
    std::less<>>
    Constructors = {
        {"int_literal", Literal::create},
        {"float_literal", Literal::create},
        {"basis_literal", Literal::create},
        {"identifier", Variable::create},
        {"parenthesized_expression", trivial_node},
        {"call_expression", CallExpression::create},
        {"unary_plus", trivial_node},
        {"unary_minus", UnaryMinus::create},
        {"projection", Projection::create},
        {"assignment", BinaryExpression::create},
        {"binary_plus", BinaryExpression::create},
        {"binary_minus", BinaryExpression::create},
        {"geometric_product", BinaryExpression::create},
        {"wedge_product", BinaryExpression::create},
        {"vee_product", BinaryExpression::create},
        {"dot_product", BinaryExpression::create},
        {"scalar_product", BinaryExpression::create},
        {"variable_declaration", VariableDeclaration::create},
        {"variable_definition", VariableDefinition::create},
        {"return_statement", ReturnStatement::create},
        {"using_statement", UsingStatement::create},
        {"function_definition", FunctionDefinition::create},
};

NodePtr Node::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  std::string_view Type = TSN.type();
  auto ConstructIt = Constructors.find(Type);
  if (ConstructIt == Constructors.end())
    throw std::runtime_error("Unknown node name " + std::string(Type));

  return ConstructIt->second(TSN, Context);
}

//=============================================================================
// Printing
//=============================================================================

void CallExpression::print(std::ostream &OS) const {
  printSeparated(OS, Args, "(", ", ", ")");
}

std::map<BinaryExpression::BinOp, std::string> BinaryExpression::OpStr = {
    {assign, " = "}, {plus, " + "},  {minus, " - "}, {geom, " "},
    {dot, " . "},    {wedge, " w "}, {vee, " v "},   {scalar, " * "},
};

void BinaryExpression::print(std::ostream &OS) const {
  OS << "(" << Left << OpStr.at(Op) << Right << ")";
}

void VariableDeclaration::print(std::ostream &OS) const {
  if (!Var)
    return;

  GA::Type *Type = dynamic_cast<Variable *>(Var.get())->getType();
  OS << *Type << " " << Var;
};

void FunctionDefinition::print(std::ostream &OS) const {
  OS << "function " << Name;
  printSeparated(OS, Params, " (", ", ", ")");
  OS << " -> " << *Type;
  printSeparated(OS, Body, "\n  ", "\n  ", "\nend\n");
}

std::ostream &operator<<(std::ostream &OS, const SyntaxTree &Tree) {
  printSeparated(OS, Tree.Statements, "---=== AST dump begin ===---\n", "\n",
                 "---===  AST dump end  ===---");
  return OS;
}

//=============================================================================
// Binary expression's type resolve
//=============================================================================

GA::Type *BinaryExpression::getType() const {
  GA::RankSet Ranks;
  GA::Type *LType = dynamic_cast<Expression *>(Left.get())->getType();
  GA::Type *RType = dynamic_cast<Expression *>(Right.get())->getType();
  GA::GASpace &Space = LType->getSpace();

  switch (Op) {
  case assign:
    if (LType->getRanks() != RType->getRanks())
      throw std::runtime_error(
          "Left and Right expressions have different types");

    return LType;
  case plus:
  case minus:
    Ranks.merge(LType->getRanks());
    Ranks.merge(RType->getRanks());
    break;
  case geom:
    for (auto LRank : LType->getRanks())
      for (auto RRank : RType->getRanks())
        for (auto Rank = std::min(LRank, RRank); Rank < std::max(LRank, RRank);
             Rank++)
          Ranks.insert(Rank);
    break;
  case dot:
    for (auto LRank : LType->getRanks())
      for (auto RRank : RType->getRanks())
        Ranks.insert(std::max(LRank, RRank) - std::min(LRank, RRank));
    break;
  case wedge:
    for (auto LRank : LType->getRanks())
      for (auto RRank : RType->getRanks())
        if (LRank + RRank <= Space.dim())
          Ranks.insert(LRank + RRank);
    break;
  case vee:
    for (auto LRank : LType->getRanks())
      for (auto RRank : RType->getRanks())
        if (LRank + RRank >= Space.dim())
          Ranks.insert(LRank + RRank - Space.dim());
    break;
  case scalar:
    return GA::Type::get(Space, {0});
  }

  return GA::Type::get(Space, Ranks);
}
