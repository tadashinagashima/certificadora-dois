#include "essential_tests.h"
#include <cstdio>
int main(int argc, char** argv) {
  if (argc > 2) return 2;
  return runEssentialTests(argc == 2 ? argv[1] : nullptr, [](const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  });
}
