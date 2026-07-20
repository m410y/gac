#pragma once

namespace GA {
class Type;
class Element;
} // namespace GA
class SyntaxTree;

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <map>
#include <memory>

class BuildContext {
  std::map<GA::Type *, llvm::StructType *> GATypes;
  std::map<std::string, llvm::AllocaInst *, std::less<>> Vars;

public:
  llvm::LLVMContext LLVM;
  llvm::Type *Real;
  std::unique_ptr<llvm::Module> Module;
  std::unique_ptr<llvm::IRBuilder<>> Builder;

  BuildContext(std::string_view ModuleName)
      : LLVM(), Real(llvm::Type::getDoubleTy(LLVM)),
        Module(std::make_unique<llvm::Module>(ModuleName, LLVM)),
        Builder(std::make_unique<llvm::IRBuilder<>>(LLVM)) {}

  llvm::StructType *getType(GA::Type *Type);
  llvm::AllocaInst *allocVar(llvm::Value *Val, std::string_view Name);
  llvm::Value *loadVar(std::string Name) const;
  llvm::Value *getConst(GA::Element *Elem);
  llvm::Value *getZero(GA::Type *Type);
  llvm::Value *callBuiltin(llvm::Value *Val, std::string_view Name);
};
