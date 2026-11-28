#include "MoveTracker.h"

namespace chessboard {

bool AttemptSnapshot::reliable() const {
  return initialized && readingAvailable && !historyOverflow && !inputInterrupted;
}

void MoveTracker::observe(Occupancy occupancy, std::uint32_t timestampMs) {
  if (!attempt_.initialized) {
    attempt_.initialOccupancy = occupancy;
    attempt_.currentOccupancy = occupancy;
    attempt_.initialized = true;
    attempt_.readingAvailable = true;
    return;
  }

  const Occupancy previous = attempt_.currentOccupancy;
  attempt_.currentOccupancy = occupancy;
  attempt_.readingAvailable = true;

  // Preserve the complete history prefix after a loss of observations.
  if (attempt_.historyOverflow || attempt_.inputInterrupted || previous == occupancy) {
    return;
  }
  if (attempt_.frameCount == kHistoryCapacity) {
    attempt_.historyOverflow = true;
    return;
  }

  FrameChanges& frame = attempt_.frames[attempt_.frameCount++];
  frame.removed = previous & ~occupancy;
  frame.added = occupancy & ~previous;
  frame.timestampMs = timestampMs;
}

void MoveTracker::markInputInterrupted() {
  attempt_.readingAvailable = false;
  // Without a baseline there is no attempt history to lose.
  if (attempt_.initialized) attempt_.inputInterrupted = true;
}

AttemptSnapshot MoveTracker::snapshot() const {
  return attempt_;
}

void MoveTracker::discardAttempt() {
  attempt_.initialOccupancy = attempt_.currentOccupancy;
  attempt_.frameCount = 0;
  attempt_.historyOverflow = false;
  // Discarding while acquisition is unavailable cannot make it trustworthy.
  attempt_.inputInterrupted = attempt_.initialized && !attempt_.readingAvailable;
}

}  // namespace chessboard
