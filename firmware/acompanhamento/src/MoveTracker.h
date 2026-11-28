#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace chessboard {

// Same square order as the board: a1=0, h1=7, a8=56, h8=63; 1 means occupied.
using Occupancy = std::uint64_t;
constexpr std::size_t kHistoryCapacity = 64;

struct FrameChanges {
  Occupancy removed = 0;
  Occupancy added = 0;
  // Opaque caller time in milliseconds. Array order, not time, orders frames.
  std::uint32_t timestampMs = 0;
};

struct AttemptSnapshot {
  Occupancy initialOccupancy = 0;
  Occupancy currentOccupancy = 0;
  std::array<FrameChanges, kHistoryCapacity> frames{};
  std::size_t frameCount = 0;
  bool initialized = false;
  bool readingAvailable = false;
  bool historyOverflow = false;
  bool inputInterrupted = false;

  // Reliability concerns the received observations, not physical correctness.
  bool reliable() const;
};

class MoveTracker {
 public:
  // Accept only complete, valid observations. The first establishes a baseline.
  void observe(Occupancy occupancy, std::uint32_t timestampMs);

  // Call instead of observe when a read fails or acquisition continuity is lost.
  void markInputInterrupted();

  AttemptSnapshot snapshot() const;

  // Start another attempt at the last observed occupancy.
  // A missing reading remains unavailable until observe receives a valid one.
  void discardAttempt();

 private:
  AttemptSnapshot attempt_{};
};

}  // namespace chessboard
