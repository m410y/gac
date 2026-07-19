#pragma once

#include <ostream>
#include <string_view>

template <typename T>
inline static void printSeparated(std::ostream &OS, const T &Container,
                                  std::string_view Start, std::string_view Sep,
                                  std::string_view Stop) {
  bool begin = true;
  for (const auto &Element : Container)
    OS << (begin ? (begin = false, Start) : Sep) << Element;

  OS << Stop;
}
