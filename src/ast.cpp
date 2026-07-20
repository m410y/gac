#include "ast.hpp"
#include "algebra.hpp"
#include "iohelper.hpp"
#include "ts_node_wrapper.hpp"
#include <cstdlib>
#include <utility>

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
  return GA::Type::get(Context.getSpace(), TypeNode);
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
// Expressions
//=============================================================================

GA::Type *Expression::getType(const NodePtr &Node) {
  const Expression *Expr = dynamic_cast<const Expression *>(Node.get());
  if (!Expr)
    throw std::runtime_error("Invalid expression pointer");

  return Expr->getType();
}

NodePtr Literal::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  return std::unique_ptr<Literal>(
      new Literal(GA::Element::create(Context.getSpace(), TSN)));
}

NodePtr Variable::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  std::string_view Name = TSN.str();
  GA::Type *Type = Context.getVarType(Name);
  return std::unique_ptr<Variable>(new Variable(Name, Type));
}

NodePtr CallExpression::create(const TSNodeWrapper &TSN,
                               ParseContext &Context) {
  std::string_view Name = TSN.field("name").str();
  FuncPtr Proto = Context.getFunc(Name);

  std::vector<NodePtr> Args;
  for (const auto &Arg : TSN.field("args").children())
    Args.push_back(Node::create(Arg, Context));

  return std::unique_ptr<CallExpression>(
      new CallExpression(Proto, std::move(Args)));
}

NodePtr UnaryMinus::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  NodePtr Expr = Node::create(TSN.child(), Context);
  return std::unique_ptr<UnaryMinus>(new UnaryMinus(std::move(Expr)));
}

NodePtr Projection::create(const TSNodeWrapper &TSN, ParseContext &Context) {
  NodePtr Expr = Node::create(TSN.child(), Context);
  GA::Type *Type = getNodeType(TSN, Context);
  return std::unique_ptr<Projection>(new Projection(std::move(Expr), Type));
}

//=============================================================================
// Local scope statements
//=============================================================================

NodePtr VariableDeclaration::create(const TSNodeWrapper &TSN,
                                    ParseContext &Context) {
  GA::Type *Type = getNodeType(TSN, Context);
  TSNodeWrapper VarNode = TSN.field("name");
  Context.setVarType(VarNode.str(), Type);

  return std::unique_ptr<VariableDeclaration>(
      new VariableDeclaration(Node::create(VarNode, Context)));
}

NodePtr VariableDefinition::create(const TSNodeWrapper &TSN,
                                   ParseContext &Context) {
  NodePtr Decl = Node::create(TSN.field("decl"), Context);
  NodePtr Expr = Node::create(TSN.field("expr"), Context);
  return std::unique_ptr<VariableDefinition>(
      new VariableDefinition(std::move(Decl), std::move(Expr)));
}

NodePtr ReturnStatement::create(const TSNodeWrapper &TSN,
                                ParseContext &Context) {
  NodePtr Expr = Node::create(TSN.child(), Context);
  return std::unique_ptr<ReturnStatement>(new ReturnStatement(std::move(Expr)));
}

//=============================================================================
// Top-level statements
//=============================================================================

NodePtr UsingStatement::create(const TSNodeWrapper &TSN,
                               ParseContext &Context) {
  GA::GASpace Space(TSN.child());
  std::unique_ptr<UsingStatement> NewNode(new UsingStatement(std::move(Space)));
  Context.setSpace(&NewNode->Space);
  return std::move(NewNode);
}

NodePtr FunctionDeclaration::create(const TSNodeWrapper &TSN,
                                    ParseContext &Context) {
  std::string_view Name = TSN.field("name").str();
  GA::Type *RetType = getNodeType(TSN, Context);
  auto Params = getParamList(TSN, Context);

  FuncPtr Proto = Context.declareFunc(Name, RetType, Params);
  return std::unique_ptr<FunctionDeclaration>(new FunctionDeclaration(Proto));
}

