#include <Arduino.h>
#include <GameRecord.h>
#include "record_tests.h"

// The suite retains controllers and previous positions while generating SAN.
SET_LOOP_TASK_STACK_SIZE(48 * 1024);

volatile int recordTestFailures = -1;

void reportTest(const char* name, bool passed) {
  Serial.printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

void setup() {
  Serial.begin(115200);
  recordTestFailures = runRecordTests(nullptr, reportTest);
  Serial.printf("Record test failures: %d\n", recordTestFailures);
}

void loop() {}
