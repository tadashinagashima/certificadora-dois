#include <Arduino.h>
#include <MoveInterpreter.h>
#include "interpreter_tests.h"

// The suite keeps snapshots and board copies to check that inputs stay unchanged.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

volatile int interpreterTestFailures = -1;

void reportTest(const char* name, bool passed) {
  Serial.printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

void setup() {
  Serial.begin(115200);
  interpreterTestFailures = runInterpreterTests(nullptr, reportTest);
  Serial.printf("Interpreter test failures: %d\n", interpreterTestFailures);
}

void loop() {}