NodePtr FunctionDefinition::create(const TSNodeWrapper &TSN,
                                   ParseContext &Context) {
  std::string_view Name = TSN.field("name").str();
  GA::Type *RetType = getNodeType(TSN, Context);
  auto Params = getParamList(TSN, Context);

  FuncPtr Proto = Context.defineFunc(Name, RetType, Params);

  for (const auto &Param : Proto->Params)
    if (Param.second.has_value())
      Context.setVarType(Param.second.value(), Param.first);

  std::vector<NodePtr> Body;
  for (auto &statement : TSN.field("body").children())
    Body.push_back(Node::create(statement, Context));

  return std::unique_ptr<FunctionDefinition>(
      new FunctionDefinition(Proto, std::move(Body)));
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

static const std::map<std::string,
                      NodePtr (*)(const TSNodeWrapper &, ParseContext &),
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
        {"assignment", BinaryExpression<assign>::create},
        {"binary_plus", BinaryExpression<plus>::create},
        {"binary_minus", BinaryExpression<minus>::create},
        {"geometric_product", BinaryExpression<geom>::create},
        {"wedge_product", BinaryExpression<wedge>::create},
        {"vee_product", BinaryExpression<vee>::create},
        {"dot_product", BinaryExpression<dot>::create},
        {"scalar_product", BinaryExpression<scalar>::create},
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

static std::map<BinOp, const char *> OpStrings = {
    {assign, "="}, {plus, "+"}, {minus, "-"}, {dot, "."},
    {wedge, "w"},  {vee, "v"},  {scalar, "*"}};

template <BinOp Op> void BinaryExpression<Op>::print(std::ostream &OS) const {
  OS << '(' << Left << ' ';

  if (OpStrings.count(Op))
    OS << OpStrings[Op] << ' ';
  else if (Op == geom)
    OS << ' ';
  else
    OS << " ??? ";

  OS << Right << ')';
}

void VariableDeclaration::print(std::ostream &OS) const {
  if (!Var)
    return;

  OS << Expression::getType(Var) << " " << Var;
};

std::ostream &operator<<(std::ostream &OS, const FuncProto::Param &Param) {
  if (Param.second.has_value()) {
    OS << Param.first << " " << Param.second.value();
  } else {
    OS << Param.first;
  }
  return OS;
}

std::ostream &operator<<(std::ostream &OS, FuncPtr Proto) {
  if (!Proto)
    throw std::runtime_error("Attemt to acceess nullptr FuncProto");

  Proto->print(OS);
  return OS;
}

void FuncProto::print(std::ostream &OS) const {
  OS << "function " << Name;
  printSeparated(OS, Params, " (", ", ", ") -> ");
  OS << RetType;
}

void FunctionDefinition::print(std::ostream &OS) const {
  OS << Proto;
  printSeparated(OS, Body, "\n  ", "\n  ", "\nend");
}

std::ostream &operator<<(std::ostream &OS, const SyntaxTree &Tree) {
  printSeparated(OS, Tree.Statements, "", "\n", "");
  return OS;
}

//=============================================================================
// Binary expression's type resolve
//=============================================================================

template <BinOp Op>
GA::RankSet gaMulImpl(GA::RankTy left, GA::RankTy right, GA::RankTy dim);

template <>
GA::RankSet gaMulImpl<geom>(GA::RankTy min, GA::RankTy max, GA::RankTy dim) {
  GA::RankSet Ranks;
  for (GA::RankTy rank = max - min; rank <= std::min(dim, max + min); rank += 2)
    Ranks.insert(rank);
  return Ranks;
}

template <>
GA::RankSet gaMulImpl<dot>(GA::RankTy min, GA::RankTy max, GA::RankTy) {
  return GA::RankSet{max - min};
}

template <>
GA::RankSet gaMulImpl<wedge>(GA::RankTy min, GA::RankTy max, GA::RankTy dim) {
  GA::RankTy sum = min + max;
  return sum > dim ? GA::RankSet{} : GA::RankSet{sum};
}

template <>
GA::RankSet gaMulImpl<vee>(GA::RankTy min, GA::RankTy max, GA::RankTy dim) {
  GA::RankTy sum = min + max;
  return sum < dim ? GA::RankSet{} : GA::RankSet{dim - sum};
}

template <>
GA::RankSet gaMulImpl<scalar>(GA::RankTy min, GA::RankTy max, GA::RankTy) {
  return min != max ? GA::RankSet{} : GA::RankSet{0};
}

template <BinOp Op>
static GA::RankSet binaryExprImpl(GA::RankSet &&Left, GA::RankSet &&Right,
                                  GA::RankTy dim) {
  GA::RankSet Ranks;
  for (GA::RankTy left : Left)
    for (GA::RankTy right : Right) {
      auto minmax = std::minmax(left, right);
      Ranks.merge(gaMulImpl<Op>(minmax.first, minmax.second, dim));
    }

  return Ranks;
}

template <>
GA::RankSet binaryExprImpl<assign>(GA::RankSet &&Left, GA::RankSet &&Right,
                                   GA::RankTy) {
  if (Left != Right)
    throw std::runtime_error("Left and Right expressions have different types");

  return Left;
}

template <>
GA::RankSet binaryExprImpl<plus>(GA::RankSet &&Left, GA::RankSet &&Right,
                                 GA::RankTy) {
  GA::RankSet Ranks;
  Ranks.merge(Left);
  Ranks.merge(Right);
  return Ranks;
}

template <>
GA::RankSet binaryExprImpl<minus>(GA::RankSet &&Left, GA::RankSet &&Right,
                                  GA::RankTy dim) {
  return binaryExprImpl<plus>(std::move(Left), std::move(Right), dim);
}

template <BinOp Op> GA::Type *BinaryExpression<Op>::getType() const {
  GA::Type *LType = Expression::getType(Left);
  GA::Type *RType = Expression::getType(Right);
  if (&LType->getSpace() != &RType->getSpace())
    throw std::runtime_error(
        "Left and right expressions from different spaces");

  GA::GASpace &Space = LType->getSpace();
  GA::RankSet LRanks = LType->getRanks();
  GA::RankSet RRanks = LType->getRanks();
  return GA::Type::get(
      Space,
      binaryExprImpl<Op>(std::move(LRanks), std::move(RRanks), Space.dim()));
}
