#include "ast.hpp"
#include "codegen.hpp"
#include "parser.hpp"
#include <fstream>
#include <iostream>
#include <llvm/Support/raw_ostream.h>

int main(int argc, char *argv[]) {
  const char *filename = "test/test.ga";

  if (argc != 2) {
    std::cout << "Usage: gac FILENAME" << std::endl;
    // return 0;
  } else {
    filename = argv[1];
  }

  std::ifstream File(filename);
  if (!File.is_open()) {
    std::cerr << "Cant open " << argv[1] << std::endl;
    return 0;
  }

  Parser Parser(File);
  File.close();

  SyntaxTree AST(Parser.getRoot());
  std::cout << "---=== AST dump begin ===---" << std::endl;
  std::cout << AST << std::endl;
  std::cout << "---===  AST dump end  ===---" << std::endl;

  BuildContext Context(basename(filename));
  AST.codegen(Context);
  auto Module = Context.Module.get();
  Module->print(llvm::errs(), nullptr);

  return 0;
}
