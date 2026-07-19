#pragma once

#include "ts_node_wrapper.hpp"

#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>

struct TSParserDeleter {
  void operator()(TSParser *p) const noexcept {
    if (p)
      ts_parser_delete(p);
  }
};

struct TSTreeDeleter {
  void operator()(TSTree *p) const noexcept {
    if (p)
      ts_tree_delete(p);
  }
};

class Parser {
  std::unique_ptr<TSParser, TSParserDeleter> ParserPtr;
  std::string Src;
  std::unique_ptr<TSTree, TSTreeDeleter> TreePtr;

  void parse();

public:
  Parser();
  Parser(std::string_view source) : Parser() { parse(source); };
  Parser(std::istream &is) : Parser() { parse(is); };
  ~Parser() = default;

  Parser(Parser &&) = default;
  Parser &operator=(Parser &&) = default;

  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;

  void parse(std::string_view source);
  void parse(std::istream &is);

  TSNodeWrapper getRoot() const;
};
