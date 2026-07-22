#include "ast.hpp"
#include "codegen.hpp"
#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

int main(int argc, char *argv[]) {
  const char *filename = "test/test.ga";

  if (argc != 2) {
    // std::cout << "Usage: gac FILENAME" << std::endl;
    // return 0;
    std::cout << "Default filename is " << filename << std::endl;
  } else {
    filename = argv[1];
  }

  std::ifstream File(filename);
  if (!File.is_open()) {
    std::cerr << "Cant open " << filename << std::endl;
    return 0;
  }

  Parser Parser(File);
  File.close();

  SyntaxTree AST(Parser.getRoot());
  AST.verify();

  std::cout << "---=== AST dump begin ===---" << std::endl;
  std::cout << AST << std::endl;
  std::cout << "---===  AST dump end  ===---" << std::endl;

  BuildContext Context(basename(filename));
  AST.codegen(Context);
  llvm::Module *Module = Context.Module.get();
  Module->print(llvm::errs(), nullptr);
  llvm::verifyModule(*Module, &llvm::errs());

  return 0;
}
