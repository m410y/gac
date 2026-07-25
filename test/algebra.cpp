#include "../src/algebra.hpp"
#include <iostream>

using namespace ga;

int main(int argc, char *argv[]) {
  Space<double> Ew{1, 1, 1, 1};
  auto a = Ew.element({1});
  std::cout << (a).values().begin()->second << std::endl;
  return 0;
}
