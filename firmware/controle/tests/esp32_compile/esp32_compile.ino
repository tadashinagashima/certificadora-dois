#include <Arduino.h>
#include <GameController.h>
#include "controller_tests.h"

// Test fixtures retain controllers and snapshots alongside the interpreter calls.
SET_LOOP_TASK_STACK_SIZE(48 * 1024);

volatile int controllerTestFailures = -1;

void reportTest(const char* name, bool passed) {
  Serial.printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

void setup() {
  Serial.begin(115200);
  controllerTestFailures = runControllerTests(nullptr, reportTest);
  Serial.printf("Controller test failures: %d\n", controllerTestFailures);
}

void loop() {}
