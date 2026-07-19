#include "ast.hpp"
#include "parser.hpp"
#include <fstream>
#include <iostream>
#include <llvm/Support/raw_ostream.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "Usage: gac FILENAME" << std::endl;
    return 0;
  }

  std::ifstream File(argv[1]);
  if (!File.is_open()) {
    std::cerr << "Cant open " << argv[1] << std::endl;
    return 0;
  }

  Parser parser(File);
  File.close();

  SyntaxTree ast(parser.getRoot());
  std::cout << ast << std::endl;
  // auto module = ast.codegen();
  // module->print(llvm::errs(), nullptr);

  return 0;
}
