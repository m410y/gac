#include "parser.hpp"
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <tree_sitter/api.h>

extern "C" const TSLanguage *tree_sitter_ga(void);

Parser::Parser() {
  ParserPtr.reset(ts_parser_new());
  if (!ParserPtr)
    throw std::runtime_error("Failed to create tree-sitter parser");

  if (!ts_parser_set_language(ParserPtr.get(), tree_sitter_ga()))
    throw std::runtime_error("Error during setting parser language");
}

void Parser::parse(std::istream &is) {
  Src.assign(std::string((std::istreambuf_iterator<char>(is)),
                         std::istreambuf_iterator<char>()));
  parse();
}

void Parser::parse(std::string_view sv) {
  Src = sv;
  parse();
}

void Parser::parse() {
  TreePtr.reset();

  TSTree *new_tree = ts_parser_parse_string(ParserPtr.get(), nullptr,
                                            Src.c_str(), Src.length());
  if (!new_tree)
    throw std::runtime_error("Error during parsing");

  TreePtr.reset(new_tree);
}

TSNodeWrapper Parser::getRoot() const {
  if (!TreePtr)
    throw std::runtime_error("No tree parsed yet");

  TSNode Root = ts_tree_root_node(TreePtr.get());
  if (ts_node_is_null(Root))
    throw std::runtime_error("Root node is null after tree creationg");

  return TSNodeWrapper(Root, Src);
}
