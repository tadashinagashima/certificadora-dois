#include "record_tests.h"

#include <cstdio>

int main(int argc, char** argv) {
  if (argc > 2) return 2;
  return runRecordTests(argc == 2 ? argv[1] : nullptr, [](const char* name, bool passed) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
  });
}
