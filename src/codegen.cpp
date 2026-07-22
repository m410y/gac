#include "codegen.hpp"
#include "algebra.hpp"
#include "ast.hpp"
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <stdexcept>

//=============================================================================
// Expressions
//=============================================================================

llvm::Value *Literal::codegen(BuildContext &Context) const {
  return Context.getConst(Val);
}

llvm::Value *Variable::codegen(BuildContext &Context) const {
  return Context.loadVar(Name);
}

llvm::Value *CallExpression::codegen(BuildContext &Context) const {
  llvm::Function *Callee = Context.Module->getFunction(Proto->Name);

  std::vector<llvm::Value *> Values;
  for (auto &arg : Args)
    Values.push_back(arg->codegen(Context));

  return Context.Builder->CreateCall(Callee, Values, "call");
}

llvm::Value *UnaryMinus::codegen(BuildContext &Context) const {
  llvm::Value *Val = Expr->codegen(Context);
  GA::Type *GAType = getType();
  llvm::Value *Res = llvm::UndefValue::get(Context.getType(GAType));
  GA::GASpace &Space = GAType->getSpace();
  GA::IDSet Ranks = GAType->ranks();
  GA::ID n = 0;
  for (GA::ID rank : Ranks) {
    for (GA::ID i = 0; i < Space.size(rank); i++) {
      llvm::Value *Elem = Context.Builder->CreateExtractValue(Val, {n, 0, i});
      llvm::Value *Neg = Context.Builder->CreateFNeg(Elem);
      Res = Context.Builder->CreateInsertValue(Res, Neg, {n, 0, i});
    }
    n++;
  }
  return Res;
}

llvm::Value *Projection::codegen(BuildContext &Context) const {
  llvm::Value *Val = Expr->codegen(Context);
  GA::Type *InTy = Expr->getType();
  GA::Type *OutTy = getType();
  GA::GASpace &Space = OutTy->getSpace();
  llvm::Value *Res = llvm::UndefValue::get(Context.getType(OutTy));
  GA::IDSet Ranks = InTy->ranks();
  auto RanksIt = Ranks.begin();
  GA::ID n = 0;
  GA::ID m = 0;
  for (GA::ID rank : OutTy->ranks()) {
    while (RanksIt != Ranks.end() && *RanksIt < rank) {
      RanksIt++;
      m++;
    }
    llvm::Value *Elem;
    if (RanksIt == Ranks.end() || *RanksIt != rank)
      Elem = Context.getZero(Space.getRanked({rank}));
    else
      Elem = Context.Builder->CreateExtractValue(Val, {m});

    Res = Context.Builder->CreateInsertValue(Res, Elem, {n++});
  }
  return Res;
}

//=============================================================================
// Local scope statements
//=============================================================================

llvm::Value *VariableDeclaration::codegen(BuildContext &Context) const {
  llvm::Value *Val = Context.getZero(Var->getType());
  Context.allocVar(Val, Var->getName());
  return nullptr;
}

llvm::Value *VariableDefinition::codegen(BuildContext &Context) const {
  llvm::Value *Val = Expr->codegen(Context);
  Context.allocVar(Val, Decl->Var->getName());
  return nullptr;
}

llvm::Value *ReturnStatement::codegen(BuildContext &Context) const {
  llvm::Value *RetVal = Expr->codegen(Context);
  return Context.Builder->CreateRet(RetVal);
}

//=============================================================================
// Top-level statements
//=============================================================================

llvm::Value *UsingStatement::codegen(BuildContext &) const { return nullptr; }

llvm::Function *FuncProto::codegen(BuildContext &Context) {
  llvm::Type *Ty = Context.getType(RetType);
  std::vector<llvm::Type *> ArgTypes;
  for (const auto &Decl : Params)
    ArgTypes.push_back(Context.getType(Decl.first));

  llvm::FunctionType *FuncType = llvm::FunctionType::get(Ty, ArgTypes, false);
  return llvm::Function::Create(FuncType, llvm::Function::ExternalLinkage, Name,
                                Context.Module.get());
}

llvm::Value *FunctionDeclaration::codegen(BuildContext &Context) const {
  return Proto->codegen(Context);
}

llvm::Value *FunctionDefinition::codegen(BuildContext &Context) const {
  llvm::Function *Func = Proto->codegen(Context);
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context.LLVM, "entry", Func);
  Context.Builder->SetInsertPoint(Entry);

  size_t n = 0;
  for (const auto &[_, Name] : Proto->Params) {
    llvm::Argument *Arg = Func->getArg(n++);
    Arg->setName(Name.value());
    Context.allocVar(Arg, Name.value());
  }

  for (const auto &Node : Body)
    Node->codegen(Context);

  return Func;
}

//=============================================================================
// Syntax tree
//=============================================================================

llvm::Value *SyntaxTree::codegen(BuildContext &Context) const {
  for (const auto &Node : Statements)
    Node->codegen(Context);

  return nullptr;
}

//=============================================================================
// BuildContext
//=============================================================================

llvm::StructType *BuildContext::getType(GA::Type *GAType) {
  if (GATypes.count(GAType))
    return GATypes.at(GAType);

  GA::GASpace &Space = GAType->getSpace();

  if (GAType->ranks().size() == 1) {
    GA::ID rank = *GAType->ranks().begin();
    llvm::ArrayType *Data = llvm::ArrayType::get(Real, Space.size(rank));
    llvm::StructType *LLType =
        llvm::StructType::create(Data, GAType->getName());
    GATypes.insert({GAType, LLType});
    return LLType;
  }

  std::vector<llvm::Type *> Data;
  for (GA::ID rank : GAType->ranks()) {
    llvm::StructType *SingleRank = getType(Space.getRanked({rank}));
    Data.push_back(SingleRank);
  }

  llvm::StructType *LLType = llvm::StructType::create(Data, GAType->getName());
  GATypes.insert({GAType, LLType});
  return LLType;
}

llvm::AllocaInst *BuildContext::allocVar(llvm::Value *Val,
                                         std::string_view Name) {
  if (!Val)
    throw std::runtime_error("Void allocate");

  llvm::AllocaInst *Alloca = Builder->CreateAlloca(
      Val->getType(), nullptr, std::string(Name) + "_alloc");
  Builder->CreateStore(Val, Alloca);
  Vars.insert({std::string(Name), Alloca});
  return Alloca;
}

llvm::LoadInst *BuildContext::loadVar(std::string_view Name) const {
  llvm::AllocaInst *Var = Vars.at(std::string(Name));
  llvm::LoadInst *Load = Builder->CreateLoad(Var->getAllocatedType(), Var,
                                             std::string(Name) + "_load");
  return Load;
}

llvm::Constant *BuildContext::getConst(GA::ElementValue Val) {
  llvm::StructType *LLType = getType(Val.getType());
  std::vector<double> Values = Val.getValues();
  llvm::Constant *Data = llvm::ConstantDataArray::get(LLVM, Values);
  return llvm::ConstantStruct::get(LLType, {Data});
}

llvm::Constant *BuildContext::getZero(GA::Type *GAType) {
  llvm::StructType *LLType = getType(GAType);
  return llvm::ConstantAggregateZero::get(LLType);
}
