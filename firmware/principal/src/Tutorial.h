#pragma once

#include <ChessAdapter.h>

namespace principal {

struct LessonStep {
  chessboard::Move move;
  const char* cue; // Second physical action, or the piece to use on promotion.
};

struct Tutorial {
  const char* id;
  const char* fen;
  const char* title;
  unsigned count;
  LessonStep steps[2];
};

} // namespace principal
