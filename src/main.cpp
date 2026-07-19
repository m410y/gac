#include "ast.hpp"
#include "parser.hpp"
#include <exception>
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

  Parser parser(File);
  File.close();

  // try {
  SyntaxTree ast(parser.getRoot());
  std::cout << ast << std::endl;
  // } catch (std::exception &e) {
  //   std::cerr << "Error: " << e.what() << std::endl;
  // }
  // auto module = ast.codegen();
  // module->print(llvm::errs(), nullptr);

  return 0;
}
