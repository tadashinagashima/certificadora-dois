#include <Arduino.h>

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

void startProbe();
void pollProbe();

void setup() { startProbe(); }
void loop() { pollProbe(); }
