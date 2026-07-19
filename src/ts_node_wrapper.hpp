#pragma once

#include <charconv>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <tree_sitter/api.h>
#include <vector>

class TSNodeWrapper {
  TSNode Node;
  std::string_view Source;

public:
  TSNodeWrapper() : Source() { memset(&Node, 0, sizeof(Node)); }
  TSNodeWrapper(TSNodeWrapper &&) = default;
  TSNodeWrapper(const TSNodeWrapper &) = default;
  TSNodeWrapper &operator=(TSNodeWrapper &&) = default;
  TSNodeWrapper &operator=(const TSNodeWrapper &) = default;
  ~TSNodeWrapper() = default;

  TSNodeWrapper(const TSNode &node, std::string_view source)
      : Node(node), Source(source) {}

  std::string_view type() const { return ts_node_type(Node); }
  bool is_null() const { return ts_node_is_null(Node); }
  TSNodeWrapper child(size_t n = 0) const {
    return TSNodeWrapper(ts_node_named_child(Node, n), Source);
  }

  std::string_view string() const {
    size_t start = ts_node_start_byte(Node);
    size_t end = ts_node_end_byte(Node);
    return Source.substr(start, end - start);
  }

  template <typename T> std::from_chars_result parse(T &Val) const {
    std::string_view str = string();
    return std::from_chars(str.data(), str.data() + str.size(), Val);
  }

  template <typename T> T parse() const {
    T Val;
    std::string_view str = string();
    std::from_chars(str.data(), str.data() + str.size(), Val);
    return Val;
  }

  TSNodeWrapper field(std::string_view fieldname) const {
    TSNode child =
        ts_node_child_by_field_name(Node, fieldname.data(), fieldname.length());
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
