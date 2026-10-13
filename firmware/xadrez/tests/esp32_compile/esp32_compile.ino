#include <Arduino.h>
#include <ChessAdapter.h>
#include "adapter_tests.h"

// The suite keeps board copies on the stack while checking query independence.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

volatile int adapterTestFailures = -1;

void reportTest(const char* name, bool passed) {
  Serial.printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

void setup() {
  Serial.begin(115200);
  adapterTestFailures = runAdapterTests(nullptr, reportTest);
  Serial.printf("Adapter test failures: %d\n", adapterTestFailures);
}

void loop() {}
