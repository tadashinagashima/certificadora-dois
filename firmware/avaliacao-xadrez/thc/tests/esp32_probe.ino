#include "essential_tests.h"
volatile int evaluationFailures = -1;
void setup() {
  evaluationFailures = runEssentialTests(nullptr, nullptr);
}
void loop() {}
