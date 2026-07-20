#include "ast.hpp"
#include "ts_node_wrapper.hpp"
#include <ostream>

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

NodePtr Literal::createNode(const TSNodeWrapper &TSN, ParseContext &Context) {
  return std::make_unique<Literal>(
      GA::Element::create(Context.getSpace(), TSN));
}

NodePtr Variable::createNode(const TSNodeWrapper &TSN, ParseContext &Context) {
  std::string_view Name = TSN.str();
  GA::Type *Type = Context.getVarType(Name);
  return std::make_unique<Variable>(Name, Type);
}

NodePtr CallExpression::createNode(const TSNodeWrapper &TSN,
                                   ParseContext &Context) {
  std::string_view Name = TSN.field("name").str();
  FuncPtr Proto = Context.getFunc(Name);

  std::vector<NodePtr> Args;
  for (const auto &Arg : TSN.field("args").children())
    Args.push_back(create<Node>(Arg, Context));

  return std::make_unique<CallExpression>(Proto, std::move(Args));
}

NodePtr UnaryMinus::createNode(const TSNodeWrapper &TSN,
                               ParseContext &Context) {
  ExprPtr Expr = create<Expression>(TSN.child(), Context);
  return std::make_unique<UnaryMinus>(std::move(Expr));
}

NodePtr Projection::createNode(const TSNodeWrapper &TSN,
                               ParseContext &Context) {
  ExprPtr Expr = create<Expression>(TSN.child(), Context);
  GA::Type *Type = getNodeType(TSN, Context);
  return std::make_unique<Projection>(std::move(Expr), Type);
}

//=============================================================================
// Local scope statements
//=============================================================================

NodePtr VariableDeclaration::createNode(const TSNodeWrapper &TSN,
                                        ParseContext &Context) {
  GA::Type *Type = getNodeType(TSN, Context);
  TSNodeWrapper VarNode = TSN.field("name");
  Context.setVarType(VarNode.str(), Type);
  std::unique_ptr<Variable> Var = create<Variable>(VarNode, Context);

  return std::make_unique<VariableDeclaration>(std::move(Var));
}

NodePtr VariableDefinition::createNode(const TSNodeWrapper &TSN,
                                       ParseContext &Context) {
  std::unique_ptr<Variable> Var = create<Variable>(TSN.field("decl"), Context);
  ExprPtr Expr = create<Expression>(TSN.field("expr"), Context);
  return std::make_unique<VariableDefinition>(std::move(Var), std::move(Expr));
}

NodePtr ReturnStatement::createNode(const TSNodeWrapper &TSN,
                                    ParseContext &Context) {
  ExprPtr Expr = create<Expression>(TSN.child(), Context);
  return std::make_unique<ReturnStatement>(std::move(Expr));
}

//=============================================================================
// Top-level statements
//=============================================================================

NodePtr UsingStatement::createNode(const TSNodeWrapper &TSN,
                                   ParseContext &Context) {
  std::unique_ptr<GA::GASpace> Space =
      std::make_unique<GA::GASpace>(TSN.child());
  Context.setSpace(Space.get());
  return std::make_unique<UsingStatement>(std::move(Space));
}

NodePtr FunctionDeclaration::createNode(const TSNodeWrapper &TSN,
                                        ParseContext &Context) {
  std::string_view Name = TSN.field("name").str();
  GA::Type *RetType = getNodeType(TSN, Context);
  auto Params = getParamList(TSN, Context);

  FuncPtr Proto = Context.declareFunc(Name, RetType, Params);
  return std::make_unique<FunctionDeclaration>(Proto);
}

NodePtr FunctionDefinition::createNode(const TSNodeWrapper &TSN,
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
    Body.push_back(create<Node>(statement, Context));

  Context.clearVars();
  return std::make_unique<FunctionDefinition>(Proto, std::move(Body));
}

//=============================================================================
// Syntax tree
//=============================================================================

SyntaxTree::SyntaxTree(const TSNodeWrapper &root) {
  ParseContext Context;
  for (auto &top_level_statement : root.children())
    Statements.push_back(create<Node>(top_level_statement, Context));
}

static NodePtr trivialNode(const TSNodeWrapper &TSN, ParseContext &Context) {
  return create<Node>(TSN.child(), Context);
}

static std::map<std::string, NodePtr (*)(const TSNodeWrapper &, ParseContext &),
                std::less<>>
    Constructors = {
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

template <class T>
std::unique_ptr<T> create(const TSNodeWrapper &TSN, ParseContext &Context) {
  NodePtr NewNode = create<Node>(TSN, Context);
  Node *RawNode = NewNode.release();
  T *RawExpr = dynamic_cast<T *>(RawNode);
  return std::unique_ptr<T>(RawExpr);
}

template <>
NodePtr create<Node>(const TSNodeWrapper &TSN, ParseContext &Context) {
  std::string_view Type = TSN.type();
  auto ConstructIt = Constructors.find(Type);
  if (ConstructIt == Constructors.end())
    throw std::runtime_error("Unknown node name " + std::string(Type));

  return ConstructIt->second(TSN, Context);
}

//=============================================================================
// Printing
//=============================================================================

static std::ostream &operator<<(std::ostream &OS, const NodePtr &NPtr) {
  if (NPtr)
    NPtr->print(OS);

  return OS;
}

static std::ostream &operator<<(std::ostream &OS, Node *NPtr) {
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

#include "iohelper.hpp"

void Literal::print(std::ostream &OS) const { OS << El; }

void UnaryMinus::print(std::ostream &OS) const { OS << "-" << Expr.get(); };

void Projection::print(std::ostream &OS) const {
  OS << "<" << Expr.get() << ">" << Type;
};

void CallExpression::print(std::ostream &OS) const {
  printSeparated(OS, Args, "(", ", ", ")");
}

static std::map<BinOp, const char *> OpStrings = {
    {assign, "="}, {plus, "+"}, {minus, "-"}, {dot, "."},
    {wedge, "w"},  {vee, "v"},  {scalar, "*"}};

template <BinOp Op> void BinaryExpression<Op>::print(std::ostream &OS) const {
  OS << '(' << Left.get() << ' ';

  if (OpStrings.count(Op))
    OS << OpStrings[Op] << ' ';
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
  OS << Var.get() << " = " << Expr.get();
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
  GA::Type *LType = Left->getType();
  GA::Type *RType = Right->getType();
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
