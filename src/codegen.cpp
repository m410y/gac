#include "codegen.hpp"
#include "ast.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

//=============================================================================
// Expressions
//=============================================================================

Value *Literal::codegen(BuildContext &context) const {
  // GA::Type type(el.rank(), ssign);
  // std::vector<double> values(type.dof());
  // values[el.ranked_id()] = el.val;
  // Constant *Data = ConstantDataArray::get(Context, values);
  // return ConstantStruct::get(context.types[type], {Data});
  return nullptr;
}

Value *Variable::codegen(BuildContext &context) const {
  return nullptr;
  // try {
  //   return context.vars.at(name);
  // } catch (std::out_of_range &e) {
  //   std::cerr << "no variable named " << name << "\n";
  //   return nullptr;
  // }
}

Value *CallExpression::codegen(BuildContext &context) const {
  // Function *Callee = context.module->getFunction(name);
  // if (Callee->arg_size() != args.size()) {
  //   std::cerr << "wrong number of arguments\n";
  //   return nullptr;
  // }
  //
  // std::vector<Value *> values;
  // for (auto &arg : args) {
  //   values.push_back(arg->codegen(context));
  //   if (!values.back())
  //     return nullptr;
  // }
  //
  // return context.builder->CreateCall(Callee, values, "call");
  return nullptr;
}

Value *UnaryMinus::codegen(BuildContext &context) const {
  // Value *Val = expr->codegen(context);
  // if (!Val)
  //   return nullptr;
  //
  return nullptr;
}

Value *Projection::codegen(BuildContext &) const { return nullptr; }
Value *BinaryExpression::codegen(BuildContext &) const { return nullptr; }

//=============================================================================
// Local scope statements
//=============================================================================

void VariableDeclaration::codegen(BuildContext &) const {}
void VariableDefinition::codegen(BuildContext &) const {}
void ReturnStatement::codegen(BuildContext &context) const {
  // Value *retval = Expr->codegen(context);
  // if (!retval) {
  //   std::cerr << "no value provided to return\n";
  // }
  //
  // context.builder->CreateRet(Expr->codegen(context));
}

//=============================================================================
// Top-level statements
//=============================================================================

void UsingStatement::codegen(BuildContext &Context) const {
  // Type *Double = Type::getDoubleTy(Context.LLVM);
}

void FunctionDefinition::codegen(BuildContext &Context) const {
  // std::vector<Type *> argtypes;
  // for (const auto &Decl : Params) {
  //   dynamic_cast<VariableDeclaration *>(Decl.get());
  //   argtypes.push_back(Context.Types[var->getType()]);
  // }
  //
  // Function *Func =
  //     Function::Create(FunctionType::get(Context.Types[Type], argtypes,
  //     false),
  //                      Function::ExternalLinkage, Name,
  //                      Context.Module.get());
  //
  // BasicBlock *Entry = BasicBlock::Create(Context, "entry", Func);
  // context.builder->SetInsertPoint(Entry);
  //
  // for (size_t i = 0; i < params.size(); i++) {
  //   std::string_view argname = params[i].getName();
  //   Argument *Arg = Func->getArg(i);
  //   Arg->setName(argname);
  //   context.vars[name] = Arg;
  // }
  //
  // for (const auto &statement : body)
  //   statement->codegen(context);
  //
  // verifyFunction(*Func, &errs());
  // return Func;
}

//=============================================================================
// Syntax tree
//=============================================================================

void SyntaxTree::codegen(BuildContext &Context) const {
  for (const auto &Node : Statements) {
    auto *TopLevelStatement = dynamic_cast<Statement *>(Node.get());
    TopLevelStatement->codegen(Context);
  }
}

//=============================================================================
// IRCompiler
//=============================================================================

std::unique_ptr<llvm::Module> IRCompiler::codegen(const SyntaxTree &AST) {
  BuildContext Context(LLVMCtx);
  AST.codegen(Context);
  return std::move(Context.Module);
}
