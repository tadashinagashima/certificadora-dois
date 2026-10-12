#include <MoveTracker.h>

chessboard::MoveTracker tracker;
volatile bool probeReliable = false;
volatile unsigned probeFrameCount = 0;

void setup() {
  tracker.observe(chessboard::Occupancy{1} << 12, 0);  // e2
  tracker.observe(0, 25);
  tracker.observe(chessboard::Occupancy{1} << 28, 50);  // e4
  const auto attempt = tracker.snapshot();
  probeReliable = attempt.reliable();
  probeFrameCount = static_cast<unsigned>(attempt.frameCount);
  tracker.markInputInterrupted();
  tracker.discardAttempt();
}

void loop() {}
