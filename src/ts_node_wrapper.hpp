#pragma once

#include <charconv>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <vector>

class TSNodeWrapper {
  TSNode Node;
  std::string_view Source;

  TSNodeWrapper(const TSNode &Node, std::string_view Source)
      : Node(Node), Source(Source) {}
  friend class Parser;

public:
  TSNodeWrapper() : Source() { memset(&Node, 0, sizeof(Node)); }
  TSNodeWrapper(TSNodeWrapper &&) = default;
  TSNodeWrapper(const TSNodeWrapper &) = default;
  TSNodeWrapper &operator=(TSNodeWrapper &&) = default;
  TSNodeWrapper &operator=(const TSNodeWrapper &) = default;
  ~TSNodeWrapper() = default;

  std::string_view type() const { return ts_node_type(Node); }
  TSNodeWrapper child(size_t n = 0) const {
    size_t child_count = ts_node_named_child_count(Node);
    if (n >= child_count)
      throw std::out_of_range(
          child_count == 0
              ? "Node has no children"
              : "Node has only " + std::to_string(child_count) + " children");

    return TSNodeWrapper(ts_node_named_child(Node, n), Source);
  }

  std::string_view str() const {
    size_t start = ts_node_start_byte(Node);
    size_t end = ts_node_end_byte(Node);
    return Source.substr(start, end - start);
  }

  template <typename T> std::from_chars_result parse(T &Val) const {
    std::string_view str = this->str();
    return std::from_chars(str.data(), str.data() + str.size(), Val);
  }

  template <typename T> T parse() const {
    T Val;
    std::string_view str = this->str();
    std::from_chars(str.data(), str.data() + str.size(), Val);
    return Val;
  }

  TSNodeWrapper field(std::string_view fieldname) const {
    TSNode child =
        ts_node_child_by_field_name(Node, fieldname.data(), fieldname.length());
    if (ts_node_is_null(child))
      throw std::runtime_error("No child named " + std::string(fieldname));

    return TSNodeWrapper(child, Source);
  }

  std::vector<TSNodeWrapper> children() const {
    size_t child_count = ts_node_named_child_count(Node);
    std::vector<TSNodeWrapper> childs;
    childs.reserve(child_count);
    for (size_t i = 0; i < child_count; i++) {
      TSNode child = ts_node_named_child(Node, i);
      childs.push_back(TSNodeWrapper(child, Source));
    }
    return childs;
  }
};
