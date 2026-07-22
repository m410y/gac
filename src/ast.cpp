#include "ast.hpp"
#include "algebra.hpp"
#include "ts_node_wrapper.hpp"
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

static std::runtime_error catError(std::string_view Where,
                                   std::string_view Err) {
  return std::runtime_error(std::string(Where) + " -> " + std::string(Err));
}

bool FuncProto::operator==(const FuncProto &Right) const {
  if (Params.size() != Right.Params.size())
    return false;

  for (size_t i = 0; i < Params.size(); i++)
    if (Params[i].first != Right.Params[i].first)
      return false;

  return Name == Right.Name && RetType == Right.RetType;
}

GA::GASpace &ParseContext::getSpace() const {
  if (!Space)
    throw std::runtime_error("No space in context");

  return *Space;
}

void ParseContext::setSpace(GA::GASpace *Space) { this->Space = Space; }

GA::Type *ParseContext::getVarType(std::string_view Name) const {
  auto TypesIt = Types.find(Name);
  if (TypesIt == Types.end())
    throw std::runtime_error("Undefined variable " + std::string(Name));

  return TypesIt->second;
}

void ParseContext::setVarType(std::string_view Name, GA::Type *Type) {
  auto TypesIt = Types.find(Name);
  if (TypesIt != Types.end())
    throw std::runtime_error("Redifinition of variable " + std::string(Name));

  Types.insert({std::string(Name), Type});
}

FuncPtr ParseContext::getFunc(std::string_view Name) const {
  auto FuncIt = Funcs.find(Name);
  if (FuncIt == Funcs.end())
    throw std::runtime_error("Undefined variable " + std::string(Name));

  return FuncIt->second;
}

FuncPtr ParseContext::createFunc(const FuncProto &Proto) {
  auto FuncIt = Funcs.find(Proto.Name);
  if (FuncIt == Funcs.end()) {
    FuncPtr NewFunc = std::make_shared<FuncProto>(Proto);
    Funcs.insert({Proto.Name, NewFunc});
    return NewFunc;
  }
  FuncPtr OldProto = FuncIt->second;

  if (OldProto->IsDefined)
    throw std::runtime_error("Already defined function " + Proto.Name);

  if (*OldProto != Proto)
    throw std::runtime_error(
        "Definition doesn't match declaration for function " + Proto.Name);

  OldProto->IsDefined = Proto.IsDefined;
  OldProto->Params = Proto.Params;
  return OldProto;
}

static GA::Type *getNodeType(const TSNodeWrapper &TSN, ParseContext &Context) {
  TSNodeWrapper TypeNode = TSN.field("type");
  return Context.getSpace().getRanked(TypeNode);
}

static std::vector<FuncProto::Param> getParamList(const TSNodeWrapper &TSN,
                                                  ParseContext &Context) {
  std::vector<FuncProto::Param> Params;
  for (const auto &ParamNode : TSN.field("params").children()) {
    GA::Type *Type = getNodeType(ParamNode, Context);
    try {
      Params.push_back({Type, std::string(ParamNode.field("name").str())});
    } catch (std::runtime_error &e) {
      Params.push_back({Type, {}});
    }
  }
  return Params;
}

//=============================================================================
// Static constructors
//=============================================================================

