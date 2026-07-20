#include "codegen.hpp"
#include "algebra.hpp"
#include "ast.hpp"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <stdexcept>
#include <string_view>

using namespace llvm;

//=============================================================================
// Expressions
//=============================================================================

Value *Literal::codegen(BuildContext &Context) const {
  return Context.getConst(El);
}

Value *Variable::codegen(BuildContext &Context) const {
  return Context.loadVar(Name);
}

Value *CallExpression::codegen(BuildContext &Context) const {
  Function *Callee = Context.Module->getFunction(Proto->Name);
  if (Callee->arg_size() != Args.size())
    throw std::runtime_error("wrong number of arguments");

  std::vector<Value *> Values;
  for (auto &arg : Args)
    Values.push_back(arg->codegen(Context));

  return Context.Builder->CreateCall(Callee, Values, "call");
}

Value *UnaryMinus::codegen(BuildContext &Context) const {
  Value *Val = Expr->codegen(Context);
  return Context.callBuiltin(Val, "unaryNeg");
}

Value *Projection::codegen(BuildContext &Context) const {
  Value *Val = Expr->codegen(Context);
  return Context.callBuiltin(Val, "proj");
}

//=============================================================================
// Local scope statements
//=============================================================================

Value *VariableDeclaration::codegen(BuildContext &Context) const {
  Value *Val = Context.getZero(Var->getType());
  Context.allocVar(Val, Var->getName());
  return nullptr;
}

Value *VariableDefinition::codegen(BuildContext &Context) const {
  Value *Val = Expr->codegen(Context);
  Context.allocVar(Val, Var->getName());
  return nullptr;
}

Value *ReturnStatement::codegen(BuildContext &Context) const {
  Value *RetVal = Expr->codegen(Context);
  if (!RetVal)
    throw std::runtime_error("No value provided to return");

  return Context.Builder->CreateRet(RetVal);
}

//=============================================================================
// Top-level statements
//=============================================================================

Value *UsingStatement::codegen(BuildContext &) const { return nullptr; }

llvm::Function *FuncProto::codegen(BuildContext &Context) {
  Type *Ty = Context.getType(RetType);
  std::vector<Type *> ArgTypes;
  for (const auto &Decl : Params)
    ArgTypes.push_back(Context.getType(Decl.first));

  FunctionType *FuncType = FunctionType::get(Ty, ArgTypes, false);
  return Function::Create(FuncType, Function::ExternalLinkage, Name,
                          Context.Module.get());
}

Value *FunctionDeclaration::codegen(BuildContext &Context) const {
  return Proto->codegen(Context);
}

Value *FunctionDefinition::codegen(BuildContext &Context) const {
  Function *Func = Proto->codegen(Context);
  BasicBlock *Entry = BasicBlock::Create(Context.LLVM, "entry", Func);
  Context.Builder->SetInsertPoint(Entry);

  size_t n = 0;
  for (const auto &[_, Name] : Proto->Params) {
    Argument *Arg = Func->getArg(n++);
    Arg->setName(Name.value());
    Context.allocVar(Arg, Name.value());
  }

  for (const auto &Node : Body)
    Node->codegen(Context);

  verifyFunction(*Func, &errs());
  return Func;
}

//=============================================================================
// Syntax tree
//=============================================================================

void SyntaxTree::codegen(BuildContext &Context) const {
  for (const auto &Node : Statements)
    Node->codegen(Context);
}

//=============================================================================
// Context and IRCompiler
//=============================================================================

llvm::StructType *BuildContext::getType(GA::Type *GAType) {
  auto MapIt = GATypes.find(GAType);
  if (MapIt != GATypes.end())
    return MapIt->second;

  llvm::ArrayType *Data = ArrayType::get(Real, GAType->dof());
  StructType *LLType = StructType::create({Data}, GAType->getName());
  GATypes.insert({GAType, LLType});
  return LLType;
}

AllocaInst *BuildContext::allocVar(Value *Val, std::string_view Name) {
  AllocaInst *Alloca = Builder->CreateAlloca(Val->getType(), nullptr, Name);
  Builder->CreateStore(Val, Alloca);
  Vars.insert({std::string(Name), Alloca});
  return Alloca;
}

Value *BuildContext::loadVar(std::string Name) const {
  AllocaInst *Var = Vars.at(Name);
  return Builder->CreateLoad(Var->getAllocatedType(), Var, Name.c_str());
}

Value *BuildContext::getConst(GA::Element *Elem) {
  StructType *LLType = getType(Elem->getType());
  std::vector<double> Values(Elem->getValues());
  Constant *Data = ConstantDataArray::get(LLVM, Values);
  return ConstantStruct::get(LLType, {Data});
}

Value *BuildContext::getZero(GA::Type *GAType) {
  StructType *LLType = getType(GAType);
  std::vector<double> Values(GAType->dof(), 0.0);
  Constant *Data = ConstantDataArray::get(LLVM, Values);
  return ConstantStruct::get(LLType, {Data});
}

Value *BuildContext::callBuiltin(llvm::Value *Val, std::string_view Name) {
  Function *Callee = Module->getFunction(Name);
  return Builder->CreateCall(Callee, {Val}, Name);
}
