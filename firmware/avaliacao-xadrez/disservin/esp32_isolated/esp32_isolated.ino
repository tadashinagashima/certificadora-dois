// Arduino injects Arduino.h into .ino files. Isolate the unmodified chess header
// in a separate C++ translation unit to avoid the Arduino sq(x) macro.
unsigned int countLegalMoves();
volatile unsigned int legalMoveCount = 0;

void setup() { legalMoveCount = countLegalMoves(); }
void loop() {}
