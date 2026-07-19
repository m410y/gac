#pragma once

#include "algebra.hpp"
#include <functional>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <map>
#include <memory>

class SyntaxTree;

struct BuildContext {
  BuildContext(llvm::LLVMContext &Context)
      : LLVM(Context),
        Module(std::make_unique<llvm::Module>("module", Context)),
        Builder(std::make_unique<llvm::IRBuilder<>>(Context)) {}

  llvm::LLVMContext &LLVM;
  std::unique_ptr<llvm::Module> Module;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::map<GA::Type *, llvm::StructType *> Types;
  std::map<std::string, llvm::Value *, std::less<>> Vars;
};

class IRCompiler {
  llvm::LLVMContext LLVMCtx;

public:
  IRCompiler() : LLVMCtx() {}
  std::unique_ptr<llvm::Module> codegen(const SyntaxTree &AST);
};