#define CREATENODE(CLASS, BLOCK)                                               \
  NodePtr CLASS::createNode(const TSNodeWrapper &TSN, ParseContext &Context) { \
    try {                                                                      \
      BLOCK;                                                                   \
    } catch (std::runtime_error & e) {                                         \
      throw catError(#CLASS, e.what());                                        \
    }                                                                          \
  }

CREATENODE(Literal, {
  GA::ElementValue Val = Context.getSpace().getElement(TSN);
  return std::make_unique<Literal>(Val);
})

CREATENODE(Variable, {
  std::string_view Name = TSN.str();
  GA::Type *Type = Context.getVarType(Name);
  return std::make_unique<Variable>(Name, Type);
})

CREATENODE(CallExpression, {
  std::string_view Name = TSN.field("name").str();
  FuncPtr Proto = Context.getFunc(Name);

  std::vector<NodePtr> Args;
  for (const auto &Arg : TSN.field("args").children())
    Args.push_back(create<Node>(Arg, Context));

  return std::make_unique<CallExpression>(Proto, std::move(Args));
})

CREATENODE(UnaryMinus, {
  ExprPtr Expr = create<Expression>(TSN.child(), Context);
  return std::make_unique<UnaryMinus>(std::move(Expr));
})

CREATENODE(Projection, {
  ExprPtr Expr = create<Expression>(TSN.child(), Context);
  GA::Type *Type = getNodeType(TSN, Context);
  return std::make_unique<Projection>(std::move(Expr), Type);
})

CREATENODE(VariableDeclaration, {
  GA::Type *Type = getNodeType(TSN, Context);
  TSNodeWrapper VarNode = TSN.field("name");
  Context.setVarType(VarNode.str(), Type);
  std::unique_ptr<Variable> Var = create<Variable>(VarNode, Context);

  return std::make_unique<VariableDeclaration>(std::move(Var));
})

CREATENODE(VariableDefinition, {
  std::unique_ptr<VariableDeclaration> Decl =
      create<VariableDeclaration>(TSN.field("decl"), Context);
  ExprPtr Expr = create<Expression>(TSN.field("expr"), Context);
  return std::make_unique<VariableDefinition>(std::move(Decl), std::move(Expr));
})

CREATENODE(ReturnStatement, {
  ExprPtr Expr = create<Expression>(TSN.child(), Context);
  return std::make_unique<ReturnStatement>(std::move(Expr));
})

CREATENODE(UsingStatement, {
  std::unique_ptr<GA::GASpace> Space =
      std::make_unique<GA::GASpace>(TSN.child());
  Context.setSpace(Space.get());
  return std::make_unique<UsingStatement>(std::move(Space));
})

CREATENODE(FunctionDeclaration, {
  std::string_view Name = TSN.field("name").str();
  GA::Type *RetType = getNodeType(TSN, Context);
  auto Params = getParamList(TSN, Context);

  FuncPtr Proto = Context.declareFunc(Name, RetType, Params);
  return std::make_unique<FunctionDeclaration>(Proto);
})

CREATENODE(FunctionDefinition, {
  std::string_view Name = TSN.field("name").str();
  GA::Type *RetType = getNodeType(TSN, Context);
  auto Params = getParamList(TSN, Context);

  FuncPtr Proto = Context.defineFunc(Name, RetType, Params);

  for (const auto &Param : Proto->Params)
    if (Param.second.has_value())
      Context.setVarType(Param.second.value(), Param.first);

  std::vector<NodePtr> Body;
  for (auto &statement : TSN.field("body").children())
    Body.push_back(create<Node>(statement, Context));

  Context.clearVars();
  return std::make_unique<FunctionDefinition>(Proto, std::move(Body));
})

static NodePtr trivialNode(const TSNodeWrapper &TSN, ParseContext &Context) {
  return create<Node>(TSN.child(), Context);
}

static NodePtr parseError(const TSNodeWrapper &, ParseContext &) {
  throw std::runtime_error("Parser error");
}

#undef CREATENODE

//=============================================================================
// Syntax tree construction
//=============================================================================

static const std::map<std::string_view,
                      NodePtr (*)(const TSNodeWrapper &, ParseContext &)>
    Constructors = {
        {"ERROR", parseError},
        {"MISSING", parseError},
        {"UNEXPECTED", parseError},
        {"int_literal", Literal::createNode},
        {"float_literal", Literal::createNode},
        {"basis_literal", Literal::createNode},
        {"identifier", Variable::createNode},
        {"parenthesized_expression", trivialNode},
        {"call_expression", CallExpression::createNode},
        {"unary_plus", trivialNode},
        {"unary_minus", UnaryMinus::createNode},
        {"projection", Projection::createNode},
        {"assignment", BinaryExpression<assign>::createNode},
        {"binary_plus", BinaryExpression<plus>::createNode},
        {"binary_minus", BinaryExpression<minus>::createNode},
        {"geometric_product", BinaryExpression<geom>::createNode},
        {"wedge_product", BinaryExpression<wedge>::createNode},
        {"vee_product", BinaryExpression<vee>::createNode},
        {"dot_product", BinaryExpression<dot>::createNode},
        {"scalar_product", BinaryExpression<scalar>::createNode},
        {"variable_declaration", VariableDeclaration::createNode},
        {"variable_definition", VariableDefinition::createNode},
        {"return_statement", ReturnStatement::createNode},
        {"using_statement", UsingStatement::createNode},
        {"function_definition", FunctionDefinition::createNode},
};

template <>
NodePtr create<Node>(const TSNodeWrapper &TSN, ParseContext &Context) {
  std::string_view Type = TSN.type();
  auto ConstructIt = Constructors.find(Type);
  if (ConstructIt == Constructors.end())
    throw std::runtime_error("Unknown node name " + std::string(Type));

  return ConstructIt->second(TSN, Context);
}

template <class T>
std::unique_ptr<T> create(const TSNodeWrapper &TSN, ParseContext &Context) {
  NodePtr NewNode = create<Node>(TSN, Context);
  Node *RawNode = NewNode.release();
  T *RawExpr = dynamic_cast<T *>(RawNode);
  return std::unique_ptr<T>(RawExpr);
}

SyntaxTree::SyntaxTree(const TSNodeWrapper &root) {
  ParseContext Context;
  for (auto &top_level_statement : root.children())
    Statements.push_back(create<Node>(top_level_statement, Context));
}

//=============================================================================
// Printing
//=============================================================================

// For debug purposes
std::string Node::dump() const {
  std::ostringstream oss;
  print(oss);
  return oss.str();
}

static std::ostream &operator<<(std::ostream &OS, const NodePtr &NPtr) {
  if (NPtr)
    NPtr->print(OS);

  return OS;
}

static std::ostream &operator<<(std::ostream &OS,
                                const FuncProto::Param &Param) {
  if (Param.second.has_value()) {
    OS << Param.first << " " << Param.second.value();
  } else {
    OS << Param.first;
  }
  return OS;
}

static std::ostream &operator<<(std::ostream &OS, FuncPtr Proto) {
  if (!Proto)
    throw std::runtime_error("Attemt to acceess nullptr FuncProto");

  Proto->print(OS);
  return OS;
}

template <typename T>
inline static void printSeparated(std::ostream &OS, const T &Container,
                                  std::string_view Start, std::string_view Sep,
                                  std::string_view Stop) {
  OS << Start;
  bool begin = true;
  for (const auto &Element : Container)
    OS << (begin ? (begin = false, "") : Sep) << Element;

  OS << Stop;
}

void Literal::print(std::ostream &OS) const { OS << Val; }

void UnaryMinus::print(std::ostream &OS) const { OS << "-" << Expr.get(); };

void Projection::print(std::ostream &OS) const {
  OS << "<" << Expr.get() << ">" << Type;
};

void CallExpression::print(std::ostream &OS) const {
  printSeparated(OS, Args, "(", ", ", ")");
}

static const std::map<BinOp, const char *> OpStrings = {
    {assign, "="}, {plus, "+"}, {minus, "-"}, {dot, "."},
    {wedge, "w"},  {vee, "v"},  {scalar, "*"}};

template <BinOp Op> void BinaryExpression<Op>::print(std::ostream &OS) const {
  OS << '(' << Left.get() << ' ';

  if (OpStrings.count(Op))
    OS << OpStrings.at(Op) << ' ';
  else if (Op == geom)
    OS << ' ';
  else
    OS << " ??? ";

  OS << Right.get() << ')';
}

void VariableDeclaration::print(std::ostream &OS) const {
  if (!Var)
    return;

  OS << Var->getType() << " " << Var.get();
};

void VariableDefinition::print(std::ostream &OS) const {
  OS << Decl.get() << " = " << Expr.get();
}

void ReturnStatement::print(std::ostream &OS) const {
  OS << "return " << Expr.get();
}

void UsingStatement::print(std::ostream &OS) const {
  if (!Space)
    throw std::runtime_error("Attemt to dereference nullptr GASpace");

  OS << "using " << Space.get() << "\n";
}

void FuncProto::print(std::ostream &OS) const {
  OS << "function " << Name;
  printSeparated(OS, Params, " (", ", ", ") -> ");
  OS << RetType;
}

void FunctionDeclaration::print(std::ostream &OS) const { OS << Proto << "\n"; }

void FunctionDefinition::print(std::ostream &OS) const {
  OS << Proto;
  printSeparated(OS, Body, "\n  ", "\n  ", "\nend");
}

void SyntaxTree::print(std::ostream &OS) const {
  printSeparated(OS, Statements, "", "\n", "");
}

//=============================================================================
// Verification
//=============================================================================

static void nullCheck(void *Ptr, std::string_view This) {
  if (!Ptr)
    throw catError(This, "is nullptr");
}

static void nullCheck(const FuncPtr &Ptr, std::string_view This) {
  nullCheck(Ptr.get(), This);
  Ptr->verify();
}

template <class T>
static void nullCheck(const std::unique_ptr<T> &Ptr, std::string_view This) {
  nullCheck(Ptr.get(), This);
  static_cast<Node *>(Ptr.get())->verify();
}

template <>
void nullCheck<GA::GASpace>(const std::unique_ptr<GA::GASpace> &Ptr,
                            std::string_view This) {
  nullCheck(Ptr.get(), This);
}

template <typename T>
static void containerCheck(const T &Cont, std::string_view Element) {
  size_t i = 1;
  for (const auto &N : Cont)
    nullCheck(N, std::string(Element) + std::to_string(i++));
}

#define VERIFY(CLASS, BLOCK)                                                   \
  void CLASS::verify() const {                                                 \
    try {                                                                      \
      BLOCK;                                                                   \
    } catch (std::runtime_error & e) {                                         \
      throw catError(#CLASS, e.what());                                        \
    }                                                                          \
  }

VERIFY(FuncProto, {
  for (const auto &[Type, _] : Params)
    nullCheck(Type, "Type");

  nullCheck(RetType, "RetType");

  if (Name.empty())
    throw std::runtime_error("Name is empty");
})

VERIFY(Node, containerCheck(children(), "Child"))
VERIFY(Expression, nullCheck(getType(), "Type"))
// FIXME how to DRY ???
VERIFY(Literal, nullCheck(getType(), "Type"))
VERIFY(Variable, nullCheck(getType(), "Type"))
VERIFY(CallExpression, {
  containerCheck(Args, "Argument");
  Proto->verify();
})
VERIFY(UnaryMinus, nullCheck(Expr, "Expresssion"))
VERIFY(Projection, {
  nullCheck(Type, "Type");
  nullCheck(Expr, "Expresssion");
})
template <BinOp Op> void BinaryExpression<Op>::verify() const {
  try {
    nullCheck(Left, "Left");
    nullCheck(Right, "Right");
  } catch (std::runtime_error &e) {
    throw catError(std::string("BinaryExpression<") + OpStrings.at(Op) + ">",
                   e.what());
  }
}
VERIFY(VariableDeclaration, nullCheck(Var, "Expression"))
VERIFY(VariableDefinition, {
  nullCheck(Decl, "Variable");
  nullCheck(Expr, "Expression");
})
VERIFY(ReturnStatement, nullCheck(Expr, "Expression"))
VERIFY(UsingStatement, nullCheck(Space, "Expression"))
VERIFY(FunctionDeclaration, nullCheck(Proto, "Expression"))
VERIFY(FunctionDefinition, {
  nullCheck(Proto, "Expression");
  containerCheck(Body, "Statement");
})
VERIFY(SyntaxTree, containerCheck(Statements, "TopLevelStatement"))

#undef VERIFY

//=============================================================================
// Binary expression's type resolve
//=============================================================================

template <BinOp Op> GA::IDSet gaMulImpl(GA::ID left, GA::ID right, GA::ID dim);

template <> GA::IDSet gaMulImpl<geom>(GA::ID min, GA::ID max, GA::ID dim) {
  GA::IDSet Ranks;
  for (GA::ID rank = max - min; rank <= std::min(dim, max + min); rank += 2)
    Ranks.insert(rank);
  return Ranks;
}

template <> GA::IDSet gaMulImpl<dot>(GA::ID min, GA::ID max, GA::ID) {
  return GA::IDSet{max - min};
}

template <> GA::IDSet gaMulImpl<wedge>(GA::ID min, GA::ID max, GA::ID dim) {
  GA::ID sum = min + max;
  return sum > dim ? GA::IDSet{} : GA::IDSet{sum};
}

template <> GA::IDSet gaMulImpl<vee>(GA::ID min, GA::ID max, GA::ID dim) {
  GA::ID sum = min + max;
  return sum < dim ? GA::IDSet{} : GA::IDSet{dim - sum};
}

template <> GA::IDSet gaMulImpl<scalar>(GA::ID min, GA::ID max, GA::ID) {
  return min != max ? GA::IDSet{} : GA::IDSet{0};
}

template <BinOp Op>
static GA::IDSet binaryExprImpl(GA::IDSet &&Left, GA::IDSet &&Right,
                                GA::ID dim) {
  GA::IDSet Ranks;
  for (GA::ID left : Left)
    for (GA::ID right : Right) {
      auto minmax = std::minmax(left, right);
      Ranks.merge(gaMulImpl<Op>(minmax.first, minmax.second, dim));
    }

  return Ranks;
}

template <>
GA::IDSet binaryExprImpl<assign>(GA::IDSet &&Left, GA::IDSet &&Right, GA::ID) {
  if (Left != Right)
    throw std::runtime_error("Left and Right expressions have different types");

  return Left;
}

template <>
GA::IDSet binaryExprImpl<plus>(GA::IDSet &&Left, GA::IDSet &&Right, GA::ID) {
  GA::IDSet Ranks;
  Ranks.merge(Left);
  Ranks.merge(Right);
  return Ranks;
}

template <>
GA::IDSet binaryExprImpl<minus>(GA::IDSet &&Left, GA::IDSet &&Right,
                                GA::ID dim) {
  return binaryExprImpl<plus>(std::move(Left), std::move(Right), dim);
}

template <BinOp Op> GA::Type *BinaryExpression<Op>::getType() const {
  GA::Type *LType = Left->getType();
  GA::Type *RType = Right->getType();
  if (&LType->getSpace() != &RType->getSpace())
    throw std::runtime_error(
        "Left and right expressions from different spaces");

  GA::GASpace &Space = LType->getSpace();
  GA::IDSet LRanks = LType->ranks();
  GA::IDSet RRanks = LType->ranks();
  return Space.getRanked(
      binaryExprImpl<Op>(std::move(LRanks), std::move(RRanks), Space.dim()));
}
